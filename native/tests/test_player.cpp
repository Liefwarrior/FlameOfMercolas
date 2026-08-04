// The body: sub-tile movement, collision, the climb rule, and the promise that
// a recorded session replays to the same integers.

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/player.hpp"

using namespace granadad::sim;
namespace content = granadad::content;

namespace {

const content::World& docksWorld() {
    static const content::World world =
        content::loadWorldFile(content::bakedMap(docks::kWorldName));
    return world;
}

const TileQuery& docksTiles() {
    static const TileQuery tiles(docksWorld());
    return tiles;
}

PlayerBody spawned() {
    return PlayerBody(docksTiles(), docks::kSpawnTileX, docks::kSpawnTileY, docks::kSpawnBand,
                      docks::kSpawnYaw);
}

MoveInput walkForward() {
    MoveInput input;
    input.forward = 1;
    return input;
}

}  // namespace

TEST_CASE("the authored spawn is a place a body can actually stand") {
    const PlayerBody body = spawned();
    CHECK(body.spawnedLegally());
    CHECK(body.tileX() == docks::kSpawnTileX);
    CHECK(body.tileY() == docks::kSpawnTileY);
    CHECK(body.band() == docks::kSpawnBand);
    CHECK(body.yaw() == docks::kSpawnYaw);
    // Placed at the exact tile centre, not at its corner.
    CHECK(q8_sub(body.x()) == kSubHalf);
    CHECK(q8_sub(body.y()) == kSubHalf);
    CHECK(body.feetZ() == q8_of_tile(docks::kSpawnBand));
    CHECK(body.eyeZ() == q8_of_tile(docks::kSpawnBand) + kEyeHeight);
}

TEST_CASE("spawning inside geometry is reported rather than tolerated") {
    // Straight into a warehouse wall on Tarwalk's south side.
    REQUIRE(docksTiles().solid(143, 66, docks::kBandQuayside));
    const PlayerBody body(docksTiles(), 143, 66, docks::kBandQuayside, kFacingNorth);
    CHECK_FALSE(body.spawnedLegally());
}

TEST_CASE("movement is continuous and sub-tile, not tile-snapped") {
    PlayerBody body = spawned();
    const std::int32_t startY = body.y();
    body.step(walkForward());
    // One step at walk speed is 11/256 of a tile: the body has moved, and it
    // has NOT jumped a whole tile.
    CHECK(body.y() == startY - kWalkSpeed);
    CHECK(body.tileY() == docks::kSpawnTileY);
    CHECK(body.x() == q8_tile_centre(docks::kSpawnTileX));  // due north is pure -Y
}

TEST_CASE("running is faster than walking and both are per step, not per frame") {
    PlayerBody walker = spawned();
    PlayerBody runner = spawned();
    MoveInput run = walkForward();
    run.run = true;
    for (int i = 0; i < 10; ++i) {
        walker.step(walkForward());
        runner.step(run);
    }
    CHECK(walker.y() == q8_tile_centre(docks::kSpawnTileY) - 10 * kWalkSpeed);
    CHECK(runner.y() == q8_tile_centre(docks::kSpawnTileY) - 10 * kRunSpeed);
    CHECK(runner.stepCount() == 10);
}

TEST_CASE("a diagonal is not faster than a straight line") {
    PlayerBody straight = spawned();
    PlayerBody diagonal = spawned();
    MoveInput both = walkForward();
    both.strafe = 1;

    for (int i = 0; i < 60; ++i) {
        straight.step(walkForward());
        diagonal.step(both);
    }
    const auto distance = [](const PlayerBody& body) {
        const std::int64_t dx = body.x() - q8_tile_centre(docks::kSpawnTileX);
        const std::int64_t dy = body.y() - q8_tile_centre(docks::kSpawnTileY);
        return dx * dx + dy * dy;
    };
    // Within 4% of each other; without the 1/sqrt(2) scale the diagonal would
    // be 41% longer, which is the classic bug this guards.
    const std::int64_t straightDistance = distance(straight);
    const std::int64_t diagonalDistance = distance(diagonal);
    CHECK(diagonalDistance <= straightDistance);
    CHECK(diagonalDistance * 100 > straightDistance * 92);
}

TEST_CASE("keyboard turn is Barony-weighty, not the Java build's twitch") {
    PlayerBody body = spawned();
    MoveInput turn;
    turn.turn = 1;
    for (int i = 0; i < kStepsPerSecond; ++i) {
        body.step(turn);
    }
    const Angle afterOneSecond = body.yaw();
    // 60-70 deg/s is the measured Barony band (COMBAT-FEEL-REFERENCE.md s2).
    CHECK(afterOneSecond >= angle_from_degrees(60));
    CHECK(afterOneSecond <= angle_from_degrees(70));
    // And emphatically not 165 deg/s.
    CHECK(afterOneSecond < angle_from_degrees(100));
}

