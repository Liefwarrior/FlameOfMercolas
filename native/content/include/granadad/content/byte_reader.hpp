#pragma once

// Little-endian primitives and a bounds-checked cursor.
//
// java.io.Data*Stream is BIG-endian, so the Java build routes every multi-byte
// field in the save format through its own LittleEndian helper. There is not a
// single big-endian field anywhere in TROJSAV. This is the C++ side of that
// contract.
//
// Reads are assembled with explicit shifts rather than memcpy-into-a-scalar
// because section blobs are unaligned and byte order must not depend on the
// host. The cost is irrelevant — this runs once per load, not per tick.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "granadad/content/format_error.hpp"

namespace granadad::content {

// ---------------------------------------------------------------------------
// Explicit wrapping arithmetic.
//
// BINDING CONSTRAINT: signed overflow is undefined in C++ but well-defined
// wraparound in Java. -fwrapv is set project-wide as a second line of defence,
// but anywhere the Java reference leans on int wrap we say so in the code.
//
// These duplicate granadad::sim::wrap_* deliberately: the content module has no
// dependency on sim-core's headers, so it can be unit-tested and reused stand-
// alone. A later consolidation pass can merge them.
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr std::int32_t wrapAdd(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr std::int32_t wrapSub(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr std::int32_t wrapMul(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

/// A forward cursor over a byte buffer. Every read is bounds-checked and throws
/// FormatError rather than reading past the end; the buffer is borrowed, not
/// owned, and must outlive the reader.
class ByteReader {
public:
    ByteReader(std::span<const std::uint8_t> bytes, std::string what);

    [[nodiscard]] std::uint8_t u8();
    [[nodiscard]] std::uint16_t u16();
    [[nodiscard]] std::uint32_t u32();
    [[nodiscard]] std::uint64_t u64();

    /// Reads a 32-bit field that the Java writes from a signed int.
    [[nodiscard]] std::int32_t i32();

    /// Borrows the next `n` bytes without copying.
    [[nodiscard]] std::span<const std::uint8_t> take(std::size_t n);

    /// Fills `dst` completely — the analogue of DataInput.readFully.
    void readFully(std::span<std::uint8_t> dst);

    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - pos_; }
    [[nodiscard]] bool exhausted() const noexcept { return pos_ == bytes_.size(); }

    /// Throws unless every byte was consumed. The Java loaders rely on the
    /// stream landing exactly on the end; a short read means the writer and the
    /// reader disagree about the layout, which is a format error, not slack.
    void requireExhausted() const;

    void seek(std::size_t position);

    [[nodiscard]] const std::string& what() const noexcept { return what_; }

private:
    void require(std::size_t n) const;

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
    std::string what_;
};

}  // namespace granadad::content
