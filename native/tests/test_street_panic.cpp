// THE STREET RUNS FROM A KNIFE.
//
// Eli, 2026-09-10: "I'd expect the watch and crowd to react appropriately to
// the violence they're witnessing." The combat gap analysis found the biggest
// lever for that sentence was not in the Gull at all: WardPolicy::Flee was
// built, hashed and DEAD for people -- every authored actor row starts Safety
// at nine or ten thousand with a decay of zero, and the only thing that ever
// lowered it was a cat closing on a mouse. One verb, WardPopulation::alarm(),
// is the trigger, and everything under it is the machinery that already ran.
//
// WHAT THESE CASES CLAIM, and how each is kept honest:
//
//   * an alarm within sight drops Safety under the FLEE gate and FLEE fires --
//     against a witness rule COMPUTED HERE, INDEPENDENTLY (same band, in
//     range, line of sight through the shared tiles), so the sim is checked
//     against the rule rather than against itself;
//   * out of sight, another band, beyond the radius, a beast, the Watch: each
//     untouched, one at a time, on the real district;
//   * the panic lasts as long as its severity says and no longer -- a blade
//     for half a minute, a killing for the better part of two -- and only the
//     frightened pay the panic rate: everybody else's Safety arithmetic is
//     byte-for-byte the un-alarmed twin's, which is the baseline argument in
//     miniature;
//   * the population twin-runs byte-identical after an alarm, and differs from
//     a ward nobody frightened -- so the hash proves the alarm did something
//     and did it the same way twice;
//   * and THROUGH THE CLIENT: a session standing on the Tarwalk with steel
//     raised -- the tavern's own stance, the client's own step -- frightens
//     exactly the people the rule says, once a second while the blade is up,
//     and the street is back at work a minute after it goes down. Session::step
//     has ONE call site for the street and this is the case that proves it
//     fires.
//
// THIS IS THE ONE DECLARED POPULATION BASELINE MOVE of the Oblivion-feel
// program (DECISIONS.md, "Oblivion feel: street panic"). The gate's own
// no-player run frightens nobody, so the number is expected to stand; it is
// re-run and re-blessed regardless, because the law is about the declaration
// and not about the arithmetic.
//
// EVERY CASE TAKES A privateWard(): every one of them mutates.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/brawl.hpp"
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

/// The Tarwalk is walked at eight: the day trades start at seven and it is the
/// road they walk to get there, so at eight it is the busiest ground in the
/// district and the honest place to ask whether a street scatters.
constexpr std::int32_t kHour = 8;
/// Ticks the ward walks before anybody is frightened, so the bodies are
/// somewhere the schedule put them rather than where the bake dropped them.
constexpr std::int64_t kSettleTicks = 30;

std::int32_t safetyOf(const sim::WardActor& actor) {
    return actor.need(sim::Need::Safety);
}

/// THE RULE, WRITTEN A SECOND TIME. Whether `actor` would see an event at
/// (x, y, band) from within `radius`, by the three clauses the sim claims to
/// keep -- same band, Chebyshev range, line of sight -- plus the refusals
/// (a corpse, a beast, the Watch). Deliberately not a call into the sim: a
/// case that asked the sim who it frightened and then checked it frightened
/// them would be checking the sim against itself.
bool wouldSee(const sim::WardActor& actor, std::int32_t x, std::int32_t y, std::int32_t band,
              std::int32_t radius) {
    if (!actor.visible() || !sim::isPerson(actor.type) ||
        actor.type == sim::WardType::MilitiaWatch) {
        return false;
    }
    if (actor.band != band) {
        return false;
    }
    if (std::max(std::abs(actor.x - x), std::abs(actor.y - y)) > radius) {
        return false;
    }
    if (actor.x == x && actor.y == y) {
        return true;
    }
    return sharedTiles().lineOfSight(actor.x, actor.y, x, y, band);
}