TEST_CASE("yaw wraps and pitch clamps") {
    PlayerBody body = spawned();
    MoveInput look;
    look.yawDelta = 30000;
    body.step(look);
    body.step(look);
    body.step(look);  // 90000 BAM = one turn and change
    CHECK(body.yaw() >= 0);
    CHECK(body.yaw() < kTurnFull);
    CHECK(body.yaw() == (90000 & (kTurnFull - 1)));

    MoveInput up;
    up.pitchDelta = 5000;
    for (int i = 0; i < 20; ++i) {
        body.step(up);
    }
    CHECK(body.pitch() == kMaxPitch);
    MoveInput down;
    down.pitchDelta = -5000;
    for (int i = 0; i < 40; ++i) {
        body.step(down);
    }
    CHECK(body.pitch() == -kMaxPitch);
}

TEST_CASE("walls stop the body and it never ends up inside one") {
    PlayerBody body = spawned();
    MoveInput south = walkForward();
    // Turn to face the warehouse wall behind the spawn and walk into it for
    // ten seconds.
    body.setYaw(kFacingSouth);
    for (int i = 0; i < 10 * kStepsPerSecond; ++i) {
        body.step(south);
        REQUIRE(docksTiles().standable(body.tileX(), body.tileY(), body.band()));
        REQUIRE_FALSE(docksTiles().solid(body.tileX(), body.tileY(), body.band()));
    }
    // It went somewhere and then stopped, short of the wall.
    CHECK(body.y() > q8_tile_centre(docks::kSpawnTileY));
    CHECK(docksTiles().solid(body.tileX(), body.tileY() + 1, body.band()));
}

TEST_CASE("the body cannot walk off the quay onto the water") {
    // North of the spawn Tarwalk runs into the harbour: OPEN air over deep
    // water. Walking north for twenty seconds must end on stone.
    PlayerBody body = spawned();
    for (int i = 0; i < 20 * kStepsPerSecond; ++i) {
        body.step(walkForward());
        REQUIRE(docksTiles().walkable(body.tileX(), body.tileY(), body.band()));
        REQUIRE(docksTiles().fluidDepth(body.tileX(), body.tileY(), body.band()) <
                kBlockingFluidDepth);
    }
}

TEST_CASE("a body sliding along a wall keeps its tangential speed") {
    // South-east into the warehouse front: the southward component is refused
    // and the eastward one is not. Resolving both axes together would stop the
    // body dead on the wall; resolving them one at a time slides it along.
    PlayerBody blocked = spawned();
    PlayerBody sliding = spawned();
    blocked.setYaw(kFacingSouth);
    sliding.setYaw(kFacingSouth - kTurnFull / 8);  // yaw runs clockwise, so this is SE

    const std::int32_t startX = sliding.x();
    for (int i = 0; i < 3 * kStepsPerSecond; ++i) {
        blocked.step(walkForward());
        sliding.step(walkForward());
    }
    // Both walked south until something stopped them...
    CHECK(blocked.tileY() > docks::kSpawnTileY + 1);
    CHECK(sliding.tileY() > docks::kSpawnTileY + 1);
    CHECK(docksTiles().solid(blocked.tileX(), blocked.tileY() + 1, blocked.band()));
    // ...and the one walking straight at the wall has not moved sideways at
    // all, while the one at an angle has kept every bit of its eastward speed.
    CHECK(blocked.x() == q8_tile_centre(docks::kSpawnTileX));
    CHECK(sliding.x() - startX > kSubOne);
}

namespace {

/// A ramp or stair on the quayside with quayside footing on one side and
/// mid-slope footing on the other, plus the facing that walks up it.
struct Climb {
    std::int32_t footX = 0;
    std::int32_t footY = 0;
    Angle facing = 0;
    bool found = false;
};

Climb findClimbUp(const TileQuery& tiles) {
    struct Dir {
        std::int32_t dx;
        std::int32_t dy;
        Angle facing;
    };
    const Dir dirs[4] = {{0, -1, kFacingNorth},
                         {1, 0, kFacingEast},
                         {0, 1, kFacingSouth},
                         {-1, 0, kFacingWest}};
    for (std::int32_t y = 2; y < tiles.sizeY() - 2; ++y) {
        for (std::int32_t x = 2; x < tiles.sizeX() - 2; ++x) {
            if (!tiles.climbable(x, y, docks::kBandQuayside) ||
                !tiles.standable(x, y, docks::kBandQuayside)) {
                continue;
            }
            for (const Dir& dir : dirs) {
                const std::int32_t ux = x + dir.dx;
                const std::int32_t uy = y + dir.dy;
                const std::int32_t fx = x - dir.dx;
                const std::int32_t fy = y - dir.dy;
                // The tile ahead must have footing ONLY one level up, and the
                // tile behind footing only on the quayside. Anywhere the map
                // offers a lower ledge as well, "walk forwards" is genuinely
                // ambiguous and stepBand's down-before-up preference takes it
                // -- which is correct behaviour and a useless test fixture.
                const bool upAhead = tiles.standable(ux, uy, docks::kBandMidSlope) &&
                                     !tiles.standable(ux, uy, docks::kBandQuayside) &&
                                     !tiles.standable(ux, uy, docks::kBandQuayside - 1);
                const bool footBehind = tiles.standable(fx, fy, docks::kBandQuayside) &&
                                        !tiles.standable(fx, fy, docks::kBandQuayside - 1);
                if (upAhead && footBehind) {
                    return Climb{fx, fy, dir.facing, true};
                }
            }
        }
    }
    return Climb{};
}

}  // namespace

