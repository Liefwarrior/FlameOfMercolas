// The roofs, asked of the owner's real baked Docks.
//
// S5. Every number here is re-derived from
// content/maps/baked/docks_surface.trojsav on every build; nothing is
// transcribed from a report. The point of the file is one claim, made in three
// ways so that a mutation to any one clause of the climb rule goes red on a
// case whose NAME says what broke:
//
//   the district was two thirds shut, and the three roof moves open it.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <set>
#include <tuple>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

const content::World& docksWorld() {
    static const content::World world =
        content::loadWorldFile(content::bakedMap(docks::kWorldName));
    return world;
}

using Cell = std::tuple<std::int32_t, std::int32_t, std::int32_t>;

constexpr std::int32_t kDx[4] = {1, -1, 0, 0};
constexpr std::int32_t kDy[4] = {0, 0, 1, -1};

/// Where a leap launched from (x, y, band) along (dx, dy) with `reach` tiles of
/// carry comes down, or the whole thing refused. Mirrors PlayerBody::leap's
/// search exactly — deliberately a second implementation, because a flood fill
/// that called the body would be testing the body against itself.
[[nodiscard]] bool leapLanding(const TileQuery& tiles, std::int32_t x, std::int32_t y,
                               std::int32_t band, std::int32_t dx, std::int32_t dy,
                               std::int32_t reach, Cell& out) {
    std::int32_t clear = 0;
    for (std::int32_t t = 1; t <= reach; ++t) {
        const std::int32_t tx = x + dx * t;
        const std::int32_t ty = y + dy * t;
        if (tiles.solid(tx, ty, band)) {
            break;
        }
        if (tiles.standable(tx, ty, band)) {
            if (t == 1) {
                return false;
            }
            out = Cell{tx, ty, band};
            return true;
        }
        clear = t;
    }
    for (std::int32_t t = 1; t <= clear; ++t) {
        const std::int32_t tx = x + dx * t;
        const std::int32_t ty = y + dy * t;
        for (std::int32_t drop = 1; drop <= 2; ++drop) {
            if (band - drop >= docks::kLandingFloor && tiles.standable(tx, ty, band - drop)) {
                out = Cell{tx, ty, band - drop};
                return true;
            }
        }
    }
    return false;
}

/// Every cell reachable from the spawn. `roofs` off is the plain walking rule
/// S1 shipped; on adds the mantle, the leap and the drop and nothing else.
[[nodiscard]] std::set<Cell> reachable(const TileQuery& tiles, bool roofs) {
    const Cell start{docks::kSpawnTileX, docks::kSpawnTileY, docks::kSpawnBand};
    std::set<Cell> seen{start};
    std::deque<Cell> queue{start};
    while (!queue.empty()) {
        const auto [x, y, z] = queue.front();
        queue.pop_front();
        const auto visit = [&](const Cell& cell) {
            if (seen.insert(cell).second) {
                queue.push_back(cell);
            }
        };
        for (int d = 0; d < 4; ++d) {
            const std::int32_t nx = x + kDx[d];
            const std::int32_t ny = y + kDy[d];
            const std::int32_t walk = tiles.stepBand(x, y, z, nx, ny);
            if (walk != TileQuery::kNoBand) {
                visit(Cell{nx, ny, walk});
            }
            if (!roofs) {
                continue;
            }
            const std::int32_t up = tiles.mantleBand(x, y, z, nx, ny);
            if (up != TileQuery::kNoBand && !tiles.solid(x, y, z + 1)) {
                visit(Cell{nx, ny, up});
            }
            if (!tiles.standable(nx, ny, z) && !tiles.solid(nx, ny, z)) {
                const std::int32_t deepest =
                    std::max(docks::kLandingFloor, z - kMaxDropBands);
                if (deepest <= z - 1) {
                    const std::int32_t down =
                        tiles.landingBand(nx, ny, z - 1, z - 1 - deepest);
                    if (down != TileQuery::kNoBand) {
                        visit(Cell{nx, ny, down});
                    }
                }
            }
            Cell landed;
            if (leapLanding(tiles, x, y, z, kDx[d], kDy[d], kLeapReachTiles, landed)) {
                visit(landed);
            }
        }
        // And the flight you are standing on, up and down. See
        // PlayerBody::mantle on why a stair needs a verb in this build at all.
        if (roofs && tiles.climbable(x, y, z)) {
            if (tiles.standable(x, y, z + 1)) {
                visit(Cell{x, y, z + 1});
            }
            if (z - 1 >= docks::kLandingFloor && tiles.standable(x, y, z - 1)) {
                visit(Cell{x, y, z - 1});
            }
        }
    }
    return seen;
}

[[nodiscard]] std::int32_t countOnBand(const std::set<Cell>& cells, std::int32_t band) {
    std::int32_t total = 0;
    for (const auto& [x, y, z] : cells) {
        (void)x;
        (void)y;
        if (z == band) {
            ++total;
        }
    }
    return total;
}

}  // namespace

// ---------------------------------------------------------------------------
// the geometry
// ---------------------------------------------------------------------------

TEST_CASE("a mantle grips a wall face and comes up on top of it") {
    const TileQuery tiles(docksWorld());

    // The Gilded Gull's north exterior wall, from the guest floor above the
    // taproom. This is the burglar's own route: in the door, up the stair, and
    // out over the wall of the room you rented.
    CHECK(tiles.solid(150, gull::kFootprintY0, gull::kUpperBand));
    CHECK(tiles.standable(150, gull::kFootprintY0, gull::kUpperBand + 1));
    CHECK(tiles.mantleBand(150, 67, gull::kUpperBand, 150, gull::kFootprintY0) ==
          gull::kUpperBand + 1);
}

