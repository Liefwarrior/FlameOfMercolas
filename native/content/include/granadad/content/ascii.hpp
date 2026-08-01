#pragma once

// Integers to ASCII, and ASCII to a file, without the C runtime having a vote.
//
// WHY NOT std::to_string / snprintf. Every cross-toolchain report this project
// produces is compared BYTE FOR BYTE between a glibc build and an msvcrt-via-
// mingw build. For plain integers the two would almost certainly agree -- and
// "almost certainly agree across toolchains" is the exact claim under test, so
// the report does not ask them. Locales, thousands separators and printf format
// support are all things that legitimately differ between C runtimes and must
// not be able to move a byte of the answer.
//
// WHY NOT text mode. std::fopen in text mode on Windows rewrites every '\n' as
// "\r\n". A report written that way differs from the Linux one at almost every
// line for a reason that has nothing to do with determinism, which is the
// loudest possible false positive on the quietest possible check.
//
// This header lives in granadad::content rather than in the simulation because
// content must never depend on sim, and both sides need it: the format
// reader's fingerprint report and the simulation's world-hash report are the
// two halves of the same cross-toolchain comparison.

#include <cstdint>
#include <string>

namespace granadad::content {

/// Unsigned decimal, hand-rolled.
[[nodiscard]] inline std::string dec(std::uint64_t value) {
    char buffer[20];
    std::size_t at = sizeof(buffer);
    do {
        buffer[--at] = static_cast<char>('0' + static_cast<int>(value % 10u));
        value /= 10u;
    } while (value != 0u);
    return std::string(buffer + at, sizeof(buffer) - at);
}

// No std::size_t overload: on both targets here size_t and uint64_t are the
// same type, so it would be a redefinition rather than an overload.

/// Signed decimal. Negated in unsigned space so INT32_MIN does not overflow.
[[nodiscard]] inline std::string dec(std::int32_t value) {
    if (value < 0) {
        return "-" + dec(static_cast<std::uint64_t>(0u - static_cast<std::uint64_t>(value)));
    }
    return dec(static_cast<std::uint64_t>(value));
}

/// Signed 64-bit decimal. Same trick, so INT64_MIN prints rather than wraps.
[[nodiscard]] inline std::string dec(std::int64_t value) {
    if (value < 0) {
        return "-" + dec(static_cast<std::uint64_t>(0ull - static_cast<std::uint64_t>(value)));
    }
    return dec(static_cast<std::uint64_t>(value));
}

/// Fixed-width uppercase hex, zero padded, no prefix.
[[nodiscard]] inline std::string hexDigits(std::uint64_t value, int digits) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out(static_cast<std::size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[static_cast<std::size_t>(value & 0xFu)];
        value >>= 4;
    }
    return out;
}

[[nodiscard]] inline std::string hex32(std::uint32_t value) {
    return "0x" + hexDigits(value, 8);
}

[[nodiscard]] inline std::string hex64(std::uint64_t value) {
    return "0x" + hexDigits(value, 16);
}

/// Right-aligns `text` in `width` columns with spaces. Report cosmetics only --
/// it is inside the byte comparison, so it must be as deterministic as
/// everything else, which is why it is here and not a stream manipulator.
[[nodiscard]] inline std::string padLeft(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return std::string(width - text.size(), ' ') + text;
}

[[nodiscard]] inline std::string padRight(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

/// Writes `text` to `path` byte for byte: binary mode, no newline translation.
/// Returns false and fills `error` (when non-null) on failure.
bool writeTextFile(const std::string& path, const std::string& text, std::string* error);

}  // namespace granadad::content
