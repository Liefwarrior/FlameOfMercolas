// The wrapping-integer helpers, at the boundaries where Java and C++ disagree.
//
// -fwrapv is on, so plain `a + b` would already wrap the way Java does in this
// build. These helpers exist for the two things a compiler flag cannot do:
// state the intent at the call site, and survive a toolchain that does not have
// the flag (or a future where somebody drops it from one target's flag list and
// the build stays green because nothing asserted on it).
//
// The INT_MIN cases are not decoration. Four operations are TOTAL in Java and
// UNDEFINED in C++, and one of them -- Math.abs -- is a live bug class in the
// reference build (EffectPairing.java:124). A port that reaches for std::abs
// there turns a reproducible wrong answer into undefined behaviour.

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

#include "granadad/sim/fixed.hpp"

using namespace granadad::sim;

namespace {
constexpr std::int32_t kI32Min = std::numeric_limits<std::int32_t>::min();
constexpr std::int32_t kI32Max = std::numeric_limits<std::int32_t>::max();
constexpr std::int64_t kI64Min = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t kI64Max = std::numeric_limits<std::int64_t>::max();
}  // namespace

TEST_CASE("int32 add/sub/mul wrap at both ends") {
    CHECK(wrap_add(kI32Max, 1) == kI32Min);
    CHECK(wrap_add(kI32Min, -1) == kI32Max);
    CHECK(wrap_sub(kI32Min, 1) == kI32Max);
    CHECK(wrap_sub(kI32Max, -1) == kI32Min);
    CHECK(wrap_mul(65536, 65536) == 0);
    CHECK(wrap_mul(kI32Max, 2) == -2);
    CHECK(wrap_mul(kI32Min, -1) == kI32Min);
    CHECK(wrap_mul(kI32Min, kI32Min) == 0);

    // Wrapping is exact, not approximate: adding 1 four billion-odd times
    // returns to where it started. Sampled, not looped, but at the ends.
    CHECK(wrap_add(wrap_add(kI32Max, 1), -1) == kI32Max);
}

TEST_CASE("int64 add/sub/mul wrap at both ends") {
    // Tick counters and world seeds live here: `worldSeed + TICK_STRIDE * tick`
    // overflows within the first few hundred thousand ticks of any nonzero
    // seed, and the RNG chain depends on it wrapping rather than trapping.
    CHECK(wrap_add(kI64Max, INT64_C(1)) == kI64Min);
    CHECK(wrap_add(kI64Min, INT64_C(-1)) == kI64Max);
    CHECK(wrap_sub(kI64Min, INT64_C(1)) == kI64Max);
    CHECK(wrap_mul(INT64_C(4294967296), INT64_C(4294967296)) == 0);
    CHECK(wrap_mul(kI64Max, INT64_C(2)) == INT64_C(-2));
    CHECK(wrap_mul(kI64Min, INT64_C(-1)) == kI64Min);
}

TEST_CASE("negation of INT_MIN is INT_MIN, exactly as Java says") {
    CHECK(wrap_neg(kI32Min) == kI32Min);
    CHECK(wrap_neg(kI64Min) == kI64Min);
    CHECK(wrap_neg(0) == 0);
    CHECK(wrap_neg(kI32Max) == kI32Min + 1);
    CHECK(wrap_neg(-1) == 1);
}

TEST_CASE("wrap_abs(INT_MIN) is NEGATIVE -- the bug is ported on purpose") {
    // Java's Math.abs(Integer.MIN_VALUE) returns Integer.MIN_VALUE. That is not
    // a rounding artifact, it is the documented answer, and code in the
    // reference build depends on it without knowing:
    //
    //   EffectPairing.java:124   if (Math.abs(magnitude) > ceiling) reject;
    //
    // For magnitude == INT_MIN the guard is FALSE and the largest possible
    // magnitude sails through. Reproducing that here keeps the C++ and the Java
    // wrong in the same way, which is the only state from which the two can be
    // fixed together. std::abs(INT_MIN) would instead be undefined behaviour --
    // same wrong answer today, unreproducible tomorrow.
    CHECK(wrap_abs(kI32Min) == kI32Min);
    CHECK(wrap_abs(kI32Min) < 0);
    CHECK(wrap_abs(kI64Min) == kI64Min);
    CHECK(wrap_abs(kI64Min) < 0);

    // Every other input behaves the way anyone would expect.
    CHECK(wrap_abs(0) == 0);
    CHECK(wrap_abs(-1) == 1);
    CHECK(wrap_abs(1) == 1);
    CHECK(wrap_abs(kI32Max) == kI32Max);
    CHECK(wrap_abs(kI32Min + 1) == kI32Max);
    for (std::int32_t i = -1000; i <= 1000; ++i) {
        CHECK(wrap_abs(i) >= 0);
    }
}

TEST_CASE("INT_MIN / -1 answers instead of raising SIGFPE") {
    // On x86 this is not a wrong number, it is an integer-divide-overflow trap
    // and a dead process. Java defines it as INT_MIN.
    CHECK(wrap_div(kI32Min, -1) == kI32Min);
    CHECK(wrap_div(kI64Min, INT64_C(-1)) == kI64Min);
    CHECK(wrap_mod(kI32Min, -1) == 0);
    CHECK(wrap_mod(kI64Min, INT64_C(-1)) == 0);

    // Ordinary division is unchanged -- including C++'s truncation toward zero,
    // which is also Java's.
    CHECK(wrap_div(7, 2) == 3);
    CHECK(wrap_div(-7, 2) == -3);
    CHECK(wrap_div(7, -2) == -3);
    CHECK(wrap_mod(-7, 2) == -1);
    CHECK(wrap_mod(7, -2) == 1);

    // Divide by zero returns 0 rather than trapping. Java throws; either way it
    // is a content bug, and this only decides whether the tick survives it.
    CHECK(wrap_div(1, 0) == 0);
    CHECK(wrap_mod(1, 0) == 0);
    CHECK(wrap_div(kI64Max, INT64_C(0)) == 0);
}

TEST_CASE("floor_div survives INT_MIN / -1 too") {
    // floor_div used to divide directly, so this input was a trap sitting one
    // negative constant away from a live path.
    CHECK(floor_div(kI32Min, -1) == kI32Min);
    CHECK(floor_mod(kI32Min, -1) == 0);
    CHECK(floor_div(kI32Min, 32) == kI32Min / 32);
    CHECK(floor_mod(kI32Min, 32) == 0);
    CHECK(floor_mod(kI32Min + 1, 32) == 1);
}

TEST_CASE("the helpers are constexpr, so a wrap costs nothing at run time") {
    static_assert(wrap_add(kI32Max, 1) == kI32Min);
    static_assert(wrap_abs(kI32Min) == kI32Min);
    static_assert(wrap_div(kI32Min, -1) == kI32Min);
    static_assert(wrap_neg(kI64Min) == kI64Min);
    static_assert(floor_mod(-1, 32) == 31);
    CHECK(true);
}