TEST_CASE("a mantle refuses a wall with no surface on top of it") {
    const TileQuery tiles(docksWorld());
    // The same wall, one floor down. From the TAPROOM the north wall's top is
    // the exterior wall of the guest floor -- masonry, not a roof -- so the
    // ceiling of the room you are standing in is not a ladder to the room
    // above it.
    CHECK(tiles.solid(150, gull::kFootprintY0, gull::kGroundBand));
    CHECK_FALSE(tiles.walkable(150, gull::kFootprintY0, gull::kUpperBand));
    CHECK(tiles.mantleBand(150, 67, gull::kGroundBand, 150, gull::kFootprintY0) ==
          TileQuery::kNoBand);
}

TEST_CASE("a mantle refuses open air -- there is nothing to put your hands on") {
    const TileQuery tiles(docksWorld());
    // Standing on the Tarwalk beside a tile that is open at street level. Its
    // column may well be standable one up somewhere in the district; without a
    // face to grip it is not a climb, it is levitation.
    const std::int32_t x = docks::kSpawnTileX;
    const std::int32_t y = docks::kSpawnTileY;
    REQUIRE(tiles.standable(x, y, docks::kBandQuayside));
    REQUIRE_FALSE(tiles.solid(x, y - 1, docks::kBandQuayside));
    CHECK(tiles.mantleBand(x, y, docks::kBandQuayside, x, y - 1) == TileQuery::kNoBand);
}

TEST_CASE("a landing looks down and stops at the first floor under it") {
    const TileQuery tiles(docksWorld());
    // Off the Gull's roof at its north-west corner: two bands of air and then
    // the Tarwalk.
    CHECK(tiles.landingBand(145, 70, gull::kUpperBand + 1, kMaxDropBands) ==
          docks::kBandQuayside);
    // Its own deck answers itself, which is how a caller tells a step from a
    // fall.
    CHECK(tiles.landingBand(150, 70, gull::kUpperBand + 1, kMaxDropBands) ==
          gull::kUpperBand + 1);
    // Over the water there is nothing at all within reach.
    CHECK(tiles.landingBand(150, 40, docks::kBandQuayside, kMaxDropBands) ==
          TileQuery::kNoBand);
}

// ---------------------------------------------------------------------------
// what it opens
// ---------------------------------------------------------------------------

TEST_CASE("the roof moves open two thirds again of the district") {
    const TileQuery tiles(docksWorld());

    const std::set<Cell> walking = reachable(tiles, false);
    // The walking rule is UNTOUCHED. If this moves, S5 changed how the ground
    // works, which it must not.
    CHECK(static_cast<std::int32_t>(walking.size()) == docks::kReachableFromSpawn);

    const std::set<Cell> climbing = reachable(tiles, true);
    CHECK(static_cast<std::int32_t>(climbing.size()) == docks::kReachableWithRoofMoves);
    CHECK(climbing.size() > walking.size());
    // Everything walkable stays walkable: a new verb may only ADD.
    for (const Cell& cell : walking) {
        REQUIRE(climbing.count(cell) == 1);
    }
}

TEST_CASE("the roof-slum plane was completely unreachable and is not any more") {
    const TileQuery tiles(docksWorld());

    std::int32_t standable = 0;
    for (std::int32_t y = 0; y < tiles.sizeY(); ++y) {
        for (std::int32_t x = 0; x < tiles.sizeX(); ++x) {
            if (tiles.standable(x, y, docks::kBandRoofs)) {
                ++standable;
            }
        }
    }
    CHECK(standable == docks::kStandableOnRoofs);

    CHECK(countOnBand(reachable(tiles, false), docks::kBandRoofs) == 0);
    CHECK(countOnBand(reachable(tiles, true), docks::kBandRoofs) ==
          docks::kRoofReachableOnRoofs);
    CHECK(countOnBand(reachable(tiles, true), docks::kBandUpper) ==
          docks::kRoofReachableOnUpper);
}

TEST_CASE("the Gull's guest floor has never been reachable on foot, and now is") {
    const TileQuery tiles(docksWorld());

    // ONE cell of stair, a STAIR at both bands, open floor all round it on
    // both. Every neighbour is standable at z19, so stepBand's preference
    // order -- same level, then down, then up -- keeps a body downstairs
    // forever.
    CHECK(tiles.climbable(gull::kStairX, gull::kStairY, gull::kGroundBand));
    CHECK(tiles.climbable(gull::kStairX, gull::kStairY, gull::kUpperBand));

    std::int32_t transitions = 0;
    for (std::int32_t y = gull::kFootprintY0; y <= gull::kFootprintY1; ++y) {
        for (std::int32_t x = gull::kFootprintX0; x <= gull::kFootprintX1; ++x) {
            if (!tiles.standable(x, y, gull::kGroundBand)) {
                continue;
            }
            for (int d = 0; d < 4; ++d) {
                if (tiles.stepBand(x, y, gull::kGroundBand, x + kDx[d], y + kDy[d]) ==
                    gull::kUpperBand) {
                    ++transitions;
                }
            }
        }
    }
    // NOT ONE. S2 shipped rentRoom() and sleep() and S4 wrote a case about a
    // robbery on that floor; all of them put the body up there by constructing
    // it there, and nobody ever walked.
    CHECK(transitions == 0);

    // The sprint's own up-key takes the flight.
    PlayerBody body(tiles, gull::kStairX, gull::kStairY, gull::kGroundBand, kFacingNorth);
    REQUIRE(body.spawnedLegally());
    const RoofResult up = body.mantle();
    CHECK(up.ok());
    CHECK(body.band() == gull::kUpperBand);
    CHECK(body.tileX() == gull::kStairX);
    CHECK(body.tileY() == gull::kStairY);
    // Climbing a stair is not falling and does not hurt.
    CHECK(body.takeFallBands() == 0);

    // And the same verb comes back down it.
    const RoofResult down = body.dropOff();
    CHECK(down.ok());
    CHECK(body.band() == gull::kGroundBand);
}

