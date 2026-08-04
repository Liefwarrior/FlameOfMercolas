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
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

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
    CHECK(safeDropBands(40, true) == kMaxDropBands);
    // Monotonic, and never past what the geometry allows.
    for (std::int32_t level = 0; level <= 100; ++level) {
        REQUIRE(safeDropBands(level, false) <= safeDropBands(level, true));
        REQUIRE(safeDropBands(level, true) <= kMaxDropBands);
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
