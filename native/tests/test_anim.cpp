// Task #83: menu and HUD transition polish. EasedToggle is the one piece of
// state every panel's open/close animation and the HUD's own alert fade are
// built out of -- see the header on why it counts steps and not seconds.

#include <doctest/doctest.h>

#include "granadad/render/anim.hpp"

using granadad::render::EasedToggle;
using granadad::render::ImpactPulse;

TEST_CASE("a fresh toggle starts closed and settled") {
    EasedToggle toggle;
    CHECK(toggle.value() == 0.0F);
    CHECK_FALSE(toggle.target());
    CHECK(toggle.settled());
}

TEST_CASE("snapTo jumps straight there with nothing left to animate") {
    EasedToggle toggle;
    toggle.snapTo(true);
    CHECK(toggle.value() == 1.0F);
    CHECK(toggle.settled());

    toggle.snapTo(false);
    CHECK(toggle.value() == 0.0F);
    CHECK(toggle.settled());
}

TEST_CASE("opening from rest is never exactly zero the instant it is asked for") {
    // THE WHOLE POINT. A fade that starts invisible is a flash on the very
    // first frame, not a fade -- see anim.hpp's own header. The frame drawn
    // the instant a key opens something must already show it.
    EasedToggle toggle(4, 4);
    CHECK(toggle.value() == 0.0F);
    toggle.setTarget(true);
    CHECK(toggle.value() > 0.0F);
    CHECK(toggle.value() < 1.0F);
    CHECK_FALSE(toggle.settled());
}

TEST_CASE("advance rises to fully open over exactly riseSteps calls") {
    EasedToggle toggle(4, 4);
    toggle.setTarget(true);
    // setTarget already carried the first tick -- see the case above -- so
    // three more calls to advance() should reach the ceiling.
    for (int i = 0; i < 3; ++i) {
        CHECK_FALSE(toggle.settled());
        toggle.advance();
    }
    CHECK(toggle.settled());
    CHECK(toggle.value() == 1.0F);
    // And it stays there: advance() past the ceiling does not overshoot.
    toggle.advance();
    CHECK(toggle.value() == 1.0F);
}

TEST_CASE("closing falls to exactly zero over fallSteps calls and stays there") {
    EasedToggle toggle(4, 5);
    toggle.snapTo(true);
    toggle.setTarget(false);
    for (int i = 0; i < 5; ++i) {
        toggle.advance();
    }
    CHECK(toggle.settled());
    CHECK(toggle.value() == 0.0F);
    toggle.advance();
    CHECK(toggle.value() == 0.0F);
}

TEST_CASE("re-opening mid-close resumes from where the fade had got to") {
    // NOT A POP BACK TO FULL. A panel closed and immediately reopened -- the
    // shape a player mashing the same key produces -- keeps easing from
    // wherever it was rather than snapping to 1 and restarting the rise.
    EasedToggle toggle(10, 10);
    toggle.snapTo(true);
    toggle.setTarget(false);
    toggle.advance();
    toggle.advance();
    toggle.advance();
    const float midFade = toggle.value();
    REQUIRE(midFade > 0.0F);
    REQUIRE(midFade < 1.0F);

    toggle.setTarget(true);
    // No bump: the value was already above zero, so setTarget changes
    // direction without jumping it.
    CHECK(toggle.value() == midFade);
    CHECK(toggle.target());
}

TEST_CASE("setTarget with the current target is a no-op, mid-animation included") {
    // IDEMPOTENT. Session calls this every frame a panel is conversing, and a
    // caller re-asserting "still open" must never reset an animation that is
    // already most of the way through.
    EasedToggle toggle(8, 8);
    toggle.setTarget(true);
    toggle.advance();
    toggle.advance();
    const float progressed = toggle.value();
    toggle.setTarget(true);
    CHECK(toggle.value() == progressed);

    toggle.snapTo(false);
    toggle.setTarget(false);
    CHECK(toggle.value() == 0.0F);
}

TEST_CASE("value never leaves 0..1 across a long, direction-flipping run") {
    EasedToggle toggle(3, 7);
    for (int i = 0; i < 200; ++i) {
        toggle.setTarget((i / 5) % 2 == 0);
        toggle.advance();
        CHECK(toggle.value() >= 0.0F);
        CHECK(toggle.value() <= 1.0F);
    }
}