TEST_CASE("the whole roof of the Gilded Gull can be stood on") {
    const TileQuery tiles(docksWorld());
    const std::set<Cell> climbing = reachable(tiles, true);
    const std::set<Cell> walking = reachable(tiles, false);

    std::int32_t deck = 0;
    std::int32_t onFoot = 0;
    std::int32_t byRoof = 0;
    for (std::int32_t y = gull::kFootprintY0; y <= gull::kFootprintY1; ++y) {
        for (std::int32_t x = gull::kFootprintX0; x <= gull::kFootprintX1; ++x) {
            if (!tiles.standable(x, y, gull::kRoofBand)) {
                continue;
            }
            ++deck;
            onFoot += static_cast<std::int32_t>(walking.count(Cell{x, y, gull::kRoofBand}));
            byRoof += static_cast<std::int32_t>(climbing.count(Cell{x, y, gull::kRoofBand}));
        }
    }
    // Fifteen by fourteen of flat lead over the captains' tavern.
    CHECK(deck == 210);
    // Not one cell of it by the stairs alone...
    CHECK(onFoot == 0);
    // ...and every cell of it once you can climb.
    CHECK(byRoof == deck);
}

// ---------------------------------------------------------------------------
// the body doing it
// ---------------------------------------------------------------------------

TEST_CASE("the body climbs onto the Gull's roof and the frame goes up with it") {
    const TileQuery tiles(docksWorld());
    PlayerBody body(tiles, 150, 67, gull::kUpperBand, kFacingNorth);
    REQUIRE(body.spawnedLegally());

    const RoofResult climbed = body.mantle();
    CHECK(climbed.ok());
    CHECK(climbed.bands == 1);
    CHECK(body.band() == gull::kRoofBand);
    CHECK(body.tileY() == gull::kFootprintY0);
    // Climbing up is not falling.
    CHECK(body.takeFallBands() == 0);
}

TEST_CASE("walking into the ledge climbs it, with nobody pressing a verb key") {
    // THE HEADLINE OF #77. Eli played the build and said the movement was
    // "unnecessarily archaic"; the single most archaic thing in it was that the
    // only way onto anything was to stop, line the wall up by eye, and press a
    // key no other game binds. A player who simply walked at a ledge got a body
    // stuck against masonry and no indication that climbing existed.
    //
    // Same spot the explicit case above uses: the Gull's guest floor, facing the
    // north wall, with the lead one band over it.
    const TileQuery tiles(docksWorld());
    PlayerBody body(tiles, 150, 67, gull::kUpperBand, kFacingNorth);
    REQUIRE(body.spawnedLegally());

    MoveInput forward;
    forward.forward = 1;
    // IT HAPPENS WHILE YOU ARE STILL WALKING, and that is the assertion. The
    // body starts at the centre of its tile, so the legs have to carry it to the
    // wall before there is anything to grip -- with the ramp in
    // human_scale.hpp that is a handful of steps, well under a fifth of a
    // second. What must NOT happen is that it takes a key, or that it takes so
    // long the player has stopped and gone looking for one.
    RoofResult again;
    int steps = 0;
    while (steps < kStepsPerSecond / 4 && !again.ok()) {
        body.step(forward);
        ++steps;
        again = body.takeAutoMove();
    }
    INFO("steps of walking before the climb: " << steps);

    REQUIRE(again.ok());
    CHECK(again.bands == 1);
    CHECK(body.band() == gull::kRoofBand);
    CHECK(body.tileY() == gull::kFootprintY0);
    CHECK(body.takeFallBands() == 0);
    // READ ONCE. Whoever charges the climb reads it on the step it happened and
    // it is gone; a result that stayed would be charged again every frame.
    CHECK_FALSE(body.takeAutoMove().ok());
}

TEST_CASE("a climb is a climb: it costs time, and it does not become a lift") {
    const TileQuery tiles(docksWorld());
    PlayerBody body(tiles, 150, 67, gull::kUpperBand, kFacingNorth);
    REQUIRE(body.mantle().ok());

    // A 2.7 m WALL TAKES A SECOND AND BOTH HANDS. human_scale.hpp's
    // kVaultReachMm is 1,350 -- chest height, one hand and a knee, instant --
    // and a band is twice that. So the band changes at once (the simulation is
    // never half inside a wall) and the LEGS are locked while the eye rises.
    CHECK(body.hauling());
    MoveInput forward;
    forward.forward = 1;
    const std::int32_t heldAt = body.y();
    for (int i = 0; i < kHaulSteps - 1; ++i) {
        body.step(forward);
        REQUIRE(body.hauling());
    }
    CHECK(body.y() == heldAt);  // not one Q8 of walking during the haul
    body.step(forward);
    CHECK_FALSE(body.hauling());
    // And the eye has arrived: the haul is exactly as long as the ease.
    CHECK(body.feetZ() == q8_of_tile(body.band()));

    // A SECOND CLIMB CANNOT START INSIDE THE FIRST. Without this an automatic
    // climb against a stack of ledges would fire four times in a tenth of a
    // second and read as a lift shaft.
    PlayerBody stack(tiles, 150, 67, gull::kUpperBand, kFacingNorth);
    REQUIRE(stack.mantle().ok());
    CHECK_FALSE(stack.mantle().ok());
}

