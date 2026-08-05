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

// The movement cases anchor on a tile of their own rather than on the camera's
// spawn, and on due north rather than on its facing. The camera's spawn is
// chosen for what it LOOKS at, and re-framing a screenshot in a later sprint
// must not be able to break the collision suite -- these coordinates are picked
// for their geometry: open street ahead, a warehouse wall five tiles south, the
// harbour four tiles north, nothing to climb in any direction.
constexpr std::int32_t kWalkTileX = 143;
constexpr std::int32_t kWalkTileY = 61;

PlayerBody spawned() {
    return PlayerBody(docksTiles(), kWalkTileX, kWalkTileY, docks::kSpawnBand, kFacingNorth);
}

MoveInput walkForward() {
    MoveInput input;
    input.forward = 1;
    return input;
}

}  // namespace

TEST_CASE("the authored spawn is a place a body can actually stand") {
    // The camera's spawn, the one a session and every capture starts from.
    const PlayerBody camera(docksTiles(), docks::kSpawnTileX, docks::kSpawnTileY,
                            docks::kSpawnBand, docks::kSpawnYaw);
    CHECK(camera.spawnedLegally());
    CHECK(camera.band() == docks::kBandQuayside);
    CHECK(camera.yaw() == docks::kSpawnYaw);

    const PlayerBody body = spawned();
    CHECK(body.spawnedLegally());
    CHECK(body.tileX() == kWalkTileX);
    CHECK(body.tileY() == kWalkTileY);
    CHECK(body.band() == docks::kSpawnBand);
    CHECK(body.yaw() == kFacingNorth);
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
    // #77: the DEFAULT gait is a jog, not a walk. One step is kJogSpeed/256 of
    // a tile: the body has moved, and it has NOT jumped a whole tile.
    CHECK(body.y() == startY - kJogSpeed);
    CHECK(body.tileY() == kWalkTileY);
    CHECK(body.x() == q8_tile_centre(kWalkTileX));  // due north is pure -Y
}

TEST_CASE("the three gaits are ordered and all of them are per step, not per frame") {
    PlayerBody walker = spawned();
    PlayerBody jogger = spawned();
    PlayerBody sprinter = spawned();
    MoveInput slow = walkForward();
    slow.walk = true;
    MoveInput fast = walkForward();
    fast.sprint = true;
    for (int i = 0; i < 10; ++i) {
        walker.step(slow);
        jogger.step(walkForward());
        sprinter.step(fast);
    }
    CHECK(walker.y() == q8_tile_centre(kWalkTileY) - 10 * kWalkSpeed);
    CHECK(jogger.y() == q8_tile_centre(kWalkTileY) - 10 * kJogSpeed);
    CHECK(sprinter.y() == q8_tile_centre(kWalkTileY) - 10 * kSprintSpeed);
    CHECK(sprinter.stepCount() == 10);
    // NEITHER MODIFIER IS THE FAST ONE. The whole of #77's movement complaint in
    // one assertion: the key you are not pressing is a jog, walking is what the
    // brake does, and a game where the default gait is the slow one reads
    // archaic however good the rest of it is.
    CHECK(jogger.y() < walker.y());
    CHECK(sprinter.y() < jogger.y());
}

TEST_CASE("a diagonal is not faster than a straight line") {
    PlayerBody straight = spawned();
    PlayerBody diagonal = spawned();
    MoveInput both = walkForward();
    both.strafe = 1;

    // TWENTY STEPS, NOT SIXTY, and #77 is why. The default gait is a jog now --
    // 24/256 of a tile a step against the old 11 -- so sixty steps carries the
    // body five and a half tiles, and the case's own header says the harbour is
    // four tiles north of this tile. The straight walker was arriving at the
    // quay lip and stopping there, which made the blocked body the SHORTER of
    // the two and turned a real invariant into a measurement of the coastline.
    constexpr int kSteps = 20;
    for (int i = 0; i < kSteps; ++i) {
        straight.step(walkForward());
        diagonal.step(both);
    }
    // Neither of them hit anything: the whole comparison is meaningless if one
    // of them stopped, and this is the assertion that says so out loud rather
    // than leaving the next reader to work out why the numbers moved.
    REQUIRE(straight.y() == q8_tile_centre(kWalkTileY) - kSteps * kJogSpeed);

    const auto distance = [](const PlayerBody& body) {
        const std::int64_t dx = body.x() - q8_tile_centre(kWalkTileX);
        const std::int64_t dy = body.y() - q8_tile_centre(kWalkTileY);
        return dx * dx + dy * dy;
    };
    // Within 4% of each other; without the 1/sqrt(2) scale the diagonal would
    // be 41% longer, which is the classic bug this guards.
    const std::int64_t straightDistance = distance(straight);
    const std::int64_t diagonalDistance = distance(diagonal);
    CHECK(diagonalDistance <= straightDistance);
    CHECK(diagonalDistance * 100 > straightDistance * 92);
}

