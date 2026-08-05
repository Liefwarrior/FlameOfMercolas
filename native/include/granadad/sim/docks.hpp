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

/// Tarwalk, six tiles off the Gilded Gull's door, facing the frontage.
///
/// MOVED IN S2, and the old one is worth recording. S1 spawned at (146, 64)
/// facing 300 degrees and its header called that "the view the district is most
/// recognisable from". It was not: the frame is 48% empty sky over a one-tile
/// wall, and it is the weakest picture the renderer produces. The S1 review
/// found the same frame at yaw 120 sold the visual target and the authored
/// default did not.
///
/// This one is picked for what the game is: you arrive on the working quay with
/// the whole granite frontage of the Gilded Gull (K03, DOCKS-GAZETTEER §3)
/// across the street, its door lamp burning in the gap, a warehouse shoulder to
/// the right and the Tarwalk running away to the left. 32% sky, and the sky is
/// doing work. It is also where S2's tavern is, so the first thing a player can
/// walk into is a room with people in it.
///
/// Same connected component as the old spawn -- both stand on the Tarwalk quay
/// apron -- so every reachability count below is unchanged, and
/// test_tile_query.cpp re-derives all five from the baked bytes to prove it.
///
/// THE "32% SKY" ABOVE IS NO LONGER TRUE, and saying so is the honest thing to
/// do rather than quietly deleting the sentence. polish-1 made a storey three
/// tiles instead of one (sim/vertical_scale.hpp), and the Gull's frontage six
/// tiles across the Tarwalk went from something you looked over to something
/// 8 m tall. Measured at 960x540, 13:00: the same frame is now 1.3% sky.
///
/// It is a better picture -- a lit doorway with somebody standing in it, under
/// three storeys of granite and timber, which is what the district is supposed
/// to be -- but it is a DIFFERENT picture from the one this comment argued for,
/// and the argument was partly about the sky. Whether the spawn should move
/// back a few tiles to put a roofline against the sky again is a question for
/// the owner, not a thing to change under cover of a scale fix: the spawn is
/// load-bearing for five pinned reachability counts and for every screenshot
/// comparison in the suite.
///
/// VERIFICATION GAP (polish-1): nothing tests the sky fraction of this frame,
/// which is exactly why the claim above could go stale without a build going
/// red. test_render.cpp asserts ground under the boots and sky above a
/// pitched-up camera; neither of them would notice this spawn walling itself in
/// completely.
inline constexpr std::int32_t kSpawnTileX = 152;
inline constexpr std::int32_t kSpawnTileY = 60;
inline constexpr std::int32_t kSpawnBand = kBandQuayside;
inline constexpr Angle kSpawnYaw = angle_from_degrees(165);

/// Every tile reachable from the spawn under the movement rules, counted by a
/// flood fill in test_tile_query.cpp. Pinned so that a change to the step rules
/// which quietly seals a band off cannot pass as an improvement.
///
/// RE-DERIVED IN S2 after the headroom correction in tile_query.hpp. The S1
/// numbers were 15,328 / 10,247 / 2,279 / 1,911 / 891, and the district gained
/// 1,606 reachable tiles when its doorways stopped being sealed by their own
/// lintels. All of the growth is on the two bands that have buildings on them;
/// the upper band and the strand below the quay did not move by a single tile,
/// which is what says the change opened INTERIORS and not the map.
///
/// RE-DERIVED AGAIN IN S5, and again the correction OPENED the map rather than
/// sealing it. The down-clause of the step rule let a body walk into a wall on
/// an upper floor and fall THROUGH it to the room below -- 429 places in this
/// district, including every interior partition of the Gilded Gull's guest
/// floor. Bodies that used to drop off the upper band and get stuck there stay
/// on it now: the whole of the +120 is on z21 and not one tile of it is
/// anywhere else, which is what says the change stopped a FALL rather than
/// opening a door.
inline constexpr std::int32_t kReachableFromSpawn = 17054;
inline constexpr std::int32_t kReachableOnQuayside = 11089;
inline constexpr std::int32_t kReachableOnMidSlope = 3043;
inline constexpr std::int32_t kReachableOnUpper = 2031;
/// Under the piers and down at the strand — one level below the quay.
inline constexpr std::int32_t kReachableBelowQuay = 891;

/// Tiles a body can stand on, per band, over the whole district. S1: 11,310 /
/// 7,369 / 3,901, before the doorway lintels counted as standable.
inline constexpr std::int32_t kStandableOnQuayside = 11432;
inline constexpr std::int32_t kStandableOnMidSlope = 7459;
inline constexpr std::int32_t kStandableOnUpper = 3921;

// --- S5: the roofs ----------------------------------------------------------
//
// THE DISTRICT WAS TWO THIRDS SHUT. Every number above is the WALKING rule --
// up only where a ramp or a stair was authored -- and under it 8,132 standable
// cells of the baked Docks could not be got to at all: the compound roof decks,
// the rooftop slums, and the whole of the plane below. That is not a defect;
// DOCKS-GAZETTEER section 2.6 files it as design, "roof-slum planes whose
// same-z isolation is the point until the law/economy layers learn to climb
// (S5+)".
//
// S5 is the sprint that learns. The three roof moves in player.hpp -- mantle,
// leap, drop -- are the whole of it, and test_roofrun.cpp re-derives every
// number below from the baked bytes with a flood fill that uses nothing but
// those moves and the ordinary walking one.

