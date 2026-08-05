#pragma once

// The deterministic workload the twin-run gate runs twice.
//
// WHAT THIS IS NOT. It is not the game. M1 ports the engine spine -- the RNG,
// the hasher, the tick loop -- and the actor simulation is a later milestone.
// The three systems here exist to put every mechanism of that spine under load
// so a gate has something to disagree about:
//
//   heartbeat  (tick-begin)  no draws at all; proves a drawless system still
//                            contributes state and still hashes
//   drift      (actors)      N walkers, two draws each per tick in a fixed
//                            order, movement gated on the DECODED world -- so
//                            the run's outcome depends on the format reader
//                            having produced the same tiles
//   ledger     (tick-end)    a second drawing system in the same tick, with a
//                            sorted-container iteration in its hash
//
// The drift system's dependence on world content is the load-bearing part. A
// workload that ignored the world would prove the RNG deterministic and say
// nothing about the reader; this one moves differently if a single FORM byte
// decodes differently, so the combined hash covers both.
//
// Naming: these are walkers, not actors in the game sense. There are no needs,
// no jobs and no names -- when the real actor simulation lands it registers its
// own system and this one stops being the interesting part of the gate.

#include <cstdint>
#include <string>

namespace granadad::gate {

/// What to run.
struct WorkloadConfig {
    /// A baked world under content/maps/baked, without the extension.
    std::string world = "tavern_fixture";
    /// The only persisted RNG state there is.
    std::uint64_t seed = 0x4752414E41444144ull;  // "GRANADAD"
    std::int64_t ticks = 600;
    std::int32_t walkers = 96;
    /// Emit a sample line every this many ticks. Must be >= 1.
    std::int64_t sample_every = 100;

    /// Registers the Gilded Gull as a fourth system and drives its movement
    /// clock, with a player standing at the bar. Forces `world` to
    /// docks_surface, because that is the only world the tavern is in.
    ///
    /// OFF by default, deliberately. The gate's published cross-toolchain
    /// report is compared byte for byte between Linux and Windows, and every
    /// sprint that changed its content would be a sprint that had to
    /// regenerate the thing the comparison rests on. So the default workload
    /// is frozen and the tavern gets its OWN ctest entry, which proves the
    /// same property about the same code without touching the baseline.
    bool with_tavern = false;

    /// S7. Registers the ward's compounds -- the roll, the courtyard farms and
    /// the bonds between them -- as another system. Forces `world` to
    /// docks_surface for the same reason with_tavern does.
    ///
    /// OFF by default and with its own ctest entry, for the same reason the
    /// tavern is: the published cross-toolchain report is compared byte for
    /// byte between Linux and Windows and the baseline workload stays frozen.
    ///
    /// A DAY IS THE WARD'S UNIT, so a short gate run does no day's work at all
    /// and would compare an untouched roll. The entry that runs this uses
    /// enough ticks to cross a day boundary; see the CMakeLists comment on it.
    bool with_ward = false;

    /// #78. Registers the district's POPULATION -- six hundred and seventy-eight
    /// bodies with needs, homes, jobs, hours and a night roster -- as another
    /// system. Forces `world` to docks_surface for the same reason the two
    /// above do.
    ///
    /// THIS IS THE REAL DETERMINISM TEST OF THE PORT and it is worth saying
    /// why. The tavern's fourteen and the ward's roll are both small and both
    /// mostly bookkeeping; the population is hundreds of bodies running a
    /// bounded A* with per-actor jitter, an open-addressing occupancy index
    /// with backward-shift deletion, a shove that draws against a named stream,
    /// and a policy stack whose ties are broken by stack position. Every one of
    /// those is a place where an iteration order or a hash bucket could leak
    /// into state. If this leg is green, the port kept determinism.
    ///
    /// OFF by default and with its own ctest entry, for the same reason the
    /// other two are: the published cross-toolchain report is compared byte for
    /// byte between Linux and Windows and the baseline workload stays frozen.
    bool with_population = false;

    /// The hour the population's day starts at, in seconds since midnight. The
    /// gate entry runs it across the evening so the twin run compares a
    /// district that has gone off shift, gone home, eaten and put a night
    /// roster on the street -- rather than one that spent the whole run
    /// standing at the same posts.
    std::int32_t population_start_second = 16 * 3600;
};

/// What one run produced.
struct RunResult {
    /// The world sub-hash: the decoded terrain, which the walkers never write.
    std::uint64_t world_hash = 0;
    /// World plus every system, folded in signed salt order.
    std::uint64_t combined_hash = 0;
    /// The full report text, byte for byte. This is the second comparator.
    std::string report;
};

/// Loads the world, boots an engine, runs `config.ticks` ticks, hashes.
///
/// Throws content::FormatError if the world cannot be read and
/// sim::EngineError if the engine is misconfigured. Nothing is caught here:
/// a gate that swallows an exception and reports a verdict is worse than one
/// that crashes.
[[nodiscard]] RunResult run_workload(const WorkloadConfig& config);

/// The tag the combined hash is printed under, so a report can be grepped for
/// it the same way the Java soak's is.
inline constexpr const char* kCombinedHashTag = "  COMBINED WORLD HASH: 0x";

}  // namespace granadad::gate
