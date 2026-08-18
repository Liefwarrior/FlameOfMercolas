// THE WARD, AND WHETHER IT IS ACTUALLY ALIVE.
//
// The acceptance for #78 was written by the owner and it explicitly rules OUT
// the obvious check. "A frame must contain actors" is not a gate here, because
// a cellar or a back lane at four in the morning is legitimately empty and a
// build that went red over that would be lying about what it had proved.
//
// So these cases assert the POPULATION and never the pixels:
//
//   * the roll exists, ticks, and is the size the Java build's was
//   * the per-type counts, so a bake that quietly lost every urchin is red
//   * the Java build's own behavioural bars -- serf starvation at or below 5%,
//     no guard pile-ups, per-kind item conservation exact
//   * a small set of expectations each naming a PLACE, an HOUR and a REASON
//
// The last of those is the interesting one and it is what a player would
// actually check. The Tarwalk is walked at eight because the day trades start
// at seven and it is the road they walk to get there. The Watch is on the
// Ropewynd at two because the night roster runs six to six and the Ropewynd is
// the road every compound gate opens onto. The Gilded Gull is full and loud at
// ten because that is when the Gull is full and loud.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/docks_signs.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/path_finder.hpp"
#include "granadad/sim/ward_actors.hpp"
#include "support/ward_fixture.hpp"

using namespace granadad;
using granadad::testfix::privateWard;
using granadad::testfix::sharedTiles;
using granadad::testfix::wardAt;

// ---------------------------------------------------------------------------
// THE TICK COUNTS IN THIS FILE ASCEND, PER HOUR -- AS A SAVING, NOT AS A RULE
// ---------------------------------------------------------------------------
// wardAt(hour, ticks) hands out ONE ward per hour and ticks it forward. Three
// cases used to bake three wards at two in the morning and tick them 600, 900
// and 1,200 times -- 2,700 ticks of the most expensive hour in the day (31 ms a
// tick, measured) to ask three questions about the same district.
//
// They now read one ward. Writing the shorter reading first is what makes that
// a saving; wardAt is a pure function of (hour, ticks) either way and rebuilds
// rather than answering with a district nobody asked for, so nothing here is
// order-dependent. A case that MUTATES takes a privateWard() and pays for its
// own bake.
//
//   hour  1   1200
//   hour  2    600 -> 600 -> 900
//   hour  6    300, 600 ... 3600 -> 14400
//   hour  7    900
//   hour  8      0 ->   0 ->   0 -> 600 -> 600
//   hour 12   1200
//   hour 14    600
//   hour 20    600

TEST_CASE("the ward has a roll, and it is the size the Java build's was") {
    const sim::WardPopulation& run = wardAt(8, 0);
    const sim::WardCensus roll = run.census();

    // 692 IS THE NUMBER, and it comes from the Java build's own baseline:
    // docs/BASELINE-WORLD-HASH.md line 11 reads `souls: 692`. This build splits
    // it between two systems that both already existed -- the Gilded Gull's
    // fourteen are the Tavern's and are spawned by nobody here -- so the check
    // is on the SUM and the population is the rest of it.
    CHECK(roll.total > 600);
    CHECK(roll.total < 760);
    CHECK(roll.alive == roll.total);  // nobody has starved before the first tick
    CHECK(roll.starved == 0);

    // People and beasts counted apart, because a roll padded out with mice
    // would satisfy a total and not a district.
    CHECK(roll.people > 550);
    CHECK(roll.beasts > 30);
    CHECK(roll.people + roll.beasts == roll.total);
}

TEST_CASE("every kind of person the owner named is actually in the ward") {
    // The complaint was specific: "there should be people like guards urchins
    // thieves taverns etc". Each of those is a count here, so a bake that
    // quietly stops producing one of them is a red build and not a shrug.
    const sim::WardPopulation& run = wardAt(8, 0);
    const sim::WardCensus roll = run.census();
    const auto count = [&](sim::WardType type) {
        return roll.byType[static_cast<std::size_t>(type)];
    };

    CHECK(count(sim::WardType::MilitiaWatch) >= 19);   // the gazetteer's garrison
    CHECK(count(sim::WardType::Urchin) >= 15);
    CHECK(count(sim::WardType::Thief) >= 8);
    CHECK(count(sim::WardType::Wastrel) >= 25);
    CHECK(count(sim::WardType::Serf) >= 250);
    CHECK(count(sim::WardType::Shopkeeper) >= 25);
    CHECK(count(sim::WardType::Sailor) >= 10);
    CHECK(count(sim::WardType::Fisher) >= 10);
    CHECK(count(sim::WardType::Carter) == 4);
    // Exactly one priest. DOCKS-GAZETTEER section 4: "Presence: exactly one --
    // Father Maell at the Mission. More priests exist off-map."
    CHECK(count(sim::WardType::PriestOfTheFlame) == 1);
    CHECK(count(sim::WardType::DiscipleOfTheFlame) == 4);
    // And the beasts.
    CHECK(count(sim::WardType::Cat) == 8);
    CHECK(count(sim::WardType::Mouse) == 32);
    CHECK(count(sim::WardType::Stray) == 5);
    CHECK(count(sim::WardType::Dog) == 8);

    // The night roster is exactly seven and every one of them works the dark.
    REQUIRE(run.nightRoster().size() == 7);
    for (const std::int32_t id : run.nightRoster()) {
        const sim::WardActor& guard = run.actors()[static_cast<std::size_t>(id)];
        CHECK(guard.type == sim::WardType::MilitiaWatch);
        CHECK(guard.job == sim::WardJob::NightWatch);
        CHECK(sim::wardJobParams(guard.job).worksThroughTheNight);
    }
}

