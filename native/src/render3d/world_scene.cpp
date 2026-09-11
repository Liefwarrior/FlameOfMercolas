#include "granadad/render3d/world_scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "granadad/render/lighting.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render3d {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr int kSkySegments = 32;

[[nodiscard]] std::uint8_t channel8(float value) noexcept {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
}

[[nodiscard]] Rgba8 rgba(const render::Rgb& c) noexcept {
    return Rgba8{channel8(c.r), channel8(c.g), channel8(c.b), 255};
}

void pushVertex(MeshData& mesh, float x, float y, float z, const Rgba8& c) {
    mesh.positions.push_back(x);
    mesh.positions.push_back(y);
    mesh.positions.push_back(z);
    mesh.texcoords.push_back(0.0F);
    mesh.texcoords.push_back(0.0F);
    mesh.colours.push_back(c.r);
    mesh.colours.push_back(c.g);
    mesh.colours.push_back(c.b);
    mesh.colours.push_back(c.a);
}

/// A triangle in both windings: the dome is seen from inside, and a sky
/// that back-face culling could swallow is not worth the winding argument.
void pushDoubleTriangle(MeshData& mesh, std::uint16_t a, std::uint16_t b, std::uint16_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
    mesh.indices.push_back(a);
    mesh.indices.push_back(c);
    mesh.indices.push_back(b);
}

/// Squared distance from a point to an axis-aligned box in the XZ plane.
[[nodiscard]] float boxDistanceSq(float px, float pz, float minX, float minZ, float maxX,
                                  float maxZ) noexcept {
    const float dx = px < minX ? minX - px : (px > maxX ? px - maxX : 0.0F);
    const float dz = pz < minZ ? minZ - pz : (pz > maxZ ? pz - maxZ : 0.0F);
    return dx * dx + dz * dz;
}

}  // namespace

// ---------------------------------------------------------------------------
// the sky
// ---------------------------------------------------------------------------

std::uint32_t skyDomeVersion(int timeOfDaySeconds) noexcept {
    const int minute = ((timeOfDaySeconds % 86400) + 86400) % 86400 / 60;
    return static_cast<std::uint32_t>(minute) + 1U;
}

MeshData buildSkyDome(int timeOfDaySeconds) {
    MeshData mesh;
    mesh.id = kSkyMeshId;
    mesh.version = skyDomeVersion(timeOfDaySeconds);
    const render::SkyState sky = render::skyAt(timeOfDaySeconds);
    const Rgba8 horizon = rgba(sky.skyHorizon);
    const Rgba8 top = rgba(sky.skyTop);

    // Three rings -- under the eye, at the eye, overhead -- and a lid. The
    // horizon colour holds from below up to eye level, then ramps to the
    // zenith colour, which the lid carries across the top.
    for (int i = 0; i < kSkySegments; ++i) {
        const float angle = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(kSkySegments);
        const float x = kSkyRadius * std::cos(angle);
        const float z = kSkyRadius * std::sin(angle);
        pushVertex(mesh, x, -kSkyHeight, z, horizon);
        pushVertex(mesh, x, 0.0F, z, horizon);
        pushVertex(mesh, x, kSkyHeight, z, top);
    }
    const auto lidCentre = static_cast<std::uint16_t>(mesh.vertexCount());
    pushVertex(mesh, 0.0F, kSkyHeight, 0.0F, top);

    for (int i = 0; i < kSkySegments; ++i) {
        const int next = (i + 1) % kSkySegments;
        const auto a0 = static_cast<std::uint16_t>(i * 3);
        const auto a1 = static_cast<std::uint16_t>(i * 3 + 1);
        const auto a2 = static_cast<std::uint16_t>(i * 3 + 2);
        const auto b0 = static_cast<std::uint16_t>(next * 3);
        const auto b1 = static_cast<std::uint16_t>(next * 3 + 1);
        const auto b2 = static_cast<std::uint16_t>(next * 3 + 2);
        pushDoubleTriangle(mesh, a0, b0, b1);
        pushDoubleTriangle(mesh, a0, b1, a1);
        pushDoubleTriangle(mesh, a1, b1, b2);
        pushDoubleTriangle(mesh, a1, b2, a2);
        pushDoubleTriangle(mesh, a2, b2, lidCentre);
    }
    return mesh;
}

// ---------------------------------------------------------------------------
// the world
// ---------------------------------------------------------------------------

WorldScene::WorldScene(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
                       const render::LampGlow* glow)
    : tiles_(&tiles), atlas_(&atlas), glow_(glow), materials_(ChunkMaterials::fromAtlas(atlas)) {
    chunksAcross_ = std::min(kChunksAcross, (tiles.sizeX() + kChunkTiles - 1) / kChunkTiles);
    chunksDown_ = (tiles.sizeY() + kChunkTiles - 1) / kChunkTiles;
    slots_.resize(static_cast<std::size_t>(chunksAcross_) * static_cast<std::size_t>(chunksDown_));
}

