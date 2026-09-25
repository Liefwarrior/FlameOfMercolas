#include "granadad/render3d/scene.hpp"

#include <cmath>
#include <cstring>

#include "granadad/render/world_renderer.hpp"

namespace granadad::render3d {

namespace {

constexpr float kPi = 3.14159265358979323846F;

}  // namespace

// ---------------------------------------------------------------------------
// the halo's fan
// ---------------------------------------------------------------------------

MeshData haloFanMesh() {
    // See scene.hpp: a unit disc, white, the falloff in the alpha channel so
    // both rasterizers draw the same glow with no shader between them.
    MeshData fan;
    const int segments = kHaloFanSegments;
    const int rings = kHaloFanRings;
    const auto push = [&fan](float x, float y, std::uint8_t alpha) {
        fan.positions.push_back(x);
        fan.positions.push_back(y);
        fan.positions.push_back(0.0F);
        fan.texcoords.push_back(0.0F);
        fan.texcoords.push_back(0.0F);
        constexpr std::uint8_t kFull = 255;
        fan.colours.push_back(kFull);
        fan.colours.push_back(kFull);
        fan.colours.push_back(kFull);
        fan.colours.push_back(alpha);
    };
    push(0.0F, 0.0F, std::uint8_t{255});
    for (int ring = 0; ring < rings; ++ring) {
        // Ring 0 is the edge of the flat hot core; ring rings-1 is the rim,
        // at nothing.
        const float t = static_cast<float>(ring) / static_cast<float>(rings - 1);
        const float radius = kHaloCoreFraction + (1.0F - kHaloCoreFraction) * t;
        const float skirt = (1.0F - t) * (1.0F - t);
        const auto alpha = static_cast<std::uint8_t>(skirt * 255.0F + 0.5F);
        for (int seg = 0; seg < segments; ++seg) {
            const float angle =
                2.0F * kPi * static_cast<float>(seg) / static_cast<float>(segments);
            push(radius * std::cos(angle), radius * std::sin(angle), alpha);
        }
    }
    const auto index = [](int ring, int seg, int segs) {
        return static_cast<std::uint16_t>(1 + ring * segs + (seg % segs));
    };
    for (int seg = 0; seg < segments; ++seg) {
        fan.indices.push_back(std::uint16_t{0});
        fan.indices.push_back(index(0, seg, segments));
        fan.indices.push_back(index(0, seg + 1, segments));
    }
    for (int ring = 0; ring + 1 < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            const std::uint16_t a = index(ring, seg, segments);
            const std::uint16_t b = index(ring + 1, seg, segments);
            const std::uint16_t c = index(ring + 1, seg + 1, segments);
            const std::uint16_t d = index(ring, seg + 1, segments);
            fan.indices.push_back(a);
            fan.indices.push_back(b);
            fan.indices.push_back(c);
            fan.indices.push_back(a);
            fan.indices.push_back(c);
            fan.indices.push_back(d);
        }
    }
    return fan;
}

// ---------------------------------------------------------------------------
// SceneDescription
// ---------------------------------------------------------------------------

const MeshData* SceneDescription::findMesh(std::uint32_t id) const noexcept {
    for (const MeshData& mesh : meshes) {
        if (mesh.id == id) {
            return &mesh;
        }
    }
    return nullptr;
}

MeshData* SceneDescription::findMesh(std::uint32_t id) noexcept {
    for (MeshData& mesh : meshes) {
        if (mesh.id == id) {
            return &mesh;
        }
    }
    return nullptr;
}

void SceneDescription::putMesh(MeshData mesh) {
    if (MeshData* existing = findMesh(mesh.id)) {
        *existing = std::move(mesh);
        return;
    }
    meshes.push_back(std::move(mesh));
}

void SceneDescription::removeMesh(std::uint32_t id) {
    for (auto it = meshes.begin(); it != meshes.end(); ++it) {
        if (it->id == id) {
            meshes.erase(it);
            return;
        }
    }
}

void SceneDescription::putTexture(TextureData texture) {
    for (TextureData& existing : textures) {
        if (existing.id == texture.id) {
            existing = std::move(texture);
            return;
        }
    }
    textures.push_back(std::move(texture));
}

// ---------------------------------------------------------------------------
// FNV-1a
// ---------------------------------------------------------------------------

void Fnv1a64::mix(const void* bytes, std::size_t count) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t i = 0; i < count; ++i) {
        hash_ ^= static_cast<std::uint64_t>(p[i]);
        hash_ *= kPrime;
    }
}

