// SOMETHING EATS THE RATS.
//
// The population round shipped thirty-two mice and thirteen predators and said
// so in the middle of actPursue: "the Java build's predator/prey lock -- the
// sense probe, the chase budget, the futile-chase backoff and the prey revive
// -- is NOT ported. Cats and gulls wander and feed; they do not hunt the mice."
//
// A dock district with rats nobody eats is missing a food chain, and the mice
// are also the honest reason vermin scalps have a market. So this is the port,
// and these cases are about the two things a port can get wrong.
//
// THE FIRST IS THAT IT NEVER RUNS. The wander leg pays a den nibble of 1,500
// hunger on every arrival, and on this engine's clock a leg is three tiles at a
// tile a second -- so a cat was gaining hundreds of points a tick against a
// decay of a quarter of one. The cats were never hungry. A hunt shipped beside
// that would be dead code, and a soak could not tell it from a working ecology
// because both would report a mouse count that only ever reads 32 of 32.
//
// THE SECOND IS THAT IT RUNS FOREVER. The Java build's own soak found two ways
// a chase never ends: the chokepoint freeze, where the route exists and its
// first hop is plugged by parked bodies, and the untouchable-prey orbit, where
// the prey sits in a pocket and contact never lands. One gull was pinned eight
// thousand ticks against an alcove. The budget, the backoff and the lock drop
// are all here, and so are the cases that watch them.
//
// ---------------------------------------------------------------------------
// ONE SOAK, READ BY FIVE CASES, AND THAT IS A COST DECISION
// ---------------------------------------------------------------------------
// A cat starts full out of the owner's own cat.json and drains a quarter of a
// point a tick, so the first catch in this district is five hours of ward time
// away and a caught mouse is off the board for three more. There is no short
// version of that question: an ecology is a thing you can only see over time.
//
// So the district is soaked ONCE, ten hours, and every claim about it is made
// inside ONE case.
//
// ONE CASE AND NOT FIVE, AND THE REASON IS MEASURED. The first draft put each
// claim in its own TEST_CASE reading a shared file-static soak, on the
// reasonable assumption that a static is built once per binary. It is not:
// doctest_discover_tests registers every case as its own ctest entry, and ctest
// runs each of those by launching the binary again with --test-case=. Five
// cases therefore meant five processes and FIVE SOAKS -- the gate measured it,
// 307 + 270 + 262 + 260 seconds against one soak's 300. Twenty-two minutes of
// every build for one answer repeated five ways, and the last round's ctest
// time quadrupling is exactly the thing not to do twice.
//
// So the five claims are five commented blocks under one name. Each is still a
// separate assertion with its own INFO, so a failure still says which one.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/ward_actors.hpp"

using namespace granadad;

namespace {

struct Docks {
    content::World world;
    sim::TileQuery tiles;
    explicit Docks()
        : world(content::loadWorldFile(content::bakedMap(sim::docks::kWorldName))), tiles(world) {}
};

constexpr std::uint64_t kSeed = 0x4752414E41444144ull;  // "GRANADAD"

struct WardRun {
    Docks docks;
    sim::PhasedEngine engine;
    sim::WardPopulation* people = nullptr;

    explicit WardRun(std::int32_t startHour) : engine(kSeed, docks.world) {
        auto owned = std::make_unique<sim::WardPopulation>(
            docks.tiles, sim::hourOfDay(startHour), kSeed, content::contentDir());
        people = owned.get();
        engine.register_system(std::move(owned));
        engine.boot();
    }

    void run(std::int64_t ticks) {
        for (std::int64_t t = 0; t < ticks; ++t) {
            engine.tick();
        }
    }
};

/// Ten hours of the Docks, from six in the morning, with the running tallies
/// every claim below is read off.
struct Soak {
    std::unique_ptr<WardRun> ward;

    /// The prey roll, and how far the LIVE count fell and climbed back.
    ///
    /// THE RECOVERY IS MEASURED FROM THE BOTTOM, and the first draft measured
    /// it from the first dip -- which is not a claim at all. A count that fell
    /// 32, 31, 30, 29 and never rose again would satisfy "the highest reading
    /// after the first dip beats the lowest reading" trivially, with 31 against
    /// 29, while describing a die-off. So the high water is only the readings
    /// that came AFTER the lowest one.
    std::int32_t prey = 0;
    std::int32_t lowWater = 0;
    std::int32_t highWaterAfterTheDip = 0;
    /// Every sample said the roll itself never moved: a caught mouse is off the
    /// board and never lost.
    bool rollHeld = true;
    /// And the ward's loaf ledger balanced at every one of them.
    bool ledgerHeld = true;