TEST_CASE("the keyboard turn is an accessibility fallback, and it does not set the feel") {
    PlayerBody body = spawned();
    MoveInput turn;
    turn.turn = 1;
    for (int i = 0; i < kStepsPerSecond; ++i) {
        body.step(turn);
    }
    const Angle afterOneSecond = body.yaw();
    // 140 deg/s, which is where keyboard turning sits in games people play now.
    //
    // WHAT THIS CASE USED TO ASSERT was 60-70 deg/s, "the measured Barony band
    // (COMBAT-FEEL-REFERENCE.md s2)". That measurement is real and it is a
    // measurement of a 2015 roguelike's KEYBOARD FALLBACK -- the same bullet of
    // the same document says mouse-look "was not measurable". Pinning the whole
    // game's turn rate to it made turning round take five and a half seconds,
    // which is a large part of what Eli meant by archaic. The reference doc now
    // marks the number keyboard-only on the line itself.
    CHECK(afterOneSecond >= angle_from_degrees(130));
    CHECK(afterOneSecond <= angle_from_degrees(150));
}

TEST_CASE("mouse look is raw: no smoothing, no acceleration, no rate cap") {
    // THE PRIMARY AIM PATH IS NOT RATE-LIMITED BY ANYTHING. A single step
    // carrying a whole frame's worth of a fast flick has to arrive whole --
    // clamping it to a per-step maximum is the same bug as smoothing it, and it
    // is the one that would quietly reintroduce the keyboard's number as the
    // ceiling on the mouse's.
    PlayerBody body = spawned();
    MoveInput flick;
    flick.yawDelta = kTurnQuarter;  // ninety degrees, in one step
    body.step(flick);
    CHECK(body.yaw() == (kFacingNorth + kTurnQuarter));
    // Which is more than the KEYBOARD could turn in twenty-three steps.
    CHECK(kTurnQuarter > 23 * kTurnRate);

    // And it is linear: twice the delta is exactly twice the angle, with no
    // curve applied anywhere between the mouse and the head.
    PlayerBody one = spawned();
    PlayerBody two = spawned();
    MoveInput small;
    small.yawDelta = 137;
    MoveInput big;
    big.yawDelta = 274;
    one.step(small);
    one.step(small);
    two.step(big);
    CHECK(one.yaw() == two.yaw());
}

TEST_CASE("a standing jump clears half a metre and gets you onto nothing") {
    PlayerBody body = spawned();
    const std::int32_t ground = body.feetZ();
    REQUIRE(body.jump());
    CHECK_FALSE(body.jump());  // no double jump, and no pogo from a held key

    std::int32_t apex = ground;
    int inTheAir = 0;
    for (int i = 0; i < 4 * kJumpSteps; ++i) {
        body.step(MoveInput{});
        if (body.jumping()) {
            ++inTheAir;
        }
        apex = body.feetZ() > apex ? body.feetZ() : apex;
    }
    // Back on the deck, on the band it left, having risen kJumpRiseMm and not
    // one storey. A jump that could clear a band would be the archaic climb key
    // wearing a modern binding.
    CHECK(body.feetZ() == ground);
    CHECK(body.band() == docks::kSpawnBand);
    CHECK(inTheAir == kJumpSteps - 1);
    const std::int32_t rose = apex - ground;
    CHECK(rose > 0);
    CHECK(rose <= kJumpRiseQ8);
    // In millimetres, against a person: half a metre give or take the integer
    // parabola's own apex, and a very long way under a 2,700 mm storey.
    CHECK(mmFromBandQ8(rose) >= 400);
    CHECK(mmFromBandQ8(rose) <= kJumpRiseMm);
    CHECK(mmFromBandQ8(rose) < kMillimetresPerBand / 4);
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
    CHECK(body.y() > q8_tile_centre(kWalkTileY));
    CHECK(docksTiles().solid(body.tileX(), body.tileY() + 1, body.band()));
}

