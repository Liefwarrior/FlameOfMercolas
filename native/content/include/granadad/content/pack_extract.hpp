#pragma once

// The appended content pack -- how a SINGLE .exe carries its own content/.
//
// The shareable build is the shipped executable's exact compiled bytes with a
// zip of the minimal content subset CONCATENATED after them, closed by a
// fixed-size footer at EOF:
//
//     [PE image][zip bytes][24-byte footer: magic, pack size, crc32c, self-crc]
//
// Appending rather than compiling the pack in is the point: the standalone is
// the same binary logic as dist/granadad.exe by construction -- strip, cat,
// done -- so the twin-run world hash cannot differ because no second compile
// ever happens.
//
// AT BOOT. contentDir() (content_dir.cpp) consults extractedPackDir() only
// after $GRANADAD_CONTENT_DIR and the walk-up search have both come up empty.
// A repo checkout therefore never takes this path: its exe has no footer,
// parsePackFooter refuses the tail bytes, and behaviour is bit-for-bit what it
// was before this file existed. Only the standalone -- alone in a directory
// with no content/ within three levels -- reaches the extraction:
//
//     %LOCALAPPDATA%\Granadad\content-<crc>-<size>\        (Windows)
//     $XDG_CACHE_HOME/granadad/content-<crc>-<size>/       (Linux)
//
// The cache directory is keyed by the pack's own digest, so a second launch
// finds maps/baked already there and extracts nothing, and a NEW pack (new
// digest) extracts beside the old one rather than over it. Extraction goes to
// a temp sibling and is renamed into place, so two racing first launches
// cannot hand either process a half-written tree.
//
// Every function here is deliberately small and pure enough to be tested
// against synthetic bytes and synthetic trees -- see
// tests/test_pack_extract.cpp. Nothing here writes into a repo's content/,
// which is read-only canon; the cache directory is per-user and disposable.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace granadad::content {

/// Byte size of the footer that closes a pack-carrying file.
inline constexpr std::size_t kPackFooterSize = 24;

/// The footer's magic, first 8 bytes. Versioned in the name: a future format
/// change mints GDPACK02 rather than reinterpreting these fields.
inline constexpr std::array<std::uint8_t, 8> kPackMagic = {'G', 'D', 'P', 'A',
                                                           'C', 'K', '0', '1'};

/// What the footer says about the zip that precedes it.
struct PackFooter {
    std::uint64_t packSize = 0;    ///< zip bytes, footer excluded
    std::uint32_t packCrc32c = 0;  ///< CRC32C over those zip bytes
};

/// Parses the last kPackFooterSize bytes of a file. Returns nothing unless the
/// buffer is exactly footer-sized, the magic matches AND the footer's own
/// trailing CRC32C (over its first 20 bytes) agrees -- so an ordinary exe's
/// tail cannot be misread as a pack by accident.
[[nodiscard]] std::optional<PackFooter> parsePackFooter(
    std::span<const std::uint8_t> tail) noexcept;

/// The 24 bytes that close a pack: magic, little-endian fields, self-CRC.
/// parsePackFooter(buildPackFooter(f)) == f, and the round trip is tested.
[[nodiscard]] std::array<std::uint8_t, kPackFooterSize> buildPackFooter(
    const PackFooter& footer) noexcept;

/// The digest-keyed cache directory NAME for a pack: "content-<crc>-<size>".
/// Same pack -> same name (skip extraction); different pack -> different name
/// (extract beside, never over).
[[nodiscard]] std::string packCacheDirName(const PackFooter& footer);

/// Whether a zip entry name may be written under an extraction root: relative,
/// forward slashes, and no "."/".." component that climbs back out. Shared by
/// the extractor (which refuses on it) and the writer (which refuses to author
/// a name the extractor would refuse).
[[nodiscard]] bool packEntryNameIsSafe(const std::string& name);

/// Reads and validates the footer at the END of `carrier` (an exe, or a bare
/// .pack file). Nothing when the file is too small, unreadable, or carries no
/// valid footer -- which is every repo binary, and is not an error.
[[nodiscard]] std::optional<PackFooter> readPackFooter(
    const std::filesystem::path& carrier);

/// Extracts the zip that `footer` describes out of `carrier` into `destDir`
/// (created if needed). Verifies the pack's CRC32C before touching the disk
/// and refuses entry names that escape destDir. On failure returns false and
/// explains into *error when given.
[[nodiscard]] bool extractPack(const std::filesystem::path& carrier,
                               const PackFooter& footer,
                               const std::filesystem::path& destDir,
                               std::string* error);

/// The per-user cache root the packs extract under -- the OS convention plus
/// "/Granadad" (Windows: %LOCALAPPDATA%; Linux: $XDG_CACHE_HOME or ~/.cache).
/// Empty when the platform will not name one.
[[nodiscard]] std::filesystem::path packCacheRoot();

/// The whole boot-time flow over the RUNNING executable: footer read, cache
/// hit or extract-and-rename, and the resulting content directory. Empty when
/// this executable carries no pack -- the everyday repo case, and the reason
/// contentDir() may call this unconditionally. The answer is computed once per
/// process; the executable does not change under a running program.
[[nodiscard]] std::filesystem::path extractedPackDir();

}  // namespace granadad::content
