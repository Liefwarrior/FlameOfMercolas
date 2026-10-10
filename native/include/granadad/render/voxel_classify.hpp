#pragma once

// WHAT A CELL IS, ANSWERED ONCE.
//
// The software raycaster (world_renderer.cpp) and the 3D chunk mesher
// (render3d/chunk_mesher.cpp) both turn tile lanes into faces, and the 3D
// programme's second-biggest risk (the renderer plan, section 4) is the two
// disagreeing about what a cell is: a wall the software pass draws and the
// mesher leaves out is a wall you walk into unseen, which is exactly the
// class of bug the S2 headroom finding was. So the classification -- what a
// FORM plus a FLUID depth becomes in tile-heights, which face art it wears,
// whether it has sides -- lives here, header-only, and both passes call it.
// A change to a slab thickness or to the water rule moves both pictures at
// once or neither.
//
// Also here: the two variant keys that pick a material's authored
// appearance variant for a cell, so a wall does not shimmer as you walk past
// AND wears the same variant in 3D that it wore in software.
//
// Floats are legal in this file. Nothing in it may be read by the simulation.

#include <cstdint>

#include "granadad/render/atlas.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render {

/// How far a FLOOR / RAMP / STAIR slab hangs below the level it is the surface
/// of. The walking surface is exactly at the level's own height, so the slab
/// has to be underneath it -- which is also why a pier deck is visibly a plank
/// with air under it when you stand at the water's edge.
///
/// ABSOLUTE TILE THICKNESSES, not fractions of a storey, and that distinction
/// is the reason they did not change when the storey tripled. A deck plank is
/// 0.22 of a tile -- call it 0.20 m -- of timber whether the room under it is
/// one tile high or three. Scaling these with the band would have given the
/// Long Quay a 0.60 m slab of decking, which is a bridge, not a pier.
inline constexpr float kFloorSlab = 0.22F;
inline constexpr float kRampSlab = 0.30F;
inline constexpr float kStairSlab = 0.36F;

/// Water surface height above its level's own floor, in tiles, from FLUID-lane
/// depth 1..7. A FRACTION OF THE BAND: a cell filled to the brim holds a band's
/// worth of water, so this one scales where the slabs above do not.
///
/// Depth 7 comes out at 0.70 of a band rather than 1.0, deliberately. The
/// harbour fills z=17 and z=18 at full depth and the quay deck's slab hangs
/// from 57.0 down to 56.78; a surface at the full 57.0 would z-fight the deck
/// along the entire waterfront. At 0.70 the waterline stands at 56.10, which is
/// 0.90 of a tile -- about 0.8 m -- below the deck: artifact-free, and an
/// actual tidal drop you can see from the quay edge.
[[nodiscard]] constexpr float waterSurface(int depth) noexcept {
    return static_cast<float>(depth) * 0.10F * kBandHeight;
}

/// One drawable cell: a box from `bottom` to `top` in tiles, wearing
/// `topFace` art on its top and `material`'s side art on its sides.
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

