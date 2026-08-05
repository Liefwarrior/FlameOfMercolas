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
// So the district is soaked ONCE, ten hours, and every claim
// below reads the same end state and the same running tallies. Five cases at
// thirty-six thousand ticks each would be most of the suite's runtime for one
// answer repeated five ways -- and the last round's ctest time quadrupling is
// exactly the thing not to do twice.

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
    /// Predators that ended above the scavenge ceiling and under the hunger
    /// band -- the state a scrap with no clamp used to park every one of them
    /// in, and which nothing can now produce.
    std::int32_t parkedInTheMiddle = 0;
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
        bool dipped = false;
        for (int block = 0; block < 36; ++block) {
            out.ward->run(1000);
            const sim::WardCensus roll = people.census();
            out.lowWater = std::min(out.lowWater, roll.preyUp);
            dipped = dipped || roll.preyUp < out.prey;
            if (dipped) {
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
            if (hunger > sim::kScavengeCeiling && hunger < sim::kNeedLow) {
                ++out.parkedInTheMiddle;
            }
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

TEST_CASE("a scrap is not a meal: the ward's cats actually get hungry now") {
    // THE BUG THAT WOULD HAVE MADE THE WHOLE FEATURE DEAD CODE.
    //
    // The den nibble refilled a predator faster than any decay could drain it,
    // so no predator ever entered the hunger band and the hunt could never
    // fire. A scrap now tops a predator up to kScavengeCeiling and no further,
    // which is under kNeedLow -- so a predator that cannot reach prey hovers
    // permanently hungry and permanently looking, and only a catch fills it.
    const Soak& s = soak();
    INFO(s.hungryPredators, " of thirteen predators ended in the hunger band");
    CHECK(s.hungryPredators > 0);
    // NEVER PARKED IN THE MIDDLE: comfortably above the scavenge ceiling and
    // still under the band is the state the unclamped nibble produced, and it
    // is the state nothing can now reach.
    CHECK(s.parkedInTheMiddle == 0);

    // And nothing on either side of the food chain starved to death for it.
    for (const sim::WardActor& actor : s.ward->people->actors()) {
        if (sim::isPredator(actor.type) || sim::isPrey(actor.type)) {
            INFO("a ", sim::wardTypeName(actor.type), " id ", actor.id);
            CHECK_FALSE(actor.dead);
        }
    }
}

TEST_CASE("the food chain runs: mice are taken, and the den puts more out") {
    // THE ACCEPTANCE, AND IT IS DELIBERATELY NOT "the hunt code executed".
    //
    // A mouse count that only ever rises is not an ecology. So the soak watches
    // the LIVE prey count move: it has to fall below the roll (something ate
    // one) and it has to come back up (the den replaced it). Both, or the thing
    // being measured is a die-off and not a food chain.
    const Soak& s = soak();
    INFO("catches=", s.ward->people->catches(), " futile=", s.ward->people->futileChases(),
         " live mice fell to ", s.lowWater, " of ", s.prey, " and recovered to ",
         s.highWaterAfterTheDip);

    REQUIRE(s.prey == 32);
    // Something actually ate something.
    CHECK(s.ward->people->catches() > 0);
    // The population moved, which is the difference between a food chain and a
    // counter that gets incremented.
    CHECK(s.lowWater < s.prey);
    // And it recovered: the den is a source, not a stock being drawn down.
    CHECK(s.highWaterAfterTheDip > s.lowWater);
    // NOTHING WAS DRIVEN TO EXTINCTION EITHER. Predation that outruns the den
    // is a die-off, and a district with no rats left in it is exactly as wrong
    // as a district where nothing eats them.
    CHECK(s.lowWater > s.prey / 3);
    // A caught mouse is off the board and never lost: the roll never shrank.
    CHECK(s.rollHeld);
}

TEST_CASE("a caught mouse holds no tile and is drawn nowhere") {
    // A body in a stomach must not go on occupying a square: a den mouth or a
    // doorway sealed for three hours of ward time is the one way being eaten
    // could go on hurting a street after the mouse is gone. Same rule a corpse
    // keeps, and visible() is the one place it is answered.
    const Soak& s = soak();
    REQUIRE(s.ward->people->catches() > 0);

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
    // ONE BODY PER SQUARE STILL HOLDS, counting the bodies that are actually on
    // the board. A downed mouse whose tile was never released would show up
    // here as two things standing on one cell.
    std::sort(cells.begin(), cells.end());
    INFO(down, " mice were off the board at the end of the soak");
    CHECK(std::adjacent_find(cells.begin(), cells.end()) == cells.end());
}

TEST_CASE("a chase that cannot land is abandoned, and the beast goes back to wandering") {
    // THE FUTILITY BUDGET, WHICH IS THE PART THE JAVA BUILD PAID FOR.
    //
    // A predator pinned on an unreachable mouse is not a predator that fails to
    // eat, it is a predator that stops doing anything else -- and the ward's
    // own logs would show a beast "hunting" for eight thousand ticks. So no
    // lock ever outlives its budget, and a lock dropped as futile suppresses
    // acquisition long enough for the wander to change the situation.
    const Soak& s = soak();
    INFO("worst chase seen: ", s.worstChase, " ticks against a budget of ",
         sim::kChaseBudgetTicks);
    CHECK(s.worstChase <= sim::kChaseBudgetTicks);
    // A lock only ever holds a mouse...
    CHECK(s.wrongPrey == 0);
    // ...and nobody that is not hunting is holding one, which is what stops a
    // mouse being invisible to every other predator in the ward because of a
    // hunt nobody is running.
    CHECK(s.idleLocks == 0);
    // Futile chases are COUNTED and not asserted away: a district with real
    // geometry in it will have some, and a count that runs away is the
    // chokepoint freeze coming back.
    INFO("futile chases over ten hours: ", s.ward->people->futileChases(), " against ",
         s.ward->people->catches(), " catches");
    CHECK(s.ward->people->futileChases() < 6000);
}

TEST_CASE("the ward's loaf ledger does not move when a cat eats a rat") {
    // A CAT EATING A RAT IS NOT A LOAF LEAVING A LARDER. The food ledger's
    // identity -- minted minus eaten equals held -- is a gate rather than a
    // report, because a simulation that can quietly create or destroy a loaf
    // balances its own economy by accident. The catch restores a need and
    // touches no item, and this is where that stays true.
    const Soak& s = soak();
    REQUIRE(s.ward->people->catches() > 0);
    CHECK(s.ledgerHeld);
    const sim::WardLedger& ledger = s.ward->people->ledger();
    CHECK(ledger.foodMinted - ledger.foodEaten == s.ward->people->foodHeld());
}