/// The roof-slum plane: the tops of the two-storey compounds. Local z14 in
/// DOCKS-GAZETTEER section 2.1's z-profile, world z22. Before S5, ZERO of its
/// 1,706 standable cells were reachable from the spawn by any means.
inline constexpr std::int32_t kBandRoofs = 22;

inline constexpr std::int32_t kStandableOnRoofs = 1706;

/// Reachable from the spawn once the body can climb, leap and drop. The walk-
/// only number is kReachableFromSpawn (17,054) and the S5 correction to the
/// down-rule is the ONLY thing that moved it. Everything below is what the new verbs ADD on top.
/// 7,906 tiles, and every one of them is a roof, a gallery or a back way onto
/// one: every standable cell of the mid-slope band bar sixty-nine, and 1,664 of
/// the 1,706 on the roof-slum plane that had NONE at all.
///
/// WHAT THIS NUMBER IS, EXACTLY -- restated in S6 because the S5 review found
/// the claim wider than the proof. This is what THE RULES PERMIT, counted by a
/// flood fill in test_roofrun.cpp that offers every roof move the rules allow
/// from every cell. It is NOT a count of cells PlayerBody has been driven to.
///
/// The two differ in one known place and it is worth naming: the body takes the
/// authored stair unconditionally whenever it is standing on a climbable cell
/// (PlayerBody::mantle, and the note there on why the Gull's guest floor was
/// unreachable without it), so from a stair cell the body will never offer the
/// facing mantle -- while the fill offers both. The fill is therefore an UPPER
/// BOUND: every cell it counts is reachable under the movement rules, and a
/// cell reachable only by mantling a wall while standing on a flight of stairs
/// is counted here and cannot currently be walked to.
///
/// The number that IS proved of the body is the one test_roofrun.cpp's body-
/// level cases pin -- the Gull's roof, the alley crossing and the drop -- and a
/// mutation that guts PlayerBody::mantle's stair clause turns those red while
/// leaving the three reachability counts green. That asymmetry is the reason
/// this paragraph exists.
inline constexpr std::int32_t kReachableWithRoofMoves = 24960;
inline constexpr std::int32_t kRoofReachableOnUpper = 3864;
inline constexpr std::int32_t kRoofReachableOnRoofs = 1664;

/// The lowest band a roof move may put a body on in THIS district. Everything
/// under the harbour surface is the unbuilt dungeon -- see
/// PlayerBody::setLandingFloor for the trapdoor this closes and why the seabed
/// looked walkable in the first place.
inline constexpr std::int32_t kLandingFloor = kHarbourSurfaceBand;

// --- named places -----------------------------------------------------------
//
// ADDED IN S2, because the HUD was printing street names it did not know. S1's
// bandLabel() took the z-level and NOTHING ELSE, and mapped 19/20/21 to
// TARWALK / ROPEWYND / SALTGATE RISE. Standing at (200, 100, z21) -- the far
// east of the district, nowhere near it -- the HUD read SALTGATE RISE. A label
// that asserts a location it cannot know is worse than no label.
//
// Every rectangle below is a real authored footprint, taken from the frect()
// calls in tools/scripts/gen_docks_surface.py that laid the paving down, with
// the one-chunk VOID border offset applied (local + 32, local z + 8).
// test_place.cpp re-derives that these are actually paved in the baked world.
//
// Anywhere not in the table gets the band it is on and no street name, which is
// the honest answer for the two-thirds of the district that is compounds,
// yards and back lanes nobody has named yet.

struct Place {
    const char* name;
    std::int32_t x0;
    std::int32_t y0;
    std::int32_t x1;
    std::int32_t y1;
    std::int32_t band;
};

/// Checked in order, so a building inside a street wins over the street.
inline constexpr Place kPlaces[] = {
    // K03, and the only interior S2 staffs. Both floors.
    {"THE GILDED GULL", 146, 66, 160, 79, kBandQuayside},
    {"THE GILDED GULL - ROOMS", 146, 66, 160, 79, kBandMidSlope},
    // The working spine, in its three authored reaches.
    {"TARWALK", 32, 60, 111, 65, kBandQuayside},
    {"TARWALK", 112, 60, 161, 65, kBandQuayside},
    {"TARWALK", 162, 62, 195, 67, kBandQuayside},
    // The lower-middle road, paved reach then where the paving gives out.
    {"ROPEWYND", 36, 92, 179, 97, kBandQuayside},
    {"ROPEWYND - THE DIRT END", 180, 92, 209, 97, kBandQuayside},
    // The N-S spine, one leg per band.
    {"SALTGATE RISE", 104, 58, 111, 97, kBandQuayside},
    {"SALTGATE RISE", 104, 129, 111, 147, kBandMidSlope},
    {"SALTGATE RISE", 104, 149, 111, 159, kBandUpper},
    {"GALLOWS ROW", 36, 152, 220, 154, kBandUpper},
    // North of the quay edge the district is finger piers and open water.
    {"THE LONG PIERS", 32, 32, 223, 57, kBandQuayside},
};

inline constexpr std::size_t kPlaceCount = sizeof(kPlaces) / sizeof(kPlaces[0]);

}  // namespace granadad::sim::docks
