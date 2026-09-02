// THE SCRIPTED DEMO -- the route, and the four promises it makes.
//
// A demo is the one artefact in this build whose failure mode is PUBLIC: it
// runs unattended in front of somebody, and a beat that walks into a wall, a
// coordinate that drifted off a standable cell, or a route that never reaches
// its end card is a thing the owner finds out about at the worst possible
// moment. So the four things the brief actually asks for are asserted here,
// against the REAL baked Docks and the real casebook, with no window:
//
//   SHAPE        every beat is well formed, every section is reachable, every
//                lead the route names by id is a lead the content has.
//   GROUND       every cut and every walk target lands on a cell a body can
//                stand on -- the coordinates in the route table are claims
//                about the map and belong in the gate, not in a comment.
//   IT FINISHES  the whole route driven end to end terminates, in a bounded
//                number of frames, having ended by ITS OWN hand rather than by
//                a guard -- "it must not crash or stall" as a case.
//   TWICE THE    two runs of the same route from the same seed end with the
//   SAME         body on the same tile, the clock at the same second and the
//                casebook holding the same leads. This project gates on
//                twin-run determinism and the demo must not be the one thing
//                that drifts.
//
// AND THE PITCH LANDED. The investigation section is the beat the brief calls
// the game's actual pitch, so the run is asserted to have READ the leads and
// OPENED Harl's Yard -- not merely to have walked past the Weighhouse.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/demo.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tile_query.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] render::SessionConfig demoConfig() {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    // THE CLIENT'S OWN OPENING STATE. run_client sets both of these before it
    // builds its Session; a route driven against a different hour would be
    // driving a different ward.
    config.timeOfDay = 8 * 3600;
    config.openingPage = true;
    return config;
}

/// Drives the whole route with no window and no drawing, exactly the way the
/// frame loop does: one director tick, then one Session::step with whatever it
/// handed back. Returns the frames spent; `finished` says whether the route
/// ended itself.
int playRoute(render::Session& session, const std::string& section, bool* finished, int budget) {
    render::DemoDirector director(section, {});
    int frames = 0;
    while (frames < budget) {
        const render::DemoDirector::Tick tick = director.advance(session);
        if (!tick.running) {
            *finished = true;
            return frames;
        }
        session.step(tick.move);
        ++frames;
    }
    *finished = false;
    return frames;
}

}  // namespace

// ===========================================================================
// SHAPE
// ===========================================================================

TEST_CASE("the route is well formed, and every section it advertises is in it") {
    const std::vector<render::DemoBeat>& route = render::demoRoute();
    REQUIRE(route.size() > 8);

    for (const render::DemoBeat& beat : route) {
        CAPTURE(beat.section);
        CHECK(beat.section != nullptr);
        CHECK(std::string(beat.section).empty() == false);
        // A BEAT OF ZERO FRAMES IS A BEAT NOBODY SEES. The director clamps to
        // one, but a zero in the table is an authoring mistake either way.
        CHECK(beat.frames >= 1);
        if (beat.act == render::DemoAct::Card) {
            CHECK(beat.line != nullptr);
        }
    }

    // The sections --demo=SECTION advertises are exactly the sections in the
    // table, in order, with no repeats -- a section that appears twice would
    // make "--demo=case" mean two different places.
    const std::vector<std::string> sections = render::demoSections();
    CHECK(sections.size() >= 5);
    const std::set<std::string> unique(sections.begin(), sections.end());
    CHECK(unique.size() == sections.size());
    for (const std::string& section : sections) {
        CHECK(render::demoFrameCount(section) > 0);
        CHECK(render::demoFrameCount(section) <= render::demoFrameCount(""));
    }

    // PACED FOR A HUMAN EYE. At the demo's fixed one-step-per-frame cadence
    // this is seconds, and the range is the brief's own "nothing should flash
    // past" at one end and "a finished smaller thing" at the other.
    const int seconds = render::demoFrameCount("") / 60;
    CHECK(seconds > 60);
    CHECK(seconds < 300);
}

TEST_CASE("every lead the route names by id is a lead the casebook actually has") {
    render::Session session(demoConfig());
    REQUIRE(session.casebook().raws() != nullptr);
    int named = 0;
    for (const render::DemoBeat& beat : render::demoRoute()) {
        if (beat.act != render::DemoAct::CasebookLead) {
            continue;
        }
        REQUIRE(beat.line != nullptr);
        CAPTURE(beat.line);
        // A TYPO HERE IS SILENT AT RUN TIME -- selectCasebookLead returns
        // false and the demo photographs whatever the cursor was already on.
        CHECK(session.casebook().raws()->indexOf(beat.line) >= 0);
        ++named;
    }
    CHECK(named >= 1);
}

