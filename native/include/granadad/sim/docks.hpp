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
#include <string_view>

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

/// Tarwalk, three tiles off the Gilded Gull's frontage, looking west down the
/// working spine with the door lamp over your left shoulder and the harbour
/// over your right.
///
/// THIS IS A SHOT, NOT A COORDINATE, and it is the third attempt at it.
///
/// S1 spawned at (146, 64) facing 300 and called that "the view the district is
/// most recognisable from": 48% empty sky over a one-tile wall. S2 moved it to
/// (152, 60) facing 165 to put the Gull's granite frontage across the street --
/// correct at the time, and then polish-1 made a storey three tiles instead of
/// one and the frontage stopped being something you looked over. What the S2
/// spawn ACTUALLY shipped, measured off the built .exe at 960x540:
///
///     08:00  ward=661  near=7   seen=0   sky px=6712   luma=0.18
///     20:00  ward=661  near=14  seen=3   sky px=6712   luma=0.13
///
/// Seven people within twelve tiles and none of them on screen at eight in the
/// morning. The frame is a dark slot between two warehouse walls; walk the
/// thirty steps the capture harness walks and it seals completely (sky px=0,
/// seen=0). The owner played it and reported "there were no people". The
/// population was 661 and correct. The SHOT was wrong.
///
/// #79 re-aimed it by measuring rather than by argument: a grid of 400 stands
/// across the Tarwalk reach either side of the Gull, four hours each, ranked by
/// how many of the ward the frame actually DRAWS. This stand won it, and then
/// won the eye test as well. What is in the frame, and why each of the three
/// is there:
///
///   THE STREET      the Tarwalk runs away west for sixty tiles with nothing
///                   across it, so the ward's own traffic is in shot instead of
///                   behind a wall. seen= goes 20 / 18 / 28 / 30 at
///                   02 / 08 / 14 / 20 against the old spawn's 3 / 3 / 1 / 3.
///   THE GULL        its north frontage fills the left of the frame and the
///                   door lamp at (153,66) burns at bearing 221 -- about 20
///                   degrees left of centre, in shot at every hour the doors
///                   are open and the brightest thing in the 02:00 frame.
///   THE HARBOUR     open water and the finger piers read at the right of the
///                   frame, because at this heading the frustum's right edge
///                   clears the warehouse line north of the street.
///
/// 22% sky, and walking forward walks you down the street rather than into a
/// wall. The Gull is still four seconds away, so the first thing a player can
/// walk into is still a room with people in it.
///
/// Same connected component as both older spawns -- all three stand on the
/// Tarwalk quay apron -- so every reachability count below is unchanged, and
/// test_tile_query.cpp re-derives all five from the baked bytes to prove it.
inline constexpr std::int32_t kSpawnTileX = 156;
inline constexpr std::int32_t kSpawnTileY = 63;
inline constexpr std::int32_t kSpawnBand = kBandQuayside;
inline constexpr Angle kSpawnYaw = angle_from_degrees(265);