TEST_CASE("contextual traversal does not fire on anything a player did not mean") {
    const TileQuery tiles(docksWorld());
    MoveInput forward;
    forward.forward = 1;

    // NOT WHEN STRAFING INTO IT. Climbing sideways is not a thing.
    {
        PlayerBody body(tiles, 150, 67, gull::kUpperBand, kFacingNorth);
        MoveInput sideways;
        sideways.strafe = 1;
        for (int i = 0; i < 8; ++i) {
            body.step(sideways);
        }
        CHECK(body.band() == gull::kUpperBand);
    }
    // NOT WHEN CROUCHED. You are hiding, not vaulting.
    {
        PlayerBody body(tiles, 150, 67, gull::kUpperBand, kFacingNorth);
        MoveInput sneaking = forward;
        sneaking.crouch = true;
        for (int i = 0; i < 20; ++i) {
            body.step(sneaking);
        }
        CHECK(body.band() == gull::kUpperBand);
    }
    // NOT WHEN THE CALLER SAID NOT TO. The capture script steers by walking
    // into walls to find out where they are; it must not climb the warehouse it
    // is probing.
    {
        PlayerBody body(tiles, 150, 67, gull::kUpperBand, kFacingNorth);
        MoveInput probing = forward;
        probing.autoTraverse = false;
        for (int i = 0; i < 20; ++i) {
            body.step(probing);
        }
        CHECK(body.band() == gull::kUpperBand);
    }
    // AND NOT UP A WALL WITH NO TOP. A two-storey warehouse front is a wall,
    // and walking at it has to stay walking at a wall -- otherwise contextual
    // traversal is just flying with extra steps.
    {
        // Inside the taproom, facing the open room: nothing to grip at all.
        PlayerBody body(tiles, 152, 69, gull::kGroundBand, kFacingNorth);
        REQUIRE(body.spawnedLegally());
        for (int i = 0; i < 40; ++i) {
            body.step(forward);
        }
        CHECK(body.band() == gull::kGroundBand);
    }
}

TEST_CASE("a body facing nothing to climb is told which clause refused it") {
    const TileQuery tiles(docksWorld());

    // Facing the open taproom: no wall face at all.
    PlayerBody inside(tiles, 152, 69, gull::kGroundBand, kFacingNorth);
    CHECK(inside.mantle().move == RoofMove::NoLedge);

    // Standing UNDER the guest floor's own slab and facing the north wall: the
    // grip is there and the head is not.
    PlayerBody underneath(tiles, 150, 67, gull::kGroundBand, kFacingNorth);
    const RoofResult refused = underneath.mantle();
    CHECK_FALSE(refused.ok());
    CHECK((refused.move == RoofMove::NoHeadroom || refused.move == RoofMove::NoLedge));
}

TEST_CASE("a drop off the Gull's roof falls two storeys to the Tarwalk") {
    const TileQuery tiles(docksWorld());
    PlayerBody body(tiles, gull::kFootprintX0, 70, gull::kRoofBand, kFacingWest);
    REQUIRE(body.spawnedLegally());

    const RoofResult fell = body.dropOff();
    CHECK(fell.ok());
    CHECK(fell.bands == 2);
    CHECK(body.band() == docks::kBandQuayside);
    // The fall is REPORTED, once, to whoever keeps the hit points.
    CHECK(body.takeFallBands() == 2);
    CHECK(body.takeFallBands() == 0);
}

TEST_CASE("a two-storey drop dips the eye more than a standing jump, and it recovers") {
    // #77. THE FEEL HALF: the fall curve above charges hit points; this is
    // what the CAMERA does about the same fall, and it is new. Reuses the
    // exact drop the case above proves is two bands.
    const TileQuery tiles(docksWorld());
    PlayerBody body(tiles, gull::kFootprintX0, 70, gull::kRoofBand, kFacingWest);
    REQUIRE(body.spawnedLegally());
    const std::int32_t feetAtImpact = body.feetZ();  // land() never touches feetZ

    const RoofResult fell = body.dropOff();
    REQUIRE(fell.ok());
    REQUIRE(fell.bands == 2);

    // THE IMPACT REGISTERS AT ONCE -- no ease-in on the way down, only on the
    // way back out -- and a real two-storey fall dips deeper than the floor
    // an ordinary standing jump gives (see kLandingDipMinMm, kLandingDipPerBandMm).
    CHECK(body.landingDipOffsetQ8() == landingDipQ8(2));
    CHECK(body.landingDipOffsetQ8() > landingDipQ8(0));
    CHECK(body.eyeZ() == feetAtImpact + kEyeHeight - landingDipQ8(2));

    // AND IT CLIMBS BACK OUT, the same ease-out curve every camera response
    // in this file uses.
    MoveInput idle;
    for (int i = 0; i < 200 && body.landingDipOffsetQ8() != 0; ++i) {
        body.step(idle);
    }
    CHECK(body.landingDipOffsetQ8() == 0);
    // The dip is gone; the eye is exactly standing height above wherever the
    // feet have eased down to by now, no more and no less.
    CHECK(body.eyeZ() == body.feetZ() + kEyeHeight);
}

