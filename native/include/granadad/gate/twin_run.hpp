#pragma once

// The twin-run determinism gate.
//
// Runs the same workload TWICE IN ONE PROCESS from one seed and fails on any
// divergence. It is the only check in this build that can catch nondeterminism
// rather than incorrectness, and every other guarantee in the project -- saves
// that reload, goldens that hold, a world hash that means anything -- rests on
// it.
//
// TWO COMPARATORS, NEITHER SUBSUMING THE OTHER
//
//   [1/2] the combined world hash.  Structural state. It sees every byte of
//         terrain and every field a system chose to hash, and it sees nothing
//         else -- by design, since the hash is the definition of "the state".
//
//   [2/2] the full report text, byte for byte.  Everything the hash cannot
//         structurally see, because it never entered a sink: an iteration over
//         a container whose order is not defined, a pointer value printed as an
//         identifier, a locale-formatted number, an elapsed time. A reporting
//         path that diverges is a real bug -- it is what the player and the
//         debugger both read -- and the state hash will never once flinch at it.
//
// Running both is not belt and braces. Drop [1/2] and a state divergence that
// happens not to reach the report goes unseen; drop [2/2] and every divergence
// outside the hashed set goes unseen. The Java gate makes the same argument and
// runs the same two.
//
// WHY ONE PROCESS
//
// The Java runs both halves in one JVM because the desync shapes that codebase
// produces -- static mutable state, identity-hash-ordered collections -- only
// diverge between two runs that SHARE a heap. Two pristine forked JVMs would
// each leak identically and the gate would see nothing.
//
// C++ has no identity hashes, but it has the same shape of hazard and then
// some: function-local statics, mutable globals, an allocator that hands back
// different addresses the second time, uninitialised memory that happens to be
// stable within a process, and any ordering keyed on a pointer value. One
// process is where those live.
//
// WHAT THIS GATE CANNOT SEE, stated plainly rather than left to be discovered:
//
//   * An unordered container. std::unordered_map's iteration order is a
//     function of the keys and the insertion sequence, and both runs have the
//     same ones -- so it agrees with itself here and diverges only against
//     another machine or another standard library. The docker build greps for
//     it instead.
//   * Anything that is stable within a process but varies across processes or
//     machines. That hole is covered from the other side: --fingerprint emits a
//     report that the Linux and Windows builds are compared on byte for byte.
//
// WHAT IT HAS BEEN OBSERVED CATCHING. A gate that has never gone red has not
// been shown to work, so it was run against a scratch copy of the tree with
// deliberate nondeterminism compiled in (M1, 2026-08-01):
//
//   a clock-derived nudge inside a walker's heading draw
//       -> both comparators fired; sub-hashes localised it to DRFT while WRLD,
//          HEAR and LEDG still agreed; ctest exit 8, gate exit 1
//   a wall-clock elapsed_ns line added to the REPORT only
//       -> hash comparator IDENTICAL, text comparator red at the offending
//          line; gate exit 1
//
// The second is the one that justifies having two comparators at all: the
// combined world hash agreed to the bit and the run was still not reproducible.

#include <cstddef>
#include <string>

#include "granadad/gate/workload.hpp"

namespace granadad::gate {

/// The first line at which two reports differ.
struct TextDivergence {
    bool diverged = false;
    /// 1-based line number. Meaningless when `diverged` is false.
    std::size_t line = 0;
    std::string left;
    std::string right;
};

/// Compares two reports line by line. A report that is a prefix of the other
/// diverges at the first line the shorter one does not have.
[[nodiscard]] TextDivergence first_text_divergence(const std::string& a, const std::string& b);

/// Renders up to `limit` differing line pairs, for the failure message.
[[nodiscard]] std::string render_divergence(const std::string& a, const std::string& b,
                                            std::size_t limit);

/// The verdict.
struct TwinRunOutcome {
    bool passed = false;
    /// Everything the gate printed, in order. Returned rather than printed so
    /// the gate's own behaviour is testable without capturing stdout.
    std::string log;
    std::uint64_t hash_a = 0;
    std::uint64_t hash_b = 0;
    bool hashes_agree = false;
    bool reports_agree = false;
};

/// Runs `config` twice and compares. Does not print; the caller decides.
[[nodiscard]] TwinRunOutcome twin_run(const WorkloadConfig& config);

}  // namespace granadad::gate