TEST_CASE("everybody in the ward is standing somewhere a body can stand") {
    // An authored anchor is a MARKER and a marker can sit on a counter, inside
    // a rack, or one tile into a wall. Every one of them is snapped outward to
    // real ground at the bake -- and this is what makes a future edit to
    // docks_surface.tmx that seals a shed a red build instead of a shopkeeper
    // standing in the harbour.
    const sim::WardPopulation& run = wardAt(8, 0);
    for (const sim::WardActor& actor : run.actors()) {
        INFO("actor ", actor.id, " type ", sim::wardTypeName(actor.type), " at ", actor.x, ',',
             actor.y, ",z", actor.band);
        CHECK(sharedTiles().standable(actor.x, actor.y, actor.band));
        CHECK(sharedTiles().standable(actor.homeX, actor.homeY, actor.homeBand));
        CHECK(sharedTiles().standable(actor.anchorX, actor.anchorY, actor.anchorBand));
    }
}

TEST_CASE("one body per square, and it holds while six hundred of them walk") {
    // The owner's rule, verbatim: "no more stacking actors, only one per
    // square". It is enforced at the movement commit exactly like a wall, and
    // the shove is what dissolves the deadlocks that produces.
    const sim::WardPopulation& run = wardAt(7, 900);
    std::vector<std::uint64_t> cells;
    for (const sim::WardActor& actor : run.actors()) {
        // #80: and a mouse a cat has taken off the board holds no square
        // either, exactly like a corpse. visible() is the one place that is
        // answered; see WardActor::visible.
        if (!actor.visible()) {
            continue;
        }
        cells.push_back((static_cast<std::uint64_t>(actor.band) << 40) |
                        (static_cast<std::uint64_t>(actor.y) << 20) |
                        static_cast<std::uint64_t>(actor.x));
    }
    std::sort(cells.begin(), cells.end());
    CHECK(std::adjacent_find(cells.begin(), cells.end()) == cells.end());
}

TEST_CASE("no guard pile-ups: a watchman never shoves a watchman on duty") {
    // The Java build's own etiquette gate, and the reason the patrol yield is
    // the resolution mechanism instead of two watchmen wrestling in a doorway
    // for the rest of the night.
    //
    // THE MEASURE IS THE SHOVE AND NOT THE CROWD, and the first version of this
    // case got that wrong. It counted watchmen standing within a tile of each
    // other and failed at five -- which is not a pile-up, it is a garrison
    // asleep in its own bunkroom, and the two look identical from a distance.
    // What a pile-up actually is, is guards laying hands on each other, and
    // that is counted directly. It is zero by construction; deleting the gate
    // in tryPush makes it non-zero and turns this red.
    const sim::WardPopulation& run = wardAt(1, 1200);  // the night roster is out
    CHECK(run.watchOnWatchShoves() == 0);
    // And the crowd is still measured, at a bar a bunkroom can meet and a
    // wrestling match cannot: a whole 3x3 of watchmen and nothing worse.
    INFO("worst watch crowd over twenty minutes of ward time");
    CHECK(run.worstJam() <= 9);
}

TEST_CASE("per-kind item conservation is exact, tick after tick") {
    // MINTED MINUS CONSUMED EQUALS HELD, per kind, at every tick. A simulation
    // that can quietly create or destroy a loaf will balance its own economy by
    // accident and the balance will mean nothing.
    for (int block = 0; block < 12; ++block) {
        const sim::WardPopulation& run = wardAt(6, (block + 1) * 300);
        const sim::WardLedger& ledger = run.ledger();
        INFO("after ", (block + 1) * 300, " ticks");
        CHECK(ledger.foodMinted - ledger.foodEaten == run.foodHeld());
        // And the same for coin: every royal in a purse was minted by a day's
        // work and every one spent went across a counter.
        std::int64_t purses = 0;
        for (const sim::WardActor& actor : run.actors()) {
            purses += actor.coin;
        }
        CHECK(ledger.coinMinted - ledger.coinSunk == purses);
    }
}