TEST_CASE("a mantle never dips the eye -- only coming down does") {
    // mantleToward() calls the same land() a drop does; fell is 0 there
    // because toBand > fromBand, and the guard in land() has to actually
    // discriminate rather than dipping on every band change. Same fixture as
    // "the body climbs onto the Gull's roof and the frame goes up with it".
    const TileQuery tiles(docksWorld());
    PlayerBody body(tiles, 150, 67, gull::kUpperBand, kFacingNorth);
    REQUIRE(body.spawnedLegally());
    const RoofResult up = body.mantle();
    REQUIRE(up.ok());
    CHECK(body.landingDipOffsetQ8() == 0);
}

TEST_CASE("a drop onto ordinary ground is refused as the step it is") {
    const TileQuery tiles(docksWorld());
    PlayerBody body(tiles, 150, 70, gull::kRoofBand, kFacingEast);
    CHECK(body.dropOff().move == RoofMove::NoGap);
}

TEST_CASE("a leap crosses the alley between two roofs and lands on the far one") {
    const TileQuery tiles(docksWorld());
    // The Gull's west edge, facing the neighbouring compound's roof across two
    // tiles of open air.
    PlayerBody body(tiles, gull::kFootprintX0, 70, gull::kRoofBand, kFacingWest);
    REQUIRE(body.spawnedLegally());
    REQUIRE_FALSE(tiles.standable(gull::kFootprintX0 - 1, 70, gull::kRoofBand));
    REQUIRE_FALSE(tiles.standable(gull::kFootprintX0 - 2, 70, gull::kRoofBand));
    REQUIRE(tiles.standable(gull::kFootprintX0 - 3, 70, gull::kRoofBand));

    const RoofResult jumped = body.leap(kLeapReachTiles);
    CHECK(jumped.ok());
    CHECK(jumped.tiles == 3);
    CHECK(jumped.bands == 0);
    // AND IT IS NOT A TELEPORT: the body is in the air, and the steps that
    // follow are what carries it.
    CHECK(body.airborne());
    CHECK(body.tileX() == gull::kFootprintX0);

    const std::int32_t startFeet = body.feetZ();
    std::int32_t highest = startFeet;
    MoveInput idle;
    for (int i = 0; i < 3 * kLeapStepsPerTile; ++i) {
        body.step(idle);
        highest = std::max(highest, body.feetZ());
    }
    CHECK_FALSE(body.airborne());
    CHECK(body.tileX() == gull::kFootprintX0 - 3);
    CHECK(body.band() == gull::kRoofBand);
    // There was an arc. A leap that slid flat across would be a glitch, and it
    // is what the first version of this did.
    CHECK(highest > startFeet);
}

TEST_CASE("a leap will not cross a gap with no far side, and will not go through a wall") {
    const TileQuery tiles(docksWorld());

    // Off the quay, facing the harbour: three tiles of open water and no
    // landing anywhere in them.
    PlayerBody overWater(tiles, 150, 58, docks::kBandQuayside, kFacingNorth);
    REQUIRE(overWater.spawnedLegally());
    CHECK(overWater.leap(kLeapReachTiles).move == RoofMove::NoLanding);
    CHECK_FALSE(overWater.airborne());

    // Inside the taproom facing the counter: solid masonry one tile ahead.
    PlayerBody atTheBar(tiles, 152, 70, gull::kGroundBand, kFacingSouth);
    REQUIRE(atTheBar.spawnedLegally());
    CHECK(atTheBar.leap(kLeapReachTiles).move == RoofMove::NoLanding);

    // On open floor: there is no gap to leap, so it is a walk.
    PlayerBody onTheStreet(tiles, docks::kSpawnTileX, docks::kSpawnTileY, docks::kBandQuayside,
                           kFacingEast);
    CHECK(onTheStreet.leap(kLeapReachTiles).move == RoofMove::NoGap);
}

TEST_CASE("the roof moves do not open a trapdoor into the unbuilt dungeon") {
    const TileQuery tiles(docksWorld());

    // Under the piers at low tide, facing the harbour. Three tiles out there is
    // a column that is dry all the way down to the seabed at z16 -- 4,066 cells
    // of FLOOR whose own fluid lane is clear, because the water stands in the
    // two cells above it. The walkability rule has always called that standable.
    REQUIRE(tiles.standable(167, 39, 16));
    REQUIRE_FALSE(tiles.standable(167, 39, 17));

    PlayerBody unguarded(tiles, 167, 40, docks::kHarbourSurfaceBand, kFacingNorth);
    REQUIRE(unguarded.spawnedLegally());
    // With no floor declared the body goes down the shaft -- and cannot get out
    // again, because z17 has not one standable cell to mantle onto.
    const RoofResult fell = unguarded.leap(kLeapReachTiles);
    CHECK(fell.ok());
    for (int i = 0; i < 4 * kLeapStepsPerTile; ++i) {
        MoveInput idle;
        unguarded.step(idle);
    }
    CHECK(unguarded.band() == 16);

    // The district says where its floor is, and the same jump is refused.
    PlayerBody guarded(tiles, 167, 40, docks::kHarbourSurfaceBand, kFacingNorth);
    guarded.setLandingFloor(docks::kLandingFloor);
    CHECK(guarded.leap(kLeapReachTiles).move == RoofMove::NoLanding);
    CHECK(guarded.dropOff().move == RoofMove::NoLanding);
    CHECK(guarded.band() == docks::kHarbourSurfaceBand);
}

