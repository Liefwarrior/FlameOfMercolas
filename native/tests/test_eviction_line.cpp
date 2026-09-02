// EVICTION -- BOTH PATHS, DRIVEN. The owner's third case played end to end
// through the same Session verbs a keypress makes, and played TWICE to the
// same result, for each of the two ways it closes.
//
// WHY THIS FILE EXISTS. Like the courier case, the eviction is session-
// scripted -- the hire, the ward crossings, the knock, the serve or the yield
// are presentation beats over the sim, never a change to it -- so it is
// invisible to the twin-run GATE (which constructs no Session). The
// determinism claim the gate makes for the population, this file makes for the
// case: two runs of --eviction land the same beats and end with the same book,
// byte for byte in the summary that encodes it. It also proves the one thing
// the courier case had no equivalent of -- a skill gate said out loud -- both
// refusing below its bar and passing at it.

#include <doctest/doctest.h>

#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/dialogue.hpp"

namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] render::SmokeRunResult play(render::SmokeRunConfig run) {
    run.session.contentDir = content::contentDir();
    run.session.timeOfDay = render::scriptedStartHour(run) * 3600;
    run.steps = 0;
    run.stamp = false;
    return render::runSmoke(run);
}

/// The `| evict ...` segment of the summary, which is the whole of the
/// errand's visible state -- beats, mask, read/known, dread, knocked, served,
/// closed, weapon. Comparing this between two runs is the twin-run check for a
/// session-scripted case, exactly test_case_line's caseSegment.
[[nodiscard]] std::string evictSegment(const std::string& summary) {
    const std::string::size_type at = summary.find("| evict ");
    if (at == std::string::npos) {
        return {};
    }
    const std::string::size_type end = summary.find(" | ", at + 1);
    return summary.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

}  // namespace

TEST_CASE("the participate path lands all nine beats, serves the writ and earns The Evictor") {
    render::SmokeRunConfig run;
    run.evictionRun = true;
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    // EVERY BEAT: the hire (open-hand bar passed), the writ read on the Letters
    // tile, the gate lead read, the wait to the evening, the door lead read,
    // the knock, the serve, the walk home closing the book, and The Evictor in
    // hand.
    CHECK(played.evictBeats == 9);
    CHECK(played.evictBeatMask == 0x1FF);
    // THE ERRAND IS PAID: knocked and served, the book closed, and the player
    // ARMED where every run before this one left them barehanded.
    CHECK(played.summary.find("knocked=yes") != std::string::npos);
    CHECK(played.summary.find("served=yes") != std::string::npos);
    CHECK(played.summary.find("closed=yes") != std::string::npos);
    CHECK(played.summary.find("weapon=armed") != std::string::npos);
    // Two clue sites read on foot (the gate and the door).
    CHECK(played.summary.find("read=2/") != std::string::npos);
}

TEST_CASE("two runs of the participate path end with the same book, to the byte") {
    render::SmokeRunConfig run;
    run.evictionRun = true;
    const render::SmokeRunResult a = play(run);
    const render::SmokeRunResult b = play(run);
    INFO("A: ", a.summary);
    INFO("B: ", b.summary);
    const std::string segA = evictSegment(a.summary);
    const std::string segB = evictSegment(b.summary);
    CHECK_FALSE(segA.empty());
    CHECK(segA == segB);
    CHECK(a.evictBeatMask == b.evictBeatMask);
    CHECK(a.evictBeats == b.evictBeats);
}

TEST_CASE("the disrupt path closes the case without serving, and grants no weapon") {
    render::SmokeRunConfig run;
    run.evictionRun = true;
    run.evictionEnd = "refused";
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    // Eight beats owed, eight landed: everything up to the knock, then the
    // walk-back and the yield, then the proof that no weapon was granted.
    CHECK(played.evictBeats == 8);
    // The writ was carried back whole: knocked (the family got their warning),
    // never served, the book closed all the same, and the player still
    // barehanded -- the disrupt path's whole point.
    CHECK(played.summary.find("knocked=yes") != std::string::npos);
    CHECK(played.summary.find("served=no") != std::string::npos);
    CHECK(played.summary.find("closed=yes") != std::string::npos);
    CHECK(played.summary.find("weapon=fists") != std::string::npos);
}

TEST_CASE("two runs of the disrupt path end with the same book, to the byte") {
    render::SmokeRunConfig run;
    run.evictionRun = true;
    run.evictionEnd = "refused";
    const render::SmokeRunResult a = play(run);
    const render::SmokeRunResult b = play(run);
    INFO("A: ", a.summary);
    INFO("B: ", b.summary);
    const std::string segA = evictSegment(a.summary);
    CHECK_FALSE(segA.empty());
    CHECK(segA == evictSegment(b.summary));
}

