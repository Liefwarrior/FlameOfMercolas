// THE SCRIPTED LINES, ACCEPTED. One case per `--flag` the demo advertises.
//
// WHY THIS FILE EXISTS. S9 changed what a locked box does to the `G` key -- its
// own documented change, "a LOCKED box puts the wire in rather than opening
// itself" -- and left `--skyrun`'s box beat as a single `steal()`, which from
// that day forward put a wire in a lock and walked away. The stage counts a
// CRACKED box, so the line landed 2 of 9 and exited 1 for a whole sprint. The
// S9 review re-ran `--burgle`, `--nemesis` and `--ward` and did not re-run this
// one; nothing in the suite asserted it either. It was verified against the S9
// tip before S10 touched it -- identical output, `stages=2 cracks=0` -- so it is
// S9's regression and not S10's, and it is fixed here because a flag in
// `--help` that exits 1 is a broken promise a player finds before a reviewer
// does.
//
// So: every scripted line gets a case that asserts it lands ALL its beats.
// `--burgle` and `--trail` already had one; these are the ones that did not.

#include <doctest/doctest.h>

#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"

namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] render::SmokeRunResult play(render::SmokeRunConfig run) {
    run.session.contentDir = content::contentDir();
    // Every line sets its own clock when nobody named one -- see
    // scriptedStartHour, which exists because S5 shipped `--skyrun` documented
    // at an hour its own cast is not in the building at.
    run.session.timeOfDay = render::scriptedStartHour(run) * 3600;
    run.steps = 0;
    run.stamp = false;
    return render::runSmoke(run);
}

}  // namespace

TEST_CASE("the Skyrunner line lands all nine of its stages") {
    render::SmokeRunConfig run;
    run.skyrun = true;
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.skyrunStages == 9);
    // AND THE BOX WAS ACTUALLY CRACKED. This is the number that went to zero in
    // S9 and stayed there: the beat is a cracked strongbox, and a wire left in
    // a lock is not one.
    CHECK(played.summary.find("cracks=1") != std::string::npos);
    // The rest of the line, in the units native/README.md documents it in.
    CHECK(played.summary.find("rank=2 Cutpurse") != std::string::npos);
    CHECK(played.summary.find("lifts=2") != std::string::npos);
    CHECK(played.summary.find("fences=1") != std::string::npos);
    CHECK(played.summary.find("runs=1") != std::string::npos);
}

TEST_CASE("the Priest of the Flame line lands all six of its stages") {
    render::SmokeRunConfig run;
    run.flame = true;
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.flameStages == 6);
    CHECK(played.summary.find("forged=1") != std::string::npos);
}

TEST_CASE("the ward's bounty line lands all six of its beats") {
    render::SmokeRunConfig run;
    run.contract = true;
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.contractBeats == 6);
    CHECK(played.summary.find("paid=1") != std::string::npos);
}

TEST_CASE("--contract=held stops with the job still live, and owes only its two beats") {
    // SHEETS BUILD. The full line ends PAID, which is exactly the one state
    // the Journal tile's live-contract rows have nothing to show for -- see
    // the ending's own note in runContractLine. Two beats wanted, two landed:
    // a held run that did what was asked must not read as fallen short.
    render::SmokeRunConfig run;
    run.contract = true;
    run.contractEnd = "held";
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.contractBeats == 2);
    CHECK(played.summary.find("taken=1") != std::string::npos);
    CHECK(played.summary.find("paid=0") != std::string::npos);
}

TEST_CASE("the nemesis arc lands all seven of its beats") {
    render::SmokeRunConfig run;
    run.nemesis = true;
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.nemesisBeats == 7);
    // He is somebody by the end of it: three wins, a house with members in it,
    // and a permanent toll on what the ward charges.
    CHECK(played.summary.find(" x3 ") != std::string::npos);
    CHECK(played.summary.find("members, toll") != std::string::npos);
}

