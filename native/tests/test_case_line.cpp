// COURIER CASE -- THE QUIET TENANT, DRIVEN. The errand played end to end
// through the same Session verbs a keypress makes, and played TWICE to the
// same result.
//
// WHY THIS FILE EXISTS. The case is session-scripted -- the courier, the
// break-in, the subdue and the delivery are presentation beats over the sim,
// never a change to it -- so it is invisible to the twin-run GATE (which
// constructs no Session). The determinism claim the gate makes for the
// population, this file makes for the case: two runs of --case land the same
// beats and end with the same book, byte for byte in the summary that encodes
// it. And every scripted line the build advertises gets a case that asserts it
// lands ALL its beats, the rule test_scripted_lines.cpp states.

#include <doctest/doctest.h>

#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/session.hpp"

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

/// The `| case ...` segment of the summary, which is the whole of the errand's
/// visible state -- beats, mask, read/known, dread, whether the tenant is
/// down, whether he is in hand, and whether the book closed. Comparing this
/// between two runs is the twin-run check for a session-scripted case.
[[nodiscard]] std::string caseSegment(const std::string& summary) {
    const std::string::size_type at = summary.find("| case ");
    if (at == std::string::npos) {
        return {};
    }
    const std::string::size_type end = summary.find(" | ", at + 1);
    return summary.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

}  // namespace

TEST_CASE("the courier case lands all eight of its beats and closes the book") {
    render::SmokeRunConfig run;
    run.caseRun = true;
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    // EVERY BEAT: the sheet in hand, the sheet read on the Letters tile, the
    // Gull door lead read, the wait to two, the box lead read while crouched,
    // Finch put down with fists, taken up, and delivered to the Mission's back
    // room.
    CHECK(played.caseBeats == 8);
    CHECK(played.caseBeatMask == 0xFF);
    // AND THE ERRAND IS PAID: the book closed, the man no longer in hand.
    CHECK(played.summary.find("closed=yes") != std::string::npos);
    CHECK(played.summary.find("carry=no") != std::string::npos);
    // Two clue sites read on foot (the door and the box), plus the close.
    CHECK(played.summary.find("read=3/") != std::string::npos);
}

TEST_CASE("two runs of the courier case end with the same book, to the byte") {
    render::SmokeRunConfig run;
    run.caseRun = true;
    const render::SmokeRunResult a = play(run);
    const render::SmokeRunResult b = play(run);
    INFO("A: ", a.summary);
    INFO("B: ", b.summary);
    const std::string segA = caseSegment(a.summary);
    const std::string segB = caseSegment(b.summary);
    CHECK_FALSE(segA.empty());
    CHECK(segA == segB);
    CHECK(a.caseBeatMask == b.caseBeatMask);
    CHECK(a.caseBeats == b.caseBeats);
}

TEST_CASE("the sheet shutter opens the handed letter on the Letters tile") {
    render::SmokeRunConfig run;
    run.caseRun = true;
    run.caseEnd = "sheet";
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    // Two beats: the sheet in hand, and the sheet read on the Letters tile.
    CHECK(played.caseBeats == 2);
    // The sheet is unlocked the moment the courier's lead is heard -- a handed
    // document, readable on Open where the Bloodletter five wait for Cold.
    CHECK(played.summary.find("letters=1") != std::string::npos);
    CHECK(played.summary.find("live=yes") != std::string::npos);
}

TEST_CASE("the down shutter catches the tenant on the boards, not yet carried") {
    render::SmokeRunConfig run;
    run.caseRun = true;
    run.caseEnd = "down";
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.caseBeats == 6);
    CHECK(played.summary.find("tenant=down") != std::string::npos);
    CHECK(played.summary.find("carry=no") != std::string::npos);
}

TEST_CASE("the night shutter catches the box lead read from a crouch on the guest floor") {
    render::SmokeRunConfig run;
    run.caseRun = true;
    run.caseEnd = "night";
    const render::SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    CHECK(played.caseBeats == 5);
    // The door lead and the box lead both read: two of the errand's leads on
    // foot before the man is ever touched.
    CHECK(played.summary.find("read=2/") != std::string::npos);
}