/// In range on the same band, a person, and WALLED OFF: the one class of body
/// that separates a line-of-sight rule from a radius.
bool inRangeButWalled(const sim::WardActor& actor, std::int32_t x, std::int32_t y,
                      std::int32_t band, std::int32_t radius) {
    if (!actor.visible() || !sim::isPerson(actor.type) ||
        actor.type == sim::WardType::MilitiaWatch) {
        return false;
    }
    if (actor.band != band || (actor.x == x && actor.y == y)) {
        return false;
    }
    if (std::max(std::abs(actor.x - x), std::abs(actor.y - y)) > radius) {
        return false;
    }
    return !sharedTiles().lineOfSight(actor.x, actor.y, x, y, band);
}

struct Spot {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t band = 0;
    std::int32_t seen = 0;
    std::int32_t walled = 0;
    std::int32_t otherBand = 0;
    bool found = false;
};

/// The first person, ascending id, standing on walking ground whose
/// neighbourhood at `radius` has enough of each kind of body in it to make
/// every clause of the rule observable: at least `wantSeen` who would see it,
/// at least `wantWalled` in range and walled off, and -- if the district
/// offers one -- somebody in range on another band. Chosen by the rule and not
/// by hand, so a regenerated map moves the spot rather than breaking the case.
Spot findSpot(const sim::WardPopulation& people, std::int32_t radius, std::int32_t wantSeen,
              std::int32_t wantWalled = 1) {
    Spot best;
    for (const sim::WardActor& centre : people.actors()) {
        if (!centre.visible() || !sim::isPerson(centre.type) ||
            !people.onWalkingGround(centre.x, centre.y, centre.band)) {
            continue;
        }
        Spot spot;
        spot.x = centre.x;
        spot.y = centre.y;
        spot.band = centre.band;
        for (const sim::WardActor& other : people.actors()) {
            if (other.id == centre.id) {
                continue;
            }
            if (wouldSee(other, spot.x, spot.y, spot.band, radius)) {
                ++spot.seen;
            } else if (inRangeButWalled(other, spot.x, spot.y, spot.band, radius)) {
                ++spot.walled;
            } else if (other.visible() && sim::isPerson(other.type) && other.band != spot.band &&
                       std::max(std::abs(other.x - spot.x), std::abs(other.y - spot.y)) <=
                           radius) {
                ++spot.otherBand;
            }
        }
        if (spot.seen >= wantSeen && spot.walled >= wantWalled) {
            spot.found = true;
            if (spot.otherBand >= 1) {
                return spot;  // every clause observable from one tile
            }
            if (!best.found) {
                best = spot;  // keep looking for a tile with the band clause too
            }
        }
    }
    return best;
}

std::uint64_t digestOf(const sim::WardPopulation& people) {
    sim::HashSink sink(0x5354524545545041ull);  // "STREETPA"
    people.hash_into(sink);
    return sink.finished();
}

/// A session standing on the street at `hour`, test_stance's own shape.
render::SessionConfig streetAt(int hour, std::int32_t x, std::int32_t y, std::int32_t band) {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnX = x;
    config.spawnY = y;
    config.spawnBand = band;
    config.width = 320;
    config.height = 180;
    return config;
}

/// Hands up through the client without a blow: a held guard for a step, then
/// released -- the hands stay up (the stance's own rule), the guard does not.
void raiseByGuard(render::Session& session) {
    REQUIRE_FALSE(session.casebookOpen());
    session.setBlocking(true);
    session.stepMany(sim::MoveInput{}, 1);
    session.setBlocking(false);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.tavern().playerHandsUp());
    REQUIRE_FALSE(session.tavern().playerBlocking());
}

