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
#include "granadad/sim/tile_query.hpp"

namespace granadad::render {

/// Where the eye is and where it points. Derived from the body's integers at
/// draw time; never the other way round.
struct Camera {
    /// Eye position in world tile units.
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    /// Radians. 0 faces north, which is -Y; increases clockwise.
    float yaw = 0.0F;
    /// Radians. Positive looks up.
    float pitch = 0.0F;
    /// tan(horizontal field of view / 2). 1.0 is a 90-degree view.
    float hfovTan = 1.0F;

    /// Builds a camera from a body's Q8 position and BAM facing.
    [[nodiscard]] static Camera fromBody(std::int32_t xQ8, std::int32_t yQ8, std::int32_t eyeZQ8,
                                         std::int32_t yawBam, std::int32_t pitchBam,
                                         float hfovTan) noexcept;
};

/// Anything alive, or anything that should face the camera. Drawn after the
/// world against the depth buffer the world pass wrote.
struct SpriteInstance {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float halfWidth = 0.3F;
    float halfHeight = 0.4F;
    Rgb colour{1.0F, 1.0F, 1.0F};
    /// 0 draws as a lit solid; 1 draws as pure additive glow.
    float glow = 0.0F;
    /// How soft the billboard's edge is. 1 is the flame's smooth falloff; 0 is
    /// a hard-edged ellipse, which is what a body has to be to read as chunky
    /// at 320x180 instead of as a smudge.
    float softness = 1.0F;
    /// True when this billboard is part of a PERSON.
    ///
    /// It exists because of the S2 review. "The room is lit and full at nine"
    /// asserted `spritePixels > 0`, and a lit Gull carries nine flame sprites --
    /// so the check passed with every human being in the room invisible. This
    /// flag is what lets a frame say how much of it is people, separately from
    /// how much of it is candles.
    bool person = false;
};

struct RenderSettings {
    /// Seconds since midnight. Drives ambient, fog and whether lamps carry.
    int timeOfDay = 20 * 3600;
    /// How far the DDA walks before giving up and calling it sky, in tiles.
    /// Fog closes well inside this, so raising it buys very little.
    float maxDistance = 44.0F;
    /// z levels drawn below and above the eye's own level. Four below reaches
    /// the harbour floor from the quay; six above clears the tallest authored
    /// roof. The substrate under the district is solid WALL all the way down,
    /// so nothing below the window can ever be visible.
    int levelsBelow = 4;
    int levelsAbove = 6;
    bool drawSprites = true;
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
    struct Voxel {
        float bottom = 0.0F;
        float top = 0.0F;
        FaceKind topFace = FaceKind::BlockTop;
        std::uint16_t material = 0;
        /// FLUID-lane depth at this cell, 0..7. Tints the top face.
        int wetness = 0;
        bool water = false;
        bool hasSides = true;
    };

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
