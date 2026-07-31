#pragma once

// The TROJSAV save container. READ PATH ONLY for M0.
//
// File layout v1, all little-endian:
//
//   36-byte header
//     u32 magic          0x4A4F5254   ("TROJ" as LE bytes)
//     u32 formatVersion  1
//     u64 worldSeed
//     u64 tick
//     u64 rawsFingerprint
//     u32 sectionCount
//   sectionCount x 32-byte TOC entries, sorted ascending by the 4-char ASCII id
//     4xu8 id            not null-terminated
//     u64  offset        absolute file offset of the compressed blob
//     u64  compressedLen
//     u64  uncompressedLen
//     u32  crc32c        CRC32C of the UNCOMPRESSED content
//   then the blobs, in TOC order, contiguous, no padding.
//
// Section blobs are ZLIB-WRAPPED deflate, not raw deflate — the Java uses
// Deflater(1)/Inflater() with nowrap defaulting to false, and every blob in
// every shipped file starts 78 01. The checksum is CRC32C (Castagnoli), NOT the
// IEEE CRC32 miniz ships; see crc32c.hpp.
//
// CRC is verified LAZILY, on first access to a section, exactly as the Java
// does — opening a file does not pay for decompressing sections nobody reads.
//
// NOTE ON WRITING (deliberately absent here): Java's Deflater level 1 and
// miniz's level 1 do not emit identical bytes, so a C++ writer could not
// reproduce a golden .trojsav byte-for-byte without vendoring real zlib. Hash
// the UNCOMPRESSED section bytes, never the file.

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/content/format_error.hpp"

namespace granadad::content {

/// A 4-character section id. Ordered bytewise, which is the order the TOC is
/// written in (the Java writes from a TreeMap and every id is printable ASCII).
struct SectionId {
    std::array<char, 4> chars{};

    [[nodiscard]] constexpr bool operator==(const SectionId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SectionId&) const noexcept = default;

    [[nodiscard]] std::string str() const { return std::string(chars.data(), chars.size()); }
};

[[nodiscard]] constexpr SectionId makeSectionId(const char (&text)[5]) noexcept {
    return SectionId{{text[0], text[1], text[2], text[3]}};
}

/// Every section id the format declares. Only META and WRLD are written by the
/// current baker — the systems owning the rest do not exist yet in sim-core —
/// so unknown sections are carried as opaque bytes rather than rejected.
namespace sections {
inline constexpr SectionId kMeta = makeSectionId("META");
inline constexpr SectionId kInpt = makeSectionId("INPT");
inline constexpr SectionId kEvnt = makeSectionId("EVNT");
inline constexpr SectionId kWrld = makeSectionId("WRLD");
inline constexpr SectionId kChng = makeSectionId("CHNG");
inline constexpr SectionId kFlud = makeSectionId("FLUD");
inline constexpr SectionId kThrm = makeSectionId("THRM");
inline constexpr SectionId kReac = makeSectionId("REAC");
inline constexpr SectionId kLght = makeSectionId("LGHT");
inline constexpr SectionId kBubl = makeSectionId("BUBL");
inline constexpr SectionId kEcon = makeSectionId("ECON");
inline constexpr SectionId kAeth = makeSectionId("AETH");
}  // namespace sections

/// File magic, little-endian "TROJ".
inline constexpr std::uint32_t kTrojSavMagic = 0x4A4F5254u;
/// The only container format version this build reads.
inline constexpr std::uint32_t kTrojSavFormatVersion = 1;
inline constexpr std::size_t kTrojSavHeaderBytes = 36;
inline constexpr std::size_t kTrojSavTocEntryBytes = 32;

/// An opened container. Owns the file bytes; sections inflate on demand.
class TrojSav {
public:
    struct Header {
        std::uint32_t formatVersion = 0;
        /// The only persisted RNG state.
        std::uint64_t worldSeed = 0;
        /// The tick the save was taken at (a TICK_END boundary).
        std::uint64_t tick = 0;
        /// Fingerprint of the raws the save was made with. A mismatch on load
        /// is a hard fail in the Java — goldens are meaningless across raws
        /// changes.
        ///
        /// VERIFICATION GAP (M0): this is read and can be compared, but nothing
        /// here RECOMPUTES it — the C++ raws loader does not exist yet, so
        /// there is no second opinion to check it against. All three shipped
        /// worlds carry 0x6101F30069B57FF1, which the tests pin; the mismatch
        /// hard-fail lands with the raws loader.
        std::uint64_t rawsFingerprint = 0;
    };

    struct TocEntry {
        SectionId id;
        std::uint64_t offset = 0;
        std::uint64_t compressedLen = 0;
        std::uint64_t uncompressedLen = 0;
        /// CRC32C of the uncompressed content. Stored as a signed int by the
        /// Java; compared as unsigned on both sides.
        std::uint32_t crc32c = 0;
    };

    static TrojSav readFile(const std::filesystem::path& file);
    static TrojSav readBytes(std::vector<std::uint8_t> bytes, std::string origin);

    [[nodiscard]] const Header& header() const noexcept { return header_; }

    /// The table of contents in file order.
    [[nodiscard]] const std::vector<TocEntry>& toc() const noexcept { return toc_; }

    [[nodiscard]] bool hasSection(SectionId id) const noexcept;

    /// The decompressed, CRC-verified content of a section. Inflated once and
    /// cached; the returned span is borrowed from the container.
    ///
    /// Throws FormatError when the section is absent, does not inflate to
    /// exactly its declared length, or fails its CRC32C.
    [[nodiscard]] std::span<const std::uint8_t> section(SectionId id);

    [[nodiscard]] const std::string& origin() const noexcept { return origin_; }

private:
    TrojSav() = default;

    [[nodiscard]] std::size_t indexOf(SectionId id) const noexcept;

    Header header_{};
    std::string origin_;
    std::vector<std::uint8_t> bytes_;
    std::vector<TocEntry> toc_;
    // Parallel to toc_; inflatedDone_ is char rather than bool to keep a plain
    // addressable byte per entry.
    std::vector<std::vector<std::uint8_t>> inflated_;
    std::vector<char> inflatedDone_;
};

/// Whether every character is printable ASCII, as the Java requires.
[[nodiscard]] bool isValidSectionId(SectionId id) noexcept;

}  // namespace granadad::content
