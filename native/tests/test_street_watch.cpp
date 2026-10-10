// THE WATCH ON THE BEATS GETS EYES.
//
// STREET SENSES leg (c). The gap analysis' third finding: "THE STREET WATCH IS
// SCENERY. Thirteen MilitiaWatch WardActors by day and seven by night walk
// beats with no eyes." Leg (a) gave the street a crowd that flees and cowers
// for real; leg (b) gave it bodies to hit, on the Gull's own ray, the same
// blow. This leg gives the Watch that same street the Gull's own Watch: a
// watchman who SEES cause -- a blow, a killing, steel raised, the same
// three-clause notice rule at kWatchSightTiles the Gull's own canSeePlayer and
// witnessCount are held to -- goes Closing, and either arrests at reach (a
// blow or a killing behind it, through the ONE seam the Gull's Cull uses:
// Tavern::arrestByStreetWatch -> arrestPlayer -> the charge, the seizure, the
// hearing) or demands a raised blade go down first, with its own grace, and
// only turns an ignored demand into an Offence -- heat, never an arrest by
// itself.
//
// WHAT THESE CASES CLAIM, and how each is kept honest:
//
//   * a watchman who sees a blow land closes on it (the Halt event, the cause
//     folded 1 + AlarmSeverity) and, adjacent, arrests at reach through the
//     one seam -- exercised here directly against WardPopulation::alarm() and
//     WardPopulation::takeWatchEvents(), the same verbs the client and the
//     gate's own driver call;
//   * steel alone is a DEMAND, not an arrest: the Sheathe event fires once,
//     the grace runs kSheatheGraceSeconds, and only past it with the blade
//     still up does an Offence fire -- once, and never an arrest by itself;
//   * a presented Wielder is never given cause, and a watchman already
//     closing is stood down the moment the deference flag goes up (D6,
//     absolute) -- setPlayerPresentsAsWielder's own rule;
//   * and the population twin-runs byte-identical under all three (the Halt,
//     the demand, the give-up), which is what makes the Watch's own state
//     (closingUntil, closeCause, sheatheBy) something the gate can actually
//     compare rather than merely trust.
//
// EVERY CASE TAKES A privateWard(): every one of them mutates.

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "granadad/sim/world_hash.hpp"
#include "support/ward_fixture.hpp"

using namespace granadad;
using granadad::testfix::privateWard;
using granadad::testfix::sharedTiles;
using granadad::testfix::WardRun;

namespace {

/// Four in the afternoon: the same hour the rest of the street senses lane's
/// cases settle on, walking its beats rather than sleeping in barracks.
constexpr std::int32_t kHour = 16;
constexpr std::int64_t kSettleTicks = 30;

std::uint64_t digestOf(const sim::WardPopulation& people) {
    sim::HashSink sink(0x5354525741544348ull);  // "STRWATCH"
    people.hash_into(sink);
    return sink.finished();
}

/// The first standing MilitiaWatch on walking ground with a free standable
/// orthogonal neighbour to stand the player on -- findStand's own shape
/// (test_street_bodies.cpp), asked of the Watch instead of excluding it.
struct Stand {
    std::int32_t id = -1;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
};

/// The one event this watchman raised, among however many the roster's other
/// watchmen also raised on the same alarm -- kWatchSightTiles is wide enough
/// that a busy quay is not guaranteed to hold only one, so every case asks for
/// ITS OWN watchman's event rather than assuming the mailbox holds exactly one.
const sim::WatchEvent* eventFor(const std::vector<sim::WatchEvent>& events, std::int32_t id) {
    for (const sim::WatchEvent& event : events) {
        if (event.watchmanId == id) {
            return &event;
        }
    }
    return nullptr;
}

Stand findWatch(const sim::WardPopulation& people) {
    static constexpr std::int32_t dx[4] = {1, -1, 0, 0};
    static constexpr std::int32_t dy[4] = {0, 0, 1, -1};
    for (const sim::WardActor& actor : people.actors()) {
        if (!actor.visible() || actor.type != sim::WardType::MilitiaWatch) {
            continue;
        }
        if (!people.onWalkingGround(actor.x, actor.y, actor.band)) {
            continue;
        }
        for (int n = 0; n < 4; ++n) {
            const std::int32_t sx = actor.x + dx[n];
            const std::int32_t sy = actor.y + dy[n];
            if (!sharedTiles().standable(sx, sy, actor.band)) {
                continue;
            }
            if (people.nearestTo(sx, sy, actor.band, 0) != nullptr) {
                continue;  // somebody is standing there
            }
            return Stand{actor.id, sx, sy, actor.band};
        }
    }
    return Stand{};
}

}  // namespace

