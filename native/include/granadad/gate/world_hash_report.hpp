#pragma once

// The simulation half of the cross-toolchain comparison.
//
// granadad-content-tests --fingerprint already proves that both toolchains
// DECODE the shipped worlds to the same bytes. That is the reader. It says
// nothing about whether the same bytes then HASH the same, or whether a
// simulation run over them lands in the same place -- and those are the two
// claims M1 adds, so they need the same treatment.
//
// So this emits, per shipped world:
//
//   * the canonical WRLD sub-hash and the combined hash of that section alone
//     -- the hasher, on both platforms
//   * the final per-section hashes of a short fixed workload run over that
//     world -- the RNG, the tick loop and the systems, on both platforms
//
// The docker build writes the Linux/GCC side to dist/; scripts/verify-windows.ps1
// writes the mingw/Windows side and compares the two byte for byte. Any
// disagreement anywhere -- shift signedness, integer promotion, an unordered
// container, a stray float -- moves at least one of these numbers.
//
// Same four rules as the content fingerprint, all load-bearing for a byte
// comparison: no libc formatting, no paths or timestamps or platform names in
// the file, explicit little-endian everywhere, '\n' only and binary writes.
// The banner that names the platform goes to stdout, never into the report.

#include <string>

namespace granadad::gate {

/// Bump when the layout changes, so an old report and a new one fail loudly
/// instead of diffing into noise.
inline constexpr int kWorldHashReportVersion = 1;

/// Builds the whole report. Throws if the content directory is unusable.
[[nodiscard]] std::string world_hash_report();

}  // namespace granadad::gate