// ---------------------------------------------------------------------------
// INNOVATION SPRINT (item #3): ImpactPulse -- an EVENT, not a STATE. Nothing
// holds it open; trigger() is the whole of what starts it.
// ---------------------------------------------------------------------------

TEST_CASE("a fresh pulse sits at zero until triggered") {
    ImpactPulse pulse;
    CHECK(pulse.value() == 0.0F);
    pulse.advance();
    CHECK(pulse.value() == 0.0F);
}

TEST_CASE("trigger jumps straight to full strength") {
    ImpactPulse pulse(4);
    pulse.trigger();
    CHECK(pulse.value() == 1.0F);
}

TEST_CASE("advance decays to exactly zero over decaySteps calls and stays there") {
    ImpactPulse pulse(4);
    pulse.trigger();
    for (int i = 0; i < 4; ++i) {
        CHECK(pulse.value() > 0.0F);
        pulse.advance();
    }
    CHECK(pulse.value() == 0.0F);
    pulse.advance();
    CHECK(pulse.value() == 0.0F);
}

TEST_CASE("a fresh trigger restacks a pulse that has not finished decaying") {
    // A FLURRY RESTARTS THE FLASH RATHER THAN QUEUEING BEHIND IT. Two punches
    // a handful of steps apart should each read as their own beat, not have
    // the second one silently absorbed into the first's tail.
    ImpactPulse pulse(10);
    pulse.trigger();
    pulse.advance();
    pulse.advance();
    pulse.advance();
    const float midDecay = pulse.value();
    REQUIRE(midDecay > 0.0F);
    REQUIRE(midDecay < 1.0F);

    pulse.trigger();
    CHECK(pulse.value() == 1.0F);
}

TEST_CASE("pulse value never leaves 0..1 across a long run of scattered triggers") {
    ImpactPulse pulse(3);
    for (int i = 0; i < 200; ++i) {
        if (i % 7 == 0) {
            pulse.trigger();
        }
        pulse.advance();
        CHECK(pulse.value() >= 0.0F);
        CHECK(pulse.value() <= 1.0F);
    }
}

// ---------------------------------------------------------------------------
// UI-EA-SPEC sec. 3: the one transition grammar's durations, pinned.
// ---------------------------------------------------------------------------

TEST_CASE("the transition grammar's constants hold the spec's values") {
    // THESE ARE THE SPEC'S NUMBERS, NOT TUNABLES. kPageEaseSteps = 8 (the
    // EasedToggle default every page opens on), kPlateHold ~2.5s, kTutorHold
    // ~3s, kIdleWake ~5s, all at 60 steps a second. A lane that wants a
    // different feel changes the spec first, then this case, then the value
    // -- in that order.
    CHECK(granadad::render::kPageEaseSteps == 8);
    CHECK(granadad::render::kPlateHoldSteps == 150);
    CHECK(granadad::render::kTutorHoldSteps == 180);
    CHECK(granadad::render::kIdleWakeSteps == 300);

    // THE AT-REST SHUTTER SITS INSIDE THE REST WINDOW: after the tutor band's
    // page-open raise has fully eased down, before the idle re-raise arrives.
    // This ordering is what makes the census's "rest" frame a real state and
    // not a lucky race.
    CHECK(granadad::render::kCaptureRestSteps >
          granadad::render::kTutorHoldSteps + granadad::render::kPageEaseSteps);
    CHECK(granadad::render::kCaptureRestSteps < granadad::render::kIdleWakeSteps);
}

TEST_CASE("the shipped defaults ARE the named constants") {
    // EasedToggle() and ImpactPulse() default to kPageEaseSteps -- proven by
    // behaviour rather than by reading the header: a default toggle reaches
    // fully open in exactly kPageEaseSteps worth of motion (setTarget carries
    // the first tick, see "opening from rest" above), and a default pulse
    // decays to zero in exactly kPageEaseSteps advances.
    EasedToggle toggle;
    toggle.setTarget(true);
    for (int i = 0; i < granadad::render::kPageEaseSteps - 1; ++i) {
        CHECK_FALSE(toggle.settled());
        toggle.advance();
    }
    CHECK(toggle.settled());
    CHECK(toggle.value() == 1.0F);

    ImpactPulse pulse;
    pulse.trigger();
    for (int i = 0; i < granadad::render::kPageEaseSteps - 1; ++i) {
        pulse.advance();
        CHECK(pulse.value() > 0.0F);
    }
    pulse.advance();
    CHECK(pulse.value() == 0.0F);
}
