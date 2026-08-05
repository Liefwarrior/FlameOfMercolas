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

#include <doctest/doctest.h>

#include <algorithm>
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
    //
    // Five hours of ward time. A cat starts at 8,000 out of the owner's own
    // cat.json and drains a quarter of a point a tick, so this is about the
    // moment the first of them reaches the band.
    WardRun run(9);
    run.run(20000);

    std::int32_t predators = 0;
    std::int32_t hungry = 0;
    for (const sim::WardActor& actor : run.people->actors()) {
        if (!sim::isPredator(actor.type)) {
            continue;
        }
        ++predators;
        INFO("a ", sim::wardTypeName(actor.type), " at hunger ", actor.need(sim::Need::Hunger));
        // NEVER PARKED IN THE MIDDLE. A predator is either recently fed (the
        // catch takes it well past the band) or scavenging at the ceiling. The
        // one thing it must never be is comfortably above the ceiling and below
        // the band on a scrap, which is what a nibble with no clamp produced.
        if (actor.need(sim::Need::Hunger) < sim::kNeedLow) {
            ++hungry;
        }
        // And nothing starved to death for want of a mouse.
        CHECK_FALSE(actor.dead);
    }
    INFO(hungry, " of ", predators, " predators were in the hunger band after five hours");
    CHECK(predators == 13);
    CHECK(hungry > 0);
}

TEST_CASE("the food chain runs: mice are taken, and the den puts more out") {
    // THE ACCEPTANCE, AND IT IS DELIBERATELY NOT "the hunt code executed".
    //
    // A mouse count that only ever rises is not an ecology. So this soaks the
    // district for most of a day and watches the LIVE prey count move: it has
    // to fall below the roll (something ate one) and it has to come back up
    // (the den replaced it). Both, or the thing being measured is a die-off and
    // not a food chain.
    WardRun run(6);

    const std::int32_t roll = run.people->census().prey;
    REQUIRE(roll == 32);

    std::int32_t lowWater = roll;
    std::int32_t highWaterAfterTheDip = 0;
    bool dipped = false;
    // Sixty thousand ticks is sixteen and a half hours: long enough for every
    // predator to drain into the band, hunt, and be well into its second
    // hunger, and for the first mice taken to have come back out of the den
    // (a caught mouse is off the board for an eighth of a day).
    for (int block = 0; block < 60; ++block) {
        run.run(1000);
        const sim::WardCensus roll_now = run.people->census();
        lowWater = std::min(lowWater, roll_now.preyUp);
        if (roll_now.preyUp < roll) {
            dipped = true;
        }
        if (dipped) {
            highWaterAfterTheDip = std::max(highWaterAfterTheDip, roll_now.preyUp);
        }
        // AND NOTHING IS EVER LOST. A caught mouse is not killed -- it is off
        // the board with a countdown on it -- so the roll never shrinks and
        // nothing ever goes permanently missing.
        CHECK(roll_now.prey == roll);
        CHECK(roll_now.preyUp <= roll);
    }

    INFO("catches=", run.people->catches(), " futile=", run.people->futileChases(),
         " live mice fell to ", lowWater, " of ", roll, " and recovered to ",
         highWaterAfterTheDip);
    // Something actually ate something.
    CHECK(run.people->catches() > 0);
    // The population moved, which is the difference between a food chain and a
    // counter that is incremented.
    CHECK(lowWater < roll);
    // And it recovered: the den is a source and not a stock being drawn down.
    CHECK(highWaterAfterTheDip > lowWater);

    // NOTHING WAS DRIVEN TO EXTINCTION EITHER. Predation that outruns the den
    // is a die-off, and a district with no rats left in it is exactly as wrong
    // as a district where nothing eats them.
    CHECK(lowWater > roll / 3);

    // No beast starved on either side of it.
    for (const sim::WardActor& actor : run.people->actors()) {
        if (sim::isPredator(actor.type) || sim::isPrey(actor.type)) {
            INFO("a ", sim::wardTypeName(actor.type), " id ", actor.id);
            CHECK_FALSE(actor.dead);
        }
    }
}

TEST_CASE("a caught mouse holds no tile and is drawn nowhere") {
    // A body in a stomach must not go on occupying a square: a den mouth or a
    // doorway sealed for three hours of ward time is the one way being eaten
    // could go on hurting a street after the mouse is gone. Same rule a corpse
    // keeps, and visible() is the one place it is answered.
    WardRun run(6);
    run.run(30000);
    REQUIRE(run.people->catches() > 0);

    std::int32_t down = 0;
    std::vector<std::uint64_t> cells;
    for (const sim::WardActor& actor : run.people->actors()) {
        if (actor.downedUntil >= 0) {
            ++down;
            CHECK_FALSE(actor.visible());
            CHECK(sim::isPrey(actor.type));
            // It comes back, and at an ABSOLUTE tick rather than off a
            // countdown somebody has to remember to decrement.
            CHECK(actor.downedUntil > run.people->currentTick());
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
    WardRun run(6);
    for (int block = 0; block < 30; ++block) {
        run.run(1000);
        for (const sim::WardActor& actor : run.people->actors()) {
            if (!sim::isPredator(actor.type)) {
                continue;
            }
            INFO("a ", sim::wardTypeName(actor.type), " id ", actor.id, " chase ticks ",
                 actor.huntTicks);
            CHECK(actor.huntTicks <= sim::kChaseBudgetTicks);
            // A lock only ever holds a mouse.
            if (actor.huntTarget >= 0) {
                CHECK(actor.huntTarget >= run.people->preyFirst());
                CHECK(actor.huntTarget < run.people->preyEnd());
                const sim::WardActor& prey =
                    run.people->actors()[static_cast<std::size_t>(actor.huntTarget)];
                CHECK(sim::isPrey(prey.type));
            }
            // And nobody that is not hunting is holding one, which is what
            // stops a mouse being invisible to every other predator in the ward
            // because of a hunt nobody is running.
            if (actor.policy != sim::WardPolicy::Hunt) {
                CHECK(actor.huntTarget == -1);
            }
        }
    }
    // Futile chases are COUNTED and not asserted away: a district with real
    // geometry in it will have some, and a count that runs away is the
    // chokepoint freeze coming back.
    INFO("futile chases over eight hours: ", run.people->futileChases(), " against ",
         run.people->catches(), " catches");
    CHECK(run.people->futileChases() < 4000);
}

TEST_CASE("the ward's loaf ledger does not move when a cat eats a rat") {
    // A CAT EATING A RAT IS NOT A LOAF LEAVING A LARDER. The food ledger's
    // identity -- minted minus eaten equals held -- is a gate rather than a
    // report, because a simulation that can quietly create or destroy a loaf
    // balances its own economy by accident. The catch restores a need and
    // touches no item, and this is where that stays true.
    WardRun run(6);
    for (int block = 0; block < 24; ++block) {
        run.run(1000);
        const sim::WardLedger& ledger = run.people->ledger();
        INFO("after ", (block + 1) * 1000, " ticks, ", run.people->catches(), " catches");
        CHECK(ledger.foodMinted - ledger.foodEaten == run.people->foodHeld());
    }
    REQUIRE(run.people->catches() > 0);
}