TEST_CASE("the body cannot walk off the quay onto the water") {
    // REWRITTEN IN S2, because the S1 version could not fail. It asserted
    // walkable(bodyTile) and fluidDepth(bodyTile) < blocking -- the same two
    // predicates that gate movement, so both were true by construction. Proven
    // in the S1 review: making walkable() accept OPEN, which is precisely the
    // failure this case is named for, turned four other cases red and left this
    // one green.
    //
    // So the claim is now about the HARBOUR'S OWN GEOMETRY, read out of the
    // baked bytes, and about where the body ends up on it.
    const TileQuery& tiles = docksTiles();
    constexpr std::int32_t kLastQuayRow = 58;
    constexpr std::int32_t kEdgeRow = 57;

    // The map first. Column 143 is a gap between two finger piers: the quay's
    // last floor row is y=58, and the two rows north of it hold nothing at all.
    REQUIRE(tiles.form(kWalkTileX, kLastQuayRow, docks::kSpawnBand) ==
            content::TileForm::Floor);
    CHECK(tiles.form(kWalkTileX, kEdgeRow, docks::kSpawnBand) == content::TileForm::Open);
    CHECK(tiles.form(kWalkTileX, kEdgeRow - 1, docks::kSpawnBand) == content::TileForm::Open);
    // ...and the harbour is under them, filled to the top of z=18 at the depth
    // that blocks a body outright.
    CHECK(tiles.fluidDepth(kWalkTileX, kEdgeRow - 1, docks::kHarbourSurfaceBand) == 7);
    CHECK(tiles.fluidDepth(kWalkTileX, kEdgeRow - 1, docks::kHarbourSurfaceBand) >=
          kBlockingFluidDepth);
    // There is no band to step into off the edge, at any level the rule allows.
    CHECK(tiles.stepBand(kWalkTileX, kLastQuayRow, docks::kSpawnBand, kWalkTileX, kEdgeRow) ==
          TileQuery::kNoBand);

    // Now the body. Twenty seconds due north is far more than the seven tiles
    // it has, so it must arrive at the edge and stop dead on the last row.
    PlayerBody body = spawned();
    for (int i = 0; i < 20 * kStepsPerSecond; ++i) {
        body.step(walkForward());
    }
    CHECK(body.tileY() == kLastQuayRow);
    CHECK(body.tileX() == kWalkTileX);
    CHECK(body.band() == docks::kBandQuayside);
    // It walked all the way to the lip -- within one step's travel of the tile
    // boundary -- rather than stopping a comfortable tile short.
    CHECK(body.y() - q8_of_tile(kLastQuayRow) >= 0);
    CHECK(body.y() - q8_of_tile(kLastQuayRow) < kJogSpeed);
    // The collision square DOES hang out over the water, and that is deliberate:
    // OPEN is not solid, so leaning over a quay edge is legal. What is not legal
    // is putting the body's CENTRE -- its feet, and the tile every other system
    // asks about -- anywhere but on the deck.
    CHECK(q8_tile(body.y() - kBodyRadius) == kEdgeRow);
    CHECK(q8_tile(body.y()) == kLastQuayRow);
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
    CHECK(blocked.tileY() > kWalkTileY + 1);
    CHECK(sliding.tileY() > kWalkTileY + 1);
    CHECK(docksTiles().solid(blocked.tileX(), blocked.tileY() + 1, blocked.band()));
    // ...and the one walking straight at the wall has not moved sideways at
    // all, while the one at an angle has kept every bit of its eastward speed.
    CHECK(blocked.x() == q8_tile_centre(kWalkTileX));
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
            input.sprint = (i % 4) == 0;
            input.walk = (i % 9) == 0;
            input.jump = (i % 137) == 0;
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
    CHECK(kSprintSpeed < kMaxStepQ8);
    CHECK(kBodyRadius * 2 < kSubOne);  // and the body fits a one-tile doorway
}