/// The tile the harbour-walk case starts from, and it is NOT the spawn.
///
/// test_render.cpp walks north off the Tarwalk to the quay lip and requires the
/// lower half of the frame to recede and darken into open water. That is a
/// claim about the RENDERER, and hanging it off kSpawnTileY made it a claim
/// about the spawn as well: re-aiming the opening shot in #79 would have turned
/// it red for a reason that has nothing to do with what it tests. So the walk
/// has its own named start, on the open apron where the quay edge is four tiles
/// north and nothing is in the way.
inline constexpr std::int32_t kQuayApproachX = 152;
inline constexpr std::int32_t kQuayApproachY = 60;

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
/// RE-DERIVED for the archetype-diversity pass (Docks building material/height/roof-cap
/// pass): only kReachableFromSpawn and kReachableOnMidSlope moved (-132 and -132 tiles;
/// Quayside/Upper/BelowQuay did not move by one tile). The cause is entirely roof-cap
/// authoring in tools/scripts/gen_docks_surface.py: the workshop/industrial and market
/// buckets lost roof caps they used to have (K06/K07-main/K07-lean-to/K09/K20/K22/K23 --
/// "no roof cap, reinforce openness" is the deliberate bucket rule), which converts those
/// roof cells from standable FLOOR to open air; the shop bucket partly offsets this by
/// gaining roof caps it never had (K02/K26/K27/K28). Net: 933 fewer standable cells on the
/// mid-slope band (see kStandableOnMidSlope below -- same delta, verified by direct area
/// arithmetic over every changed rect: 1,118 sq. tiles of roof removed, 185 sq. tiles added,
/// net -933), of which only 132 had been reachable from the spawn by ordinary walking in the
/// first place -- the rest were already same-z-isolated roof decks by design (see the S5
/// section below). test_ward_actors.cpp -- the suite that actually covers actor home/job
/// reachability -- is UNCHANGED and green: no actor's home or job route crosses any of these
/// roof cells, so this is a district-wide walkability-audit number moving for a documented
/// reason, not a sealed route.
///
/// RE-DERIVED for DISTRICT PHASE B (Thresholds: the Saltgate gate-house, the four compound
/// gate frames, the Mission's lantern-turret). This pass raises masonry and never removes
/// any, so every number below can only fall, and the accounting is cell-exact.
///
/// TWENTY-TWO standable cells were built over, and they split cleanly in two:
///
///   * SIXTEEN roof cells, (86-89, 66-69) on K17's z12 roof cap (world z20), taken by the
///     lantern-turret's 4x4 shaft. Every one of them was a roof deck with no walking route
///     onto it, so kReachableFromSpawn does not move by a single tile for the turret -- only
///     kStandableOnMidSlope and kReachableWithRoofMoves do.
///   * SIX street cells at the gate-house, and these are the only cells in the district this
///     pass takes OUT of the walking map: (80,115) and (81,115) on the z12 Terrace Walk
///     frontage under the east tower (world z20), and (70,116)/(71,116)/(80,116)/(81,116) on
///     the z13 band-edge ledge under both towers (world z21).
///
/// So kStandableOnMidSlope is -18 (16 roof + 2 street), kStandableOnUpper is -4, and
/// kReachableFromSpawn is -6 and NOT -22. Quayside, BelowQuay and the roof-slum plane
/// (kStandableOnRoofs) do not move by one tile, because the pass authored nothing on them.
///
/// THE OATH, DISCHARGED BY MEASUREMENT AND NOT BY ARGUMENT. A flood fill over stepBand from
/// the spawn, run against the old bytes and the new ones and differenced, loses exactly those
/// six cells and gains none -- zero collateral, no pocket sealed anywhere else in the ward. No
/// marker, script_anchor, patrol waypoint, garbage bin, victualler stand or guard post stands
/// on any of the 128 changed cells, and not one script_anchor in the map changes standability.
/// The single route this genuinely costs is named rather than hidden: the 1-tile-deep ledge at
/// x62-69, y116 (C1's ring wall north of it, K21's north wall south of it) loses its east end
/// onto the Rise and becomes a dead end, while staying reachable along its whole length from
/// the west. The Rise itself -- roadway, both kerbs and all eight ramp cells at (72-79, 116)
/// -- is untouched, which is the whole point of a gate you walk through rather than a door.
///
/// RE-DERIVED FOR DISTRICT PHASE C (Quarters: the Quayward's second gate, two gate-lintel
/// roof bridges, one roof hut shrunk off a deck it was sealing, three roof caches). This
/// pass both opens and closes, twenty-seven cells in all, and the two fills move for
/// completely different reasons -- which is the whole point of keeping both.
///
/// THE WALKING FILL MOVES BY TWO, AND THE TWO ARE THE GATE. C1's south ring wall is pierced
/// at (68,115)/(69,115) local (world (100,147)/(101,147), band 20) with a pair of RAMP cells,
/// so the band-edge ledge at y116 that District Phase B left a dead end now walks down into
/// the compound's courtyard and out its east gate onto the Rise. Both ramp cells are
/// themselves newly standable and newly reachable; NOTHING ELSE joins, because both ends of
/// the gate were already reachable -- the ledge from the west, the courtyard from the Rise.
/// The gate is a shortcut, not a rescue: from the ledge's east end to the courtyard's east
/// end was 54 walking steps the long way round and is now 10.
///
/// THE ROOF FILL MOVES BY FIFTY-FIVE, and every one is a roof:
///
///   +43 on the roof-slum plane (band 22). roofhut_12 on C3's deck spanned the deck's whole
///       depth and its east wall column sealed 42 cells behind it -- cells no move in the
///       game could reach, and precisely the standing gap between kStandableOnRoofs and
///       kRoofReachableOnRoofs. Shrinking the hut one row north opens them, plus the 6 cells
///       of its old north wall, less the 4 its new north wall stands on and 1 cache crate.
///   +11 on the upper band (21): the twelve cells of the two gate-lintel bridges, less one
///       cache crate. A body mantles the bridge off the flanking roof deck, walks the span
///       and steps down the far side -- so C2's four ground-level roof decks are one circuit
///       and C4's thatch plane is one, where before they were three and two.
///    +1 on the mid-slope band (20): the two gate ramps, less one cache crate.
///
/// AND THE NUMBER THIS PASS EXISTS FOR: kRoofReachableOnRoofs is now 1,707, which is
/// kStandableOnRoofs exactly. The world-z22 roof-slum plane is wholly reachable for the first
/// time since it was authored, and the last unreachable roof island in the district is gone.
///
/// THE OATH, DISCHARGED BY MEASUREMENT. Walking fill from the spawn, old bytes against new,
/// differenced: it GAINS the two ramp cells and LOSES NOTHING -- no pocket sealed anywhere in
/// the ward. The roof fill loses exactly six cells and all six are cells this pass
/// deliberately built on: the four of roofhut_12's interior under its moved north wall, and
/// two of the three cache crates. No marker, script_anchor, patrol waypoint, bin, victualler
/// stand or guard post sits on any of the twenty-seven changed cells.
///
/// AND ONE THING THE FILLS DO NOT SAY, learned from the gate rather than from a fill: a
/// fourth tuning was authored, tested and REMOVED. Breaking two cells of C2's roof-slum
/// parapet joined its slum deck to c02's roof and made C2's roofs a single 874-cell circuit
/// -- and drained the roof slum, because UP over a broken parapet is a mantle (a PLAYER verb)
/// while DOWN is stepBand's own down-clause, which every body in the ward has. The C2 slum
/// plane's walking region went from 210 cells on its own band to 17,208 across four bands,
/// the built .exe reported ward=655 and roof=7/7 against 656 and 8/8, and a roof tenant who
/// should never leave the roof (gazetteer 2.5/2.6) left it. gen_docks_surface.py now carries
/// a generator-level guard that refuses any map where a roof-slum plane's walking region
/// escapes its own band and footprint. THE ROOF ROAD IS A PLAYER ROAD.
inline constexpr std::int32_t kReachableFromSpawn = 16918;
inline constexpr std::int32_t kReachableOnQuayside = 11089;
inline constexpr std::int32_t kReachableOnMidSlope = 2911;
inline constexpr std::int32_t kReachableOnUpper = 2027;
/// Under the piers and down at the strand — one level below the quay.
inline constexpr std::int32_t kReachableBelowQuay = 891;

