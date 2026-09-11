#pragma once

// THE WORLD SCENE -- the Docks as a SceneDescription, frame after frame.
//
// This is what replaced the starter scene under the HUD. It owns the chunk
// geometries (built once, from the tiles), recolours them when the clock's
// minute bucket or the dynamic lamp set moves, rewrites the instances every
// frame (only the chunks near the eye), places the sky dome on the eye and
// sets the camera from the body's own render::Camera -- Session::camera(),
// which is Camera::fromBody over the sim's Q8 position, BAM yaw and pitch
// and the settings page's field of view, with the combat pitch impulses
// composed in. Nothing here is written back; floats derived from integers,
// render-side only.
//
// THE SKY is geometry, not a clear: a dome (a cylinder with a lid) of radius
// kSkyRadius centred on the eye, coloured per vertex from the day curve --
// skyHorizon at eye level, skyTop overhead -- so it is pitch-correct, it is
// part of the hashed description, and it needs nothing from the adapter.
// The clear colour is the horizon too, for the strip under the dome's rim.
// Fog is NOT here: rlsw has no shader for it and a multiply can't add the
// fog colour in; the GPU path's fog shader is the render lane's, and until
// then the district reads sharp to its far end.

#include <cstdint>
#include <vector>

#include "granadad/render/lamps.hpp"
#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/scene.hpp"

namespace granadad::render {
struct Camera;
class LampGlow;
class TileAtlas;
}  // namespace granadad::render

namespace granadad::render3d {

/// The sky dome's mesh id: in the starter/debug range, above the two
/// starter meshes, below the chunks.
inline constexpr std::uint32_t kSkyMeshId = 900;
/// Dome radius in tiles. Must clear the far plane (512) with the dome's
/// height on top, and exceed the farthest geometry from any eye in the
/// district (the authored Docks are 192x128 tiles: nothing is 300 away).
inline constexpr float kSkyRadius = 400.0F;
inline constexpr float kSkyHeight = 220.0F;

/// The sky dome for a time of day: its version is the minute bucket.
[[nodiscard]] MeshData buildSkyDome(int timeOfDaySeconds);
[[nodiscard]] std::uint32_t skyDomeVersion(int timeOfDaySeconds) noexcept;

struct WorldSceneParams {
    /// Seconds since midnight, from the session's clock.
    int timeOfDaySeconds = 12 * 3600;
    /// Lights that come and go: Session::tavernLights().
    std::vector<render::Lamp> dynamicLamps;
    /// Chunks whose nearest edge is further than this from the eye are not
    /// instanced this frame. The software pass stops its rays at 54.
    float maxDistance = 64.0F;
};

struct WorldSceneStats {
    std::size_t chunksBuilt = 0;
    std::size_t chunksWithGeometry = 0;
    std::size_t chunksInstanced = 0;
    std::size_t trianglesBuilt = 0;
    std::size_t meshesRecoloured = 0;
    bool anyTruncated = false;
};

class WorldScene {
public:
    /// Borrows all three for its lifetime. `glow` may be null (ambient only).
    WorldScene(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
               const render::LampGlow* glow);

    /// Builds every chunk's geometry. Called by the first refresh() if the
    /// caller did not; separate so a loading step can pay for it up front.
    void buildAll();

    /// Marks a chunk's geometry stale (a door, a fluid change). It rebuilds
    /// on the next refresh with its rebuild count bumped.
    void invalidate(ChunkKey key);

    /// The frame: meshes whose version moved are re-coloured and put into
    /// `scene`, the instances are rewritten, the sky is placed, the camera
    /// and the clear colour are set.
    void refresh(SceneDescription& scene, const render::Camera& camera, float aspect,
                 const WorldSceneParams& params);

    [[nodiscard]] const WorldSceneStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const ChunkMaterials& materials() const noexcept { return materials_; }

private:
    struct Slot {
        ChunkGeometry geometry;
        std::uint32_t rebuildCount = 0;
        bool built = false;
        bool stale = true;
        /// The version the mesh in the scene carries, 0 = not put yet.
        std::uint32_t putVersion = 0;
        /// Bounds in scene space, for the distance cull.
        float minX = 0.0F, minY = 0.0F, minZ = 0.0F;
        float maxX = 0.0F, maxY = 0.0F, maxZ = 0.0F;
    };

    [[nodiscard]] Slot& slot(ChunkKey key) noexcept;
    void build(Slot& s, ChunkKey key);

    const sim::TileQuery* tiles_;
    const render::TileAtlas* atlas_;
    const render::LampGlow* glow_;
    ChunkMaterials materials_;
    std::int32_t chunksAcross_ = 0;
    std::int32_t chunksDown_ = 0;
    std::vector<Slot> slots_;
    bool texturePut_ = false;
    std::uint32_t skyVersionPut_ = 0;
    WorldSceneStats stats_;
};

}  // namespace granadad::render3d