TEST_CASE("what a skill and a guild buy is reach and a safe height, never a chance") {
    CHECK(safeDropBands(0, false) == kSafeDropBands);
    CHECK(safeDropBands(10, false) == kSafeDropBands + 1);
    CHECK(safeDropBands(0, true) == kSafeDropBands + 1);
    // The best roof-runner alive stops at kMaxSafeDropBands, which is STRICTLY
    // SHORTER than the deepest drop the geometry allows. That gap is the point:
    // a band is 2.7 m now, so a three-band fall is 8.2 m, and a rule that
    // handed anybody an 8.2 m drop for free would have stopped being a skill.
    CHECK(safeDropBands(40, true) == kMaxSafeDropBands);
    // Monotonic, and never past what any pair of legs can take.
    for (std::int32_t level = 0; level <= 100; ++level) {
        REQUIRE(safeDropBands(level, false) <= safeDropBands(level, true));
        REQUIRE(safeDropBands(level, true) <= kMaxSafeDropBands);
    }
    // The deepest fall in the district is never free, for anybody, ever.
    for (std::int32_t level = 0; level <= 100; ++level) {
        REQUIRE(safeDropBands(level, true) < kMaxDropBands);
    }
    CHECK(leapReachTiles(0, false) == kLeapReachTiles);
    CHECK(leapReachTiles(0, true) == kLeapReachTiles + 1);
    CHECK(leapReachTiles(20, true) == kLeapReachTiles + 2);
}

TEST_CASE("the four-point compass lines a jump up rather than half-committing to two") {
    CHECK(facing_step(kFacingNorth).dy == -1);
    CHECK(facing_step(kFacingEast).dx == 1);
    CHECK(facing_step(kFacingSouth).dy == 1);
    CHECK(facing_step(kFacingWest).dx == -1);
    // Every facing snaps to exactly one axis, all the way round.
    for (Angle yaw = 0; yaw < kTurnFull; yaw += 97) {
        const TileStep step = facing_step(yaw);
        REQUIRE((step.dx == 0) != (step.dy == 0));
        REQUIRE(step.dx * step.dx + step.dy * step.dy == 1);
    }
    // Just short of due east still reads as east; just past it still does.
    CHECK(facing_step(kFacingEast - 1).dx == 1);
    CHECK(facing_step(kFacingEast + 1).dx == 1);
}

TEST_CASE("a body in the air is in the digest, and cannot be shoved out of its arc") {
    const TileQuery tiles(docksWorld());
    PlayerBody a(tiles, gull::kFootprintX0, 70, gull::kRoofBand, kFacingWest);
    PlayerBody b(tiles, gull::kFootprintX0, 70, gull::kRoofBand, kFacingWest);
    REQUIRE(a.digest() == b.digest());

    REQUIRE(a.leap(kLeapReachTiles).ok());
    CHECK(a.digest() != b.digest());

    // A bouncer's shove lands on a body that is not there to be shoved.
    const std::uint64_t midFlight = a.digest();
    a.push(500, 0);
    CHECK(a.digest() == midFlight);

    REQUIRE(b.leap(kLeapReachTiles).ok());
    MoveInput idle;
    for (int i = 0; i < 3 * kLeapStepsPerTile; ++i) {
        a.step(idle);
        b.step(idle);
        REQUIRE(a.digest() == b.digest());
    }
}

// ---------------------------------------------------------------------------
// S6: the landing, and who charges it
// ---------------------------------------------------------------------------
//
// Two findings of the S5 review live here.
//
//   * THE LEAP WAS A TELEPORT IN THE SHIPPED CLIENT. PlayerBody's arc was
//     proved above -- and Session::climb then ran `while (airborne()) step()`
//     inside the keypress and threw it away. The design note on
//     PlayerBody::leap and the assertion in the case above were both true of
//     the body and false of the game.
//   * THE GLUE HAD NO TEST. Fall damage, the skill use, the roof-run crime and
//     both questline tallies went through Session::settleLanding, which lived
//     in the client layer where the simulation suite could not reach it.
//     test_crime.cpp had to hand-call noteTally/noteCrime to get past the roof
//     beats of the Skyrunner line, which proved the questline and nothing else.
//
// The charge is Tavern::settleLanding now. These cases drive it both ways: from
// the room directly, and through the key a player presses.

