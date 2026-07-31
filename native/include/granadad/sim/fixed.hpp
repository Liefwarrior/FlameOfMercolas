#pragma once

// Fixed-point and wrapping-arithmetic primitives.
//
// BINDING CONSTRAINT: no float or double ever appears in simulation state or in
// any math that affects state. The world hash has to match byte-for-byte across
// Windows, Linux and consoles, and IEEE-754 does not promise that once a
// compiler is allowed to reassociate or contract. Integers and fixed-point do.
//
// BINDING CONSTRAINT: signed overflow is undefined behaviour in C++ but is
// well-defined wraparound in Java. Wherever the Java reference build leans on
// int wrap, call wrap_add/wrap_sub/wrap_mul here rather than writing `a + b`
// and hoping. -fwrapv is set as a second line of defence, not the first.

#include <cstdint>

namespace granadad::sim {

// ---------------------------------------------------------------------------
// Explicit wrapping arithmetic. Defined via unsigned round-trip, which is the
// one path the standard actually guarantees.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr std::int32_t wrap_add(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr std::int32_t wrap_sub(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr std::int32_t wrap_mul(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// ---------------------------------------------------------------------------
// Q16.16 fixed point. 16 bits of fraction: ~1.5e-5 resolution, +/-32768 range.
// ---------------------------------------------------------------------------

inline constexpr int Q16_SHIFT = 16;
inline constexpr std::int32_t Q16_ONE = 1 << Q16_SHIFT;

[[nodiscard]] constexpr std::int32_t q16_from_int(std::int32_t whole) noexcept {
    return wrap_mul(whole, Q16_ONE);
}

[[nodiscard]] constexpr std::int32_t q16_mul(std::int32_t a, std::int32_t b) noexcept {
    // Widen to 64 bits so the intermediate cannot overflow, then shift back.
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> Q16_SHIFT);
}

[[nodiscard]] constexpr std::int32_t q16_div(std::int32_t a, std::int32_t b) noexcept {
    if (b == 0) {
        return 0;  // Division by zero is a content bug, not a crash. Caller checks.
    }
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) << Q16_SHIFT) / b);
}

// Floor division / modulo that behave the same for negative operands on every
// platform. C++ truncates toward zero; tile-grid math wants floor.
[[nodiscard]] constexpr std::int32_t floor_div(std::int32_t a, std::int32_t b) noexcept {
    const std::int32_t q = a / b;
    return ((a % b != 0) && ((a < 0) != (b < 0))) ? q - 1 : q;
}

[[nodiscard]] constexpr std::int32_t floor_mod(std::int32_t a, std::int32_t b) noexcept {
    return wrap_sub(a, wrap_mul(floor_div(a, b), b));
}

}  // namespace granadad::sim
