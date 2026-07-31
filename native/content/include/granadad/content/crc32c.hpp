#pragma once

// CRC-32/ISCSI (Castagnoli) — the checksum TROJSAV sections carry.
//
// THIS IS NOT THE CRC32 MINIZ SHIPS. java.util.zip.CRC32C is Castagnoli
// (reflected polynomial 0x82F63B78); mz_crc32 / java.util.zip.CRC32 are IEEE
// (0xEDB88320). Calling the wrong one mismatches every section in every file,
// and does so silently in the sense that nothing crashes — the loader just
// rejects a perfectly good save. Verified against all six section CRCs in the
// three shipped .trojsav files.
//
//   normal poly     0x1EDC6F41
//   reflected poly  0x82F63B78   <- used here, with the right-shifting table
//   init            0xFFFFFFFF
//   refIn / refOut  true / true
//   xorOut          0xFFFFFFFF
//   check("123456789") = 0xE3069283
//
// The implementation is deliberately the scalar table, not SSE4.2 _mm_crc32_u64
// or ARM __crc32cd. Those produce identical results, but determinism must not
// depend on which CPU features the build machine happened to have.

#include <cstdint>
#include <span>
#include <string_view>

namespace granadad::content {

/// The reflected Castagnoli polynomial.
inline constexpr std::uint32_t kCrc32cReflectedPoly = 0x82F63B78u;

/// Continues a running CRC32C. `crc` is 0 for a fresh checksum; the
/// init/xorOut of 0xFFFFFFFF is applied internally so partial updates compose.
[[nodiscard]] std::uint32_t crc32cUpdate(std::uint32_t crc,
                                         std::span<const std::uint8_t> data) noexcept;

/// CRC32C over a whole buffer.
[[nodiscard]] std::uint32_t crc32c(std::span<const std::uint8_t> data) noexcept;

/// CRC32C over the bytes of a string — for test vectors.
[[nodiscard]] std::uint32_t crc32c(std::string_view text) noexcept;

}  // namespace granadad::content