    /// The longest any lock ever ran. Must never exceed the chase budget.
    std::int32_t worstChase = 0;
    /// A lock held by a predator that was not hunting this tick, or a lock on
    /// something that is not a mouse. Both are zero by construction.
    std::int32_t idleLocks = 0;
    std::int32_t wrongPrey = 0;
    /// Predators that were in the hunger band at the end.
    std::int32_t hungryPredators = 0;
    /// Predators sitting AT OR UNDER the scavenge ceiling -- which is where a
    /// predator with nothing to catch ends up and stays. Under the unclamped
    /// nibble this was structurally impossible: a scrap put a beast back at
    /// full every few seconds and nothing was ever hungry at all.
    std::int32_t atTheCeiling = 0;
    /// The most hunger any predator held. A number rather than a threshold: it
    /// is what says a catch is worth a great deal more than a scrap.
    std::int32_t fullestPredator = 0;
};

/// Built on first use and read by every case. Deterministic: one seed, one
/// hour, one tick count, so whichever case asks first gets the same district
/// every other case then reads.
const Soak& soak() {
    static const Soak cached = [] {
        Soak out;
        out.ward = std::make_unique<WardRun>(6);
        const sim::WardPopulation& people = *out.ward->people;
        out.prey = people.census().prey;
        out.lowWater = out.prey;
        for (int block = 0; block < 36; ++block) {
            out.ward->run(1000);
            const sim::WardCensus roll = people.census();
            if (roll.preyUp < out.lowWater) {
                // A new bottom RESTARTS the recovery measurement, so what the
                // case reads is always "how far it climbed back from the worst
                // it ever got" and never "how high it was before it fell".
                out.lowWater = roll.preyUp;
                out.highWaterAfterTheDip = roll.preyUp;
            } else {
                out.highWaterAfterTheDip = std::max(out.highWaterAfterTheDip, roll.preyUp);
            }
            out.rollHeld = out.rollHeld && roll.prey == out.prey && roll.preyUp <= out.prey;
            const sim::WardLedger& ledger = people.ledger();
            out.ledgerHeld =
                out.ledgerHeld && ledger.foodMinted - ledger.foodEaten == people.foodHeld();
            for (const sim::WardActor& actor : people.actors()) {
                if (!sim::isPredator(actor.type)) {
                    continue;
                }
                out.worstChase = std::max(out.worstChase, actor.huntTicks);
                if (actor.huntTarget >= 0) {
                    if (actor.policy != sim::WardPolicy::Hunt) {
                        ++out.idleLocks;
                    }
                    if (actor.huntTarget < people.preyFirst() ||
                        actor.huntTarget >= people.preyEnd()) {
                        ++out.wrongPrey;
                    }
                }
            }
        }
        for (const sim::WardActor& actor : people.actors()) {
            if (!sim::isPredator(actor.type)) {
                continue;
            }
            const std::int32_t hunger = actor.need(sim::Need::Hunger);
            if (hunger < sim::kNeedLow) {
                ++out.hungryPredators;
            }
            if (hunger <= sim::kScavengeCeiling) {
                ++out.atTheCeiling;
            }
            out.fullestPredator = std::max(out.fullestPredator, hunger);
        }
        return out;
    }();
    return cached;
}

}  // namespace

TEST_CASE("the mice are a contiguous id range, which is what makes the hunt cheap") {
    // THE WHOLE COST ARGUMENT IN ONE ASSERTION. A predator's sense probe walks
    // preyFirst..preyEnd and nothing else; if the mice ever stop being the last
    // thing the roster spawns, that probe silently starts missing prey and the
    // ecology goes quietly wrong instead of loudly red.
    WardRun run(8);
    const std::int32_t first = run.people->preyFirst();
    const std::int32_t end = run.people->preyEnd();
    REQUIRE(end > first);
    CHECK(end - first == 32);
    CHECK(end == static_cast<std::int32_t>(run.people->actors().size()));

    for (const sim::WardActor& actor : run.people->actors()) {
        const bool inRange = actor.id >= first && actor.id < end;
        INFO("actor ", actor.id, " is a ", sim::wardTypeName(actor.type));
        CHECK(inRange == sim::isPrey(actor.type));
    }

    // And the ward's predators are the two beasts that hunt and nobody else --
    // not the kennel dogs, who wander and are fed, and not a person.
    CHECK(sim::isPredator(sim::WardType::Cat));
    CHECK(sim::isPredator(sim::WardType::Stray));
    CHECK_FALSE(sim::isPredator(sim::WardType::Dog));
    CHECK_FALSE(sim::isPredator(sim::WardType::Serf));
}