TEST_CASE("the ward feeds itself: nobody is on the road to starving after a day") {
    // THE BALANCE BAR IS THE JAVA BUILD'S OWN: serf starvation at or below 5%.
    //
    // THIS CASE ASSERTS THE BAR WITHOUT WAITING FOR IT, and the substitution is
    // worth stating rather than hiding. Starvation takes three days --
    // kStarvationGraceSeconds -- so a case that waited for a death would have
    // to tick 260,000 seconds against six hundred and seventy-eight bodies on
    // every build, which is minutes of ctest for one assertion.
    //
    // A body cannot starve without first spending three days at a hunger of
    // ZERO. So the equivalent claim, one day in, is that the ward is not on the
    // road there: nobody's reserve has collapsed, the great majority are fed,
    // and the food ledger shows real consumption rather than a machine nobody
    // used. The long-run version is the gate's business
    // (granadad-twin-run-gate-population) and the ward soak's, not a unit
    // case's.
    // SIX IN THE MORNING PLUS EIGHT HOURS OF WARD TIME. Long enough that the
    // day trades have opened, everybody has walked to a post, most reserves
    // have crossed the LOW line at least once and the larders have been drawn
    // on. Short enough that it runs in a Debug build on every gate -- the host
    // check compiles with -DCMAKE_BUILD_TYPE=Debug and six hundred and
    // seventy-eight bodies is not a number you can tick a whole day of there
    // and still have a build somebody will run.
    const sim::WardPopulation& run = wardAt(6, 4 * 3600);
    const sim::WardCensus roll = run.census();
    REQUIRE(roll.serfs > 200);

    // Nobody has died yet, because nobody can have.
    CHECK(roll.starved == 0);

    std::int32_t empty = 0;
    std::int32_t labouring = 0;
    for (const sim::WardActor& actor : run.actors()) {
        if (!sim::isPerson(actor.type)) {
            continue;
        }
        ++labouring;
        if (actor.need(sim::Need::Hunger) == 0) {
            ++empty;
        }
    }
    INFO(empty, " of ", labouring, " people were at an empty reserve after a day");
    // Five per cent, which is the Java build's own bar, applied to the state
    // that PRECEDES a death rather than to the death.
    CHECK(empty * 100 <= labouring * 5);

    // And the ward is not surviving on a mountain of surplus either: an economy
    // with nothing scarce in it is not balanced, it is switched off.
    CHECK(run.ledger().foodEaten > 100);
    CHECK(run.ledger().foodMinted > run.ledger().foodEaten);
}

