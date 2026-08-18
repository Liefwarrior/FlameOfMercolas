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
#include "granadad/sim/vertical_scale.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// the vertical scale
// ---------------------------------------------------------------------------
//
// EVERY HEIGHT IN THE RENDERER COMES THROUGH HERE. It is public, and in the
// header rather than tucked in a .cpp, because three translation units place
// things in the air — the world pass, the lamp billboards and the actor
// billboards in session.cpp — and the moment two of them disagree about how
// tall a level is, people stand on ceilings.
//
// It used to be an implied 1.0: a level was drawn exactly as tall as a tile is
// wide, which made every building in the Docks one tile high and is the defect
// this constant was extracted to kill. sim/vertical_scale.hpp carries the
// argument for the number.

/// Tile-widths of vertical height in one z-level.
inline constexpr float kBandHeight = static_cast<float>(sim::kTilesPerBand);

/// The height of level `z`'s own walking surface, in tiles. A FLOOR at z is
/// walked on at exactly this height and a WALL at z rises from here to the
/// surface of z + 1, which is what makes the two stack.
[[nodiscard]] constexpr float bandSurface(std::int32_t z) noexcept {
    return static_cast<float>(z) * kBandHeight;
}

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

/// The two numbers every screen-space projection in this renderer is built
/// from -- `focal` turns a world offset into pixels, `horizon` is the row a
/// point at eye height projects to. renderFrame's own horizontal-face loop
/// and drawSprite both derive these from the framebuffer size and the camera
/// alone, so it is pulled out here rather than duplicated: a second, drifting
/// copy of "half the frame width over hfovTan" is exactly how a world-space
/// overlay (a sign, say) ends up misaligned with what the ray march drew.
struct Projection {
    float focal = 0.0F;
    float horizon = 0.0F;
};

[[nodiscard]] Projection projectionFor(const Camera& camera, int width, int height) noexcept;

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

    /// DISTRICT PHASE A: true for the two authored harbour lights that behave
    /// as BEACONS after dark -- the Weighhouse signal-mast lamp and the
    /// Mission's doctrinal night lamp. A beacon's additive glow takes a reduced
    /// fog wash at night, which is the honest way a light "carries": fog eats
    /// the wall it hangs on long before it eats the flame itself. Tagged by
    /// name in lampSprites(); nothing else may set it.
    bool beacon = false;

    // --- #78: a figure somebody drew, instead of an ellipse ------------------
    //
    // A 16x16 RGBA cutout, or null for the ellipse path. Both paths stay,
    // deliberately: the lamps are ellipses and always will be, the tavern's
    // fourteen are ellipses until somebody moves them, and a renderer that
    // could only draw textured quads would have to fake a flame with one.
    //
    // Alpha is a CUTOUT and not a blend. A texel is either the figure or it is
    // whatever is behind it, which is what keeps a body hard-edged at 320x180
    // instead of fringed -- the same argument `softness = 0` makes for the
    // ellipse, one level further down.
    /// True for a body belonging to the WARD's population rather than to the
    /// taproom. Both are people and both count toward actorPixels; this is what
    /// lets a capture report how much of the district's own roll a frame is
    /// looking at, separately from how many of the Gull's fourteen are in it.
    bool ward = false;

    const std::uint32_t* art = nullptr;
    /// Side of the art cell in texels. 16, always, but stated so the sampler
    /// does not carry the constant.
    int artSize = 0;
    /// The sub-rectangle of the cell that actually carries ink, inclusive. A
    /// figure drawn fourteen rows tall inside a sixteen-row cell floats two
    /// rows above the pavement if the quad is sized to the cell.
    int artU0 = 0;
    int artU1 = 0;
    int artV0 = 0;
    int artV1 = 0;
};

struct RenderSettings {
    /// Seconds since midnight. Drives ambient, fog and whether lamps carry.
    int timeOfDay = 20 * 3600;
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