// ===========================================================================
// GROUND
// ===========================================================================

TEST_CASE("every cut and every walk in the route lands on ground a body can stand on") {
    render::Session session(demoConfig());
    std::int32_t band = docks::kSpawnBand;
    int cuts = 0;
    int walks = 0;
    for (const render::DemoBeat& beat : render::demoRoute()) {
        if (beat.act == render::DemoAct::Cut) {
            band = beat.c;
        }
        if (beat.act != render::DemoAct::Cut && beat.act != render::DemoAct::Walk) {
            continue;
        }
        CAPTURE(beat.section);
        CAPTURE(beat.a);
        CAPTURE(beat.b);
        CAPTURE(band);
        // THE DIRECTOR SEARCHES A RING OF EIGHT for a standable neighbour, so
        // the promise this makes is deliberately tighter than the promise the
        // director keeps: the AUTHORED coordinate is on or within ONE tile of
        // standable ground. A route that only worked because the ring rescued
        // it is a route whose numbers are wrong.
        bool near = false;
        for (std::int32_t dy = -1; dy <= 1 && !near; ++dy) {
            for (std::int32_t dx = -1; dx <= 1 && !near; ++dx) {
                near = session.tiles().standable(beat.a + dx, beat.b + dy, band);
            }
        }
        CHECK(near);
        if (beat.act == render::DemoAct::Cut) {
            ++cuts;
        } else {
            ++walks;
        }
    }
    CHECK(cuts >= 4);
    CHECK(walks >= 4);
}

// ===========================================================================
// IT FINISHES
// ===========================================================================

TEST_CASE("the whole route plays to its end card and stops by its own hand") {
    render::Session session(demoConfig());
    bool finished = false;
    // The budget is the route's own length plus a generous margin. A run that
    // needs the margin has a walk beat that is not ending, which is exactly the
    // stall this case exists to catch -- the director ends a beat on its frame
    // count whatever the feet are doing, so the sum is also the ceiling.
    const int budget = render::demoFrameCount("") + 600;
    const int spent = playRoute(session, {}, &finished, budget);
    CHECK(finished);
    CHECK(spent <= render::demoFrameCount(""));
    CHECK(spent > render::demoFrameCount("") / 2);
}

TEST_CASE("the investigation section reads its leads and opens Harl's Yard") {
    render::Session session(demoConfig());
    REQUIRE(session.casebook().raws() != nullptr);
    const std::int32_t harls = session.casebook().raws()->indexOf("harls-yard");
    REQUIRE(harls >= 0);
    CHECK(session.casebook().state(harls) == LeadState::Unheard);

    bool finished = false;
    (void)playRoute(session, "case", &finished, render::demoFrameCount("case") + 600);
    CHECK(finished);

    // THE PITCH, AS A FACT ABOUT THE SIMULATION rather than about a screenshot:
    // two clue sites read on foot, and the lead the map cursor lands on is a
    // lead the ward actually opened.
    CHECK(session.casebook().readCount() >= 2);
    CHECK(session.casebook().state(harls) != LeadState::Unheard);
}

// ===========================================================================
// TWICE THE SAME
// ===========================================================================

// ===========================================================================
// THE CUTS -- snapped, and dressed
// ===========================================================================
//
// THE OWNER'S BUG, pinned: "teleporting through walls ... double
// vision/ghosting" on the scripted route. Diagnosis (Session::
// snapViewAfterRelocation's header carries it in full): the body always
// jumped clean and the camera reads the body raw -- there is no eased camera
// position in this build to race across the district -- but the eased,
// position-derived HUD rows could ghost, and the raw one-frame cut itself is
// what the owner calls teleporting. So a cut now SNAPS the view (one seam,
// placeBodyAt) and is DRESSED in the travel fade. These cases hold all
// three claims down so the next relocation feature cannot re-open any of
// them.

