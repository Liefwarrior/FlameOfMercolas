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

/// A surface's light is ambient + glow, and a piece in a lamp's pool is
/// lit rather than blown out: the sum is clamped a little over one.
constexpr float kLightClamp = 1.15F;

/// A flame's halo keeps this much of its alpha at full daylight.
constexpr float kFlameDayAlpha = 0.12F;

/// A flame's halo floats toward the eye along the line of sight by this
/// much of its own height (0.29 m on a lantern's 0.58): a point moved along
/// the eye's own ray lands on the same pixel, so the glow stays centred on
/// the flame, but its plane now clears the lantern's cap, cage and base
/// (all within 0.26 m of the flame) from every side, so the body never
/// slices the glow along a line that walks with the eye -- the glow is
/// drawn over the lamp and the lamp reads through it, a lit glass and not
/// a lit hook. Never more than the cap's fraction of the way to the eye,
/// so the quad's centre stays past the near plane (0.1) even with the
/// lamp at the edge of the view and a body pressed to the wall under it
/// (0.31 m from the flame: the quad 0.19 out, clipped only past 57
/// degrees off axis). Nearer the flame than 0.72 m the cap wins and the
/// base's rim stands in front of the plane -- the underside of a lamp is
/// dark, its rim comes through the glow -- which is the honest limit of a
/// depth-tested sprite against an opaque body.
constexpr float kHaloForward = 0.5F;
constexpr float kHaloForwardCap = 0.4F;

/// A window pane goes warm when the sky is darker than this (0 at
/// midnight, 1 at noon) and the room behind it glows more than this.
constexpr float kPaneNightBelow = 0.42F;
constexpr float kPaneLitAbove = 0.12F;

/// The glass of an unlit window: a third of the light, blue-grey -- a dark
/// pane, not a wash over the wall the chunk mesh puts behind it.
[[nodiscard]] Rgba8 darkPane(const Rgba8& lit) noexcept {
    return Rgba8{static_cast<std::uint8_t>(lit.r / 4), static_cast<std::uint8_t>(lit.g / 4 + lit.g / 16),
                 static_cast<std::uint8_t>(lit.b / 3), lit.a};
}

[[nodiscard]] Vec3 sub(const Vec3& a, const Vec3& b) noexcept { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] float dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] Vec3 normalised(const Vec3& v) noexcept {
    const float len = std::sqrt(dot(v, v));
    return len > 1.0e-6F ? Vec3{v.x / len, v.y / len, v.z / len} : Vec3{0.0F, 0.0F, -1.0F};
}

/// THE FRUSTUM CULL on a piece: its origin, pushed out by its radius,
/// against the camera's forward half-space, the two side planes and the
/// two lid planes, with a margin. A piece behind the eye or well off the
/// sides is not described at all.
struct Frustum {
    Vec3 eye;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    float tanHalfX = 1.0F;
    float tanHalfY = 1.0F;

    [[nodiscard]] static Frustum of(const SceneCamera& camera, float aspect) noexcept {
        Frustum f;
        f.eye = camera.position;
        f.forward = normalised(sub(camera.target, camera.position));
        f.right = normalised(cross(f.forward, camera.up));
        f.up = cross(f.right, f.forward);
        const float halfY = camera.fovyDegrees * 0.5F * (kPi / 180.0F);
        f.tanHalfY = std::tan(halfY) * 1.15F;
        f.tanHalfX = std::tan(halfY) * std::max(0.1F, aspect) * 1.15F;
        return f;
    }

    [[nodiscard]] bool sees(const Vec3& at, float radius) const noexcept {
        const Vec3 v = sub(at, eye);
        const float ahead = dot(v, forward);
        if (ahead < -radius) {
            return false;
        }
        const float reach = std::max(0.0F, ahead + radius);
        const float side = std::fabs(dot(v, right)) - radius;
        if (side > reach * tanHalfX) {
            return false;
        }
        const float lid = std::fabs(dot(v, up)) - radius;
        return lid <= reach * tanHalfY;
    }
};

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
                       const render::LampGlow* glow, const StaticCatalogue* catalogue,
                       const std::vector<render::Lamp>* lamps)
    : tiles_(&tiles),
      atlas_(&atlas),
      glow_(glow),
      materials_(ChunkMaterials::fromAtlas(atlas)),
      catalogue_(catalogue),
      lamps_(lamps) {
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
    if (!piecesPlaced_) {
        placePieces();
    }
}

