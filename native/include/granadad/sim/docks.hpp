#pragma once

// Where a session starts, and the three names the district's walk bands have.
//
// These are facts about content/maps/baked/docks_surface.trojsav, read out of
// the actual baked bytes and pinned by test_docks.cpp. They live in one header
// because the client, the frame-capture path and the tests all need the same
// spawn, and a spawn that differs between them makes every screenshot
// comparison worthless.
//
// The world carries a one-chunk VOID border, so authored local tile (lx, ly, lz)
// from the Tiled source lands at world tile (lx + 32, ly + 32, lz + 8). Every
// number below is a WORLD tile.

#include <cstdint>

#include "granadad/sim/angle.hpp"

namespace granadad::sim::docks {

/// The baked world this district is.
inline constexpr const char* kWorldName = "docks_surface";

// --- the three walk bands ---------------------------------------------------
//
// DOCKS-GAZETTEER.md section 2.1 calls local z11 "quayside street level — the
// baseline walking plane", and the street climbs up-slope from there. In world
// z that is 19, 20, 21, and the FORM histogram of the baked file agrees: 11,401
// / 7,417 / 3,906 FLOOR cells respectively, with almost nothing walkable above.

/// Quayside street level. Tarwalk, the piers, the harbour edge.
inline constexpr std::int32_t kBandQuayside = 19;
/// One block up-slope.
inline constexpr std::int32_t kBandMidSlope = 20;
/// The top of the rise, nearest the off-map Inner Wall gate.
inline constexpr std::int32_t kBandUpper = 21;

/// The water surface sits at the TOP of z=18 — the harbour fills z=17 and z=18
/// at depth 7, and z=19 above it is open air. So the quay deck stands exactly
/// one level proud of the water, which is what makes the waterline read.
inline constexpr std::int32_t kHarbourSurfaceBand = 18;

// --- the spawn --------------------------------------------------------------

/// Tarwalk, the working spine, mid-district, facing west-north-west.
///
/// Chosen by search rather than by taste: of every standable tile on the
/// quayside and every facing, this one has four of the authored lamps in its
/// ninety-degree view with line of sight to three of them, twenty-three tiles
/// of open street ahead, and the harbour off the right shoulder. It is the view
/// the district is most recognisable from, and it is the one the sprint's
/// captures are taken from.
inline constexpr std::int32_t kSpawnTileX = 146;
inline constexpr std::int32_t kSpawnTileY = 64;
inline constexpr std::int32_t kSpawnBand = kBandQuayside;
inline constexpr Angle kSpawnYaw = angle_from_degrees(300);

/// Every tile reachable from the spawn under the movement rules, counted by a
/// flood fill in test_tile_query.cpp. Pinned so that a change to the step rules
/// which quietly seals a band off cannot pass as an improvement.
inline constexpr std::int32_t kReachableFromSpawn = 15328;
inline constexpr std::int32_t kReachableOnQuayside = 10247;
inline constexpr std::int32_t kReachableOnMidSlope = 2279;
inline constexpr std::int32_t kReachableOnUpper = 1911;
/// Under the piers and down at the strand — one level below the quay.
inline constexpr std::int32_t kReachableBelowQuay = 891;

/// Tiles a body can stand on, per band, over the whole district.
inline constexpr std::int32_t kStandableOnQuayside = 11310;
inline constexpr std::int32_t kStandableOnMidSlope = 7369;
inline constexpr std::int32_t kStandableOnUpper = 3901;

}  // namespace granadad::sim::docks