/// The classification. Returns false for a cell that draws nothing (air, the
/// void, water with more water above it).
///
/// OPEN air holding water is the harbour: a surface on the TOPMOST wet cell
/// of the column and nothing below it, because seven-deep water is opaque and
/// there is nothing down there to see. Water sitting on AUTHORED GROUND is a
/// puddle or a flooded cellar: that cell keeps its own geometry and the
/// wetness only tints its top face -- replacing a floor with a water plane
/// would delete the floor the player is standing on and leave them
/// apparently walking on the sea.
///
/// THE WALL IS THE STOREY. It rises from its own level's walking surface to
/// the next one's, so its side face is kBandHeight tiles tall -- three now,
/// one before, and that single line is the whole of what Eli was looking at
/// when he said every building appears to be one tile high.
///
/// VERIFICATION GAP (S2, and worse after polish-1): a RAMP is drawn as a
/// thinner flat slab, not as a slope. The movement rule is right (stepBand
/// authorises the climb); it is the picture that is wrong, in both renderers
/// now, and drawing the actual wedge is a change to be made HERE so both get
/// it at once.
[[nodiscard]] inline bool classifyVoxel(const sim::TileQuery& tiles, std::int32_t x,
                                        std::int32_t y, std::int32_t z, Voxel& out) noexcept {
    const content::TileForm form = tiles.form(x, y, z);
    const int depth = tiles.fluidDepth(x, y, z);

    if (form == content::TileForm::Open || form == content::TileForm::Void) {
        if (depth > 0 && tiles.fluidDepth(x, y, z + 1) == 0) {
            out.bottom = bandSurface(z);
            out.top = bandSurface(z) + waterSurface(depth);
            out.topFace = FaceKind::Water;
            out.material = 0;
            out.wetness = depth;
            out.water = true;
            out.hasSides = false;
            return true;
        }
        return false;
    }

    switch (form) {
        case content::TileForm::Wall:
            out.bottom = bandSurface(z);
            out.top = bandSurface(z) + kBandHeight;
            out.topFace = FaceKind::BlockTop;
            break;
        case content::TileForm::Floor:
            out.bottom = bandSurface(z) - kFloorSlab;
            out.top = bandSurface(z);
            out.topFace = FaceKind::FloorTop;
            break;
        case content::TileForm::Ramp:
            out.bottom = bandSurface(z) - kRampSlab;
            out.top = bandSurface(z);
            out.topFace = FaceKind::RampTop;
            break;
        case content::TileForm::Stair:
            out.bottom = bandSurface(z) - kStairSlab;
            out.top = bandSurface(z);
            out.topFace = FaceKind::StairTop;
            break;
        case content::TileForm::Void:
        case content::TileForm::Open:
        default:
            return false;
    }
    out.material = tiles.material(x, y, z);
    out.wetness = depth;
    out.water = false;
    out.hasSides = true;
    return true;
}

/// The integer mixer both variant keys go through (and TileAtlas::tileFor
/// mixes once more). Any pure function of the cell would do; this one is
/// what the shipped frames were drawn with.
[[nodiscard]] constexpr std::uint32_t variantMix(std::uint32_t value) noexcept {
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

/// The variant key a cell's SIDE faces are drawn with.
[[nodiscard]] constexpr std::uint32_t sideVariantKey(std::int32_t x, std::int32_t y,
                                                     std::int32_t z) noexcept {
    return variantMix(static_cast<std::uint32_t>(x) * 73856093U ^
                      static_cast<std::uint32_t>(y) * 19349663U ^
                      static_cast<std::uint32_t>(z) * 83492791U);
}

/// The variant key a cell's HORIZONTAL faces are drawn with.
[[nodiscard]] constexpr std::uint32_t flatVariantKey(std::int32_t x, std::int32_t y,
                                                     std::int32_t z) noexcept {
    return variantMix(static_cast<std::uint32_t>(x) * 6971U ^
                      static_cast<std::uint32_t>(y) * 40483U ^
                      static_cast<std::uint32_t>(z) * 1572869U);
}

/// Directional shading, shared by both passes. Sides that face away from the
/// light read darker: two constants, one per axis -- the cheapest form of
/// directional shading and the one Barony uses. A face on an X boundary
/// (looking east or west) gets kFacingX; on a Y boundary, kFacingY. A top is
/// lit in full and an underside at kUndersideLift.
inline constexpr float kFacingX = 0.78F;
inline constexpr float kFacingY = 0.94F;
inline constexpr float kUndersideLift = 0.62F;

/// What standing water does to the colour under it: the software pass lerps
/// the texel toward this by the pack's per-depth alpha. A puddle glosses a
/// flagstone; seven-deep harbour swallows it.
inline constexpr Rgb kDeepWaterTone{0.020F, 0.043F, 0.055F};

}  // namespace granadad::render