TEST_CASE("the nemesis arc lands all seven beats from the README's own spawn, whatever walk comes before it") {
    // STANCE & ROOM BUILD. `granadad.exe --smoke=N --nemesis` is the README's
    // shape of this line: the authored Tarwalk spawn, N steps of the smoke
    // walk, then the arc -- and N picks the timeline every roll in the fights
    // falls on. With the room's own blows now delivering every loss in the
    // line (nothing is conceded to a seam any more), the line has to land on
    // ANY of those timelines, and the first draft of this build did not: walks
    // of 40, 50 and 120 steps fell to 4/7 because the drive re-engaged with
    // connected taps and killed its own nemesis, and the fix's own first cut
    // fell to 1/7 after 80 and 100 because its walk back into the house
    // stalled at the threshold. Those five, and the spawn.
    for (const int steps : {0, 40, 50, 80, 100, 120}) {
        render::SmokeRunConfig run;
        run.nemesis = true;
        run.session.contentDir = content::contentDir();
        run.session.timeOfDay = render::scriptedStartHour(run) * 3600;
        run.steps = steps;
        run.walk = true;
        run.stamp = false;
        const render::SmokeRunResult played = render::runSmoke(run);
        INFO("after a smoke walk of ", steps, " steps: ", played.summary);
        CHECK(played.ok);
        CHECK_FALSE(played.scriptFellShort());
        CHECK(played.nemesisBeats == 7);
        CHECK(played.summary.find(" x3 ") != std::string::npos);
    }
}

TEST_CASE("the eviction line lands both of its paths in full") {
    // EVICTION (lane: eviction). The advertised flag must land ALL its beats
    // on the path it names -- the participate path's nine, and the disrupt
    // path's eight -- the same broken-promise bar every other line here is
    // held to. The detailed state (served/weapon/twin-run) lives in
    // test_eviction_line.cpp; this is the acceptance gate.
    {
        render::SmokeRunConfig run;
        run.evictionRun = true;
        const render::SmokeRunResult played = play(run);
        INFO("participate: " << played.summary);
        CHECK(played.ok);
        CHECK_FALSE(played.scriptFellShort());
        CHECK(played.evictBeats == 9);
    }
    {
        render::SmokeRunConfig run;
        run.evictionRun = true;
        run.evictionEnd = "refused";
        const render::SmokeRunResult played = play(run);
        INFO("disrupt: " << played.summary);
        CHECK(played.ok);
        CHECK_FALSE(played.scriptFellShort());
        CHECK(played.evictBeats == 8);
    }
}

TEST_CASE("the roof line gets onto the lead and back down again, all three ways") {
    for (const char* where : {"roof", "leap", "street"}) {
        render::SmokeRunConfig run;
        run.roofs = true;
        run.roofsEnd = where;
        const render::SmokeRunResult played = play(run);
        INFO(where << ": " << played.summary);
        CHECK(played.ok);
        CHECK_FALSE(played.scriptFellShort());
    }
}

TEST_CASE("--radiant takes a real errand off its own giver, and the journal shows it") {
    // RADIANT BUILD. TASK #81's acceptance in one flag: the board the session
    // posted off the live ward is reachable across a real conversation, and
    // the taken errand reads back off the Journal tile's own rows.
    render::SmokeRunConfig run;
    run.radiant = true;
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.radiantResult.found);
    CHECK(played.radiantResult.opened);
    CHECK(played.radiantResult.offered);
    CHECK(played.radiantResult.taken);
    CHECK(played.radiantResult.journal);
    // The brief is real prose bound to real nouns -- never an unsubstituted
    // token, the generator's own bar.
    CHECK_FALSE(played.radiantResult.brief.empty());
    CHECK(played.radiantResult.brief.find('{') == std::string::npos);
    CHECK_FALSE(played.radiantResult.giver.empty());
}

TEST_CASE("--radiant=offer stops with the giver's row on the open list, one beat owed") {
    render::SmokeRunConfig run;
    run.radiant = true;
    run.radiantEnd = "offer";
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.radiantResult.offered);
    // Stopped before the press, honestly: nothing taken, conversation still
    // up for the shutter.
    CHECK_FALSE(played.radiantResult.taken);
    CHECK(played.talking);
}
