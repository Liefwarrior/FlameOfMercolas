// FAST TRAVEL -- the ward map's second commit verb, driven.
//
// THE OWNER'S ASK, this session: he read the ward map's cursor, named
// selection and FACE IT as a fast-travel screen, was told it is only a
// compass, and said plainly he wants the real thing -- Daggerfall's map
// travels. So the page grew a TRAVEL verb, and this file is what a headless
// case can prove of it:
//
//   * THE COST IS THE ROUTE, at the shipped walking pace, in integers -- the
//     pure travel* functions pinned against their own arithmetic, so a
//     retune moves a number a case reads rather than a feel nobody measured.
//   * THE REFUSALS each fire on their own predicate: carrying the courier's
//     man, the Watch closing, and the wait page's own "anywhere safe" list.
//   * A TRAVEL ACTUALLY TAKEN advances the clock by exactly the minutes the
//     verb restated, lands the body on standable ground, and arms the plate.
//   * IT IS DETERMINISTIC: two identical travels produce the identical
//     summary segment, byte for byte -- the twin-run check for a
//     session-scripted feature, test_case_line's own discipline.
//
// What a case cannot prove -- that the fade reads as a fade rather than a
// flash -- is the mandatory screenshot's job, and Ship's shot list drives it.

#include <doctest/doctest.h>

#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/map_view.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/human_scale.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/region_path.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

[[nodiscard]] SmokeRunResult play(SmokeRunConfig run) {
    run.session.contentDir = content::contentDir();
    if (!run.session.timeOfDayGiven) {
        run.session.timeOfDay = scriptedStartHour(run) * 3600;
    }
    run.steps = 0;
    run.stamp = false;
    return runSmoke(run);
}

/// The `| travel ...` segment -- the whole of a travel probe's visible state.
/// Comparing it between two runs is the twin-run check for a session-scripted
/// feature, exactly test_case_line's caseSegment().
[[nodiscard]] std::string travelSegment(const std::string& summary) {
    const std::string::size_type at = summary.find("| travel ");
    if (at == std::string::npos) {
        return {};
    }
    const std::string::size_type end = summary.find(" | ", at + 1);
    return summary.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

}  // namespace

// ---------------------------------------------------------------------------
// THE COST, in the sim's own integers
// ---------------------------------------------------------------------------

TEST_CASE("travelRouteUnits counts the router's own octile currency") {
    // Ten per orthogonal step, fourteen per diagonal -- path_finder.hpp's
    // kStepCost pair, recomputed over the route the router returned so the
    // charge is exactly the distance it chose.
    const sim::PathStep from{100, 100, 0};
    // Four steps due east: 40.
    CHECK(travelRouteUnits(from, {{101, 100, 0}, {102, 100, 0}, {103, 100, 0},
                                  {104, 100, 0}}) == 40);
    // Three steps on the diagonal: 42.
    CHECK(travelRouteUnits(from, {{101, 101, 0}, {102, 102, 0}, {103, 103, 0}}) == 42);
    // A dogleg -- two east then one up-and-over: 10 + 10 + 14.
    CHECK(travelRouteUnits(from, {{101, 100, 0}, {102, 100, 0}, {103, 101, 0}}) == 34);
    // A band change on the walk rides its step at no surcharge -- what the
    // router itself charges a stair under Gait::Walk.
    CHECK(travelRouteUnits(from, {{101, 100, 0}, {101, 100, 1}}) == 20);
    // An empty route is a body already there: nothing owed.
    CHECK(travelRouteUnits(from, {}) == 0);
}

TEST_CASE("travelWalkSeconds converts at the shipped walking pace, rounding up") {
    // A walk is never free and never rounds itself shorter. One octile unit is
    // a tenth of a tile; the body covers kWalkSpeed Q8 per step at
    // kStepsPerSecond steps a second.
    CHECK(travelWalkSeconds(0) == 0);
    CHECK(travelWalkSeconds(-5) == 0);
    // The conversion is seconds = ceil(units * 256 / (10 * kWalkSpeed *
    // kStepsPerSecond)). Assert it holds against that exact formula rather
    // than a magic number, so a retune of the pace moves both together.
    const auto expect = [](std::int32_t units) {
        const std::int64_t num = static_cast<std::int64_t>(units) * 256;
        const std::int64_t den = 10LL * sim::kWalkSpeed * sim::kStepsPerSecond;
        return static_cast<std::int32_t>((num + den - 1) / den);
    };
    for (std::int32_t units : {1, 10, 40, 350, 3500, 24000}) {
        CHECK(travelWalkSeconds(units) == expect(units));
    }
    // A whole tile (10 units) at ~1.48 m/s is a shade over half a second, so
    // it rounds up to one -- the smallest honest walk.
    CHECK(travelWalkSeconds(10) == 1);
}