TEST_CASE("the food chain runs: mice are taken, and the den puts more out") {
    // TEN HOURS OF THE DOCKS, AND FIVE CLAIMS OFF ONE RUN. See the file header
    // for why they are not five cases.
    const Soak& s = soak();
    INFO("catches=", s.ward->people->catches(), " futile=", s.ward->people->futileChases(),
         "; live mice fell to ", s.lowWater, " of ", s.prey, " and climbed back to ",
         s.highWaterAfterTheDip, "; ", s.hungryPredators,
         " of thirteen predators ended in the hunger band, ", s.atTheCeiling,
         " of them scavenging at the ceiling, and the fullest held ", s.fullestPredator);

    // ---------------------------------------------------------------------
    // 1. THE ACCEPTANCE. A mouse count that only ever rises is not an ecology.
    // ---------------------------------------------------------------------
    REQUIRE(s.prey == 32);
    // Something actually ate something.
    CHECK(s.ward->people->catches() > 0);
    // The population MOVED, which is the difference between a food chain and a
    // counter that gets incremented.
    CHECK(s.lowWater < s.prey);
    // And it came back from the bottom: the den is a source, not a stock being
    // drawn down. Measured from the lowest reading, not from the first dip --
    // see Soak::lowWater.
    CHECK(s.highWaterAfterTheDip > s.lowWater);
    // NOTHING WAS DRIVEN TO EXTINCTION EITHER. Predation that outruns the den
    // is a die-off, and a district with no rats left in it is exactly as wrong
    // as a district where nothing eats them.
    CHECK(s.lowWater > s.prey / 3);
    // A caught mouse is off the board and never lost: the roll never shrank.
    CHECK(s.rollHeld);

    // ---------------------------------------------------------------------
    // 2. A SCRAP IS NOT A MEAL -- the bug that would have made all of the
    //    above dead code. The den nibble refilled a predator faster than any
    //    decay could drain it, so no predator ever entered the hunger band and
    //    the hunt could never fire. THE MICE WERE SAFE BECAUSE THE CATS WERE
    //    NEVER HUNGRY.
    // ---------------------------------------------------------------------
    CHECK(s.hungryPredators > 0);
    // At the ceiling and staying there is where a predator with nothing to
    // catch ends up: permanently hungry, permanently looking, never dead.
    CHECK(s.atTheCeiling > 0);
    // And a catch is worth a great deal more than a scrap. Somebody ate.
    CHECK(s.fullestPredator > sim::kScavengeCeiling);

    // ---------------------------------------------------------------------
    // 3. THE FUTILITY BUDGET, which is the part the Java build paid for. A
    //    predator pinned on an unreachable mouse is not a predator that fails
    //    to eat, it is a predator that stops doing anything else -- one gull
    //    was logged "hunting" for eight thousand ticks against a plugged
    //    alcove. No lock outlives its budget; none is held by a beast that is
    //    not hunting; none ever holds anything but a mouse.
    // ---------------------------------------------------------------------
    CHECK(s.worstChase <= sim::kChaseBudgetTicks);
    CHECK(s.idleLocks == 0);
    CHECK(s.wrongPrey == 0);
    // Futile chases are COUNTED and not asserted away: a district with real
    // geometry in it will have some, and a count that runs away is the
    // chokepoint freeze coming back.
    CHECK(s.ward->people->futileChases() < 6000);

    // ---------------------------------------------------------------------
    // 4. A CAUGHT MOUSE HOLDS NO TILE. A body in a stomach must not go on
    //    occupying a square: a den mouth sealed for three hours of ward time
    //    is the one way being eaten could go on hurting a street after the
    //    mouse is gone. Same rule a corpse keeps.
    // ---------------------------------------------------------------------
    std::int32_t down = 0;
    std::vector<std::uint64_t> cells;
    for (const sim::WardActor& actor : s.ward->people->actors()) {
        if (actor.downedUntil >= 0) {
            ++down;
            CHECK_FALSE(actor.visible());
            CHECK(sim::isPrey(actor.type));
            // It comes back, and at an ABSOLUTE tick rather than off a
            // countdown somebody has to remember to decrement.
            CHECK(actor.downedUntil > s.ward->people->currentTick());
            continue;
        }
        if (!actor.visible()) {
            continue;
        }
        cells.push_back((static_cast<std::uint64_t>(actor.band) << 40) |
                        (static_cast<std::uint64_t>(actor.y) << 20) |
                        static_cast<std::uint64_t>(actor.x));
    }
    std::sort(cells.begin(), cells.end());
    INFO(down, " mice were off the board at the end of the soak");
    CHECK(std::adjacent_find(cells.begin(), cells.end()) == cells.end());

    // ---------------------------------------------------------------------
    // 5. AND THE LOAF LEDGER DID NOT MOVE. A cat eating a rat is not a loaf
    //    leaving a larder. The ledger's identity -- minted minus eaten equals
    //    held -- is a gate rather than a report, because a simulation that can
    //    quietly create or destroy a loaf balances its own economy by
    //    accident. The catch restores a need and touches no item.
    // ---------------------------------------------------------------------
    CHECK(s.ledgerHeld);
    const sim::WardLedger& ledger = s.ward->people->ledger();
    CHECK(ledger.foodMinted - ledger.foodEaten == s.ward->people->foodHeld());

    // And nothing on either side of the chain starved to death for it.
    for (const sim::WardActor& actor : s.ward->people->actors()) {
        if (sim::isPredator(actor.type) || sim::isPrey(actor.type)) {
            INFO("a ", sim::wardTypeName(actor.type), " id ", actor.id);
            CHECK_FALSE(actor.dead);
        }
    }
}
