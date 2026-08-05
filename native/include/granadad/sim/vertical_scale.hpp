#pragma once

// HOW TALL A STOREY IS. One number, and everything vertical in the game reads
// it from here.
//
// THE BUG THIS FILE EXISTS TO CLOSE
//
// Until now nothing said. The renderer drew a z-band as `z` to `z + 1.0` in the
// same units it used for a tile's WIDTH, so one storey of a building was one
// tile wide and one tile tall, and the eye sat at 205/256 = 0.80 of that. Play
// it and the Docks are a scale model: every warehouse is a kerb, every roof is
// a doorstep, and a three-storey compound is a garden wall you could see over.
// That is the failure this projection is most often fudged into, and the Java
// build's DECISIONS.md named it out loud when it ruled the same question:
// setting a band to one tile-width is "the crawlspace failure".
//
// WHAT A TILE IS, IN METRES, AND WHY THAT SETTLES IT
//
// The grid's occupancy rule is one actor per cell, and the body already agrees:
// sim/player.hpp's kBodyRadius is 90/256, so a person's footprint is 0.70 of a
// tile square. A human standing square is about 0.64 m across the shoulders and
// elbows, which puts ONE TILE at roughly 0.9 m. That number is not invented
// here -- it is implied by a collision constant that has been in the build
// since S1.
//
// A storey is floor-to-floor about 2.5-2.7 m. At 0.9 m to the tile that is
// between 2.75 and 3.0 tiles.
//
// WHY 3 AND NOT 2.75
//
// The Java build ruled 2.75 (DECISIONS.md, "First-person EXPLORATION camera").
// Eli, playing this build, ruled "triple". They are the same answer to within
// the precision anybody can see, and 3 wins the tie on the constraint this
// codebase actually has:
//
//   INTEGER MATHS. The body's vertical axis is Q8. With kTilesPerBand = 3 a
//   band is exactly 768 Q8 sub-tile units and the eye's 435 Q8 (1.70 tiles)
//   converts to band-relative Q8 as exactly 145, with no remainder anywhere in
//   the chain. 2.75 gives 704, and 435 * 256 / 704 is 158.18 -- a rounding in
//   the one part of the build that is not allowed to round.
//
// So: a storey is three tiles, about 2.7 m, and the eye is 1.70 tiles (1.55 m)
// off the floor inside it.
//
// WHICH AXIS IS IN WHICH UNIT -- read this before touching anything vertical
//
//   * The SIMULATION's z axis is BAND-RELATIVE Q8. PlayerBody::feetZ() is
//     `band << 8`: 256 units is one whole band, not one tile. Every band-shaped
//     rule (stepBand, mantleBand, landingBand, kMaxDropBands) speaks bands and
//     is unaffected by anything in this file.
//   * The RENDERER's z axis is TILE-widths, the same unit as x and y, because
//     that is what a projection needs. Converting is one multiply by
//     kTilesPerBand and it happens at every single place a level number becomes
//     a height. There is no third unit.

#include <cstdint>

namespace granadad::sim {

/// Tile-widths of vertical height in one z-band. See the header comment for the
/// whole argument; the short version is that a band is a STOREY and a storey is
/// not one shoulder of standing room.
inline constexpr std::int32_t kTilesPerBand = 3;

/// One band in the Q8 sub-TILE units the renderer's world space uses. Exactly
/// 768, which is the point of choosing an integer.
inline constexpr std::int32_t kBandHeightQ8 = kTilesPerBand << 8;

// ---------------------------------------------------------------------------
// how big a person is, in tiles
// ---------------------------------------------------------------------------
//
// These live beside the storey height rather than in the renderer because the
// PLAYER's eye and an NPC's head have to agree by construction. They did not
// before: the body's eye was 0.80 tiles and an actor billboard's head topped
// out at 0.93, so everyone in the district was a metre tall and nobody could
// tell, because the buildings were a metre tall too.

/// Eye above the feet, Q8 tile-widths. 435/256 = 1.70 tiles, about 1.55 m.
inline constexpr std::int32_t kEyeHeightTilesQ8 = 435;

/// Crown of the head above the feet, Q8 tile-widths. 480/256 = 1.875 tiles,
/// about 1.71 m -- so the eye sits a hand below the top of the skull, which is
/// where eyes are.
inline constexpr std::int32_t kStandingHeightTilesQ8 = 480;

}  // namespace granadad::sim
