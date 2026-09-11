#pragma once

// THE SCENE DESCRIPTION -- the one thing the 3D renderer draws.
//
// Everything the raylib adapter (granadad-render3d-rl) puts on screen comes
// through this struct, and nothing else: meshes as plain vertex arrays,
// textures as plain RGBA bytes, instances as (mesh, transform, tint), a camera
// and a clear colour. It is built by PURE C++ in granadad-render3d-core --
// the chunk mesher, the actor instancer, the viewmodel -- as a function of
// Session state, and it never sees raylib.h.
//
// WHY A DESCRIPTION AND NOT DRAW CALLS. The determinism claim of this project
// stops at the GPU: a frame drawn by a driver cannot be hashed across
// machines, and the plan says so out loud. What CAN be hashed is this struct.
// sceneHash() takes an FNV-1a over its canonical byte image, so two sessions
// driven by the same script must produce identical scene bytes -- the 3D
// equivalent of `CHECK(a.pixels() == b.pixels())`, float-only-from-integers,
// and it runs with no renderer at all. The software rasterizer (rlsw) then
// draws the SAME description in the docker gate and its pixels are compared
// byte for byte in-process; the GPU build is proved by stats and by a
// similarity report, never by a hash. See docs/3d (the renderer plan) for the
// three tiers.
//
// SCENE SPACE. raylib's right-handed, Y-up frame at ONE UNIT = ONE TILE:
//
//     X = east   (world x, tiles)
//     Y = up     (world z, in TILES -- render::bandSurface() already scales
//                bands to tiles; sim/vertical_scale.hpp owns the number)
//     Z = south  (world y, tiles)
//
// (east, up, south) is a proper right-handed triple -- east x up = south --
// so the mapping is a rotation, not a mirror, and counter-clockwise triangles
// stay counter-clockwise. Front faces are CCW seen from outside; the adapter
// culls back faces. Yaw follows the body: 0 faces north (-Z) and increases
// CLOCKWISE seen from above, exactly render::Camera's own convention.
//
// MESH IDS are owned by the lane that emits them and never reused for a
// different shape (the adapter caches uploads by id and re-uploads only when
// `version` moves):
//
//     1 .. 999          starter / debug meshes (this header)
//     1000 .. 99999     chunk meshes  -- chunk_mesher.hpp
//     100000 .. 199999  actor rigs    -- actor_instances.hpp
//     200000 .. 209999  viewmodel     -- viewmodel.hpp
//
// Floats are legal here and only here (render-side). Nothing in this header
// is allowed anywhere near simulation state.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace granadad::render {
struct Camera;
}