TEST_CASE("a leap is armed by the key and flown by the pump, and lands ONCE") {
    render::SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = docks::kWorldName;
    config.timeOfDay = 22 * 3600;
    config.timeOfDayGiven = true;
    // Standing on the Gull's lead at its west edge, facing the alley -- the
    // same two tiles of air the body-level case above crosses.
    config.spawnX = gull::kFootprintX0;
    config.spawnY = 70;
    config.spawnBand = gull::kRoofBand;
    config.spawnYaw = kFacingWest;
    config.spawnYawGiven = true;
    config.width = 64;
    config.height = 64;
    render::Session session(config);
    REQUIRE(session.body().spawnedLegally());

    DialogueDirector& talk = session.tavern().dialogue();
    const SkillTrack::Entry* craft = talk.skills().find(kRoofSkill);
    REQUIRE(craft != nullptr);
    REQUIRE(craft->uses == 0);
    // Standing on the lead is where the session began, so nothing is owed for
    // it: a body that has not arrived anywhere has not run a roof.
    REQUIRE(talk.crimes().tally(Crime::RoofRun) == 0);
    REQUIRE(session.tavern().highestBandReached() == gull::kRoofBand);

    session.climb();

    // THE PRESS DID NOT RESOLVE THE JUMP. This is the whole finding: the body
    // is in the air, the landing has not been charged, and the craft it takes
    // has not been paid for yet.
    CHECK(session.body().airborne());
    CHECK(session.awaitingLanding());
    CHECK(session.body().tileX() == gull::kFootprintX0);
    CHECK(talk.skills().find(kRoofSkill)->uses == 0);

    const int flown = session.flyOutLeap();
    // Eight movement steps a tile, three tiles: a quarter of a second of air,
    // which is a jump you can watch.
    CHECK(flown == 3 * kLeapStepsPerTile);
    CHECK_FALSE(session.body().airborne());
    CHECK_FALSE(session.awaitingLanding());
    CHECK(session.body().tileX() == gull::kFootprintX0 - 3);

    // AND THE LANDING CHARGED, on the step the feet touched: a leap is worth
    // two uses of the craft, a climb one.
    CHECK(talk.skills().find(kRoofSkill)->uses == 2);

    // Another hundred steps of standing there charge nothing more.
    session.stepMany(MoveInput{}, 100);
    CHECK(talk.skills().find(kRoofSkill)->uses == 2);
    // And crossing back at the same height is not an arrival anywhere new.
    CHECK(talk.crimes().tally(Crime::RoofRun) == 0);
}

TEST_CASE("the up-key onto the lead is a roof-run the moment it lands") {
    // The other half of the glue, through the key a player actually presses: a
    // mantle resolves under the hand, so the arrival, the crime and both sides
    // of the mirror move on the press itself.
    render::SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = docks::kWorldName;
    config.timeOfDay = 22 * 3600;
    config.timeOfDayGiven = true;
    // The guest floor's north wall, facing it -- the burglar's own way out.
    config.spawnX = 150;
    config.spawnY = 67;
    config.spawnBand = gull::kUpperBand;
    config.spawnYaw = kFacingNorth;
    config.spawnYawGiven = true;
    config.width = 64;
    config.height = 64;
    render::Session session(config);
    REQUIRE(session.body().spawnedLegally());

    DialogueDirector& talk = session.tavern().dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    const std::int32_t watch = talk.factions().indexOf("watch");
    REQUIRE(roofs >= 0);
    REQUIRE(watch >= 0);
    const std::int32_t skyBefore = talk.standings().standing(roofs);
    const std::int32_t watchBefore = talk.standings().standing(watch);
    REQUIRE(session.tavern().highestBandReached() == gull::kUpperBand);

    session.climb();

    CHECK_FALSE(session.awaitingLanding());
    CHECK(session.body().band() == gull::kRoofBand);
    CHECK(session.tavern().highestBandReached() == gull::kRoofBand);
    CHECK(talk.crimes().tally(Crime::RoofRun) == 1);
    CHECK(talk.standings().standing(roofs) > skyBefore);
    CHECK(talk.standings().standing(watch) < watchBefore);
    CHECK(talk.skills().find(kRoofSkill)->uses == 1);
}

TEST_CASE("the room charges a landing: the craft, the fall, the roof and the tally") {
    const TileQuery tiles(docksWorld());
    Tavern gull(tiles, hourOfDay(22), 0x5350524E47ull, granadad::content::contentDir());
    DialogueDirector& talk = gull.dialogue();
    const std::int32_t roofs = talk.factions().indexOf("skyrunners");
    const std::int32_t watch = talk.factions().indexOf("watch");
    REQUIRE(roofs >= 0);
    REQUIRE(watch >= 0);

    gull.setPlayer(q8_tile_centre(gull::kBartenderX), q8_tile_centre(gull::kBarY - 1),
                   gull::kGroundBand);
    const std::int32_t hpBefore = gull.playerHp();
    const std::int32_t skyBefore = talk.standings().standing(roofs);
    const std::int32_t watchBefore = talk.standings().standing(watch);

    // A mantle onto the lead: one band up, no fall, and the first arrival
    // anywhere high.
    const Tavern::LandingResult up =
        gull.settleLanding(RoofResult{RoofMove::Done, 1, 1}, 0, gull::kRoofBand);
    CHECK(up.hurt == 0);
    CHECK(up.roofRun);
    CHECK(up.counted == "climbs");
    CHECK(talk.crimes().tally(Crime::RoofRun) == 1);
    CHECK(talk.skills().level(kRoofSkill) >= 0);
    CHECK(gull.playerHp() == hpBefore);
    // The mirror ranks.json declares: what the roofs gain, the garrison loses
    // half of. A landing that reached the tally and missed this would be the
    // exact bug noteCrime's one-call-site shape exists to prevent.
    CHECK(talk.standings().standing(roofs) > skyBefore);
    CHECK(talk.standings().standing(watch) < watchBefore);

    // A second arrival at the same height is not a second roof-run.
    const Tavern::LandingResult again =
        gull.settleLanding(RoofResult{RoofMove::Done, 0, 3}, 0, gull::kRoofBand);
    CHECK_FALSE(again.roofRun);
    CHECK(again.counted == "leaps");
    CHECK(talk.crimes().tally(Crime::RoofRun) == 1);

    // And a fall costs what GRAVITY costs. Two bands is 5.4 m; a body arrives at
    // 10.3 m/s and untaught legs absorb the first 5.4 m/s of it, so the excess
    // squared comes to fifty of a hundred hit points. A serious injury, which is
    // what falling two storeys is.
    //
    // #77 REPLACED A STAIRCASE HERE. The old rule was
    // `(fellBands - safeBands) * 24` -- a flat slab per storey past a threshold,
    // with no height in it anywhere, which is why the only available fix when
    // the storey tripled was to double the literal from 12 to 24.
    REQUIRE(safeDropBands(talk.skills().level(kRoofSkill), false) == kSafeDropBands);
    const Tavern::LandingResult down =
        gull.settleLanding(RoofResult{RoofMove::Done, 2, 0}, 2, gull::kGroundBand);
    CHECK(down.fellMm == 2 * kMillimetresPerBand);
    CHECK_FALSE(down.softLanding);
    CHECK(down.hurt == 50);
    CHECK(gull.playerHp() == hpBefore - 50);
}