TEST_CASE("the ward keeps its hours: everybody is somewhere for a reason") {
    // Four hours, four claims, each naming a PLACE, an HOUR and a REASON. These
    // are the expectations a player could check by standing in the street.
    using namespace granadad::sim::wardplaces;

    SUBCASE("the Tarwalk is walked at eight, because the day trades start at seven") {
        const sim::WardPopulation& run = wardAt(8, 600);
        const std::int32_t onTheRoad = run.countIn(
            kTarwalkX0, kTarwalkY0, kTarwalkX1, kTarwalkY1, sim::docks::kBandQuayside);
        INFO("bodies on the Tarwalk at 08:00: ", onTheRoad);
        CHECK(onTheRoad >= 8);
    }

    SUBCASE("the Watch is on the Ropewynd at two, because the night roster runs six to six") {
        const sim::WardPopulation& run = wardAt(2, 600);
        const std::int32_t guards =
            run.countIn(kRopewyndX0, kRopewyndY0, kRopewyndX1, kRopewyndY1,
                                sim::docks::kBandQuayside, sim::WardType::MilitiaWatch);
        INFO("watchmen on the Ropewynd at 02:00: ", guards);
        CHECK(guards >= 1);

        // AND THE DAY BEAT IS IN BED, which is the other half of the claim. A
        // district where every watchman is out at every hour is not a district
        // with a night roster, it is a district with no schedule at all.
        //
        // "IN BED" IS A ROOM AND NOT A TILE, and the first version of this case
        // got that wrong too: it asked for atHome(), which is exact tile
        // equality, and under one-per-square a watchman whose own bunk is
        // occupied by a family member stands beside it. Fourteen of twenty-four
        // were home and not on their bed. Two tiles is the room.
        std::int32_t dayBeatIndoors = 0;
        std::int32_t dayBeatTotal = 0;
        for (const sim::WardActor& actor : run.actors()) {
            if (actor.type != sim::WardType::MilitiaWatch || actor.job == sim::WardJob::NightWatch) {
                continue;
            }
            ++dayBeatTotal;
            if (actor.band == actor.homeBand &&
                std::max(std::abs(actor.x - actor.homeX), std::abs(actor.y - actor.homeY)) <= 2) {
                ++dayBeatIndoors;
            }
        }
        REQUIRE(dayBeatTotal > 0);
        INFO(dayBeatIndoors, " of ", dayBeatTotal, " off-roster watchmen were at their own bunk");
        CHECK(dayBeatIndoors * 4 >= dayBeatTotal * 3);
    }

    SUBCASE("the Gilded Gull is full and loud at ten, because that is when it is") {
        // ONE ROSTER, TWO SOURCES, and this is the case that says so. K03's
        // fourteen are the Tavern's and the population deliberately spawns
        // nobody inside its walls -- so "the Gull is full at ten" is asked of
        // the Tavern, and "the ward is on the street at ten" is asked of the
        // population, and the two numbers add up to the district.
        render::SessionConfig config;
        config.width = 64;
        config.height = 36;
        config.timeOfDay = 22 * 3600;
        config.timeOfDayGiven = true;
        render::Session session(config);
        INFO("in the Gull at 22:00: ", session.tavern().presentCount(), ", noise ",
             session.tavern().noise());
        CHECK(session.tavern().presentCount() >= 8);
        CHECK(session.tavern().noise() > 0);

        // And nobody from the ward's own roll is standing inside it. The Gull's
        // footprint is docks::kPlaces[0] and the roster refuses it outright.
        const std::int32_t intruders =
            session.people().countIn(146, 66, 160, 79, sim::docks::kBandQuayside);
        INFO(intruders, " ward bodies were inside the Gull's walls");
        CHECK(intruders == 0);
    }

    SUBCASE("the district is never empty, which is the whole of the complaint") {
        // At every one of the four hours the acceptance names, somebody is
        // outdoors on the quayside band. This is the closest thing to "the
        // street is not dead" that can be stated without asserting pixels.
        //
        // TEN MINUTES OF WARD TIME AND NOT FIVE. It read at 300 ticks before
        // #81; two and eight are read at 600 by the subcases above, and the
        // shared ward only moves forward, so reading all four at the same 600
        // costs two hours of the day nothing at all. It is the same claim with
        // longer for the district to disperse into it, which if anything is the
        // harder version.
        for (const std::int32_t hour : {2, 8, 14, 20}) {
            const sim::WardPopulation& run = wardAt(hour, 600);
            const std::int32_t out = run.countIn(
                kDistrictX0, kDistrictY0, kDistrictX1, kDistrictY1, sim::docks::kBandQuayside);
            INFO("bodies on the quayside band at ", hour, ":00 -- ", out);
            CHECK(out >= 40);
        }
    }
}

TEST_CASE("a rostered guard on the night beat does not oscillate on its own bunk") {
    // THE OFF-SHIFT BUG, PORTED WITH ITS FIX.
    //
    // RETURN_HOME's night term is priced at 385, above the entire job band. A
    // watchman rostered for the dark and standing anywhere but its bunk is
    // therefore dragged home every tick and shoved back out the next -- the
    // Java build measured 1,500 RETURN_HOME/GOAL_PURSUE flips in the worst
    // 3,000-tick night window before worksThroughTheNight existed.
    //
    // So: at two in the morning the roster is PURSUING and not flipping.
    const sim::WardPopulation& run = wardAt(2, 900);
    std::int32_t pursuing = 0;
    std::int32_t goingHome = 0;
    for (const std::int32_t id : run.nightRoster()) {
        const sim::WardActor& guard = run.actors()[static_cast<std::size_t>(id)];
        if (guard.policy == sim::WardPolicy::Pursue) {
            ++pursuing;
        }
        if (guard.policy == sim::WardPolicy::ReturnHome) {
            ++goingHome;
        }
    }
    INFO(pursuing, " of the roster pursuing, ", goingHome, " heading home at 02:00");
    CHECK(pursuing >= 5);
    CHECK(goingHome == 0);

    // AND THE FIX'S OTHER HALF: off shift, the same job walks its body home.
    //
    // THE ASSERTION HERE IS ABOUT WHERE THEY ARE AND NOT ABOUT WHICH POLICY WON,
    // and the first version of this case had that backwards. Off shift a
    // rostered guard's policy is still PURSUE -- pursueOffShiftHome IS a pursue,
    // that is the whole design: the job walks its own body home instead of
    // handing the problem to RETURN_HOME and letting the two argue about it
    // every tick. What matters is that the guard ends up at its bunk and not on
    // its beat, and that it is not paid for the walk.
    const sim::WardPopulation& day = wardAt(12, 1200);
    std::int32_t home = 0;
    for (const std::int32_t id : day.nightRoster()) {
        const sim::WardActor& guard = day.actors()[static_cast<std::size_t>(id)];
        INFO("rostered guard ", id, " at noon, at ", guard.x, ',', guard.y, " home ", guard.homeX,
             ',', guard.homeY);
        // Off shift the goal target is CLEARED and never set to the bed. The
        // first Java draft cached the home cell as the leg's corner, and at the
        // dawn tick the beat found itself already standing on its own "corner"
        // and awarded a full unit of duty and a full unit of skill for having
        // slept -- the dawn free-duty bug, and this is what refuses it.
        const bool bedParkedInTarget =
            guard.targetBand != 0 && guard.targetX == guard.homeX &&
            guard.targetY == guard.homeY && guard.targetBand == guard.homeBand;
        CHECK_FALSE(bedParkedInTarget);
        if (guard.band == guard.homeBand &&
            std::max(std::abs(guard.x - guard.homeX), std::abs(guard.y - guard.homeY)) <= 3) {
            ++home;
        }
    }
    INFO(home, " of the night roster were at their own bunk at noon");
    CHECK(home >= 5);
}