TEST_CASE("travelClockMinutes floors at one whole minute and never lies") {
    // The verb's restated minutes are the minutes delivered -- the wait page's
    // own honesty rule. Anything up to a minute IS a minute; above it rounds up.
    CHECK(travelClockMinutes(0) == 1);
    CHECK(travelClockMinutes(1) == 1);
    CHECK(travelClockMinutes(60) == 1);
    CHECK(travelClockMinutes(61) == 2);
    CHECK(travelClockMinutes(120) == 2);
    CHECK(travelClockMinutes(121) == 3);
}

TEST_CASE("travelCostLabel restates the cost in the verb's own voice") {
    CHECK(travelCostLabel(1) == "1 MIN");
    CHECK(travelCostLabel(4) == "4 MIN");
    CHECK(travelCostLabel(59) == "59 MIN");
    // Sixty and up reads as the ruling's own phrase.
    CHECK(travelCostLabel(60) == "ABOUT AN HOUR");
    CHECK(travelCostLabel(75) == "ABOUT AN HOUR");
}

// ---------------------------------------------------------------------------
// THE PLAN, over the real baked Docks
// ---------------------------------------------------------------------------

TEST_CASE("the travel plan costs a real crossing off the district's own router") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 640;
    config.height = 360;
    // Mid-evening, safe, nothing carried, no Watch closing: the plan is live.
    config.timeOfDay = 20 * 3600;
    config.timeOfDayGiven = true;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 1);
    session.toggleDistrictMap();
    REQUIRE(session.districtMapOpen());

    REQUIRE(session.selectDistrictMapPlace("Mission of the Flame"));
    const Session::TravelPlan plan = session.districtMapTravelPlan();
    CHECK_FALSE(plan.standingIn);
    CHECK(plan.available);
    CHECK(plan.refusal.empty());
    // A crossing of the ward is a real walk: some route, some units, and a
    // cost that is at least one whole minute and a good deal less than a day.
    CHECK(plan.routeSteps > 0);
    CHECK(plan.units > 0);
    CHECK(plan.minutes >= 1);
    CHECK(plan.minutes < 60 * 24);
    // The landing is standable ground on the place's own band -- never inside
    // geometry, the demo Cut's own rule.
    CHECK(session.tiles().standable(plan.toX, plan.toY, plan.toBand));

    // THE PLAN'S NUMBERS ARE THE FUNCTIONS' NUMBERS. The seconds are the units
    // walked, and the minutes are those seconds floored -- one derivation, so
    // the pane's restated cost and the delivered skip cannot disagree.
    CHECK(plan.seconds == travelWalkSeconds(plan.units));
    CHECK(plan.minutes == travelClockMinutes(plan.seconds));
}

TEST_CASE("standing in the selection offers no travel -- there is nowhere to go") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 640;
    config.height = 360;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 1);
    session.toggleDistrictMap();
    // The map opens with the cursor on the ground underfoot -- the body is
    // inside its own selection, so the plan says standingIn and the pane
    // reads HERE rather than a verb.
    const Session::TravelPlan here = session.districtMapTravelPlan();
    CHECK(here.standingIn);
    CHECK_FALSE(here.available);
}

