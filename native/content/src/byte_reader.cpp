#include "granadad/content/byte_reader.hpp"

#include <algorithm>
#include <cstring>

namespace granadad::content {

ByteReader::ByteReader(std::span<const std::uint8_t> bytes, std::string what)
    : bytes_(bytes), what_(std::move(what)) {}

void ByteReader::require(std::size_t n) const {
    if (n > remaining()) {
        throw FormatError(what_ + ": truncated at byte " + std::to_string(pos_) + " (need " +
                          std::to_string(n) + " more, have " + std::to_string(remaining()) +
                          " of " + std::to_string(bytes_.size()) + ")");
    }
}

std::uint8_t ByteReader::u8() {
    require(1);
    return bytes_[pos_++];
}

std::uint16_t ByteReader::u16() {
    require(2);
    const std::uint16_t value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes_[pos_]) |
                                static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes_[pos_ + 1]) << 8));
    pos_ += 2;
    return value;
}

std::uint32_t ByteReader::u32() {
    require(4);
    const std::uint32_t value = static_cast<std::uint32_t>(bytes_[pos_]) |
                                (static_cast<std::uint32_t>(bytes_[pos_ + 1]) << 8) |
                                (static_cast<std::uint32_t>(bytes_[pos_ + 2]) << 16) |
                                (static_cast<std::uint32_t>(bytes_[pos_ + 3]) << 24);
    pos_ += 4;
    return value;
}

std::uint64_t ByteReader::u64() {
    // The Java writes a long as writeInt(lo) then writeInt(hi).
    const std::uint64_t lo = u32();
    const std::uint64_t hi = u32();
    return lo | (hi << 32);
}

std::int32_t ByteReader::i32() {
    return static_cast<std::int32_t>(u32());
}

std::span<const std::uint8_t> ByteReader::take(std::size_t n) {
    require(n);
    const std::span<const std::uint8_t> slice = bytes_.subspan(pos_, n);
    pos_ += n;
    return slice;
}

void ByteReader::readFully(std::span<std::uint8_t> dst) {
    const std::span<const std::uint8_t> src = take(dst.size());
    if (!dst.empty()) {
        std::memcpy(dst.data(), src.data(), dst.size());
    }
}

void ByteReader::requireExhausted() const {
    if (!exhausted()) {
        throw FormatError(what_ + ": " + std::to_string(remaining()) +
                          " trailing bytes after byte " + std::to_string(pos_) + " of " +
                          std::to_string(bytes_.size()));
    }
}

void ByteReader::seek(std::size_t position) {
    if (position > bytes_.size()) {
        throw FormatError(what_ + ": seek past end (" + std::to_string(position) + " > " +
                          std::to_string(bytes_.size()) + ")");
    }
    pos_ = position;
}

}  // namespace granadad::content