/// Steps the client until the ward has taken exactly one more tick. The
/// street's alarm fires on the SAME client step the tick is taken on, after
/// it, so on return the ward stands where the alarm saw it.
void stepOneTick(render::Session& session) {
    const std::int64_t before = session.people().currentTick();
    for (int guard = 0; guard < 4 * sim::kStepsPerSecond; ++guard) {
        session.stepMany(sim::MoveInput{}, 1);
        if (session.people().currentTick() != before) {
            return;
        }
    }
    FAIL("the ward did not tick inside four seconds of client steps");
}

}  // namespace

// ---------------------------------------------------------------------------
// an alarm within sight
// ---------------------------------------------------------------------------

TEST_CASE("an alarm within sight drops Safety below critical and the Flee policy fires") {
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();

    const Spot spot = findSpot(people, sim::kAlarmRadiusKill, 3);
    REQUIRE_MESSAGE(spot.found,
                    "no tile on the walking ground at eight has three people in sight and one "
                    "walled off within the kill radius -- the district changed under this case");
    INFO("alarm at ", spot.x, ",", spot.y, ",z", spot.band, " seen=", spot.seen,
         " walled=", spot.walled, " otherBand=", spot.otherBand);

    // Who the rule says would see it, decided BEFORE the sim is asked.
    std::vector<std::int32_t> seen;
    for (const sim::WardActor& actor : people.actors()) {
        if (wouldSee(actor, spot.x, spot.y, spot.band, sim::kAlarmRadiusKill)) {
            seen.push_back(actor.id);
            // Nobody is frightened before the alarm: the channel is dead for
            // people until this build, and the case would prove nothing if it
            // were not.
            CHECK(safetyOf(actor) >= sim::kNeedCritical);
            CHECK(actor.policy != sim::WardPolicy::Flee);
        }
    }
    REQUIRE(seen.size() >= 3);

    const std::int32_t saw =
        people.alarm(spot.x, spot.y, spot.band, sim::kAlarmRadiusKill, sim::AlarmSeverity::Kill);
    CHECK(saw == static_cast<std::int32_t>(seen.size()));

    // Driven to the floor at once...
    for (const std::int32_t id : seen) {
        const sim::WardActor& actor = *people.byId(id);
        INFO("actor ", id, " ", sim::wardTypeName(actor.type));
        CHECK(safetyOf(actor) == sim::kPanicSafetyKill);
        CHECK(safetyOf(actor) < sim::kNeedCritical);
    }
    // ...and FLEE wins the very next tick for every one of them who is not
    // starving. The one policy priced above FLEE's 950 is SEEK_FOOD at its
    // critical bonus (305 + 1000 for a serf), which is the raws' own ruling
    // that a starving man is past being frightened; nobody is that hungry
    // thirty ticks after an eight o'clock bake, and the case says so rather
    // than assuming it.
    own->run(1);
    std::int32_t fleeing = 0;
    for (const std::int32_t id : seen) {
        const sim::WardActor& actor = *people.byId(id);
        INFO("actor ", id, " ", sim::wardTypeName(actor.type), " hunger ",
             actor.need(sim::Need::Hunger), " policy ", sim::wardPolicyName(actor.policy));
        REQUIRE(actor.need(sim::Need::Hunger) >= sim::kNeedCritical);
        CHECK(actor.policy == sim::WardPolicy::Flee);
        if (actor.policy == sim::WardPolicy::Flee) {
            ++fleeing;
        }
    }
    CHECK(fleeing == static_cast<std::int32_t>(seen.size()));
    // The census sees the same street scattering that the ids do.
    CHECK(people.census().byPolicy[static_cast<std::size_t>(sim::WardPolicy::Flee)] >= fleeing);
}

// ---------------------------------------------------------------------------
// out of sight, another band, beyond the radius, a beast, the Watch
// ---------------------------------------------------------------------------