TEST_CASE("arrived, the map sells no second ticket to the door you stand at") {
    // THE SHIP NOTE'S "ZERO-PACES RE-TRAVEL" (kw6-mapafter, kw7-press),
    // closed on the KEY's side. The Counting-House's door -- the anchor, the
    // travel's aim -- sits one row NORTH of its own footprint, so the
    // arrival ring lands the body at the door with contains() still false.
    // The pane's zero-paces predicate already reads HERE there and sleeps
    // the row; this case pins the other half: the plan's HERE clause (the
    // same predicate, d2 <= 6, in integers) answers standingIn, so the T
    // press re-planning through it is inert rather than silently charging a
    // minute to shuffle one step and re-fire the plate.
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 640;
    config.height = 360;
    config.timeOfDay = 20 * 3600;
    config.timeOfDayGiven = true;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 1);
    session.toggleDistrictMap();
    REQUIRE(session.districtMapOpen());
    REQUIRE(session.selectDistrictMapPlace("The Royal Counting-House"));
    const Session::TravelPlan plan = session.districtMapTravelPlan();
    REQUIRE(plan.available);
    session.travelDistrictMapSelection();
    REQUIRE_FALSE(session.districtMapOpen());  // the travel was taken

    // The defect's own precondition, pinned so this case cannot quietly rot
    // into the plain contains() path: the ring left the body OUTSIDE the
    // footprint. (Content is frozen -- if a re-bake ever moves this door,
    // this line is the one that should speak up.)
    const std::vector<MapPlace>& places = mapPlaces();
    const MapPlace* house = nullptr;
    for (const MapPlace& p : places) {
        if (p.name == "The Royal Counting-House") {
            house = &p;
        }
    }
    REQUIRE(house != nullptr);
    CHECK_FALSE(house->contains(session.body().tileX(), session.body().tileY()));

    // Reopen on the same selection: the plan says you are standing in it,
    // and no travel is on offer.
    session.toggleDistrictMap();
    REQUIRE(session.districtMapOpen());
    REQUIRE(session.selectDistrictMapPlace("The Royal Counting-House"));
    const Session::TravelPlan again = session.districtMapTravelPlan();
    CHECK(again.standingIn);
    CHECK_FALSE(again.available);
    // And the page can price no ticket: a standingIn plan leaves the state's
    // travel row empty -- no cost, no refusal -- matching the pane's own
    // sleeping row, so the page and the key agree at the door.
    const DistrictMapState paneState = session.districtMapState();
    CHECK(paneState.travelCost.empty());
    CHECK(paneState.travelRefusal.empty());

    // The press is inert: no minute charged, no step taken, the page still up.
    const int clock = session.timeOfDay();
    const std::int32_t atX = session.body().tileX();
    const std::int32_t atY = session.body().tileY();
    session.travelDistrictMapSelection();
    CHECK(session.timeOfDay() == clock);
    CHECK(session.body().tileX() == atX);
    CHECK(session.body().tileY() == atY);
    CHECK(session.districtMapOpen());
}

// ---------------------------------------------------------------------------
// THE REFUSALS, each on its own predicate
// ---------------------------------------------------------------------------

TEST_CASE("travel refuses while carrying the courier's man, in the ward's voice") {
    // Drive the courier case to the one state it never otherwise parks in --
    // the man genuinely in hand -- and press TRAVEL against it. The refusal is
    // load-bearing: a permitted travel-while-carrying would teleport the body
    // to the Mission, where stepSheetCase() completes the delivery on arrival,
    // finishing the case's whole final act off one keypress.
    SmokeRunConfig run;
    run.caseRun = true;
    run.caseEnd = "taken";
    run.travelTo = "Mission of the Flame";
    const SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    // The man is in hand...
    CHECK(played.summary.find("carry=yes") != std::string::npos);
    // ...and travel is refused, by name, with the clock unmoved and the page
    // still up (moved=no). The refusal is in the register, not a silent no-op.
    CHECK(played.travelResult.found);
    CHECK_FALSE(played.travelResult.plan.available);
    CHECK(played.travelResult.plan.refusal == "NOT WITH THE FINCH. WALK HIM.");
    CHECK_FALSE(played.travelResult.moved);
    CHECK(played.travelResult.clockFrom == played.travelResult.clockTo);
}

