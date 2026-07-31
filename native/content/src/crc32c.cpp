#include "granadad/content/crc32c.hpp"

#include <array>

namespace granadad::content {
namespace {

/// The standard 256-entry right-shifting table for the reflected polynomial.
/// Built at compile time so there is no static-init order to reason about.
constexpr std::array<std::uint32_t, 256> makeCrc32cTable() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256u; ++i) {
        std::uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0u ? (kCrc32cReflectedPoly ^ (crc >> 1)) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kTable = makeCrc32cTable();

// A cheap compile-time smoke check on the table itself.
static_assert(kTable[1] == 0xF26B8303u, "CRC32C table is not Castagnoli");

}  // namespace

std::uint32_t crc32cUpdate(std::uint32_t crc, std::span<const std::uint8_t> data) noexcept {
    std::uint32_t state = ~crc;
    for (const std::uint8_t byte : data) {
        state = kTable[(state ^ byte) & 0xFFu] ^ (state >> 8);
    }
    return ~state;
}

std::uint32_t crc32c(std::span<const std::uint8_t> data) noexcept {
    return crc32cUpdate(0u, data);
}

std::uint32_t crc32c(std::string_view text) noexcept {
    return crc32c(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                                text.size()));
}

}  // namespace granadad::content