TEST_CASE("out of sight, another band, or beyond the radius does nothing") {
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();

    const Spot spot = findSpot(people, sim::kAlarmRadiusKill, 3);
    REQUIRE(spot.found);
    INFO("alarm at ", spot.x, ",", spot.y, ",z", spot.band, " seen=", spot.seen,
         " walled=", spot.walled, " otherBand=", spot.otherBand);

    // Everybody's Safety and its accumulator, before.
    std::vector<std::int32_t> safetyBefore;
    std::vector<std::int32_t> accumBefore;
    for (const sim::WardActor& actor : people.actors()) {
        safetyBefore.push_back(safetyOf(actor));
        accumBefore.push_back(actor.needAccum[static_cast<std::size_t>(sim::Need::Safety)]);
    }

    people.alarm(spot.x, spot.y, spot.band, sim::kAlarmRadiusKill, sim::AlarmSeverity::Kill);

    std::int32_t walledChecked = 0;
    std::int32_t otherBandChecked = 0;
    std::int32_t beyondChecked = 0;
    std::int32_t beastsChecked = 0;
    std::int32_t watchChecked = 0;
    for (const sim::WardActor& actor : people.actors()) {
        const auto index = static_cast<std::size_t>(actor.id);
        const bool inRange =
            std::max(std::abs(actor.x - spot.x), std::abs(actor.y - spot.y)) <=
            sim::kAlarmRadiusKill;
        if (wouldSee(actor, spot.x, spot.y, spot.band, sim::kAlarmRadiusKill)) {
            continue;  // the previous case's subject
        }
        INFO("actor ", actor.id, " ", sim::wardTypeName(actor.type), " at ", actor.x, ",",
             actor.y, ",z", actor.band);
        // WHATEVER the reason it is not a witness, nothing moved: not the
        // reserve and not its accumulator.
        CHECK(safetyOf(actor) == safetyBefore[index]);
        CHECK(actor.needAccum[static_cast<std::size_t>(sim::Need::Safety)] == accumBefore[index]);
        if (inRangeButWalled(actor, spot.x, spot.y, spot.band, sim::kAlarmRadiusKill)) {
            ++walledChecked;  // in range, same band, a wall between
        } else if (actor.visible() && sim::isPerson(actor.type) && actor.band != spot.band &&
                   inRange) {
            ++otherBandChecked;  // in range, a floor away
        } else if (actor.visible() && sim::isPerson(actor.type) && actor.band == spot.band &&
                   !inRange) {
            ++beyondChecked;  // same band, too far
        } else if (actor.visible() && !sim::isPerson(actor.type) && actor.band == spot.band &&
                   inRange) {
            ++beastsChecked;  // a dog on the same street, not asked
        } else if (actor.visible() && actor.type == sim::WardType::MilitiaWatch &&
                   actor.band == spot.band && inRange) {
            ++watchChecked;  // the Watch, holding
        }
    }
    // Each clause was actually exercised by the district, not vacuously
    // satisfied by an empty class. The band clause is asked only where the
    // spot search found a body to ask it of; the others are always there.
    CHECK(walledChecked >= 1);
    CHECK(beyondChecked >= 1);
    if (spot.otherBand >= 1) {
        CHECK(otherBandChecked >= 1);
    }
    MESSAGE("untouched: walled=", walledChecked, " otherBand=", otherBandChecked,
            " beyond=", beyondChecked, " beasts=", beastsChecked, " watch=", watchChecked);

    // And one tick on, none of them is fleeing.
    own->run(1);
    for (const sim::WardActor& actor : people.actors()) {
        if (wouldSee(actor, spot.x, spot.y, spot.band, sim::kAlarmRadiusKill) ||
            !sim::isPerson(actor.type)) {
            continue;
        }
        INFO("actor ", actor.id, " ", sim::wardTypeName(actor.type));
        CHECK(actor.policy != sim::WardPolicy::Flee);
    }
}