namespace granadad::render3d {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Rgba8 {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

/// Mesh id spaces. See the header on who owns which range.
inline constexpr std::uint32_t kStarterGroundMeshId = 1;
inline constexpr std::uint32_t kStarterCubeMeshId = 2;
inline constexpr std::uint32_t kChunkMeshIdBase = 1000;
inline constexpr std::uint32_t kActorMeshIdBase = 100000;
inline constexpr std::uint32_t kViewmodelMeshIdBase = 200000;

/// Plain vertex arrays. Indexed triangles, CCW front faces, at most 65535
/// vertices (raylib indexes with unsigned short; a chunk is 16x16 columns so
/// it never gets near that, and an actor rig is a few thousand).
struct MeshData {
    /// Stable identity -- the adapter keys its upload cache on it.
    std::uint32_t id = 0;
    /// Bumped whenever the arrays change (a chunk rebuilt, a recolour when the
    /// time-of-day bucket moves). Same id + same version = nothing re-uploads.
    std::uint32_t version = 0;
    /// xyz per vertex.
    std::vector<float> positions;
    /// uv per vertex. Empty means "all zero" (untextured).
    std::vector<float> texcoords;
    /// rgba per vertex -- THIS IS THE LIGHTING. Vertex colours, no shaders:
    /// what makes the rlsw frame and the GPU frame the same picture. Empty
    /// means white.
    std::vector<std::uint8_t> colours;
    /// Three per triangle.
    std::vector<std::uint16_t> indices;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return positions.size() / 3; }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
};

/// An RGBA8 image the adapter uploads once per (id, version). The tile atlas
/// (content/art/custom/tiles.png) and actor skins ride in here.
struct TextureData {
    std::uint32_t id = 0;
    std::uint32_t version = 0;
    int width = 0;
    int height = 0;
    /// R,G,B,A per pixel, top row first -- render::Framebuffer's own order.
    std::vector<std::uint8_t> pixels;
};

/// One drawn thing: a mesh at a place. Positions arrive as floats DERIVED
/// FROM the sim's Q8 integers at description time and are never written back
/// (the 2026-07-31 ruling quoted in session.cpp's wardSprites).
struct Instance {
    std::uint32_t meshId = 0;
    /// 0 = untextured (the mesh's vertex colours alone).
    std::uint32_t textureId = 0;
    Vec3 position;
    /// Radians, clockwise from above, 0 faces north (-Z).
    float yaw = 0.0F;
    float scale = 1.0F;
    /// Multiplies the vertex colours. Fog and distance dimming live here on
    /// the rlsw path, where there is no shader to do it.
    Rgba8 tint;
};

struct SceneCamera {
    Vec3 position;
    Vec3 target;
    Vec3 up{0.0F, 1.0F, 0.0F};
    /// Vertical field of view in degrees -- what raylib's Camera3D takes.
    float fovyDegrees = 60.0F;
};

struct SceneDescription {
    SceneCamera camera;
    /// The sky -- what the frame is cleared to before anything draws.
    Rgba8 clearColour{0, 0, 0, 255};
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
    std::vector<Instance> instances;

    [[nodiscard]] const MeshData* findMesh(std::uint32_t id) const noexcept;
    [[nodiscard]] MeshData* findMesh(std::uint32_t id) noexcept;
    /// Adds or replaces the mesh with this id.
    void putMesh(MeshData mesh);
    void putTexture(TextureData texture);
};

/// FNV-1a, 64-bit, streamed. The hash every determinism claim on this side of
/// the line rests on, so it is spelled out rather than borrowed.
class Fnv1a64 {
public:
    static constexpr std::uint64_t kOffset = 0xCBF29CE484222325ULL;
    static constexpr std::uint64_t kPrime = 0x100000001B3ULL;

    void mix(const void* bytes, std::size_t count) noexcept;
    void mixU8(std::uint8_t value) noexcept { mix(&value, 1); }
    void mixU16(std::uint16_t value) noexcept;
    void mixU32(std::uint32_t value) noexcept;
    void mixU64(std::uint64_t value) noexcept;
    void mixI32(std::int32_t value) noexcept { mixU32(static_cast<std::uint32_t>(value)); }
    /// The float's IEEE bit pattern, so -0.0 and 0.0 hash apart on purpose:
    /// a description that differs in a sign bit is a different description.
    void mixF32(float value) noexcept;

    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_ = kOffset;
};

/// The canonical byte image of a description, hashed. Field by field, in a
/// fixed order, with every count written before the bytes it counts -- never
/// a memcpy of a struct, so padding and layout cannot leak in.
[[nodiscard]] std::uint64_t sceneHash(const SceneDescription& scene) noexcept;

/// The camera adapter: render::Camera (x east, y south, z up in tiles; yaw
/// BAM-derived radians clockwise from north; pitch radians positive up;
/// tan(half horizontal fov)) into scene space. `aspect` is the frame's
/// width/height -- the vertical fov raylib wants is derived from the
/// horizontal one the settings page carries.
[[nodiscard]] SceneCamera cameraFrom(const render::Camera& camera, float aspect) noexcept;

/// World tiles (east, south, up) to scene space. The one place the axis swap
/// is written down.
[[nodiscard]] constexpr Vec3 toScene(float east, float south, float up) noexcept {
    return Vec3{east, up, south};
}

}  // namespace granadad::render3d