/// Tiles a body can stand on, per band, over the whole district. S1: 11,310 /
/// 7,369 / 3,901, before the doorway lintels counted as standable.
/// PHASE C: MidSlope +1 (two gate ramps, one cache crate), Upper +11 (twelve bridge cells,
/// one cache crate). Quayside and BelowQuay do not move by a tile -- nothing was authored
/// on either.
inline constexpr std::int32_t kStandableOnQuayside = 11432;
inline constexpr std::int32_t kStandableOnMidSlope = 6509;
inline constexpr std::int32_t kStandableOnUpper = 3928;

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

/// PHASE C: 1,706 -> 1,707, the first time this number has ever moved. Six cells of
/// roofhut_12's old north wall become deck, four become its new north wall, one becomes a
/// cache crate: +6 -4 -1 = +1.
inline constexpr std::int32_t kStandableOnRoofs = 1707;

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
/// RE-DERIVED alongside kReachableFromSpawn above, same cause, same -933: every one of
/// those roof cells was already counted here (a roof move only ever ADDS to the walk-only
/// number), so removing them moves this number by the identical amount. Nothing on the
/// compound roof-slum plane (kStandableOnRoofs, kRoofReachableOnRoofs) or the upper band
/// moved at all -- this pass never touched a compound.
/// RE-DERIVED for DISTRICT PHASE B, and this is the number that shows the turret up. The
/// walking fill loses 6 cells; this one loses 22 -- the same 6, plus the 16 cells of K17's
/// roof cap the lantern-turret's shaft stands on, which were mantle-reachable and nothing
/// else. That gap between -6 and -22 IS the statement that the turret took a roof and not a
/// street. kRoofReachableOnUpper falls by the 4 ledge cells at (70,116)/(71,116)/(80,116)/
/// (81,116); kRoofReachableOnRoofs does not move by one tile, because the shaft rises from a
/// z12 roof cap and the roof-slum plane is z14 -- this pass, like the archetype pass before
/// it, never touched a compound roof.
inline constexpr std::int32_t kReachableWithRoofMoves = 24060;
inline constexpr std::int32_t kRoofReachableOnUpper = 3871;
inline constexpr std::int32_t kRoofReachableOnRoofs = 1707;

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