TEST_CASE("the Watch holds where the crowd runs") {
    // Asked directly rather than only in passing above: a watchman in plain
    // sight of a killing keeps his Safety and his beat. Respond is 9b's, after
    // the justice build; a watchman who fled would be worse than one who
    // stands.
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();

    // Any watchman on walking ground: the alarm goes off at his own tile, the
    // one place a line of sight cannot be in doubt.
    const sim::WardActor* watchman = nullptr;
    for (const sim::WardActor& actor : people.actors()) {
        if (actor.visible() && actor.type == sim::WardType::MilitiaWatch &&
            people.onWalkingGround(actor.x, actor.y, actor.band)) {
            watchman = &actor;
            break;
        }
    }
    REQUIRE(watchman != nullptr);
    const std::int32_t before = safetyOf(*watchman);
    people.alarm(watchman->x, watchman->y, watchman->band, sim::kAlarmRadiusKill,
                 sim::AlarmSeverity::Kill);
    CHECK(safetyOf(*watchman) == before);
    own->run(1);
    CHECK(safetyOf(*watchman) >= sim::kNeedCritical);
    CHECK(watchman->policy != sim::WardPolicy::Flee);
}

TEST_CASE("the radius is the radius: one tile past it nobody hears") {
    const std::unique_ptr<WardRun> own = privateWard(kHour);
    own->run(kSettleTicks);
    sim::WardPopulation& people = own->people();

    const Spot spot = findSpot(people, sim::kAlarmRadiusKill, 3);
    REQUIRE(spot.found);
    // The nearest person who would see it at the kill radius, by distance.
    std::int32_t nearest = -1;
    std::int32_t nearestDistance = sim::kAlarmRadiusKill + 1;
    for (const sim::WardActor& actor : people.actors()) {
        if (!wouldSee(actor, spot.x, spot.y, spot.band, sim::kAlarmRadiusKill)) {
            continue;
        }
        const std::int32_t d = std::max(std::abs(actor.x - spot.x), std::abs(actor.y - spot.y));
        if (d > 0 && d < nearestDistance) {
            nearestDistance = d;
            nearest = actor.id;
        }
    }
    REQUIRE(nearest >= 0);
    // An alarm one tile SHORT of him frightens him not at all...
    const std::int32_t before = safetyOf(*people.byId(nearest));
    people.alarm(spot.x, spot.y, spot.band, nearestDistance - 1, sim::AlarmSeverity::Kill);
    CHECK(safetyOf(*people.byId(nearest)) == before);
    // ...and one that reaches him exactly does.
    people.alarm(spot.x, spot.y, spot.band, nearestDistance, sim::AlarmSeverity::Kill);
    CHECK(safetyOf(*people.byId(nearest)) == sim::kPanicSafetyKill);
}

// ---------------------------------------------------------------------------
// the panic recovers, and only the frightened pay for it
// ---------------------------------------------------------------------------

