// The client's fixed-timestep loop, tested where SDL cannot reach.
//
// The case that matters is the one S1 shipped broken: a frame that produces
// ZERO movement steps. Above 60 fps that is most frames, and the mouse delta
// accumulated during it used to die with the frame-local it was accumulated
// into.

#include <doctest/doctest.h>

#include "granadad/render/session.hpp"
#include "granadad/render/step_pump.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/player.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;

TEST_CASE("sixty steps a second, whatever the frame rate") {
    StepPump pump;
    std::int32_t total = 0;
    // 120 fps for one second.
    for (int frame = 0; frame < 120; ++frame) {
        total += pump.advance(1.0 / 120.0);
    }
    CHECK(total == 60);

    StepPump slow;
    std::int32_t slowTotal = 0;
    // 20 fps for one second.
    for (int frame = 0; frame < 20; ++frame) {
        slowTotal += slow.advance(1.0 / 20.0);
    }
    CHECK(slowTotal == 60);
}

TEST_CASE("a frame that runs no step keeps its mouse look for the next one") {
    // THE REGRESSION. At 120 fps every other frame produces no step at all.
    StepPump pump;
    pump.addLook(400, 0);
    const std::int32_t steps = pump.advance(1.0 / 120.0);
    REQUIRE(steps == 0);
    CHECK(pump.pendingYaw() == 400);

    // Second frame: one step is due, and it carries BOTH frames' motion.
    pump.addLook(300, 0);
    CHECK(pump.advance(1.0 / 120.0) == 1);
    const sim::MoveInput input = pump.nextStepInput(sim::MoveInput{});
    CHECK(input.yawDelta == 700);
    CHECK(pump.pendingYaw() == 0);
}

TEST_CASE("look is applied to exactly one step of a batch, never to all of them") {
    // The other half of the same bug: a slow frame owing four steps must not
    // turn the head four times.
    StepPump pump;
    pump.addLook(1000, -250);
    const std::int32_t steps = pump.advance(4.0 / 60.0);
    REQUIRE(steps == 4);

    sim::Angle appliedYaw = 0;
    sim::Angle appliedPitch = 0;
    for (std::int32_t i = 0; i < steps; ++i) {
        const sim::MoveInput input = pump.nextStepInput(sim::MoveInput{});
        appliedYaw += input.yawDelta;
        appliedPitch += input.pitchDelta;
    }
    CHECK(appliedYaw == 1000);
    CHECK(appliedPitch == -250);
}

TEST_CASE("the body turns by the same total at 200 fps as at 30 fps") {
    // The behavioural claim, on the real body over the real Docks: the same
    // mouse motion, delivered in the same wall-clock second, must leave the
    // head pointing the same way whatever the renderer managed.
    SessionConfig config;
    config.world = sim::docks::kWorldName;
    config.width = 64;
    config.height = 64;

    // 3600 BAM of yaw — about twenty degrees — delivered over one second. Both
    // frame rates divide it exactly, so the totals are equal by arithmetic and
    // any difference is the pump's.
    constexpr sim::Angle kYawPerSecond = 3600;

    const auto turnedYawAt = [&config](int fps) {
        Session session(config);
        StepPump pump;
        const double frameSeconds = 1.0 / static_cast<double>(fps);
        for (int frame = 0; frame < fps; ++frame) {
            pump.addLook(kYawPerSecond / fps, 0);
            const std::int32_t steps = pump.advance(frameSeconds);
            for (std::int32_t i = 0; i < steps; ++i) {
                session.step(pump.nextStepInput(sim::MoveInput{}));
            }
        }
        // Whatever is still pending belongs to the player; count it.
        return sim::wrap_add(session.body().yaw(), pump.pendingYaw()) & (sim::kTurnFull - 1);
    };

    const sim::Angle slow = turnedYawAt(30);
    const sim::Angle fast = turnedYawAt(200);
    CHECK(slow == fast);
    // ...and it actually turned, or the check above is two zeroes agreeing.
    CHECK(slow != (sim::docks::kSpawnYaw & (sim::kTurnFull - 1)));
}

TEST_CASE("a stalled window drops the missing steps rather than replaying them") {
    // After a drag or a breakpoint, catching up in real time is a burst of
    // movement the player did not ask for.
    StepPump pump;
    const std::int32_t steps = pump.advance(30.0);
    CHECK(steps <= static_cast<std::int32_t>(StepPump::maxCatchUpSeconds() *
                                             static_cast<double>(sim::kStepsPerSecond)));
    CHECK(steps == 15);
}
