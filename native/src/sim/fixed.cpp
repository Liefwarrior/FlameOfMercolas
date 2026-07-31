#include "granadad/sim/fixed.hpp"

// fixed.hpp is header-only and constexpr. This translation unit exists so the
// sim library has an object file of its own and so the static-assert block
// below is compiled exactly once.

namespace granadad::sim {
namespace {

// Cheap compile-time proof that the wrapping helpers wrap the way Java does.
// If a toolchain ever miscompiles these, the build fails here rather than the
// world hash drifting six months later.
static_assert(wrap_add(2147483647, 1) == -2147483648, "int32 add must wrap like Java");
static_assert(wrap_sub(-2147483648, 1) == 2147483647, "int32 sub must wrap like Java");
static_assert(wrap_mul(65536, 65536) == 0, "int32 mul must wrap like Java");

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