void Fnv1a64::mixU16(std::uint16_t value) noexcept {
    // Little-endian byte order by construction, whatever the host is: the
    // hash has to mean the same thing on every machine that computes it.
    const std::uint8_t bytes[2] = {static_cast<std::uint8_t>(value & 0xFFU),
                                   static_cast<std::uint8_t>((value >> 8) & 0xFFU)};
    mix(bytes, sizeof(bytes));
}

void Fnv1a64::mixU32(std::uint32_t value) noexcept {
    const std::uint8_t bytes[4] = {static_cast<std::uint8_t>(value & 0xFFU),
                                   static_cast<std::uint8_t>((value >> 8) & 0xFFU),
                                   static_cast<std::uint8_t>((value >> 16) & 0xFFU),
                                   static_cast<std::uint8_t>((value >> 24) & 0xFFU)};
    mix(bytes, sizeof(bytes));
}

void Fnv1a64::mixU64(std::uint64_t value) noexcept {
    mixU32(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    mixU32(static_cast<std::uint32_t>(value >> 32));
}

void Fnv1a64::mixF32(float value) noexcept {
    static_assert(sizeof(float) == sizeof(std::uint32_t), "IEEE single expected");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    mixU32(bits);
}

namespace {

void mixVec3(Fnv1a64& h, const Vec3& v) noexcept {
    h.mixF32(v.x);
    h.mixF32(v.y);
    h.mixF32(v.z);
}

void mixRgba(Fnv1a64& h, const Rgba8& c) noexcept {
    h.mixU8(c.r);
    h.mixU8(c.g);
    h.mixU8(c.b);
    h.mixU8(c.a);
}

}  // namespace

std::uint64_t sceneHash(const SceneDescription& scene) noexcept {
    Fnv1a64 h;
    // A format tag first, so a future field added to the byte image cannot
    // collide with an old image by accident. STILL "SCN5" WITH THE WEATHER
    // IN: the veils are mixed only when there are any (the end of this
    // function), so a clear frame's description hashes to exactly what it
    // hashed to before the weather lane -- the same picture, the same digest.
    h.mixU32(0x53434E35U);  // "SCN5" -- the rig's pitch, clip, frame and socket joined the hands

    mixVec3(h, scene.camera.position);
    mixVec3(h, scene.camera.target);
    mixVec3(h, scene.camera.up);
    h.mixF32(scene.camera.fovyDegrees);
    mixRgba(h, scene.clearColour);

    h.mixU64(static_cast<std::uint64_t>(scene.meshes.size()));
    for (const MeshData& mesh : scene.meshes) {
        h.mixU32(mesh.id);
        h.mixU32(mesh.version);
        h.mixU64(static_cast<std::uint64_t>(mesh.positions.size()));
        for (const float f : mesh.positions) {
            h.mixF32(f);
        }
        h.mixU64(static_cast<std::uint64_t>(mesh.texcoords.size()));
        for (const float f : mesh.texcoords) {
            h.mixF32(f);
        }
        h.mixU64(static_cast<std::uint64_t>(mesh.colours.size()));
        if (!mesh.colours.empty()) {
            h.mix(mesh.colours.data(), mesh.colours.size());
        }
        h.mixU64(static_cast<std::uint64_t>(mesh.indices.size()));
        for (const std::uint16_t i : mesh.indices) {
            h.mixU16(i);
        }
    }

    h.mixU64(static_cast<std::uint64_t>(scene.textures.size()));
    for (const TextureData& texture : scene.textures) {
        h.mixU32(texture.id);
        h.mixU32(texture.version);
        h.mixI32(texture.width);
        h.mixI32(texture.height);
        h.mixU64(static_cast<std::uint64_t>(texture.pixels.size()));
        if (!texture.pixels.empty()) {
            h.mix(texture.pixels.data(), texture.pixels.size());
        }
    }

    const auto mixInstance = [&h](const Instance& instance) {
        h.mixU32(instance.meshId);
        h.mixU32(instance.textureId);
        mixVec3(h, instance.position);
        h.mixF32(instance.yaw);
        h.mixF32(instance.scale);
        mixRgba(h, instance.tint);
    };
    h.mixU64(static_cast<std::uint64_t>(scene.instances.size()));
    for (const Instance& instance : scene.instances) {
        mixInstance(instance);
    }
    // The people: the placement AND what they are doing. A clip or a frame
    // that moved is a different picture, so it is a different digest.
    h.mixU64(static_cast<std::uint64_t>(scene.actors.size()));
    for (const ActorInstance& actor : scene.actors) {
        mixInstance(actor.instance);
        h.mixU8(actor.rig);
        h.mixU8(static_cast<std::uint8_t>(actor.clip));
        h.mixU32(actor.clipFrame);
        h.mixU8(static_cast<std::uint8_t>(actor.skinned ? 1 : 0));
    }
    // The hands: the machine's state and every part's placement. A frame of
    // a swing that moved is a different picture, so it is a different digest.
    const ViewmodelInstance& hands = scene.viewmodel;
    h.mixU8(static_cast<std::uint8_t>(hands.visible ? 1 : 0));
    h.mixU8(hands.kind);
    h.mixU8(static_cast<std::uint8_t>(hands.state));
    h.mixI32(hands.stateSteps);
    h.mixF32(hands.phase);
    h.mixF32(hands.fovyDegrees);
    mixVec3(h, hands.rigOffset);
    h.mixF32(hands.rigYaw);
    h.mixF32(hands.rigScale);
    h.mixF32(hands.rigPitch);
    h.mixU8(hands.rigClip);
    h.mixF32(hands.rigFrame);
    mixVec3(h, hands.socketOffset);
    mixVec3(h, hands.socketRotation);
    mixRgba(h, hands.tint);
    h.mixU64(static_cast<std::uint64_t>(hands.parts.size()));
    for (const ViewmodelPart& part : hands.parts) {
        h.mixU32(part.meshId);
        mixVec3(h, part.position);
        mixVec3(h, part.rotation);
        h.mixF32(part.scale);
        mixRgba(h, part.tint);
    }
    // The static pieces: the table they index (the catalogue's files, so a
    // catalogue edit is a different picture) and every placement.
    h.mixU64(static_cast<std::uint64_t>(scene.pieces.size()));
    for (const StaticPieceRef& piece : scene.pieces) {
        h.mixU64(static_cast<std::uint64_t>(piece.file.size()));
        if (!piece.file.empty()) {
            h.mix(piece.file.data(), piece.file.size());
        }
    }
    h.mixU64(static_cast<std::uint64_t>(scene.statics.size()));
    for (const StaticInstance& piece : scene.statics) {
        h.mixU16(piece.piece);
        h.mixU8(piece.role);
        mixVec3(h, piece.position);
        h.mixF32(piece.yaw);
        h.mixF32(piece.pitch);
        h.mixF32(piece.roll);
        mixVec3(h, piece.scale);
        mixRgba(h, piece.tint);
        mixRgba(h, piece.tint2);
        h.mixF32(piece.gradientFrom);
        h.mixF32(piece.gradientTo);
        mixRgba(h, piece.tint3);
        mixRgba(h, piece.tint4);
        h.mixF32(piece.gradientFromZ);
        h.mixF32(piece.gradientToZ);
        mixRgba(h, piece.pane);
        h.mixU8(piece.mode);
    }
    // The veils: the weather's own instances, last in the pass and last in
    // the image. A fog that thickened by a shade is a different picture.
    // MIXED ONLY WHEN THERE ARE ANY: a clear frame has no veils and no veil
    // mesh, so its byte image -- and its digest -- is the one it had before
    // the weather lane. A veiled image cannot collide with a clear one by
    // accident: its mesh list already carries the veil mesh above.
    if (!scene.veils.empty()) {
        h.mixU64(static_cast<std::uint64_t>(scene.veils.size()));
        for (const Instance& veil : scene.veils) {
            mixInstance(veil);
        }
    }
    return h.value();
}

// ---------------------------------------------------------------------------
// The camera adapter
// ---------------------------------------------------------------------------

SceneCamera cameraFrom(const render::Camera& camera, float aspect) noexcept {
    SceneCamera out;
    out.position = toScene(camera.x, camera.y, camera.z);
    // The same sin / -cos convention the software pass and session.cpp's
    // sprite math use: yaw 0 is north (-Z), a quarter turn clockwise is east
    // (+X). Pitch tilts the forward vector up (+Y).
    const float cosPitch = std::cos(camera.pitch);
    const Vec3 forward{std::sin(camera.yaw) * cosPitch, std::sin(camera.pitch),
                       -std::cos(camera.yaw) * cosPitch};
    out.target = Vec3{out.position.x + forward.x, out.position.y + forward.y,
                      out.position.z + forward.z};
    out.up = Vec3{0.0F, 1.0F, 0.0F};
    // raylib wants the VERTICAL field of view; the settings page carries the
    // horizontal one as tan(half angle). Same frustum, other axis.
    const float safeAspect = aspect > 0.01F ? aspect : 1.0F;
    const float halfVertical = std::atan(camera.hfovTan / safeAspect);
    out.fovyDegrees = 2.0F * halfVertical * (180.0F / kPi);
    return out;
}

}  // namespace granadad::render3d
