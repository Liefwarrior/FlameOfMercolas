#pragma once

// THE VERTICAL SCALE, in tiles: the voxel classifier, the 3D chunk mesher and
// every scene builder name a level's surface height through this one header.
//
// EVERY HEIGHT IN THE WORLD COMES THROUGH HERE. The chunk mesher, the actor
// instancer, the lamp halos and the signage all place things in the air; the
// moment two of them disagree about how tall a level is, people stand on
// ceilings. It used to
// be an implied 1.0 -- every building one tile high -- and this constant is
// what killed that. sim/vertical_scale.hpp carries the argument for the
// number.

#include <cstdint>

#include "granadad/sim/vertical_scale.hpp"

namespace granadad::render {

/// Tile-widths of vertical height in one z-level.
inline constexpr float kBandHeight = static_cast<float>(sim::kTilesPerBand);

/// The height of level `z`'s own walking surface, in tiles. A FLOOR at z is
/// walked on at exactly this height and a WALL at z rises from here to the
/// surface of z + 1, which is what makes the two stack.
[[nodiscard]] constexpr float bandSurface(std::int32_t z) noexcept {
    return static_cast<float>(z) * kBandHeight;
}

}  // namespace granadad::render
