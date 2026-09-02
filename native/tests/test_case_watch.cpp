// CASE WATCH -- THE QUIET TENANT, made watchable, and the three promises the
// watch makes on top of the drive's own (test_case_line.cpp).
//
//   THE TAPE IS THE DRIVE   recordCaseDrive runs the identical runCaseLine the
//                           --case harness runs; two recordings are equal to
//                           the op, and the drive's marks land the same eight
//                           beats the harness lands.
//   THE REPLAY IS ITS TWIN  driving a fresh session through CaseWatchDirector
//                           exactly the way the frame loop does -- one advance,
//                           then 0 or 1 session.step with what it handed back
//                           -- ends by its own hand, with the session's
//                           fingerprint equal to the drive's, and having spent
//                           EXACTLY the drive's step count in simulation steps.
//                           The watcher's holds and cards cost the sim nothing,
//                           which is the whole determinism argument.
//   PACED FOR AN EYE        the planned runtime sits in the demo's
//                           neighbourhood -- test_demo.cpp's own 60..300 second
//                           rule, because both artefacts exist to be watched.

#include <doctest/doctest.h>

#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/case_watch.hpp"
#include "granadad/render/session.hpp"

namespace content = granadad::content;
namespace render = granadad::render;

namespace {

[[nodiscard]] render::SmokeRunConfig caseConfig(const std::string& ending = {}) {
    render::SmokeRunConfig run;
    run.caseRun = true;
    run.caseEnd = ending;
    run.session.contentDir = content::contentDir();
    return run;
}

/// The frame loop's own contract, with no window and no drawing: one director
/// tick, then step the session however many times (0 or 1) it said. Returns
/// frames spent; `finished` says the route ended itself, `simSteps` counts
/// what actually reached the simulation.
int playWatch(render::CaseWatchDirector& director, render::Session& session, bool* finished,
              std::int64_t* simSteps, int budget) {
    int frames = 0;
    *simSteps = 0;
    while (frames < budget) {
        const render::CaseWatchDirector::Tick tick = director.advance(session);
        if (!tick.running) {
            *finished = true;
            return frames;
        }
        for (std::int32_t i = 0; i < tick.steps; ++i) {
            session.step(tick.move);
            ++*simSteps;
        }
        ++frames;
    }
    *finished = false;
    return frames;
}

}  // namespace

TEST_CASE("two recordings of the drive are the same tape, to the op") {
    const render::CaseWatchDrive a = render::recordCaseDrive(caseConfig());
    const render::CaseWatchDrive b = render::recordCaseDrive(caseConfig());
    // The drive's own marks: all eight beats, none dropped -- the harness's
    // 8/8 mask=255, read off the recording rather than a summary string.
    CHECK(a.beats == 8);
    CHECK(a.beatsWanted == 8);
    CHECK(a.mask == 0xFF);
    CHECK(a.stepCount > 0);
    // Twin-run, memberwise: same ops in the same order with the same inputs
    // and the same pre-step yaws, and the same end fingerprint.
    REQUIRE(a.ops.size() == b.ops.size());
    CHECK(a.ops == b.ops);
    CHECK(a.end == b.end);
    CHECK(a.beats == b.beats);
    CHECK(a.mask == b.mask);
}

TEST_CASE("the replay ends where the drive ended, spending exactly the drive's steps") {
    const render::SmokeRunConfig config = caseConfig();
    render::CaseWatchDrive drive = render::recordCaseDrive(config);
    REQUIRE(drive.beats == 8);
    const render::WatchFingerprint want = drive.end;
    const std::int32_t driveSteps = drive.stepCount;

    // THE SAME SESSION THE CLIENT BUILDS for the watch: the harness's config
    // at the scripted hour -- run_client's own caseWatch branch, restated.
    render::SessionConfig start = config.session;
    start.timeOfDay = render::scriptedStartHour(config) * 3600;
    render::Session session(start);

    render::CaseWatchDirector director(std::move(drive), {});
    const int planned = director.plannedFrames();
    bool finished = false;
    std::int64_t simSteps = 0;
    const int spent = playWatch(director, session, &finished, &simSteps, planned + 600);

    // IT FINISHES, by its own hand, inside its own plan -- the demo's rule.
    CHECK(finished);
    CHECK(director.finished());
    CHECK(spent <= planned);
    // ZERO EXTRA SIMULATION: every hold, caption and card was a render-only
    // frame, so what reached the sim is the drive's steps and nothing else.
    CHECK(simSteps == driveSteps);
    // AND IT IS THE DRIVE'S TWIN: the replayed session stands exactly where
    // the recorded drive stood -- clock, tile, yaw, book, letters, the lot.
    CHECK(director.twinMatched(session));
    CHECK(render::watchFingerprintOf(session) == want);
    // The end line a watcher reads agrees with what the harness prints.
    const std::string summary = director.endSummary(session);
    CHECK(summary.find("case beats=8/8") != std::string::npos);
    CHECK(summary.find("mask=255") != std::string::npos);
    CHECK(summary.find("closed=yes") != std::string::npos);
    CHECK(summary.find("carry=no") != std::string::npos);
}

TEST_CASE("the watch is paced for an eye -- the demo's own neighbourhood") {
    render::CaseWatchDrive drive = render::recordCaseDrive(caseConfig());
    const render::CaseWatchDirector director(std::move(drive), {});
    const int seconds = director.plannedFrames() / 60;
    // test_demo.cpp's rule, verbatim: over a minute (nothing flashes past),
    // under five (a finished smaller thing).
    CHECK(seconds > 60);
    CHECK(seconds < 300);
}

TEST_CASE("a short ending stops the tape where the harness stops the drive") {
    const render::SmokeRunConfig config = caseConfig("night");
    render::CaseWatchDrive drive = render::recordCaseDrive(config);
    // The night shutter's own owed count: five beats, the box lead read from
    // a crouch -- test_case_line's numbers, off the recording.
    CHECK(drive.beats == 5);
    CHECK(drive.beatsWanted == 5);
    const render::WatchFingerprint want = drive.end;

    render::SessionConfig start = config.session;
    start.timeOfDay = render::scriptedStartHour(config) * 3600;
    render::Session session(start);
    render::CaseWatchDirector director(std::move(drive), {});
    bool finished = false;
    std::int64_t simSteps = 0;
    (void)playWatch(director, session, &finished, &simSteps, director.plannedFrames() + 600);
    CHECK(finished);
    CHECK(director.twinMatched(session));
    CHECK(render::watchFingerprintOf(session) == want);
}