void WorldScene::placePieces() {
    piecesPlaced_ = true;
    placements_ = StaticPlacements{};
    litTints_.clear();
    litVersion_ = 0;
    if (catalogue_ == nullptr || catalogue_->empty()) {
        stats_.piecesPlaced = 0;
        return;
    }
    static const std::vector<render::Lamp> kNoLamps;
    placements_ = placeStaticPieces(*tiles_, *catalogue_, lamps_ != nullptr ? *lamps_ : kNoLamps);
    stats_.piecesPlaced = placements_.placements.size();
}

void WorldScene::relightPieces(const ChunkLighting& lighting) {
    // The same surface light the chunk colour stage computes for a cell --
    // ambient + max(baked, dynamic) -- times the piece's facing factor,
    // clamped a little over one so a piece in a lamp's pool is lit rather
    // than blown out, folded into the unlit catalogue tint.
    const render::SkyState sky = render::skyAt(lighting.timeOfDaySeconds);
    const bool hasDynamic = lighting.dynamicLamps != nullptr && !lighting.dynamicLamps->empty();
    litTints_.resize(placements_.placements.size() * kLitSlots);
    const auto glowAt = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
        const render::Rgb baked = glow_ != nullptr ? glow_->at(x, y, z) : render::Rgb{};
        const render::Rgb live =
            hasDynamic ? render::dynamicGlowAt(*lighting.dynamicLamps, x, y, z) : render::Rgb{};
        return render::Rgb{std::max(baked.r, live.r), std::max(baked.g, live.g),
                           std::max(baked.b, live.b)};
    };
    // The glow AT A CUT between two cell centres, `t` of the way from the
    // first to the second, so the two pieces meeting there share one value.
    const auto glowBetween = [&](std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by,
                                 float t, std::int32_t z) {
        const render::Rgb a = glowAt(ax, ay, z);
        const render::Rgb b = glowAt(bx, by, z);
        return render::Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    };
    // The glow at a grid CORNER POINT (px, py): the average of the four
    // cells that meet there.
    const auto glowAtCorner = [&](std::int32_t px, std::int32_t py, std::int32_t z) {
        render::Rgb sum{};
        for (std::int32_t dy = -1; dy <= 0; ++dy) {
            for (std::int32_t dx = -1; dx <= 0; ++dx) {
                const render::Rgb g = glowAt(px + dx, py + dy, z);
                sum.r += g.r;
                sum.g += g.g;
                sum.b += g.b;
            }
        }
        return render::Rgb{sum.r * 0.25F, sum.g * 0.25F, sum.b * 0.25F};
    };
    const Rgba8 litPaneTint =
        catalogue_ != nullptr ? catalogue_->knobs().litPane : Rgba8{255, 196, 120, 255};
    const bool night = sky.daylight < kPaneNightBelow;
    for (std::size_t i = 0; i < placements_.placements.size(); ++i) {
        const StaticPlacement& p = placements_.placements[i];
        Rgba8* slots = &litTints_[i * kLitSlots];
        const auto lit = [&p, &sky](const render::Rgb& glow, const Rgba8& tint) {
            const auto ch = [&p](float ambient, float g, std::uint8_t t) {
                const float light = std::min(kLightClamp, ambient + g) * p.facing;
                return static_cast<std::uint8_t>(
                    std::clamp(light * static_cast<float>(t), 0.0F, 255.0F) + 0.5F);
            };
            return Rgba8{ch(sky.ambient.r, glow.r, tint.r), ch(sky.ambient.g, glow.g, tint.g),
                         ch(sky.ambient.b, glow.b, tint.b), tint.a};
        };
        if (p.selfLit) {
            // A lamp is its own light. A flame's halo (a translucent quad)
            // fades with the daylight: full at night, a third at noon.
            Rgba8 own = p.instance.tint;
            if (p.role == PieceRole::Flame) {
                const float glowAlpha = static_cast<float>(own.a) * (kFlameDayAlpha + (1.0F - kFlameDayAlpha) *
                                                                                      (1.0F - sky.daylight));
                own.a = static_cast<std::uint8_t>(std::clamp(glowAlpha, 0.0F, 255.0F) + 0.5F);
            }
            slots[0] = slots[1] = slots[2] = slots[3] = own;
            slots[4] = darkPane(own);
        } else if (p.gradient) {
            // A run piece: lit at each end AT THE BOUNDARY, blended across
            // by the adapter. The piece's local X runs from the a0 end
            // unless it was turned around to show its other finish.
            const Rgba8 a = lit(glowBetween(p.lightX, p.lightY, p.endAX, p.endAY, p.endAT, p.lightZ),
                                p.instance.tint);
            const Rgba8 b = lit(glowBetween(p.lightX2, p.lightY2, p.endBX, p.endBY, p.endBT, p.lightZ),
                                p.instance.tint);
            if (p.alongZ) {
                // The blend runs along the piece's Z: its near end (z = 0)
                // first.
                slots[0] = slots[1] = p.flipped ? b : a;
                slots[2] = slots[3] = p.flipped ? a : b;
            } else {
                slots[0] = slots[2] = p.flipped ? b : a;
                slots[1] = slots[3] = p.flipped ? a : b;
            }
            slots[4] = darkPane(slots[0]);
        } else if (p.bilinear) {
            // A block: its four corner points, in the piece's own order.
            slots[0] = lit(glowAtCorner(p.lightX, p.lightY, p.lightZ), p.instance.tint);
            slots[1] = lit(glowAtCorner(p.lightX2, p.lightY2, p.lightZ), p.instance.tint);
            slots[2] = lit(glowAtCorner(p.endAX, p.endAY, p.lightZ), p.instance.tint);
            slots[3] = lit(glowAtCorner(p.endBX, p.endBY, p.lightZ), p.instance.tint);
            slots[4] = darkPane(slots[0]);
        } else {
            // A point piece, or a flat block: averaged over the cells it
            // covers.
            const std::int32_t x0 = std::min(p.lightX, p.lightX2);
            const std::int32_t x1 = std::max(p.lightX, p.lightX2);
            const std::int32_t y0 = std::min(p.lightY, p.lightY2);
            const std::int32_t y1 = std::max(p.lightY, p.lightY2);
            render::Rgb sum{};
            int count = 0;
            for (std::int32_t y = y0; y <= y1; ++y) {
                for (std::int32_t x = x0; x <= x1; ++x) {
                    const render::Rgb g = glowAt(x, y, p.lightZ);
                    sum.r += g.r;
                    sum.g += g.g;
                    sum.b += g.b;
                    ++count;
                }
            }
            const float inv = 1.0F / static_cast<float>(std::max(1, count));
            const Rgba8 flat = lit(render::Rgb{sum.r * inv, sum.g * inv, sum.b * inv}, p.instance.tint);
            slots[0] = slots[1] = slots[2] = slots[3] = flat;
            slots[4] = darkPane(flat);
        }
        // A window whose room is lit at night shows it: the pane goes
        // warm and bright, its own light -- lit by a lamp that reaches the
        // room, or by the candle the tile hash keeps in a roofed room.
        if (p.hasInside && night) {
            const render::Rgb room = glowAt(p.insideX, p.insideY, p.insideZ);
            if (p.homely || std::max(room.r, std::max(room.g, room.b)) > kPaneLitAbove) {
                slots[4] = litPaneTint;
            }
        }
    }
    ++stats_.piecesRelit;
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

    // The static pieces: relit when the lighting bucket moved, described
    // when inside their role's reach AND inside the frustum (a piece
    // behind the eye or off the sides is not described). The table rides
    // with them.
    scene.camera = cameraFrom(camera, aspect);
    scene.statics.clear();
    stats_.piecesInstanced = 0;
    stats_.piecesInReach = 0;
    if (catalogue_ != nullptr && !placements_.placements.empty()) {
        const std::uint32_t litVersion = chunkVersion(0, lighting);
        if (litVersion != litVersion_ ||
            litTints_.size() != placements_.placements.size() * kLitSlots) {
            relightPieces(lighting);
            litVersion_ = litVersion;
        }
        scene.pieces = catalogue_->pieceRefs();
        const std::vector<PieceSpec>& specs = catalogue_->pieces();
        const Frustum frustum = Frustum::of(scene.camera, aspect);
        for (std::size_t i = 0; i < placements_.placements.size(); ++i) {
            const StaticPlacement& p = placements_.placements[i];
            const float roleReach =
                p.instance.piece < specs.size() ? specs[p.instance.piece].maxDistance : reach;
            // Measured to the piece's nearest reach, not its origin, so a
            // wide block (a water plane, a floor) is not dropped while it
            // still runs under the eye.
            const float limit = std::min(reach, roleReach) + p.radius;
            const float dx = p.instance.position.x - eye.x;
            const float dz = p.instance.position.z - eye.z;
            if (dx * dx + dz * dz > limit * limit) {
                continue;
            }
            ++stats_.piecesInReach;
            if (!frustum.sees(p.instance.position, p.radius)) {
                continue;
            }
            StaticInstance at = p.instance;
            const Rgba8* slots = &litTints_[i * kLitSlots];
            at.tint = slots[0];
            at.tint2 = slots[1];
            at.tint3 = slots[2];
            at.tint4 = slots[3];
            at.pane = slots[4];
            at.mode = p.mode;
            if (p.billboard && p.instance.piece < specs.size()) {
                // A halo faces the eye IN THREE DIMENSIONS, a sphere's
                // billboard: its quad's normal (local +Z) along the whole
                // line to the eye, not just its shadow on the ground. The
                // yaw first -- a clockwise yaw takes +Z to (-sin, cos) in
                // XZ -- then the pitch about the quad's own X (the adapter
                // applies it before the yaw), which tips the normal down
                // to an eye under the lamp and up to one on a roof. A
                // yaw-only turn left the quad standing plumb, so a body
                // under a lantern looking up saw it foreshortened to a
                // bar and, from the roof, to a bright sliver: this is what
                // the placement critic called the halo's edge at arm's
                // length. The origin -- the bottom-left corner of a quad
                // `w` wide and `h` tall -- is half a width back along its
                // own +X from the anchor and half a height down its own
                // +Y, both turned by the pitch and the yaw, so the centre
                // holds on the anchor's ray whichever way the quad tips.
                // The centre itself floats toward the eye along that ray
                // (kHaloForward), clear of the lamp's own body. Facing the
                // eye, the quad lies across the ray and reaches its own
                // half-diagonal (0.37 m on a lantern) from the centre;
                // the flame stands 0.42 m off its wall and the body's own
                // radius keeps the eye 0.35 m off it, so the float never
                // carries the centre more than a few centimetres nearer
                // the plaster and the quad never touches it. Two-sided,
                // so which way along the line is all one.
                const PieceSpec& spec = specs[p.instance.piece];
                const float dx = eye.x - p.anchor.x;
                const float dy = eye.y - p.anchor.y;
                const float dz = eye.z - p.anchor.z;
                const float flat = std::sqrt(dx * dx + dz * dz);
                const float len = std::sqrt(flat * flat + dy * dy);
                const float yaw = flat > 1.0e-3F ? std::atan2(-dx, dz) : 0.0F;
                // +Z pitched by `pitch` is (0, -sin, cos): positive tips the
                // normal down, toward an eye below the anchor.
                const float pitch = std::atan2(-dy, flat);
                const float w = spec.width * at.scale.x;
                const float h = spec.height * at.scale.y;
                const float c = std::cos(yaw);
                const float s = std::sin(yaw);
                const float cp = std::cos(pitch);
                const float sp = std::sin(pitch);
                // The float toward the eye, as a fraction of the way there.
                const float forward =
                    len > 1.0e-3F ? std::min(kHaloForward * h, kHaloForwardCap * len) / len : 0.0F;
                const Vec3 centre{p.anchor.x + dx * forward, p.anchor.y + dy * forward,
                                  p.anchor.z + dz * forward};
                at.yaw = yaw < 0.0F ? yaw + 2.0F * kPi : yaw;
                at.pitch = pitch;
                // The half-height step along the quad's own +Y, which the
                // pitch takes to (0, cos, sin) and the yaw then turns.
                at.position = Vec3{centre.x - 0.5F * w * c + 0.5F * h * sp * s,
                                   centre.y - 0.5F * h * cp + spec.lift,
                                   centre.z - 0.5F * w * s - 0.5F * h * sp * c};
            }
            scene.statics.push_back(at);
            ++stats_.piecesInstanced;
        }
    } else {
        scene.pieces.clear();
    }

    const render::SkyState skyState = render::skyAt(params.timeOfDaySeconds);
    scene.clearColour = rgba(skyState.skyHorizon);
}

}  // namespace granadad::render3d