// ---------------------------------------------------------------------------
// a blow: the halt, and the arrest at reach
// ---------------------------------------------------------------------------

TEST_CASE("a watchman who sees a blow closes, halts, and arrests at reach") {
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();
    const Stand cop = findWatch(people);
    REQUIRE_MESSAGE(cop.id >= 0, "no watchman on walking ground with a free tile at sixteen");

    // The player stood a tile off him -- adjacent, so reach and sight are
    // never in question here, and the case is purely about the state machine.
    people.setPlayer(cop.x, cop.y, cop.band);
    REQUIRE_FALSE(people.watchmanClosing(cop.id));

    // A blow landed where the player stands (WardPopulation::alarm(), the
    // exact verb applyStreetBlow and the gate's own driver call).
    (void)people.alarm(cop.x, cop.y, cop.band, sim::alarmRadius(sim::AlarmSeverity::Blow),
                       sim::AlarmSeverity::Blow);
    const std::vector<sim::WatchEvent> haltEvents = people.takeWatchEvents();
    const sim::WatchEvent* halt = eventFor(haltEvents, cop.id);
    REQUIRE(halt != nullptr);
    CHECK(halt->kind == sim::WatchEventKind::Halt);
    CHECK(halt->cause == 1 + static_cast<std::uint8_t>(sim::AlarmSeverity::Blow));
    CHECK(people.watchmanClosing(cop.id));
    CHECK(people.byId(cop.id)->closingUntil == people.currentTick() + sim::kWatchClosingSeconds);
    // Read-and-clear, the mailbox rule leg (b) already keeps.
    CHECK(people.takeWatchEvents().empty());

    // One tick: Close outscores everything else on offer (1900, under Brawl's
    // 2000 and over every job and need), he is already in reach, and a blow is
    // behind the cause -- so this is the arrest, not the demand.
    own->run(1);
    CHECK(people.byId(cop.id)->policy == sim::WardPolicy::Close);
    const std::vector<sim::WatchEvent> arrestEvents = people.takeWatchEvents();
    const sim::WatchEvent* arrest = eventFor(arrestEvents, cop.id);
    REQUIRE(arrest != nullptr);
    CHECK(arrest->kind == sim::WatchEventKind::Arrest);
    CHECK(arrest->cause == 1 + static_cast<std::uint8_t>(sim::AlarmSeverity::Blow));
    // The latch is spent: the chase this cause started is over.
    CHECK_FALSE(people.watchmanClosing(cop.id));
    CHECK(people.byId(cop.id)->closingUntil == 0);
}

// ---------------------------------------------------------------------------
// steel: a demand first, an offence only past the grace, never an arrest
// ---------------------------------------------------------------------------

TEST_CASE("steel is a demand with a grace, and only an ignored one becomes an offence") {
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();
    const Stand cop = findWatch(people);
    REQUIRE_MESSAGE(cop.id >= 0, "no watchman on walking ground with a free tile at sixteen");

    people.setPlayer(cop.x, cop.y, cop.band);
    (void)people.alarm(cop.x, cop.y, cop.band, sim::alarmRadius(sim::AlarmSeverity::Steel),
                       sim::AlarmSeverity::Steel);
    const std::vector<sim::WatchEvent> sheatheEvents = people.takeWatchEvents();
    const sim::WatchEvent* sheathe = eventFor(sheatheEvents, cop.id);
    REQUIRE(sheathe != nullptr);
    CHECK(sheathe->kind == sim::WatchEventKind::Sheathe);
    CHECK(sheathe->cause == 1 + static_cast<std::uint8_t>(sim::AlarmSeverity::Steel));
    CHECK(people.watchmanClosing(cop.id));
    CHECK(people.byId(cop.id)->sheatheBy == people.currentTick() + sim::kSheatheGraceSeconds);

    // Past the grace, adjacent the whole time (inReach, so he never has to
    // chase for this to fire) -- draining every tick, the mailbox's own rule,
    // so no intermediate event is lost to the next tick's clear.
    std::int32_t offences = 0;
    std::int32_t arrests = 0;
    for (std::int64_t t = 0; t < sim::kSheatheGraceSeconds + 2; ++t) {
        own->run(1);
        for (const sim::WatchEvent& event : people.takeWatchEvents()) {
            if (event.watchmanId != cop.id) {
                continue;
            }
            if (event.kind == sim::WatchEventKind::Offence) {
                CHECK(event.cause == 1 + static_cast<std::uint8_t>(sim::AlarmSeverity::Steel));
                ++offences;
            } else if (event.kind == sim::WatchEventKind::Arrest) {
                ++arrests;
            }
        }
    }
    // Fired ONCE (D5), and never an arrest -- steel alone never is one.
    CHECK(offences == 1);
    CHECK(arrests == 0);
    CHECK(people.byId(cop.id)->sheatheBy == 0);
    // The clock (kWatchClosingSeconds, 12) outlasts the grace (6): he is still
    // closing, holding the demand, with nothing left to say twice.
    CHECK(people.watchmanClosing(cop.id));
}