TEST_CASE("the fall curve reads as gravity: a knock, an injury, and a death") {
    // THE THREE HEIGHTS THE DISTRICT ACTUALLY CONTAINS, against a hundred hit
    // points, with nothing learnt and nothing soft underfoot. This is the brief
    // Eli set for #77 stated as three numbers.
    const std::int32_t oneStorey = fallInjury(kMillimetresPerBand, kFreeFallMm);
    const std::int32_t twoStoreys = fallInjury(2 * kMillimetresPerBand, kFreeFallMm);
    const std::int32_t threeStoreys = fallInjury(3 * kMillimetresPerBand, kFreeFallMm);

    // ~2.7 m: survivable with a knock.
    CHECK(oneStorey > 0);
    CHECK(oneStorey <= 15);
    // ~5.4 m: a serious injury, half of what a person has.
    CHECK(twoStoreys >= 35);
    CHECK(twoStoreys <= 65);
    // ~8.1 m: more than a person has. The build still floors at
    // kPlayerBrawlFloor because nothing kills the player yet -- but the number
    // is honest, so the day they can die the roofs will kill them without this
    // being retuned.
    CHECK(threeStoreys > 100);

    // IT IS A CURVE AND NOT A STAIRCASE. The old rule cost the same for every
    // storey past the line; this one costs more for each one, because energy is
    // the square of the speed and the speed is the square root of the height.
    CHECK(twoStoreys - oneStorey > oneStorey);
    CHECK(threeStoreys - twoStoreys > twoStoreys - oneStorey);

    // A drop shorter than the free allowance is free, and it is free because it
    // is SHORT, not because a band counter has not ticked over yet.
    CHECK(fallInjury(kFreeFallMm, kFreeFallMm) == 0);
    CHECK(fallInjury(kFreeFallMm - 1, kFreeFallMm) == 0);
    CHECK(fallInjury(0, kFreeFallMm) == 0);

    // Every extra millimetre of fall is worth at least as much as the last: the
    // curve never goes backwards, which a piecewise table can quietly do.
    std::int32_t last = 0;
    for (std::int32_t mm = 0; mm <= 4 * kMillimetresPerBand; mm += 25) {
        const std::int32_t hurt = fallInjury(mm, kFreeFallMm);
        REQUIRE(hurt >= last);
        last = hurt;
    }
}

TEST_CASE("skill and a soft landing shift the fall curve without flattening it") {
    // THE ROOFS' TEACHING IS WORTH A HEIGHT, not an exemption. A fully taught
    // skyrunner walks off one storey for nothing and is still broken by three,
    // which is the difference between a skill and a cheat -- the same rule
    // kMaxSafeDropBands states in bands, stated here in metres.
    const std::int32_t taught =
        kFreeFallMm + (safeDropBands(40, true) - kSafeDropBands) * kTaughtLandingMm;
    CHECK(fallInjury(kMillimetresPerBand, taught) == 0);
    CHECK(fallInjury(2 * kMillimetresPerBand, taught) > 0);
    CHECK(fallInjury(3 * kMillimetresPerBand, taught) > 0);
    // And it is still worse than two storeys was for a novice would have been
    // survivable: the best roof-runner alive takes real damage off the Gull.
    CHECK(fallInjury(3 * kMillimetresPerBand, taught) <
          fallInjury(3 * kMillimetresPerBand, kFreeFallMm));

    // WATER UNDER YOU IS WORTH MORE THAN ANY AMOUNT OF SKILL, and it should be:
    // going off a quay into the harbour is how people actually survive these.
    const std::int32_t wet = kFreeFallMm + kSoftLandingMm;
    CHECK(fallInjury(3 * kMillimetresPerBand, wet) <
          fallInjury(3 * kMillimetresPerBand, taught));
    // Not infinite, though. Eight metres into two feet of dock water still
    // hurts, and a cushion that made the deepest drop in the district free would
    // be the same cheat in a different costume.
    CHECK(fallInjury(3 * kMillimetresPerBand, wet) > 0);
}
