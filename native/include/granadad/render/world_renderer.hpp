#pragma once

// First person over the tile world, in software.
//
// THE ALGORITHM, because it is not the obvious one.
//
// A Wolfenstein-style raycaster casts one ray per screen column and draws one
// wall slice. That cannot draw the Docks: this world is genuinely three
// dimensional — quayside at z=19, mid-slope at z=20, upper at z=21, a harbour
// two levels below the deck, warehouse roofs above, piers you can walk under.
// A single-slab raycaster would flatten all of it.
//
// So: one ray per column, DDA across the (x, y) grid, and at every CELL the ray
// enters, every solid voxel in a window of z levels is drawn as up to three
// faces —
//
//     side    the vertical face the ray entered through, at the entry distance
//     top     the horizontal face, spanning entry distance to exit distance
//     bottom  the same from underneath, when the eye is below it
//
// Cells are visited in strictly increasing distance and the first writer of a
// pixel wins, so occlusion falls out of the traversal with no z-sort and no
// per-pixel depth compare. Within one cell every voxel is at the same distance,
// so their screen spans are disjoint and their order does not matter.
//
// The ray direction is `forward + right * cameraX`, deliberately NOT normalised,
// which makes the DDA's distances already perpendicular to the view plane —
// no fisheye correction, and a horizontal face's texture coordinate at a screen
// row follows directly from the row's distance.
//
// Empty air is skipped a whole COLUMN at a time: a 32-bit mask per (x, y) says
// which z levels hold anything, so the inner loop only visits voxels that exist.
//
// Floats are legal in this file. Nothing in it may be read by the simulation.

#include <cstdint>
#include <vector>

#include "granadad/render/atlas.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/scene_types.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// the vertical scale
// ---------------------------------------------------------------------------
//
// kBandHeight and bandSurface() live in render/vertical.hpp now, and the
// cell classification (Voxel, classifyVoxel) in render/voxel_classify.hpp,
// because the 3D chunk mesher draws from the SAME classification this pass
// does and the two must not be allowed to drift. Both are included here so
// every existing caller keeps its names.

// Camera, Projection and SpriteInstance are in render/scene_types.hpp now: the
// 3D scene builders, the signage pass and the session all speak these types, so
// they live in a neutral header and not in the software world pass. Included
// here so every existing caller keeps its names.

struct RenderSettings {
    /// Seconds since midnight. Drives ambient, fog and whether lamps carry.
    int timeOfDay = 20 * 3600;
    /// WEATHER. The sky is skyAt(timeOfDay, weather); a clear Weather (the
    /// default) is the sky every frame before the weather lane was drawn
    /// under. Session::drawFrame fills it from Session::weather().
    Weather weather;
    /// How far the DDA walks before giving up and calling it sky, in tiles.
    /// Fog closes well inside this, so raising it alone buys very little —
    /// kept proportional to lighting.cpp's own fogDistance ceiling.
    float maxDistance = 54.0F;
    /// z levels drawn below and above the eye's own level. Four below reaches
    /// the harbour floor from the quay; six above clears the tallest authored
    /// roof. The substrate under the district is solid WALL all the way down,
    /// so nothing below the window can ever be visible.
    int levelsBelow = 4;
    int levelsAbove = 6;
    bool drawSprites = true;
    /// NOTE there is no skyline switch. The city backdrop (the owner's pick,
    /// the stepped silhouette -- see kSkyline in world_renderer.cpp) is simply
    /// how the southern sky looks. It writes SKY PIXELS ONLY -- it never
    /// touches depth, geometry, or anything the sim can see -- so world
    /// geometry occludes it exactly the way a real landmark is occluded, and
    /// the world hash cannot move.
    /// Lights that are not in the baked sidecar because they come and go: a
    /// tavern hearth, the candles on its tables. See dynamicGlowAt().
    std::vector<Lamp> dynamicLamps;
};

/// What a frame turned out to be. Cheap to compute, and the only way a test can
/// assert that a render did something without a human looking at it.
struct FrameStats {
    std::size_t skyPixels = 0;
    std::size_t worldPixels = 0;
    std::size_t spritePixels = 0;
    /// The subset of spritePixels contributed by billboards flagged `person`.
    /// See SpriteInstance::person for the defect this exists to catch.
    std::size_t actorPixels = 0;
    /// #78: how many of the WARD's own bodies actually put a pixel on screen.
    /// A count of billboards, so it is a count of PEOPLE -- each ward actor is
    /// exactly one -- and it is what a capture reports as evidence. It is
    /// evidence and never a gate: the owner ruled out "a frame must contain
    /// actors" outright, because a cellar or a back lane at four in the morning
    /// is legitimately empty and a build that failed on that would be lying.
    std::size_t wardActorsDrawn = 0;
    /// Mean luminance of the whole frame, 0..1.
    float meanLuma = 0.0F;
    /// Distinct packed colours, capped — a solid fill scores 1.
    std::size_t distinctColours = 0;
    /// Nearest and furthest world surface actually drawn, in tiles.
    float nearestDepth = 0.0F;
    float furthestDepth = 0.0F;
};

class WorldRenderer {
public:
    WorldRenderer(const sim::TileQuery& tiles, const TileAtlas& atlas, std::vector<Lamp> lamps);

    /// Draws one frame. Clears the buffer first.
    FrameStats renderFrame(Framebuffer& target, const Camera& camera,
                           const RenderSettings& settings,
                           const std::vector<SpriteInstance>& sprites) const;

    [[nodiscard]] const std::vector<Lamp>& lamps() const noexcept { return lamps_; }
    [[nodiscard]] const LampGlow& glow() const noexcept { return glow_; }

    /// The lamps as billboards — a flame at each authored source. Until actors
    /// arrive these are the only living things in the frame, and they are what
    /// proves the sprite path works.
    [[nodiscard]] std::vector<SpriteInstance> lampSprites(float phase) const;

private:
    [[nodiscard]] bool voxelAt(std::int32_t x, std::int32_t y, std::int32_t z,
                               Voxel& out) const noexcept;

    [[nodiscard]] std::uint32_t columnMask(std::int32_t x, std::int32_t y) const noexcept;

    void drawSprite(Framebuffer& target, const Camera& camera, const RenderSettings& settings,
                    const SkyState& sky, const SpriteInstance& sprite, float focal, float horizon,
                    std::size_t& spritePixels) const;

    const sim::TileQuery* tiles_;
    const TileAtlas* atlas_;
    std::vector<Lamp> lamps_;
    LampGlow glow_;
    /// Bit z set when cell (x, y, z) draws anything at all.
    std::vector<std::uint32_t> columnMask_;
};

}  // namespace granadad::render
