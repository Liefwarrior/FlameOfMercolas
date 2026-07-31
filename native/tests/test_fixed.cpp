#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "granadad/sim/build_info.hpp"
#include "granadad/sim/fixed.hpp"

using namespace granadad::sim;

TEST_CASE("int32 arithmetic wraps the way Java does") {
    // The Java reference build relies on int wraparound. C++ calls signed
    // overflow undefined, so these must go through the explicit helpers.
    CHECK(wrap_add(2147483647, 1) == -2147483648);
    CHECK(wrap_add(-2147483648, -1) == 2147483647);
    CHECK(wrap_sub(-2147483648, 1) == 2147483647);
    CHECK(wrap_mul(65536, 65536) == 0);
    CHECK(wrap_mul(2147483647, 2) == -2);
}

TEST_CASE("Q16.16 round-trips and multiplies without losing the unit") {
    CHECK(q16_from_int(0) == 0);
    CHECK(q16_from_int(1) == Q16_ONE);
    CHECK(q16_from_int(-3) == -3 * Q16_ONE);

    CHECK(q16_mul(Q16_ONE, Q16_ONE) == Q16_ONE);
    CHECK(q16_mul(q16_from_int(3), q16_from_int(4)) == q16_from_int(12));
    CHECK(q16_mul(Q16_ONE / 2, Q16_ONE / 2) == Q16_ONE / 4);
}

TEST_CASE("Q16.16 division") {
    CHECK(q16_div(q16_from_int(12), q16_from_int(4)) == q16_from_int(3));
    CHECK(q16_div(Q16_ONE, q16_from_int(2)) == Q16_ONE / 2);

    // Division by zero is a content bug. It returns 0 rather than trapping, so
    // a bad raw cannot take the whole simulation down mid-tick.
    CHECK(q16_div(Q16_ONE, 0) == 0);
}

TEST_CASE("floor division and modulo agree with tile-grid intuition") {
    // C++ truncates toward zero; grid math wants floor. Tile -1 must land in
    // chunk -1 at offset 31, not chunk 0 at offset -1.
    CHECK(floor_div(-1, 32) == -1);
    CHECK(floor_mod(-1, 32) == 31);
    CHECK(floor_div(-33, 32) == -2);
    CHECK(floor_mod(-33, 32) == 31);
    CHECK(floor_div(33, 32) == 1);
    CHECK(floor_mod(33, 32) == 1);

    // floor_mod is never negative — that is the whole point of it.
    for (int i = -100; i <= 100; ++i) {
        CHECK(floor_mod(i, 32) >= 0);
        CHECK(floor_mod(i, 32) < 32);
    }
}

TEST_CASE("build info identifies the binary") {
    const BuildInfo info = build_info();
    CHECK_FALSE(info.version.empty());
    CHECK_FALSE(info.revision.empty());
    CHECK(info.target != "unknown");
}