/// The ward's own word for whatever is standing at (x, y, band), checked in
/// table order so a building inside a street wins over the street -- the same
/// rule render::Session::placeLabel has used since S2. Empty when nothing
/// authored covers the tile, which is the honest answer for the two-thirds of
/// the district that is compounds, yards and back lanes nobody has named yet.
///
/// #81. PULLED OUT OF THE RENDER LAYER SO A SIM-SIDE CALLER CAN NAME A PLACE
/// TOO. The HUD has asked this question since S2; the radiant quest generator
/// is the first caller that is not the HUD, and it asks it of a body that
/// moved there on its own schedule rather than of the player -- which is the
/// whole difference between a job that says "the Weighhouse" because a raws
/// file was written that way once and a job that says it because that is
/// where the body actually is RIGHT NOW.
[[nodiscard]] inline std::string_view placeNameAt(std::int32_t x, std::int32_t y,
                                                  std::int32_t band) noexcept {
    for (std::size_t i = 0; i < kPlaceCount; ++i) {
        const Place& place = kPlaces[i];
        if (band == place.band && x >= place.x0 && x <= place.x1 && y >= place.y0 &&
            y <= place.y1) {
            return place.name;
        }
    }
    return std::string_view{};
}

/// What a band alone says, when no street name covers the tile. Verbatim the
/// four strings render::Session::placeLabel has always fallen back to.
[[nodiscard]] inline std::string_view bandFallbackLabel(std::int32_t band) noexcept {
    if (band == kBandQuayside) {
        return "THE DOCKS - QUAYSIDE";
    }
    if (band == kBandMidSlope) {
        return "THE DOCKS - MID SLOPE";
    }
    if (band == kBandUpper) {
        return "THE DOCKS - UPPER";
    }
    if (band < kBandQuayside) {
        return "UNDER THE PIERS";
    }
    return "THE DOCKS";
}

/// placeNameAt, with bandFallbackLabel behind it -- never empty. What "where
/// is this body standing" reads as in one call.
[[nodiscard]] inline std::string_view placeLabelAt(std::int32_t x, std::int32_t y,
                                                   std::int32_t band) noexcept {
    const std::string_view named = placeNameAt(x, y, band);
    return named.empty() ? bandFallbackLabel(band) : named;
}

}  // namespace granadad::sim::docks