TEST_CASE("the ward's needs come out of the owner's raws, not out of a table here") {
    // Every rate is read from content/raws/actors and rescaled once, in one
    // function, from the Java's 24,000-tick day to this engine's 86,400. Getting
    // that wrong is not a balance issue, it is a starving ward -- unscaled, a
    // serf burns three and a half reserves a day.
    const sim::WardTypeTable table = sim::WardTypeTable::load(content::contentDir());
    REQUIRE(table.fromAuthoredRaws());
    // Eleven authored files, every one of them read, and none unread.
    CHECK(table.filesRead() == 11);

    // serf.json says hunger decays at 1000 per kilotick against a 24,000-tick
    // day, which is 24,000 points of appetite a day. It has to still be 24,000
    // points a day here.
    const sim::WardTypeStats& serf = table[sim::WardType::Serf];
    const std::int64_t perDay =
        static_cast<std::int64_t>(serf.needs[0].decayPerKilotick) * sim::kSecondsPerDay / 1000;
    INFO("a serf's daily appetite: ", perDay);
    CHECK(perDay > 22000);
    CHECK(perDay < 26000);

    // And the raws-enforced invariant the Java loader checks in all four bands:
    // eating never scores below going to bed, or a starving body walks home to
    // die in its own bed rather than to the larder.
    CHECK(serf.seekFoodPriority >= serf.returnHomePriority);
}

TEST_CASE("no job in the ward can outscore going to bed") {
    // kJobPriorityMax is 299 and RETURN_HOME is 305. A job that could reach 305
    // would out-argue bedtime and the ward would never sleep. The table refuses
    // it at construction rather than clamping, because a clamp is a bug that
    // balances itself.
    for (std::size_t i = 0; i < sim::kWardJobCount; ++i) {
        const sim::JobParams& params = sim::wardJobParams(static_cast<sim::WardJob>(i));
        INFO("job ", sim::wardJobName(static_cast<sim::WardJob>(i)));
        CHECK(params.priority >= sim::kJobPriorityMin);
        CHECK(params.priority + params.rhythmBonus <= sim::kJobPriorityMax);
    }
    // Exactly one job works through the night, and it is the roster's.
    std::int32_t nightJobs = 0;
    for (std::size_t i = 0; i < sim::kWardJobCount; ++i) {
        const sim::WardJob job = static_cast<sim::WardJob>(i);
        if (job == sim::WardJob::Wander) {
            continue;  // a beast keeps no hours and is not on a roster
        }
        if (sim::wardJobParams(job).worksThroughTheNight) {
            ++nightJobs;
        }
    }
    CHECK(nightJobs == 1);
    // The thief's window is open at two in the morning and the thief is STILL
    // not on a roster -- exempting "any job whose window is open" would have
    // silently rewritten the ward's nocturnal economy.
    CHECK(sim::wardJobParams(sim::WardJob::Thieving).inWindow(sim::hourOfDay(2)));
    CHECK_FALSE(sim::wardJobParams(sim::WardJob::Thieving).worksThroughTheNight);
}