TEST_CASE("panic recovers, as long as the severity says and no longer") {
    // Two wards, one alarm each, the same spot: a blade and a killing. The
    // blade's crowd is back at work inside forty seconds; the killing's is
    // still running at fifty and back by two hundred. The arithmetic behind
    // the numbers is in kPanicRecoverPerTick's own comment.
    const std::unique_ptr<WardRun> steel = privateWard(kHour);
    const std::unique_ptr<WardRun> kill = privateWard(kHour);
    steel->run(kSettleTicks);
    kill->run(kSettleTicks);
    const Spot spot = findSpot(steel->people(), sim::kAlarmRadiusKill, 3);
    REQUIRE(spot.found);

    std::vector<std::int32_t> seen;
    for (const sim::WardActor& actor : steel->people().actors()) {
        if (wouldSee(actor, spot.x, spot.y, spot.band, sim::kAlarmRadiusKill)) {
            seen.push_back(actor.id);
        }
    }
    REQUIRE(seen.size() >= 3);

    steel->people().alarm(spot.x, spot.y, spot.band, sim::kAlarmRadiusKill,
                          sim::AlarmSeverity::Steel);
    kill->people().alarm(spot.x, spot.y, spot.band, sim::kAlarmRadiusKill,
                         sim::AlarmSeverity::Kill);
    for (const std::int32_t id : seen) {
        CHECK(safetyOf(*steel->people().byId(id)) == sim::kPanicSafetySteel);
        CHECK(safetyOf(*kill->people().byId(id)) == sim::kPanicSafetyKill);
    }

    // Twenty seconds on, both crowds are still running.
    steel->run(20);
    kill->run(20);
    for (const std::int32_t id : seen) {
        INFO("actor ", id, " at 20");
        CHECK(safetyOf(*steel->people().byId(id)) < sim::kNeedCritical);
        CHECK(steel->people().byId(id)->policy == sim::WardPolicy::Flee);
        CHECK(safetyOf(*kill->people().byId(id)) < sim::kNeedCritical);
        CHECK(kill->people().byId(id)->policy == sim::WardPolicy::Flee);
    }
    // Forty: the blade's crowd is over the gate and back to whatever it was
    // doing; the killing's is still running, and is at fifty.
    steel->run(20);
    kill->run(30);
    for (const std::int32_t id : seen) {
        INFO("actor ", id, " steel at 40 / kill at 50");
        CHECK(safetyOf(*steel->people().byId(id)) >= sim::kNeedCritical);
        CHECK(steel->people().byId(id)->policy != sim::WardPolicy::Flee);
        CHECK(safetyOf(*kill->people().byId(id)) < sim::kNeedCritical);
        CHECK(kill->people().byId(id)->policy == sim::WardPolicy::Flee);
    }
    // Two hundred: everybody is back, and the reserve climbed only as far as
    // the panic rate took it -- past the gate it is the raws' own half a
    // point a second again, so nobody is anywhere near full.
    kill->run(150);
    for (const std::int32_t id : seen) {
        INFO("actor ", id, " kill at 200");
        CHECK(safetyOf(*kill->people().byId(id)) >= sim::kNeedCritical);
        CHECK(safetyOf(*kill->people().byId(id)) < sim::kNeedLow);
        CHECK(kill->people().byId(id)->policy != sim::WardPolicy::Flee);
    }
}

TEST_CASE("only the frightened pay the panic rate") {
    // THE BASELINE ARGUMENT IN MINIATURE. The panic rate is added to a
    // person's Safety recovery only while that person is under the FLEE gate,
    // and nothing but alarm() puts a person under it. So after an alarm,
    // everybody the alarm did NOT reach carries Safety and its accumulator
    // byte-for-byte as the un-alarmed twin does -- which is exactly why the
    // gate's own no-player run, which alarms nobody, is expected to hash
    // unchanged. Positions may differ (the frightened shove and block the
    // rest); the Safety arithmetic may not.
    const std::unique_ptr<WardRun> quiet = privateWard(kHour);
    const std::unique_ptr<WardRun> alarmed = privateWard(kHour);
    quiet->run(kSettleTicks);
    alarmed->run(kSettleTicks);
    const Spot spot = findSpot(alarmed->people(), sim::kAlarmRadiusKill, 3);
    REQUIRE(spot.found);

    std::vector<bool> frightened(alarmed->people().actors().size(), false);
    for (const sim::WardActor& actor : alarmed->people().actors()) {
        frightened[static_cast<std::size_t>(actor.id)] =
            wouldSee(actor, spot.x, spot.y, spot.band, sim::kAlarmRadiusKill);
    }
    alarmed->people().alarm(spot.x, spot.y, spot.band, sim::kAlarmRadiusKill,
                            sim::AlarmSeverity::Kill);
    quiet->run(120);
    alarmed->run(120);

    std::int32_t compared = 0;
    for (const sim::WardActor& actor : alarmed->people().actors()) {
        if (frightened[static_cast<std::size_t>(actor.id)] || !sim::isPerson(actor.type)) {
            continue;  // the claim is about people; a mouse's rate is its own
        }
        const sim::WardActor& twin = *quiet->people().byId(actor.id);
        INFO("actor ", actor.id, " ", sim::wardTypeName(actor.type));
        CHECK(safetyOf(actor) == safetyOf(twin));
        CHECK(actor.needAccum[static_cast<std::size_t>(sim::Need::Safety)] ==
              twin.needAccum[static_cast<std::size_t>(sim::Need::Safety)]);
        ++compared;
    }
    CHECK(compared > 500);
}

