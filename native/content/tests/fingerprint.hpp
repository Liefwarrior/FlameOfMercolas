#pragma once

// The cross-toolchain comparator.
//
// WHY THIS EXISTS. sim-core bans float/double so that the same bytes decode to
// the same world state on every toolchain. Until task #75 that claim had never
// been tested across two toolchains: the 57 doctest cases ran only under
// GCC/Linux inside the build container, while the mingw build of the very same
// sources was compiled, linked, and then thrown away unexecuted.
//
// Running the suite on both sides is necessary but weak. "57 passed" on Linux
// and "57 passed" on Windows are equal strings no matter what the two binaries
// actually decoded — every assertion could agree while the two platforms
// disagreed about anything the suite does not pin (and it pins a few dozen
// facts out of ~2.7 million tiles).
//
// So this emits a REPORT instead: for every shipped world, the section CRCs of
// what miniz actually inflated, a CRC32C over each decoded lane, and decoded
// per-form / per-material / per-flags / per-fluid histograms over every tile.
// Two platforms are then compared byte-for-byte on the report file. Any
// disagreement anywhere in the decode — endianness, shift signedness, char
// signedness, integer promotion, miniz output — moves at least one number.
//
// Rules the report obeys, all of them load-bearing for a byte comparison:
//   * NO libc formatting. Integers go to ASCII by hand. glibc and msvcrt must
//     not get a vote in what the answer looks like.
//   * NO paths, timestamps, platform names or compiler versions in the file.
//     Those legitimately differ; they go to stdout instead.
//   * Multi-byte values are serialised little-endian EXPLICITLY, never by
//     reinterpret_cast over memory, because host byte order is part of the
//     question and must not also be part of the answer.
//   * '\n' only, and the file is opened in binary mode on both sides.

#include <string>

namespace granadad::content::testing {

/// The report format version. Bump it when the layout changes, so an old
/// report and a new one fail loudly instead of diffing into noise.
inline constexpr int kFingerprintVersion = 1;

/// Builds the whole report. Throws FormatError (or a filesystem error) if the
/// content directory is unusable.
[[nodiscard]] std::string fingerprintReport();

/// Writes `text` to `path` byte-for-byte, binary mode, no newline translation.
/// Returns false and fills `error` on failure.
bool writeReportFile(const std::string& path, const std::string& text, std::string* error);

}  // namespace granadad::content::testing