TEST_CASE("a route never cuts a solid corner, and never comes back partial") {
    // The rule lives in three places -- PathFinder, Actor::tryStep and
    // PushMechanics -- and all three must agree. When two of them disagreed in
    // the Java build, a greedy step could squeeze diagonally between two wall
    // corners into a pocket whose every A* exit needs the cut A* refuses, and
    // the body was sealed in for good.
    sim::PathFinder finder(sharedTiles());
    std::vector<sim::PathStep> route;

    // A real walk across the district: the Tarwalk's west end to its east.
    const bool found = finder.find(sim::PathStep{62, 62, 19}, sim::PathStep{154, 65, 19}, 1, route);
    REQUIRE(found);
    REQUIRE_FALSE(route.empty());
    CHECK(route.back().x == 154);
    CHECK(route.back().y == 65);

    // Every hop is one tile, standable, and never a diagonal between two
    // corners.
    sim::PathStep at{62, 62, 19};
    for (const sim::PathStep& step : route) {
        const std::int32_t dx = step.x - at.x;
        const std::int32_t dy = step.y - at.y;
        INFO("hop from ", at.x, ',', at.y, ",z", at.band, " to ", step.x, ',', step.y, ",z",
             step.band);
        CHECK(std::max(std::abs(dx), std::abs(dy)) == 1);
        CHECK(std::abs(step.band - at.band) <= 1);
        CHECK(sharedTiles().standable(step.x, step.y, step.band));
        if (dx != 0 && dy != 0) {
            CHECK(sharedTiles().stepBand(at.x, at.y, at.band, step.x, at.y) !=
                  sim::TileQuery::kNoBand);
            CHECK(sharedTiles().stepBand(at.x, at.y, at.band, at.x, step.y) !=
                  sim::TileQuery::kNoBand);
        }
        at = step;
    }

    // An unreachable destination comes back EMPTY and never partial. A partial
    // route is worse than none: the body walks confidently to somewhere that is
    // not where it wanted to go, arrives, and re-asks, forever.
    std::vector<sim::PathStep> nowhere;
    CHECK_FALSE(finder.find(sim::PathStep{62, 62, 19}, sim::PathStep{0, 0, 19}, 1, nowhere));
    CHECK(nowhere.empty());
}

TEST_CASE("two actors asking the same question walk it differently") {
    // THE JITTER, and why it is a hash and not a draw. Without it a dozen
    // dockhands leaving the same compound for the same warehouse walk the same
    // tile sequence in single file. With it the tie-breaking landscape differs
    // per actor and the routes fan out -- and because it is a pure avalanche
    // over (salt, cell), the same actor asked twice gets the same answer and no
    // RNG stream is touched.
    sim::PathFinder finder(sharedTiles());
    std::vector<sim::PathStep> a;
    std::vector<sim::PathStep> b;
    std::vector<sim::PathStep> again;
    const sim::PathStep from{107, 66, 19};
    const sim::PathStep to{154, 65, 19};
    REQUIRE(finder.find(from, to, 1, a));
    REQUIRE(finder.find(from, to, 97, b));
    REQUIRE(finder.find(from, to, 1, again));
    CHECK(a == again);   // pure
    CHECK(a != b);       // and different per actor
    // Near-optimal all the same: the jitter is small against the 10/14 base, so
    // neither route is allowed to wander.
    CHECK(std::abs(static_cast<int>(a.size()) - static_cast<int>(b.size())) < 8);
}

TEST_CASE("the population is registered in the windowed game and keeps the hour") {
    // The whole point of #78: it is not a batch report, it is in the game. A
    // Session builds one, ticks it on the same engine the taproom runs on, and
    // carries its clock across every skip -- which is the S7 finding about the
    // compound roll, closed again for the people.
    render::SessionConfig config;
    config.width = 160;
    config.height = 90;
    config.timeOfDay = 8 * 3600;
    config.timeOfDayGiven = true;
    render::Session session(config);

    CHECK(session.people().census().alive > 600);
    CHECK(session.people().secondOfDay() == 8 * 3600);

    // And a skip moves the people, not just the clock on the wall.
    session.skipToHour(2);
    CHECK(session.people().secondOfDay() == 2 * 3600);
    std::int32_t outAtTwo = 0;
    for (const sim::WardActor& actor : session.people().actors()) {
        if (!actor.dead && actor.type == sim::WardType::MilitiaWatch &&
            actor.job == sim::WardJob::NightWatch) {
            ++outAtTwo;
        }
    }
    CHECK(outAtTwo == 7);

    // The ward's billboards are real, drawn figures, and there is one per body.
    const std::vector<render::SpriteInstance> figures = session.wardSprites();
    CHECK(figures.size() == static_cast<std::size_t>(session.people().census().alive));
    for (const render::SpriteInstance& sprite : figures) {
        CHECK(sprite.art != nullptr);
        CHECK(sprite.ward);
        CHECK(sprite.glow == 0.0F);
    }
}

// ---------------------------------------------------------------------------
// TIME-AND-TENURE BUILD -- whose ground, and the petition for it
// ---------------------------------------------------------------------------