// ---------------------------------------------------------------------------
// the twin run
// ---------------------------------------------------------------------------

TEST_CASE("the population twin-run stays byte-identical run-to-run after an alarm") {
    // Two wards from one seed, the same alarm at the same tick, ticked on, and
    // their whole hashed state compared at three readings -- the moment of the
    // alarm, mid-panic, and after everybody is back. And a THIRD ward nobody
    // frightened, which must differ: a twin run that agreed because nothing
    // happened would prove nothing about the alarm.
    const std::unique_ptr<WardRun> a = privateWard(kHour);
    const std::unique_ptr<WardRun> b = privateWard(kHour);
    const std::unique_ptr<WardRun> quiet = privateWard(kHour);
    a->run(kSettleTicks);
    b->run(kSettleTicks);
    quiet->run(kSettleTicks);
    REQUIRE(digestOf(a->people()) == digestOf(b->people()));
    REQUIRE(digestOf(a->people()) == digestOf(quiet->people()));

    const Spot spot = findSpot(a->people(), sim::kAlarmRadiusKill, 3);
    REQUIRE(spot.found);
    const std::int32_t sawA =
        a->people().alarm(spot.x, spot.y, spot.band, sim::kAlarmRadiusKill, sim::AlarmSeverity::Kill);
    const std::int32_t sawB =
        b->people().alarm(spot.x, spot.y, spot.band, sim::kAlarmRadiusKill, sim::AlarmSeverity::Kill);
    CHECK(sawA == sawB);
    CHECK(sawA >= 3);
    CHECK(digestOf(a->people()) == digestOf(b->people()));
    CHECK(digestOf(a->people()) != digestOf(quiet->people()));

    // Mid-panic: the street is scattering, drawing its flee steps, shoving.
    a->run(30);
    b->run(30);
    quiet->run(30);
    CHECK(digestOf(a->people()) == digestOf(b->people()));
    CHECK(digestOf(a->people()) != digestOf(quiet->people()));
    CHECK(a->people().census().byPolicy[static_cast<std::size_t>(sim::WardPolicy::Flee)] >= 3);
    CHECK(quiet->people().census().byPolicy[static_cast<std::size_t>(sim::WardPolicy::Flee)] == 0);

    // And long after: everybody back, and the two runs still one district.
    a->run(300);
    b->run(300);
    CHECK(digestOf(a->people()) == digestOf(b->people()));
    CHECK(a->people().reportLine() == b->people().reportLine());
    CHECK(a->people().census().byPolicy[static_cast<std::size_t>(sim::WardPolicy::Flee)] == 0);
}

// ---------------------------------------------------------------------------
// through the client
// ---------------------------------------------------------------------------