TEST_CASE("the body climbs to the mid-slope band by walking up a ramp or stair") {
    const TileQuery& tiles = docksTiles();
    const Climb climb = findClimbUp(tiles);
    REQUIRE(climb.found);

    PlayerBody body(tiles, climb.footX, climb.footY, docks::kBandQuayside, climb.facing);
    REQUIRE(body.spawnedLegally());
    for (int i = 0; i < 5 * kStepsPerSecond; ++i) {
        body.step(walkForward());
    }
    CHECK(body.band() >= docks::kBandMidSlope);
    // The feet settled onto the new band rather than staying at the old height.
    CHECK(body.feetZ() == q8_of_tile(body.band()));
}

TEST_CASE("the eye eases across a band change instead of snapping") {
    const TileQuery& tiles = docksTiles();
    const Climb climb = findClimbUp(tiles);
    REQUIRE(climb.found);

    PlayerBody body(tiles, climb.footX, climb.footY, docks::kBandQuayside, climb.facing);
    const std::int32_t startBand = body.band();
    int stepsToChange = 0;
    while (body.band() == startBand && stepsToChange < 5 * kStepsPerSecond) {
        body.step(walkForward());
        ++stepsToChange;
    }
    REQUIRE(body.band() == startBand + 1);
    // The instant the band changed the feet are still at the OLD height: a
    // whole tile of climb has not happened in one step.
    CHECK(body.feetZ() == q8_of_tile(startBand) + kEyeEaseRate);
    CHECK(body.feetZ() < q8_of_tile(body.band()));

    // And it arrives in 256/16 = 16 steps, a quarter of a second.
    int easeSteps = 0;
    MoveInput idle;
    while (body.feetZ() != q8_of_tile(body.band()) && easeSteps < 100) {
        body.step(idle);
        ++easeSteps;
    }
    CHECK(easeSteps == kSubOne / kEyeEaseRate - 1);
    CHECK(kEyeEaseRate * kStepsPerSecond > kSubOne);  // never slower than a tile a second
}

TEST_CASE("the same input sequence replays to the same body, bit for bit") {
    // The determinism claim, at the altitude the player lives at. Two bodies,
    // one scripted session, one digest.
    const auto run = [](int seed) {
        PlayerBody body = spawned();
        MoveInput input;
        for (int i = 0; i < 1200; ++i) {
            const int phase = (i * 7 + seed) % 11;
            input.forward = phase < 6 ? 1 : (phase < 8 ? -1 : 0);
            input.strafe = phase == 3 ? 1 : (phase == 9 ? -1 : 0);
            input.turn = phase % 3 == 0 ? 1 : (phase % 5 == 0 ? -1 : 0);
            input.yawDelta = (i % 17) * 13 - 100;
            input.pitchDelta = (i % 23) * 7 - 70;
            input.run = (i % 4) == 0;
            body.step(input);
        }
        return body;
    };
    const PlayerBody a = run(0);
    const PlayerBody b = run(0);
    CHECK(a.digest() == b.digest());
    CHECK(a.x() == b.x());
    CHECK(a.y() == b.y());
    CHECK(a.band() == b.band());
    CHECK(a.yaw() == b.yaw());
    CHECK(a.stepCount() == 1200);

    // And the digest is not a constant: a different session lands elsewhere.
    const PlayerBody c = run(5);
    CHECK(a.digest() != c.digest());
}

TEST_CASE("nothing tunnels, whatever the speed constants become") {
    // kMaxStepQ8 caps a single resolved displacement well below the body's
    // own radius, so a wall can never be crossed between two collision checks.
    CHECK(kMaxStepQ8 < kBodyRadius);
    CHECK(kRunSpeed < kMaxStepQ8);
    CHECK(kBodyRadius * 2 < kSubOne);  // and the body fits a one-tile doorway
}
