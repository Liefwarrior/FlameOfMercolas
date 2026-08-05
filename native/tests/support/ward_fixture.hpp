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
// wardAt(hour, ticks) IS MONOTONE, AND THAT IS DELIBERATE
// ---------------------------------------------------------------------------
// Three cases at two in the morning used to bake three wards and tick them 600,
// 900 and 1,200 times: 2,700 ticks to answer three questions about the same
// district, and a ward tick at two in the morning costs thirty-one milliseconds.
//
// So there is ONE ward per hour and it is ticked FORWARD. Asking for 1,200 after
// somebody asked for 600 runs the 600 that are missing and no more. Asking for
// 600 after somebody asked for 1,200 is not silently answered with the wrong
// district -- it THROWS, naming the file and the fix. There is no reading here
// that can be quietly wrong; there is a reading that stops the build.
//
// The order cases run in is fixed by --order-by=file in the ctest registration,
// so "the tick counts in a file ascend, per hour" is a property of the source
// and not of the machine. Keep it that way when adding a case, or take a
// privateWard() and pay for it.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

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
/// lifts coin off somebody, turns a body, skips the clock -- or that needs a
/// tick count out of order with the rest of its file.
[[nodiscard]] inline std::unique_ptr<WardRun> privateWard(std::int32_t startHour) {
    return std::make_unique<WardRun>(startHour);
}

/// THE SHARED WARD AT `hour`, TICKED TO `ticks`, READ-ONLY.
///
/// One per hour per process, ticked forward and never backward. Throws if a
/// case asks for a district younger than one already handed out, because the
/// alternative is answering with a district nobody asked for.
[[nodiscard]] inline const sim::WardPopulation& wardAt(std::int32_t hour, std::int64_t ticks) {
    // Twenty-four slots, indexed by the hour, so there is no map and no
    // iteration order anywhere near this.
    static std::unique_ptr<WardRun> byHour[24];
    const auto slot = static_cast<std::size_t>(((hour % 24) + 24) % 24);
    if (!byHour[slot]) {
        byHour[slot] = std::make_unique<WardRun>(hour);
    }
    WardRun& run = *byHour[slot];
    if (ticks < run.ticksRun()) {
        throw std::runtime_error(
            "wardAt(" + std::to_string(hour) + ", " + std::to_string(ticks) +
            ") wants a district younger than the " + std::to_string(run.ticksRun()) +
            " ticks already run at that hour.\n"
            "The shared ward only moves forward. Either move this case above the "
            "longer one in its file -- ctest runs a file in --order-by=file order, "
            "so that is a decision the source makes -- or give it a privateWard(" +
            std::to_string(hour) + ") and pay for its own bake.");
    }
    run.runTo(ticks);
    return run.people();
}

}  // namespace granadad::testfix
