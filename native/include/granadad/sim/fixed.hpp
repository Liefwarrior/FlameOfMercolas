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

// The 64-bit half. Tick counters, world seeds, hash accumulators and packed
// keys all live here, and every one of them can wrap in the Java.

[[nodiscard]] constexpr std::int64_t wrap_add(std::int64_t a, std::int64_t b) noexcept {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}

[[nodiscard]] constexpr std::int64_t wrap_sub(std::int64_t a, std::int64_t b) noexcept {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}

[[nodiscard]] constexpr std::int64_t wrap_mul(std::int64_t a, std::int64_t b) noexcept {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}

// ---------------------------------------------------------------------------
// The INT_MIN family. Four operations that are TOTAL in Java and UNDEFINED in
// C++, for one input each -- so the C++ is not "slightly riskier", it is a
// different program on that input.
//
//     Java                                C++
//     -Integer.MIN_VALUE  == MIN_VALUE    UB
//     Math.abs(MIN_VALUE) == MIN_VALUE    UB (and std::abs is UB too)
//     MIN_VALUE / -1      == MIN_VALUE    UB -- SIGFPE on x86, not a wrong
//     MIN_VALUE % -1      == 0                 number but a dead process
//
// The middle one is a live bug class in the reference build, not a hypothetical:
// EffectPairing.java:124 guards a spell magnitude with `Math.abs(magnitude) >
// ceiling`, which is FALSE for Integer.MIN_VALUE because the absolute value
// comes back negative -- so the one magnitude that most needs rejecting is the
// one that passes. Porting that guard with std::abs would turn a wrong answer
// into undefined behaviour, which is worse: the bug stops being reproducible.
//
// These helpers reproduce Java's answer exactly. Where the reference build has
// a bug, the port has the SAME bug, visibly, until somebody fixes it in both.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr std::int32_t wrap_neg(std::int32_t a) noexcept {
    return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(a));
}

[[nodiscard]] constexpr std::int64_t wrap_neg(std::int64_t a) noexcept {
    return static_cast<std::int64_t>(0ull - static_cast<std::uint64_t>(a));
}

/// Java's Math.abs: NEGATIVE for INT_MIN, on purpose. Never std::abs, which is
/// undefined there.
[[nodiscard]] constexpr std::int32_t wrap_abs(std::int32_t a) noexcept {
    return a < 0 ? wrap_neg(a) : a;
}

[[nodiscard]] constexpr std::int64_t wrap_abs(std::int64_t a) noexcept {
    return a < 0 ? wrap_neg(a) : a;
}

/// Truncating division with Java's overflow answer for MIN/-1.
///
/// Divide by zero returns 0 rather than trapping -- the same call the existing
/// q16_div makes, and for the same reason: a bad raw or a bad content value
/// must not be able to take the whole simulation down mid-tick. Java throws
/// ArithmeticException here, so a zero divisor is a bug on both sides; this
/// only decides how loudly it fails.
[[nodiscard]] constexpr std::int32_t wrap_div(std::int32_t a, std::int32_t b) noexcept {
    if (b == 0) {
        return 0;
    }
    if (b == -1) {
        return wrap_neg(a);
    }
    return a / b;
}

[[nodiscard]] constexpr std::int64_t wrap_div(std::int64_t a, std::int64_t b) noexcept {
    if (b == 0) {
        return 0;
    }
    if (b == -1) {
        return wrap_neg(a);
    }
    return a / b;
}

/// Truncating remainder, total. `a % -1` is 0 for every a, INT_MIN included.
[[nodiscard]] constexpr std::int32_t wrap_mod(std::int32_t a, std::int32_t b) noexcept {
    if (b == 0 || b == -1) {
        return 0;
    }
    return a % b;
}

[[nodiscard]] constexpr std::int64_t wrap_mod(std::int64_t a, std::int64_t b) noexcept {
    if (b == 0 || b == -1) {
        return 0;
    }
    return a % b;
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
//
// Routed through wrap_div/wrap_mod so floor_div(INT32_MIN, -1) answers instead
// of raising SIGFPE. Grid divisors are positive constants in practice, which is
// exactly why nobody would ever notice that edge until it happened in a soak.
[[nodiscard]] constexpr std::int32_t floor_div(std::int32_t a, std::int32_t b) noexcept {
    const std::int32_t q = wrap_div(a, b);
    return ((wrap_mod(a, b) != 0) && ((a < 0) != (b < 0))) ? wrap_sub(q, 1) : q;
}

[[nodiscard]] constexpr std::int32_t floor_mod(std::int32_t a, std::int32_t b) noexcept {
    return wrap_sub(a, wrap_mul(floor_div(a, b), b));
}

}  // namespace granadad::sim