WorldScene::Slot& WorldScene::slot(ChunkKey key) noexcept {
    return slots_[static_cast<std::size_t>(key.cy) * static_cast<std::size_t>(chunksAcross_) +
                  static_cast<std::size_t>(key.cx)];
}

void WorldScene::build(Slot& s, ChunkKey key) {
    if (s.built) {
        ++s.rebuildCount;
        stats_.trianglesBuilt -= s.geometry.triangleCount();
        if (s.geometry.triangleCount() > 0) {
            --stats_.chunksWithGeometry;
        }
    }
    s.geometry = buildChunkGeometry(*tiles_, *atlas_, materials_, key, s.rebuildCount);
    s.built = true;
    s.stale = false;
    s.putVersion = 0;
    ++stats_.chunksBuilt;
    stats_.trianglesBuilt += s.geometry.triangleCount();
    if (s.geometry.triangleCount() > 0) {
        ++stats_.chunksWithGeometry;
    }
    stats_.anyTruncated = stats_.anyTruncated || s.geometry.truncated;

    s.minX = s.minY = s.minZ = std::numeric_limits<float>::max();
    s.maxX = s.maxY = s.maxZ = std::numeric_limits<float>::lowest();
    const std::vector<float>& p = s.geometry.positions;
    for (std::size_t i = 0; i + 2 < p.size(); i += 3) {
        s.minX = std::min(s.minX, p[i]);
        s.maxX = std::max(s.maxX, p[i]);
        s.minY = std::min(s.minY, p[i + 1]);
        s.maxY = std::max(s.maxY, p[i + 1]);
        s.minZ = std::min(s.minZ, p[i + 2]);
        s.maxZ = std::max(s.maxZ, p[i + 2]);
    }
}

void WorldScene::buildAll() {
    for (std::int32_t cy = 0; cy < chunksDown_; ++cy) {
        for (std::int32_t cx = 0; cx < chunksAcross_; ++cx) {
            const ChunkKey key{cx, cy};
            Slot& s = slot(key);
            if (!s.built || s.stale) {
                build(s, key);
            }
        }
    }
}

void WorldScene::invalidate(ChunkKey key) {
    if (key.cx < 0 || key.cy < 0 || key.cx >= chunksAcross_ || key.cy >= chunksDown_) {
        return;
    }
    slot(key).stale = true;
}

void WorldScene::refresh(SceneDescription& scene, const render::Camera& camera, float aspect,
                         const WorldSceneParams& params) {
    buildAll();

    ChunkLighting lighting;
    lighting.timeOfDaySeconds = params.timeOfDaySeconds;
    lighting.glow = glow_;
    lighting.dynamicLamps = &params.dynamicLamps;
    lighting.lampKey = dynamicLampKey(params.dynamicLamps);

    // The atlas, once per scene.
    bool haveTexture = false;
    for (const TextureData& texture : scene.textures) {
        if (texture.id == kChunkAtlasTextureId && texture.version == materials_.texture().version) {
            haveTexture = true;
            break;
        }
    }
    if (!haveTexture) {
        scene.putTexture(materials_.texture());
    }

    // The sky, recoloured with the minute.
    const std::uint32_t skyVersion = skyDomeVersion(params.timeOfDaySeconds);
    const MeshData* sky = scene.findMesh(kSkyMeshId);
    if (sky == nullptr || sky->version != skyVersion) {
        scene.putMesh(buildSkyDome(params.timeOfDaySeconds));
    }

    // The chunks: recoloured when their version moved, instanced when near.
    const Vec3 eye = toScene(camera.x, camera.y, camera.z);
    const float reach = std::max(0.0F, params.maxDistance);
    const float reachSq = reach * reach;
    scene.instances.clear();

    Instance skyAt;
    skyAt.meshId = kSkyMeshId;
    skyAt.position = eye;
    scene.instances.push_back(skyAt);

    stats_.chunksInstanced = 0;
    for (std::int32_t cy = 0; cy < chunksDown_; ++cy) {
        for (std::int32_t cx = 0; cx < chunksAcross_; ++cx) {
            const ChunkKey key{cx, cy};
            Slot& s = slot(key);
            if (s.geometry.triangleCount() == 0) {
                continue;
            }
            const std::uint32_t version = chunkVersion(s.rebuildCount, lighting);
            const std::uint32_t id = chunkMeshId(key);
            const MeshData* have = scene.findMesh(id);
            if (have == nullptr || have->version != version) {
                scene.putMesh(colourChunk(s.geometry, lighting));
                ++stats_.meshesRecoloured;
            }
            if (boxDistanceSq(eye.x, eye.z, s.minX, s.minZ, s.maxX, s.maxZ) > reachSq) {
                continue;
            }
            Instance at;
            at.meshId = id;
            at.textureId = kChunkAtlasTextureId;
            scene.instances.push_back(at);
            ++stats_.chunksInstanced;
        }
    }

    scene.camera = cameraFrom(camera, aspect);
    const render::SkyState skyState = render::skyAt(params.timeOfDaySeconds);
    scene.clearColour = rgba(skyState.skyHorizon);
}

}  // namespace granadad::render3d