TEST_CASE("the writ shutter opens the handed writ on the Letters tile") {
    render::SmokeRunConfig run;
    run.evictionRun = true;
    run.evictionEnd = "writ";
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    // Two beats: the writ in hand (case live), and the writ read on the tile.
    CHECK(played.evictBeats == 2);
    // The writ is unlocked the moment the hire lead is heard -- a handed
    // document, readable on Open where the widow's petition and the served
    // notice wait for Cold.
    CHECK(played.summary.find("letters=1") != std::string::npos);
    CHECK(played.summary.find("live=yes") != std::string::npos);
}

TEST_CASE("the knock shutter catches the door answered, the choice still open") {
    render::SmokeRunConfig run;
    run.evictionRun = true;
    run.evictionEnd = "knock";
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.evictBeats == 6);
    CHECK(played.summary.find("knocked=yes") != std::string::npos);
    CHECK(played.summary.find("served=no") != std::string::npos);
    CHECK(played.summary.find("closed=no") != std::string::npos);
}

TEST_CASE("the hire gate refuses a hand below the open-hand bar, and passes at it") {
    // THE ONE SKILL CHECK IN THE BUILD THAT IS SAID OUT LOUD. The drive arms a
    // qualifying hand; here we prove the gate itself, directly on the director,
    // the way the priest's own topic reads it -- refusing below the bar with
    // both numbers named, and hiring at it.
    using namespace granadad::sim;
    DialogueDirector talk = DialogueDirector::load(content::contentDir());

    // A speaker who IS the case's priest: notable id maell, Clergy. Built by
    // hand rather than off the roster so the test needs no room around it.
    Speaker priest;
    priest.actorId = 1;
    priest.name = "Father Maell";
    priest.notableId = std::string(kEvictionPriestId);
    priest.family = JobFamily::Clergy;
    talk.setEvictionCase(0, false);  // dormant: the writ is on offer

    // BELOW THE BAR: the topic is on the list (the gate is SAID, not hidden),
    // and choosing it refuses out loud with the measure and the player's own
    // number.
    (void)talk.skills().setLevel(kOpenHandSkill, kEvictionOpenHandBar - 1);
    REQUIRE(talk.open(priest, 20 * 3600));
    int writ = -1;
    for (std::size_t i = 0; i < talk.topics().size(); ++i) {
        if (talk.topics()[i].kind == TopicKind::TakeWrit) {
            writ = static_cast<int>(i);
        }
    }
    REQUIRE(writ >= 0);
    const Reply refused = talk.choose(static_cast<std::size_t>(writ));
    CHECK_FALSE(refused.ok);
    CHECK(refused.line.find("OPEN HAND") != std::string::npos);
    CHECK(refused.line.find(std::to_string(kEvictionOpenHandBar)) != std::string::npos);
    talk.close();

    // AT THE BAR: the same topic hires, and the reply is the priest's own
    // acceptance rather than a refusal.
    (void)talk.skills().setLevel(kOpenHandSkill, kEvictionOpenHandBar);
    talk.setEvictionCase(0, false);
    REQUIRE(talk.open(priest, 20 * 3600));
    writ = -1;
    for (std::size_t i = 0; i < talk.topics().size(); ++i) {
        if (talk.topics()[i].kind == TopicKind::TakeWrit) {
            writ = static_cast<int>(i);
        }
    }
    REQUIRE(writ >= 0);
    const Reply hired = talk.choose(static_cast<std::size_t>(writ));
    CHECK(hired.ok);
    CHECK(hired.line.find("EVENING") != std::string::npos);
}

TEST_CASE("the writ topic is offered by the case's priest and nobody else") {
    // The hire tops the priest's list and no one else's -- a dockhand, however
    // clergy-adjacent, is not the Flame's arbiter. Proved on the director the
    // same hand-built way.
    using namespace granadad::sim;
    DialogueDirector talk = DialogueDirector::load(content::contentDir());
    (void)talk.skills().setLevel(kOpenHandSkill, kEvictionOpenHandBar);
    talk.setEvictionCase(0, false);

    Speaker notThePriest;
    notThePriest.actorId = 2;
    notThePriest.name = "Tarn Wrenhale";
    notThePriest.notableId = "";
    notThePriest.family = JobFamily::Serf;
    REQUIRE(talk.open(notThePriest, 20 * 3600));
    bool offered = false;
    for (const Topic& topic : talk.topics()) {
        if (topic.kind == TopicKind::TakeWrit) {
            offered = true;
        }
    }
    CHECK_FALSE(offered);
}