// ---------------------------------------------------------------------------
// deference is absolute
// ---------------------------------------------------------------------------

TEST_CASE("a presented Wielder is never given cause, and standing one down stands the Watch down") {
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();
    const Stand cop = findWatch(people);
    REQUIRE_MESSAGE(cop.id >= 0, "no watchman on walking ground with a free tile at sixteen");
    people.setPlayer(cop.x, cop.y, cop.band);

    // Closing on a blow, same as the first case.
    (void)people.alarm(cop.x, cop.y, cop.band, sim::alarmRadius(sim::AlarmSeverity::Blow),
                       sim::AlarmSeverity::Blow);
    REQUIRE(eventFor(people.takeWatchEvents(), cop.id) != nullptr);
    REQUIRE(people.watchmanClosing(cop.id));

    // D6: the deference flag goes up, and a watchman already closing stands
    // down at once -- not at the next tick, not at the next sighting.
    people.setPlayerPresentsAsWielder(true);
    CHECK_FALSE(people.watchmanClosing(cop.id));
    CHECK(people.byId(cop.id)->closingUntil == 0);
    CHECK(people.byId(cop.id)->closeCause == 0);

    // And while it holds, nothing gives him cause again.
    (void)people.alarm(cop.x, cop.y, cop.band, sim::alarmRadius(sim::AlarmSeverity::Kill),
                       sim::AlarmSeverity::Kill);
    CHECK(people.takeWatchEvents().empty());
    CHECK_FALSE(people.watchmanClosing(cop.id));

    // Put the blade away, and the same cause gives him the same cause again.
    people.setPlayerPresentsAsWielder(false);
    (void)people.alarm(cop.x, cop.y, cop.band, sim::alarmRadius(sim::AlarmSeverity::Kill),
                       sim::AlarmSeverity::Kill);
    const std::vector<sim::WatchEvent> haltAgainEvents = people.takeWatchEvents();
    const sim::WatchEvent* haltAgain = eventFor(haltAgainEvents, cop.id);
    REQUIRE(haltAgain != nullptr);
    CHECK(haltAgain->kind == sim::WatchEventKind::Halt);
    CHECK(haltAgain->cause == 1 + static_cast<std::uint8_t>(sim::AlarmSeverity::Kill));
    CHECK(people.watchmanClosing(cop.id));
}

// ---------------------------------------------------------------------------
// the twin run
// ---------------------------------------------------------------------------

TEST_CASE("the population twin-run stays byte-identical run-to-run under the Watch closing") {
    const std::unique_ptr<WardRun> a = privateWard(kHour);
    const std::unique_ptr<WardRun> b = privateWard(kHour);
    const std::unique_ptr<WardRun> quiet = privateWard(kHour);
    a->run(kSettleTicks);
    b->run(kSettleTicks);
    quiet->run(kSettleTicks);
    REQUIRE(digestOf(a->people()) == digestOf(b->people()));

    const Stand cop = findWatch(a->people());
    REQUIRE(cop.id >= 0);
    for (WardRun* run : {a.get(), b.get()}) {
        sim::WardPopulation& people = run->people();
        people.setPlayer(cop.x, cop.y, cop.band);
        (void)people.alarm(cop.x, cop.y, cop.band, sim::alarmRadius(sim::AlarmSeverity::Steel),
                           sim::AlarmSeverity::Steel);
        (void)people.takeWatchEvents();
    }
    CHECK(digestOf(a->people()) == digestOf(b->people()));
    CHECK(digestOf(a->people()) != digestOf(quiet->people()));

    // Through the grace, the offence, and the clock giving the chase up.
    a->run(sim::kWatchClosingSeconds + 1);
    b->run(sim::kWatchClosingSeconds + 1);
    quiet->run(sim::kWatchClosingSeconds + 1);
    CHECK(digestOf(a->people()) == digestOf(b->people()));
    CHECK(a->people().reportLine() == b->people().reportLine());
    CHECK(digestOf(a->people()) != digestOf(quiet->people()));
    CHECK_FALSE(a->people().watchmanClosing(cop.id));
}