TEST_CASE("steel up on the Tarwalk scatters the street through the client's one call site") {
    // The session's own ward is the same bake as a privateWard at the same
    // hour and seed (Session builds it from config.worldSeed, "GRANADAD", the
    // fixture's kSeed), so the spot is chosen on a ward of this case's own and
    // the session is spawned on it. At the blade's radius the walled clause is
    // not asked for -- six tiles of open Tarwalk rarely has a wall in it --
    // the out-of-sight rule has its own cases above.
    const std::unique_ptr<WardRun> twin = privateWard(kHour);
    const Spot spot = findSpot(twin->people(), sim::kAlarmRadiusSteel, 2, 0);
    REQUIRE_MESSAGE(spot.found, "no tile on the walking ground at eight has two people in "
                                "sight within the blade's radius");
    INFO("standing at ", spot.x, ",", spot.y, ",z", spot.band, " seen=", spot.seen);

    render::Session session(streetAt(kHour, spot.x, spot.y, spot.band));
    REQUIRE(session.body().tileX() == spot.x);
    REQUIRE(session.body().tileY() == spot.y);
    const std::int32_t px = session.body().tileX();
    const std::int32_t py = session.body().tileY();
    const std::int32_t pband = session.body().band();

    // A second on the street with the hands down: nobody is frightened. The
    // channel is dead until steel is up, exactly as it was before this build.
    stepOneTick(session);
    for (const sim::WardActor& actor : session.people().actors()) {
        if (sim::isPerson(actor.type) && actor.visible()) {
            CHECK(safetyOf(actor) >= sim::kNeedCritical);
        }
    }

    // STEEL UP: an edged weapon in the hand and the hands raised by a guard
    // press -- no blow thrown, nobody struck. The stance alone is the cause,
    // for the street exactly as it is for the Watch (Tavern::violenceInView).
    session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Subdue);
    raiseByGuard(session);
    REQUIRE(session.tavern().playerWeapon() >= sim::kFirstLethalWeapon);

    // The next ward tick carries the alarm, at the player's tile, at the
    // blade's radius: everybody the rule says can see him is at the floor,
    // and nobody else moved a point.
    stepOneTick(session);
    std::vector<std::int32_t> seen;
    for (const sim::WardActor& actor : session.people().actors()) {
        if (!sim::isPerson(actor.type) || !actor.visible()) {
            continue;
        }
        INFO("actor ", actor.id, " ", sim::wardTypeName(actor.type), " at ", actor.x, ",",
             actor.y, ",z", actor.band);
        if (wouldSee(actor, px, py, pband, sim::kAlarmRadiusSteel)) {
            seen.push_back(actor.id);
            CHECK(safetyOf(actor) == sim::kPanicSafetySteel);
        } else {
            CHECK(safetyOf(actor) >= sim::kNeedCritical);
        }
    }
    REQUIRE(seen.size() >= 2);

    // The tick after: FLEE has them, and the blade is still up so the bubble
    // is re-asserted -- a man walking the Tarwalk with steel out is given room
    // the whole way.
    stepOneTick(session);
    REQUIRE(session.tavern().playerHandsUp());
    for (const std::int32_t id : seen) {
        const sim::WardActor& actor = *session.people().byId(id);
        INFO("actor ", id, " ", sim::wardTypeName(actor.type), " policy ",
             sim::wardPolicyName(actor.policy));
        REQUIRE(actor.need(sim::Need::Hunger) >= sim::kNeedCritical);
        CHECK(actor.policy == sim::WardPolicy::Flee);
        CHECK(safetyOf(actor) < sim::kNeedCritical);
    }
    CHECK(session.people().census().byPolicy[static_cast<std::size_t>(sim::WardPolicy::Flee)] >=
          static_cast<std::int32_t>(seen.size()));

    // Blade down, and a minute later the street is back at work: the blade's
    // panic is half a minute (kPanicSafetySteel), and with the hands lowered
    // nothing re-asserts it.
    session.tavern().lowerPlayerHands();
    REQUIRE_FALSE(session.tavern().playerHandsUp());
    session.stepMany(sim::MoveInput{}, 60 * sim::kStepsPerSecond);
    for (const std::int32_t id : seen) {
        const sim::WardActor& actor = *session.people().byId(id);
        INFO("actor ", id, " ", sim::wardTypeName(actor.type), " after a minute");
        CHECK(safetyOf(actor) >= sim::kNeedCritical);
        CHECK(actor.policy != sim::WardPolicy::Flee);
    }
    CHECK(session.people().census().byPolicy[static_cast<std::size_t>(sim::WardPolicy::Flee)] == 0);
}