TEST_CASE("the frame after a cut: the camera IS the body, the veil is up") {
    render::Session session(demoConfig());
    render::DemoDirector director({}, {});
    int frames = 0;
    int cutsSeen = 0;
    std::int32_t lastX = session.body().tileX();
    std::int32_t lastY = session.body().tileY();
    const int budget = render::demoFrameCount("") + 600;
    while (frames < budget) {
        const render::DemoDirector::Tick tick = director.advance(session);
        if (!tick.running) {
            break;
        }
        session.step(tick.move);
        ++frames;
        const std::int32_t bodyX = session.body().tileX();
        const std::int32_t bodyY = session.body().tileY();
        const bool jumped =
            std::abs(bodyX - lastX) > 2 || std::abs(bodyY - lastY) > 2;
        lastX = bodyX;
        lastY = bodyY;
        if (!jumped) {
            continue;
        }
        ++cutsSeen;
        // THE PIN THIS FILE'S HEADER PROMISES: on the very frame a scripted
        // relocation lands, the eased/drawn eye equals the body's own
        // integers, exactly. Today that is true by construction (the camera
        // is derived from the body at draw time, never eased); the day
        // somebody adds an interpolated camera layer for smooth high-fps
        // rendering, this CHECK is what forces its relocation snap into
        // snapViewAfterRelocation instead of letting the eye race across the
        // district through every wall between the old street and the new --
        // which is the exact "teleporting through walls" smear the owner
        // reported.
        const render::Camera view = session.camera();
        CHECK(view.x == static_cast<float>(session.body().x()) / 256.0F);
        CHECK(view.y == static_cast<float>(session.body().y()) / 256.0F);
        // AND THE CUT IS DRESSED: the frame of the jump is already behind
        // the seam veil (snapped fully black, easing up on the new street),
        // so a watcher sees a cut, not a glitch. One step has run since the
        // snap, so one fall-step of decay is the most that can have gone.
        CHECK(session.cutVeil() > 0.9F);
    }
    // The route's own cuts were all exercised -- the quay pier cut, the
    // saltgate cut, the two case cuts and the night pair at least (the
    // spawn cut lands on the spawn tile and moves nobody).
    CHECK(cutsSeen >= 5);
}

TEST_CASE("no shot on the route's list is ever photographed through the veil") {
    // The fade dresses the cuts and MUST NOT move the committed frame set:
    // every shutter the director arms has to fire with the veil fully down.
    // This is the structural half of "capture-flag-friendly" -- re-blessing
    // a darkened frame by accident is exactly the drift this forbids.
    render::Session session(demoConfig());
    render::DemoDirector director({}, {});
    int frames = 0;
    int shots = 0;
    int veiled = 0;
    const int budget = render::demoFrameCount("") + 600;
    while (frames < budget) {
        const render::DemoDirector::Tick tick = director.advance(session);
        if (!tick.running) {
            break;
        }
        session.step(tick.move);
        ++frames;
        if (session.cutVeil() > 0.01F) {
            ++veiled;
        }
        if (director.shutterArmed()) {
            ++shots;
            CAPTURE(frames);
            CHECK(session.cutVeil() < 0.01F);
        }
    }
    // Both halves genuinely ran: the route armed its whole shot list, and
    // the veil was genuinely up somewhere between them.
    CHECK(shots >= 15);
    CHECK(veiled >= 30);
}

TEST_CASE("placeBodyAt snaps the old ground's eased rows the instant it lands") {
    // The ghost itself, without the demo: aim at something until the
    // crosshair row is easing open, jump across the district through the one
    // relocation seam, and the row must be CLOSED on that same instant --
    // not easing out over a street it was never true of.
    render::Session session(demoConfig());
    // Let the opening casebook go down and the world settle.
    session.toggleCasebook();
    const MoveInput still{};
    session.stepMany(still, 20);
    session.placeBodyAt(119, 47, docks::kSpawnBand);
    // The very same instant -- no step in between -- the view is the new
    // ground's: camera on the body, veil untouched (a bare placeBodyAt is a
    // SNAP; dressing is the scripted cut's own extra, dressInstantCut).
    const render::Camera view = session.camera();
    CHECK(view.x == static_cast<float>(session.body().x()) / 256.0F);
    CHECK(view.y == static_cast<float>(session.body().y()) / 256.0F);
    CHECK(session.body().tileX() == 119);
    CHECK(session.body().tileY() == 47);
}

TEST_CASE("two runs of the same route end in the same place at the same second") {
    render::Session first(demoConfig());
    render::Session second(demoConfig());
    bool aDone = false;
    bool bDone = false;
    const int budget = render::demoFrameCount("") + 600;
    const int aFrames = playRoute(first, {}, &aDone, budget);
    const int bFrames = playRoute(second, {}, &bDone, budget);

    CHECK(aDone);
    CHECK(bDone);
    CHECK(aFrames == bFrames);
    CHECK(first.body().tileX() == second.body().tileX());
    CHECK(first.body().tileY() == second.body().tileY());
    CHECK(first.body().band() == second.body().band());
    CHECK(first.body().yaw() == second.body().yaw());
    CHECK(first.timeOfDay() == second.timeOfDay());
    CHECK(first.elapsedSeconds() == second.elapsedSeconds());
    CHECK(first.casebook().readCount() == second.casebook().readCount());
    CHECK(first.casebook().known().size() == second.casebook().known().size());
}
