#include "granadad/sim/fixed.hpp"

// fixed.hpp is header-only and constexpr. This translation unit exists so the
// sim library has an object file of its own and so the static-assert block
// below is compiled exactly once.

#include <cstdint>
#include <limits>

namespace granadad::sim {
namespace {

// The bounds as TYPED constants, never as the literals.
//
// `-2147483648` is not an int literal in C++. It parses as unary minus applied
// to 2147483648, which does not fit in an int, so the whole expression is a
// long. That was harmless while there was one wrap_sub; the moment a 64-bit
// overload joined it, `wrap_sub(-2147483648, 1)` became an ambiguous call
// between wrap_sub(int32,int32) and wrap_sub(int64,int64) and the build stopped.
// The compiler caught it. On a platform where long is 32 bits it would have
// silently picked the 32-bit overload instead, which is the same trap wearing a
// green badge.
constexpr std::int32_t kI32Min = std::numeric_limits<std::int32_t>::min();
constexpr std::int32_t kI32Max = std::numeric_limits<std::int32_t>::max();

// Cheap compile-time proof that the wrapping helpers wrap the way Java does.
// If a toolchain ever miscompiles these, the build fails here rather than the
// world hash drifting six months later.
static_assert(wrap_add(kI32Max, 1) == kI32Min, "int32 add must wrap like Java");
static_assert(wrap_sub(kI32Min, 1) == kI32Max, "int32 sub must wrap like Java");
static_assert(wrap_mul(65536, 65536) == 0, "int32 mul must wrap like Java");

// The four operations that are total in Java and undefined in C++.
static_assert(wrap_neg(kI32Min) == kI32Min, "-INT_MIN is INT_MIN, as in Java");
static_assert(wrap_abs(kI32Min) == kI32Min, "Math.abs(INT_MIN) is NEGATIVE, as in Java");
static_assert(wrap_div(kI32Min, -1) == kI32Min, "INT_MIN / -1 answers rather than trapping");
static_assert(wrap_mod(kI32Min, -1) == 0, "INT_MIN % -1 is 0");

static_assert(q16_from_int(3) == 196608, "Q16.16 conversion");
static_assert(q16_mul(Q16_ONE, Q16_ONE) == Q16_ONE, "1.0 * 1.0 == 1.0 in Q16.16");
static_assert(q16_div(Q16_ONE, q16_from_int(2)) == Q16_ONE / 2, "1.0 / 2.0 == 0.5");

// Floor semantics for negatives — the case C++'s native / and % get "wrong"
// for grid math.
static_assert(floor_div(-1, 32) == -1, "floor_div rounds toward negative infinity");
static_assert(floor_mod(-1, 32) == 31, "floor_mod is never negative");
static_assert(floor_div(-32, 32) == -1, "floor_div exact negative");
static_assert(floor_mod(-32, 32) == 0, "floor_mod exact negative");

}  // namespace
}  // namespace granadad::sim