TEST_CASE("the four compound footprints answer 'whose ground is this tile' and the streets answer nobody's") {
    // The survey's tier-3 correlation, wired for the one question it was ever
    // going to answer -- see docks_signs.hpp's plotIdUnder. Pins one interior
    // tile per compound against the sign footprints the generated table
    // carries, and the two grounds no compound may ever claim: the Tarwalk,
    // and the Gull.
    CHECK(sim::docks::plotIdUnder(70, 138) == "C1_QUAYWARD");
    CHECK(sim::docks::plotIdUnder(170, 110) == "C2_NETTERS");
    CHECK(sim::docks::plotIdUnder(130, 140) == "C3_SALTGATE");
    CHECK(sim::docks::plotIdUnder(210, 110) == "C4_GULLET");
    CHECK(sim::docks::plotIdUnder(sim::docks::kSpawnTileX, sim::docks::kSpawnTileY).empty());
    CHECK(sim::docks::plotIdUnder(153, 70).empty());  // inside the Gilded Gull
    // The Netter HOUSE is inside the Netters' footprint, and it is the
    // compound that answers -- a house sign never claims ground.
    CHECK(sim::docks::plotIdUnder(150, 100) == "C2_NETTERS");
}

TEST_CASE("a vacant charge is petitioned for in conversation, and the ROLL is what changes") {
    // THE FIRST CALLER Ward::petitionForCharge has ever had, driven the way a
    // player drives it: find somebody of the cloth answerable from compound
    // ground, stand beside them, talk, read the roll for the feet, and
    // petition the Flame for its open prize. Everything after the placement
    // is the game -- the real clergy topic list, the real verb, the real
    // purse.
    render::SessionConfig config;
    config.width = 160;
    config.height = 90;
    config.timeOfDay = 8 * 3600;
    config.timeOfDayGiven = true;
    render::Session session(config);

    const render::PetitionLineResult line = render::runPetitionLine(session, true);
    INFO("plot=", line.plotId, " speaker=", line.speaker, " roll=\"", line.rollLine,
         "\" answer=\"", line.petitionLine, "\"");
    REQUIRE(line.found);
    REQUIRE(line.opened);
    REQUIRE_FALSE(line.plotId.empty());
    // The reading is the register's own sentence for the ground STOOD ON,
    // spoken by the priest.
    CHECK_FALSE(line.rollLine.empty());

    // The petition LANDED: the Gullet -- the roll's one vacant charge -- now
    // names the player. Tenure moved through the real verb, purse and all,
    // wherever the conversation happened to stand.
    CHECK(line.becameDuke);
    CHECK_FALSE(line.petitionLine.empty());
    const std::int32_t gullet = session.ward().plotNamed("C4_GULLET");
    REQUIRE(gullet >= 0);
    CHECK(session.ward().plots()[static_cast<std::size_t>(gullet)].playerIsDuke);
    CHECK(session.ward().plots()[static_cast<std::size_t>(gullet)].tenure ==
          sim::Tenure::Charged);
    // The answer is READABLE where the player is looking: during a
    // conversation the alert row stands down, so the panel's own line is the
    // one surface that can carry it -- speakResolved's whole reason.
    CHECK(session.tavern().dialogue().lastLine().find("DEN DUKE") != std::string::npos);
    // And asking again is refused by the register, not by the UI: with no
    // vacant charge left on the roll, the petition row is gone from the
    // rebuilt list.
    const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
    for (const sim::Topic& topic : topics) {
        CHECK(topic.kind != sim::TopicKind::Petition);
    }
}

// ---------------------------------------------------------------------------
// AND THE FIXTURE ITSELF, BECAUSE IT GOT THIS WRONG ONCE
// ---------------------------------------------------------------------------

