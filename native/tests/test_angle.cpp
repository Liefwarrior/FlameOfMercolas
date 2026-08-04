// The integer trig the body walks on.
//
// If these drift, every body in the game walks a hair off course and the world
// hash moves with them. The table is literal integers precisely so that this
// suite is comparing against a fixed thing rather than against whatever the
// host's libm happened to compute at compile time.

#include <doctest/doctest.h>

#include <cstdint>

#include "granadad/sim/angle.hpp"

using namespace granadad::sim;

TEST_CASE("the cardinal facings are exact") {
    CHECK(sin_q16(kFacingNorth) == 0);
    CHECK(cos_q16(kFacingNorth) == kTrigOne);
    CHECK(sin_q16(kFacingEast) == kTrigOne);
    CHECK(cos_q16(kFacingEast) == 0);
    CHECK(sin_q16(kFacingSouth) == 0);
    CHECK(cos_q16(kFacingSouth) == -kTrigOne);
    CHECK(sin_q16(kFacingWest) == -kTrigOne);
    CHECK(cos_q16(kFacingWest) == 0);
}

TEST_CASE("north is -Y and east is +X, which is the whole compass convention") {
    CHECK(forward_x_q16(kFacingNorth) == 0);
    CHECK(forward_y_q16(kFacingNorth) == -kTrigOne);
    CHECK(forward_x_q16(kFacingEast) == kTrigOne);
    CHECK(forward_y_q16(kFacingEast) == 0);
    CHECK(forward_x_q16(kFacingSouth) == 0);
    CHECK(forward_y_q16(kFacingSouth) == kTrigOne);
    CHECK(forward_x_q16(kFacingWest) == -kTrigOne);
    CHECK(forward_y_q16(kFacingWest) == 0);

    // Strafing right from north walks east. If this ever inverts, every
    // player's A and D keys swap and no test that only checks forward notices.
    CHECK(right_x_q16(kFacingNorth) == kTrigOne);
    CHECK(right_y_q16(kFacingNorth) == 0);
    CHECK(right_x_q16(kFacingEast) == 0);
    CHECK(right_y_q16(kFacingEast) == kTrigOne);
}

TEST_CASE("sine wraps rather than needing to be normalised") {
    for (std::int32_t k = -4; k <= 4; ++k) {
        CAPTURE(k);
        CHECK(sin_q16(kFacingEast + k * kTurnFull) == kTrigOne);
        CHECK(sin_q16(kFacingWest + k * kTurnFull) == -kTrigOne);
    }
    // Not just the cardinals: an arbitrary angle and the same angle plus three
    // whole turns are the same direction.
    for (std::int32_t a = 0; a < kTurnFull; a += 137) {
        REQUIRE(sin_q16(a) == sin_q16(a + 3 * kTurnFull));
        REQUIRE(sin_q16(a) == sin_q16(a - 3 * kTurnFull));
    }
}

TEST_CASE("sin^2 + cos^2 stays within a whisker of one everywhere") {
    // The table is interpolated, so this is not exact -- but it bounds the
    // error, which is the useful claim: a body's speed must not depend on the
    // direction it faces by more than a rounding step.
    std::int64_t worst = 0;
    for (std::int32_t a = 0; a < kTurnFull; ++a) {
        const std::int64_t s = sin_q16(a);
        const std::int64_t c = cos_q16(a);
        const std::int64_t magnitude = s * s + c * c;
        const std::int64_t one = static_cast<std::int64_t>(kTrigOne) * kTrigOne;
        const std::int64_t error = magnitude > one ? magnitude - one : one - magnitude;
        if (error > worst) {
            worst = error;
        }
    }
    // Under one part in 8000 of unit magnitude.
    CHECK(worst * 8000 < static_cast<std::int64_t>(kTrigOne) * kTrigOne);
}

TEST_CASE("sine is monotone across the first quarter turn") {
    std::int32_t previous = sin_q16(0);
    for (std::int32_t a = 1; a <= kTurnQuarter; ++a) {
        const std::int32_t value = sin_q16(a);
        REQUIRE(value >= previous);
        previous = value;
    }
    CHECK(previous == kTrigOne);
}

TEST_CASE("degrees convert to BAM the way the tuning constants assume") {
    CHECK(angle_from_degrees(0) == 0);
    CHECK(angle_from_degrees(90) == kTurnQuarter);
    CHECK(angle_from_degrees(180) == kTurnHalf);
    CHECK(angle_from_degrees(360) == kTurnFull);
}

TEST_CASE("the compass names the eight points and rounds to the nearest") {
    CHECK(compass_point(kFacingNorth) == "N");
    CHECK(compass_point(kFacingEast) == "E");
    CHECK(compass_point(kFacingSouth) == "S");
    CHECK(compass_point(kFacingWest) == "W");
    CHECK(compass_point(kFacingNorth + kTurnFull / 8) == "NE");
    CHECK(compass_point(kFacingSouth + kTurnFull / 8) == "SW");
    // One unit shy of due east still reads E, not N.
    CHECK(compass_point(kFacingEast - 1) == "E");
    CHECK(compass_point(-1) == "N");
}
