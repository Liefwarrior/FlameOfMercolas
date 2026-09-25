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
// The clear day's own fog is NOT here: rlsw has no shader for it and a
// multiply can't add the fog colour in; the GPU path's fog shader is the
// render lane's, and until then the district reads sharp to its far end.
//
// THE WEATHER'S FOG IS, as geometry (WEATHER, roadmap 21a): what a fog or an
// overcast adds over the clear day is drawn as THE VEIL -- nested translucent
// shells round the eye, coloured the fog's colour, each shell's alpha the
// fog between it and the shell inside it, drawn far to near after everything
// else in the pass with the depth test on. Whatever stands inside a shell is
// clear of it; whatever stands beyond it is seen through it; the sky beyond
// them all is seen through the lot. No shader, so the rlsw frame and the GPU
// frame are the same picture, the way every other pixel of this pass is. It
// is banded where a shader would be smooth -- nineteen steps out to fifty-six
// tiles, closer together where the eye can tell -- and it is only ever
// ADDED: a clear frame has no veil and is the frame it was.
//
// THE STATIC PIECES (static_pieces.hpp) ride here too: placed once from the
// tiles and the catalogue when the chunks are built, lit per lighting bucket
// exactly as the chunk faces are (ambient + baked + dynamic, times the
// facing factor), and written into scene.statics every frame -- only those
// within their role's reach of the eye, the cheap cull -- with the
// catalogue's piece table beside them. No catalogue: no pieces, and the
// description is what it was before the lane.

#include <cstdint>
#include <vector>

#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/static_pieces.hpp"

namespace granadad::render {
struct Camera;
class LampGlow;
class TileAtlas;
}  // namespace granadad::render

namespace granadad::render3d {

/// The sky dome's mesh id: in the starter/debug range, above the two
/// starter meshes, below the chunks.
inline constexpr std::uint32_t kSkyMeshId = 900;
/// WEATHER. The veil's mesh id, beside the sky's.
inline constexpr std::uint32_t kVeilMeshId = 901;
/// Dome radius in tiles. Must clear the far plane (512) with the dome's
/// height on top, and exceed the farthest geometry from any eye in the
/// district (the authored Docks are 192x128 tiles: nothing is 300 away).
inline constexpr float kSkyRadius = 400.0F;
inline constexpr float kSkyHeight = 220.0F;

/// The sky dome for a time of day: its version is the minute bucket. The
/// one-argument form is the clear sky.
[[nodiscard]] MeshData buildSkyDome(int timeOfDaySeconds);
[[nodiscard]] MeshData buildSkyDome(int timeOfDaySeconds, const render::Weather& weather);
[[nodiscard]] std::uint32_t skyDomeVersion(int timeOfDaySeconds) noexcept;

/// WEATHER. The veil for a sky: the shells, far to near, each ring coloured
/// the fog's colour below the horizon and the sky's own colour at its
/// elevation above it (so the sky seen through every shell is still the
/// sky), each shell's alpha 1 - exp(-veil * (its radius - the one inside)).
/// Empty (no vertices) when sky.veil is zero. `version` is the caller's
/// bucket, the sky dome's own.
[[nodiscard]] MeshData buildVeil(const render::SkyState& sky, std::uint32_t version);
/// The shell radii the veil is built on, in tiles, nearest first. Exposed
/// so a test can pin the far-to-near order and the reach.
[[nodiscard]] std::size_t veilShellCount() noexcept;
[[nodiscard]] float veilShellRadius(std::size_t shell) noexcept;

struct WorldSceneParams {
    /// Seconds since midnight, from the session's clock.
    int timeOfDaySeconds = 12 * 3600;
    /// WEATHER. Session::weather(). The default is the clear sky.
    render::Weather weather;
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
    /// S LANE. Pieces placed over the whole district, pieces inside their
    /// reach this frame, pieces described this frame (inside the reach AND
    /// the frustum), and how many times the placements were relit (once
    /// per lighting bucket, never per frame).
    std::size_t piecesPlaced = 0;
    std::size_t piecesInReach = 0;
    std::size_t piecesInstanced = 0;
    std::size_t piecesRelit = 0;
};

class WorldScene {
public:
    /// Borrows all of them for its lifetime. `glow` may be null (ambient
    /// only). `catalogue` may be null (no static pieces); with one, `lamps`
    /// is the baked lamp list the lamp pieces stand at (null = none).
    WorldScene(const sim::TileQuery& tiles, const render::TileAtlas& atlas,
               const render::LampGlow* glow, const StaticCatalogue* catalogue = nullptr,
               const std::vector<render::Lamp>* lamps = nullptr);

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
    /// Every piece placed over the district (built with the chunks), and
    /// the rule counts behind them.
    [[nodiscard]] const std::vector<StaticPlacement>& placements() const noexcept {
        return placements_.placements;
    }
    [[nodiscard]] const StaticPlacementStats& placementStats() const noexcept {
        return placements_.stats;
    }

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
    void placePieces();
    void relightPieces(const ChunkLighting& lighting);

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
    const StaticCatalogue* catalogue_ = nullptr;
    const std::vector<render::Lamp>* lamps_ = nullptr;
    StaticPlacements placements_;
    bool piecesPlaced_ = false;
    /// The lit tints per placement -- the four blend corners and the pane
    /// -- for the lighting bucket `litVersion_`.
    static constexpr std::size_t kLitSlots = 5;
    std::vector<Rgba8> litTints_;
    std::uint32_t litVersion_ = 0;
};

}  // namespace granadad::render3d
