#pragma once

// THE DISTRICT, LOADED ONCE, AND A WARD TICKED ONCE.
//
// ---------------------------------------------------------------------------
// Why this could not exist until #81
// ---------------------------------------------------------------------------
// ctest used to register a ctest entry per TEST_CASE, and it runs an entry by
// LAUNCHING THE BINARY AGAIN with --test-case=. Every case was therefore its own
// process, and a file-static fixture was built once per case rather than once.
// tests/test_ward_hunt.cpp measured that and wrote it down: five cases reading
// one soak paid for the soak five times, 307 + 270 + 262 + 260 seconds.
//
// #81 made the ctest entry the FILE, so the cases in a file share one process
// and a static is a static again. This header is what that buys.
//
// ---------------------------------------------------------------------------
// What is shared, and the rule that keeps it safe
// ---------------------------------------------------------------------------
// EVERYTHING HANDED OUT HERE IS CONST. That is not politeness, it is the whole
// safety argument: shared mutable state between cases is how you get an
// order-dependent flake, and on a build whose central claim is determinism a
// flake nobody can reproduce would be poisonous. A case that needs to CHANGE a
// ward takes privateWard(), which bakes it one of its own.
//
// The world is shared as a plain reference because nothing simulates INTO it.
// PhasedEngine takes content::World& and hands it to every system through
// TickContext::world(), and exactly one caller in the tree ever touches it --
// gate/workload.cpp, which reads a tile to ask whether a walker may step there.
// No system writes a world tile. So one decoded copy serves every engine in the
// process, and if that ever stops being true this comment is the thing that was
// wrong.
//
// ---------------------------------------------------------------------------
// wardAt(hour, ticks) ANSWERS THE QUESTION IT WAS ASKED, WHATEVER RAN BEFORE
// ---------------------------------------------------------------------------
// Three cases at two in the morning used to bake three wards and tick them 600,
// 900 and 1,200 times: 2,700 ticks to answer three questions about the same
// district, and a ward tick at two in the morning costs thirty-one milliseconds.
//
// So there is ONE ward per hour and it is ticked FORWARD when the next reading
// is later. Asking for 1,200 after somebody asked for 600 runs the 600 that are
// missing and no more.
//
// AND WHEN THE NEXT READING IS EARLIER, THE WARD IS REBUILT. This is the whole
// correctness argument and it was learned the hard way: the first version threw
// instead, on the reasoning that ctest runs each file in --order-by=file order
// so "the tick counts in a file ascend" is a property of the source. That is
// true of a FILE and the cache is per PROCESS. Run the whole binary in one go
// -- which is exactly what scripts/verify-windows.ps1 does, and what anybody
// typing ./granadad-tests does -- and the files interleave: test_ward_actors.cpp
// leaves hour eight at 600 ticks and test_ward_hunt.cpp then asks for it at
// zero. Six cases threw.
//
// A rebuild costs a bake and the ticks. It is never paid under ctest, where a
// file has the cache to itself; it is paid a handful of times in a whole-binary
// run, and it is the right price for a fixture that cannot be wrong. What
// wardAt() returns is ALWAYS "hour H, baked, ticked N" -- a pure function of its
// two arguments and nothing else. No order anywhere is load-bearing.
//
// tests/test_ward_actors.cpp has a case about that, and it does the
// interleaving on purpose: read an hour at 60 ticks, then at 300, then at 60
// again, and require the first and third to be the same district -- then
// require a privateWard(), which shares nothing with the cache, to agree with
// both.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace granadad::testfix {

/// The seed every ward case runs on. "GRANADAD".
inline constexpr std::uint64_t kSeed = 0x4752414E41444144ull;

/// The baked Docks, decoded ONCE per process.
///
/// Non-const because PhasedEngine takes content::World&. See the header note:
/// no system writes a tile, so the reference is shared and never copied.
inline content::World& sharedWorld() {
    static content::World world =
        content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    return world;
}

/// The tile view over it, built ONCE per process.
inline const sim::TileQuery& sharedTiles() {
    static const sim::TileQuery tiles(sharedWorld());
    return tiles;
}

/// A ward on an engine of its own. OWNED by whoever built it, and therefore
/// free to be changed.
///
/// Bakes a roster every time it is constructed -- 0.16 s in the Debug build the
/// gate compiles -- so a case that only READS should be asking wardAt() instead.
class WardRun {
public:
    explicit WardRun(std::int32_t startHour)
        : engine_(kSeed, sharedWorld()) {
        auto owned = std::make_unique<sim::WardPopulation>(
            sharedTiles(), sim::hourOfDay(startHour), kSeed, content::contentDir());
        people_ = owned.get();
        engine_.register_system(std::move(owned));
        engine_.boot();
    }

    WardRun(const WardRun&) = delete;
    WardRun& operator=(const WardRun&) = delete;

    [[nodiscard]] sim::WardPopulation& people() noexcept { return *people_; }
    [[nodiscard]] const sim::WardPopulation& people() const noexcept { return *people_; }
    [[nodiscard]] std::int64_t ticksRun() const noexcept { return ticksRun_; }

    void run(std::int64_t ticks) {
        for (std::int64_t t = 0; t < ticks; ++t) {
            engine_.tick();
        }
        ticksRun_ += ticks;
    }

    /// Ticks forward to `ticks` total. Never backwards -- see runTo's use in
    /// wardAt().
    void runTo(std::int64_t ticks) {
        if (ticks > ticksRun_) {
            run(ticks - ticksRun_);
        }
    }

private:
    sim::PhasedEngine engine_;
    sim::WardPopulation* people_ = nullptr;
    std::int64_t ticksRun_ = 0;
};

/// A ward of this case's very own, at `startHour`. For a case that MUTATES --
/// lifts coin off somebody, turns a body, skips the clock -- and for a case
/// that wants to compare the shared one against something the cache has never
/// touched.
[[nodiscard]] inline std::unique_ptr<WardRun> privateWard(std::int32_t startHour) {
    return std::make_unique<WardRun>(startHour);
}

/// THE SHARED WARD AT `hour`, TICKED TO `ticks`, READ-ONLY.
///
/// A PURE FUNCTION OF ITS TWO ARGUMENTS. What comes back is the district that
/// hour bakes, ticked exactly that many times, no matter which case asked
/// before or in what order. The cache underneath is an optimisation and never
/// a semantic: a later reading ticks the held ward forward, an earlier one
/// throws it away and bakes again.
///
/// Const, because the whole safety argument is that no case can change what
/// another case reads. Take privateWard() to mutate.
[[nodiscard]] inline const sim::WardPopulation& wardAt(std::int32_t hour, std::int64_t ticks) {
    // Twenty-four slots, indexed by the hour, so there is no map and no
    // iteration order anywhere near this.
    static std::unique_ptr<WardRun> byHour[24];
    const auto slot = static_cast<std::size_t>(((hour % 24) + 24) % 24);
    if (byHour[slot] && ticks < byHour[slot]->ticksRun()) {
        // Somebody has already read this hour LATER than we want it. There is
        // no rewinding a simulation, so the answer is a new one. See the header:
        // this is the case ctest never reaches and a whole-binary run does.
        byHour[slot].reset();
    }
    if (!byHour[slot]) {
        byHour[slot] = std::make_unique<WardRun>(hour);
    }
    WardRun& run = *byHour[slot];
    run.runTo(ticks);
    return run.people();
}

}  // namespace granadad::testfix