TEST_CASE("travelRefusal reads the wait page's own 'anywhere safe' list") {
    // A travel is a wait plus a relocation, so every clause of waitRefusal()
    // refuses a travel too. The hostile-neighbour clause is the one
    // test_pause.cpp already stages deterministically, so it is the one to
    // pin the delegation on: standing two tiles from the bartender, turn him
    // hostile through the ledger the attitude actually reads.
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 21 * 3600;
    config.timeOfDayGiven = true;
    config.spawnX = sim::gull::kBartenderX;
    config.spawnY = sim::gull::kBarY + 2;
    config.spawnBand = sim::gull::kGroundBand;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 2);

    // Safe while the room likes you fine: neither door refuses.
    REQUIRE(session.waitRefusal().empty());
    REQUIRE(session.travelRefusal().empty());

    // `neighbour`, not `near` -- `near` is a historic MSVC macro and this
    // tree compiles on Windows too.
    const sim::Actor* neighbour =
        session.tavern().nearestTo(session.body().x(), session.body().y(), 6 * sim::kSubOne);
    REQUIRE(neighbour != nullptr);
    sim::SocialLedger& ledger = session.tavern().dialogue().ledger();
    for (int i = 0; i < 50 && ledger.attitudeOf(neighbour->id()) != sim::Attitude::Hostile; ++i) {
        ledger.record(neighbour->id(), sim::Deed::Robbed);
    }
    REQUIRE(ledger.attitudeOf(neighbour->id()) == sim::Attitude::Hostile);

    // Both doors now name the same refusal, in the same words -- the whole
    // contract: once the carry and Watch clauses pass, the travel refusal IS
    // the wait refusal.
    CHECK(session.waitRefusal() == "NOT WITH AN ENEMY THIS CLOSE.");
    CHECK(session.travelRefusal() == "NOT WITH AN ENEMY THIS CLOSE.");
}

TEST_CASE("mere heat does not refuse travel -- the owner's wait ruling holds") {
    // waitRefusal() deliberately ignores heat and warrants (waiting one out is
    // a tactic), and travelRefusal() defers to it for exactly that class, so a
    // safe spot with nothing carried and no Watch closing refuses neither.
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 640;
    config.height = 360;
    config.timeOfDay = 20 * 3600;
    config.timeOfDayGiven = true;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.travelRefusal().empty());
    CHECK(session.waitRefusal().empty());
}

// ---------------------------------------------------------------------------
// A TRAVEL TAKEN, and taken the same way twice
// ---------------------------------------------------------------------------

TEST_CASE("a travel taken advances the clock by its own minutes and arrives named") {
    SmokeRunConfig run;
    run.mapOverlay = true;  // the page open for the shutter, the probe presses the verb
    run.travelTo = "Mission of the Flame";
    run.session.timeOfDay = 20 * 3600;
    run.session.timeOfDayGiven = true;
    const SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());

    const TravelLineResult& probe = played.travelResult;
    CHECK(probe.found);
    CHECK(probe.plan.available);
    // THE BODY MOVED to the plan's own standable landing, the page is down.
    CHECK(probe.moved);
    CHECK(probe.endX == probe.plan.toX);
    CHECK(probe.endY == probe.plan.toY);
    // THE CLOCK ADVANCED BY EXACTLY THE RESTATED MINUTES -- the wait
    // machinery, not a second time system, and not a rounding away from what
    // the verb promised.
    CHECK(probe.clockTo == (probe.clockFrom + probe.plan.minutes * 60) % sim::kSecondsPerDay);
    CHECK(probe.clockTo != probe.clockFrom);
    // AND YOU ARRIVE SOMEWHERE NAMED: the threshold plate is up and carries a
    // name, the Phase D machinery firing for a relocation exactly as it fires
    // for a walked crossing.
    CHECK(probe.plateUp);
    CHECK_FALSE(probe.plate.empty());
}

TEST_CASE("two identical travels produce the identical summary segment, to the byte") {
    SmokeRunConfig run;
    run.mapOverlay = true;
    run.travelTo = "The Weighhouse";
    run.session.timeOfDay = 20 * 3600;
    run.session.timeOfDayGiven = true;
    const SmokeRunResult a = play(run);
    const SmokeRunResult b = play(run);
    INFO("A: ", a.summary);
    INFO("B: ", b.summary);
    const std::string segA = travelSegment(a.summary);
    const std::string segB = travelSegment(b.summary);
    CHECK_FALSE(segA.empty());
    // The whole visible state -- route, units, seconds, minutes, the clock
    // either side, the landing, the plate -- identical across two runs. This
    // is the twin-run claim for a feature the population gate never sees,
    // because it constructs no Session.
    CHECK(segA == segB);
    CHECK(a.travelResult.clockTo == b.travelResult.clockTo);
    CHECK(a.travelResult.endX == b.travelResult.endX);
    CHECK(a.travelResult.endY == b.travelResult.endY);
}