TEST_CASE("the shared ward is a pure function of its hour and its tick count") {
    // A CASE ABOUT tests/support/ward_fixture.hpp, and it exists because the
    // first version of wardAt() was not this.
    //
    // That version threw when asked for a district younger than one it had
    // already handed out, reasoning that ctest runs a file in --order-by=file
    // order so "the tick counts in a file ascend" is a property of the source.
    // Which is true of a FILE, and the cache is per PROCESS. Run the whole
    // binary in one go -- scripts/verify-windows.ps1 does exactly that -- and
    // the files interleave: this file leaves hour eight at 600 ticks and
    // test_ward_hunt.cpp then asks for it at zero. SIX CASES THREW, on Windows,
    // after the Linux half of the gate had gone green.
    //
    // A shared fixture that is order-dependent is the exact failure this whole
    // round was warned about, so the fixture is not order-dependent: it rebuilds
    // rather than rewinding, and what it returns depends on its two arguments
    // and on nothing else. This is the case that says so, and it does the
    // interleaving on purpose.
    const auto signature = [](const sim::WardPopulation& ward) {
        // FNV-1a over where everybody is, what they are doing and what they
        // hold. Not the world hash -- this is a test comparing two districts to
        // each other, and it wants to be sensitive to a single body having
        // taken one different step.
        std::uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](std::int32_t value) {
            h = (h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(value))) *
                1099511628211ull;
        };
        for (const sim::WardActor& actor : ward.actors()) {
            mix(actor.x);
            mix(actor.y);
            mix(actor.band);
            mix(actor.coin);
            mix(static_cast<std::int32_t>(actor.policy));
            mix(actor.need(sim::Need::Hunger));
        }
        return h;
    };

    // Eight in the morning, which is the cheapest hour the ward has.
    const std::uint64_t atSixty = signature(wardAt(8, 60));

    // Read the SAME hour later, which ticks the held ward forward...
    const std::uint64_t atThreeHundred = signature(wardAt(8, 300));
    CHECK(atThreeHundred != atSixty);  // or the ward is not running at all

    // ...and then earlier again, which cannot be answered by rewinding.
    CHECK(signature(wardAt(8, 60)) == atSixty);
    CHECK(signature(wardAt(8, 300)) == atThreeHundred);

    // And a privately baked ward, which shares nothing with the cache at all,
    // agrees with both. That is what makes the two readings above a claim about
    // the DISTRICT rather than a claim about the cache agreeing with itself.
    const std::unique_ptr<testfix::WardRun> own = privateWard(8);
    own->run(60);
    CHECK(signature(own->people()) == atSixty);
    own->run(240);
    CHECK(signature(own->people()) == atThreeHundred);
}

// ---------------------------------------------------------------------------
// #32: a home or work anchor with no path was a bound, not a broken map
// ---------------------------------------------------------------------------

TEST_CASE("every actor's home and its work anchor are mutually reachable") {
    // #32. Measured against the Java build's docks_surface world: ~9.4% of
    // ward actors had a home or work anchor in a region with no path to it.
    // That reading was never a WORLD-DATA defect -- bakeRoster() refuses to
    // place a HOME anywhere off the ward's own walking island unless it is a
    // proven-round-trip roof bed (the componentAt() guard in section 1 of
    // ward_roster.cpp), and every WORK anchor is snapped with
    // wantWalkable=true (the default) -- so both ends of every trip in the
    // roster are, by construction, on ground the UNBOUNDED flood fill calls
    // reachable. It was a PATHING defect: PathFinder's box (kSearchPadding)
    // and node budget (kSearchMaxNodes) were sized for a LOCAL replan, and
    // stepToward() asks the same bounded search to cover a body's entire
    // commute the moment it leaves home. About a sixteenth of the shipped
    // roster's home-to-work legs are long enough, through the compounds' own
    // turns, that the tight bound answered NO ROUTE for a body that had one --
    // the actor never walked to work or home again, and every retry cooldown
    // it re-asked the same unanswerable-by-construction question at the full
    // cost of a failed search. Both constants now carry real margin over what
    // this case measures on the shipped roster; see their comments in
    // path_finder.hpp.
    const sim::WardPopulation& run = wardAt(8, 0);
    sim::PathFinder finder(sharedTiles());
    std::int32_t total = 0;
    std::int32_t failed = 0;
    std::int32_t worstOk = 0;
    for (const sim::WardActor& actor : run.actors()) {
        if (actor.homeX == actor.anchorX && actor.homeY == actor.anchorY &&
            actor.homeBand == actor.anchorBand) {
            continue;  // trivially reached: a beast's home IS its post
        }
        ++total;
        std::vector<sim::PathStep> route;
        const sim::Gait gait = actor.homeOnTheRoof ? sim::Gait::Climb : sim::Gait::Walk;
        const bool ok = finder.find(sim::PathStep{actor.homeX, actor.homeY, actor.homeBand},
                                    sim::PathStep{actor.anchorX, actor.anchorY, actor.anchorBand},
                                    static_cast<std::uint32_t>(actor.id) + 1u, route, gait);
        if (!ok) {
            ++failed;
            MESSAGE("actor ", actor.id, " (", sim::wardTypeName(actor.type), ") home ",
                    actor.homeX, ",", actor.homeY, ",z", actor.homeBand, " -> anchor ",
                    actor.anchorX, ",", actor.anchorY, ",z", actor.anchorBand,
                    " NO ROUTE, expansions=", finder.lastExpansions());
        } else {
            worstOk = std::max(worstOk, finder.lastExpansions());
        }
    }
    MESSAGE("home<->anchor: ", failed, " / ", total, " unreachable (",
            total > 0 ? (100.0 * failed) / total : 0.0, "%), worst SUCCESSFUL search expanded ",
            worstOk, " nodes");
    CHECK(failed == 0);
}
