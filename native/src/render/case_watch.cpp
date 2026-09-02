#include "granadad/render/case_watch.hpp"

#include <algorithm>
#include <sstream>
#include <string>

#include "granadad/render/capture.hpp"
#include "granadad/render/demo.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/casebook.hpp"

namespace granadad::render {
namespace {

// The watch's own clock, all of it in frames at the demo's fixed 60 Hz
// cadence -- so every number here is read as sixtieths of a second, exactly
// DemoBeat::frames' own unit.

/// The opening card: what this is, before anything moves.
constexpr std::int32_t kTitleFrames = 150;
/// The last plate gets looked at before the end card comes up...
constexpr std::int32_t kOutroHold = 90;
/// ...and the end card is the last thing on screen, the demo's own ending.
constexpr std::int32_t kEndCardFrames = 210;
/// How long after a beat lands the shutter goes: the plates ease in over
/// ~14 frames (EasedToggle's rise), so a shot on the landing frame itself
/// would photograph a plate that is not there yet -- DemoBeat::shotAt's
/// lesson, applied.
constexpr std::int32_t kShotDelay = 18;

/// One line per beat, raised as the beat begins -- the demo's caption pane,
/// in the city register, saying what is being looked at.
constexpr const char* kChapterCaptions[8] = {
    "ONNA, OUT OF THE MISSION: PAPER THAT COULD NOT WAIT.",
    "MAELL'S SHEET, READ WHERE PAPER IS READ.",
    "THE GILDED GULL, BY DAY. THE DOOR LEAD, ON FOOT.",
    "THE WAIT. THE GULL BARS ITS DOORS AT TWO.",
    "THE GUEST FLOOR, CROUCHED. EVERY PROBE IS NOISE.",
    "FINCH, PUT DOWN WITH FISTS. A BRAWL IS THE HOUSE'S OWN LAW.",
    "TAKE HIM UP.",
    "TO THE MISSION'S BACK ROOM. WALKING IN IS THE DELIVERY.",
};

/// The shutter's names, one per landed beat, in the errand's own order.
constexpr const char* kBeatShots[8] = {
    "watch-1-sheet-hand", "watch-2-sheet-read", "watch-3-gull-door", "watch-4-wait",
    "watch-5-box",        "watch-6-down",       "watch-7-carry",     "watch-8-delivered",
};

}  // namespace

CaseWatchDirector::CaseWatchDirector(CaseWatchDrive drive, std::filesystem::path shotDir)
    : drive_(std::move(drive)), shotDir_(std::move(shotDir)) {
    cardLine_ = "THE QUIET TENANT";
    cardSub_ = "A COURIER'S ERRAND, WATCHED";
}

std::int32_t CaseWatchDirector::holdFor(const WatchOp& op, bool lettersOpen) noexcept {
    switch (op.kind) {
        case WatchOpKind::Step:
            return 0;  // a step IS a frame; it holds nothing
        case WatchOpKind::Climb:
            return 0;  // the leap's own air steps follow on the tape
        case WatchOpKind::Drop:
            return 30;  // "DOWN 1 LEVEL." gets read before the feet move on
        case WatchOpKind::Punch:
            // The brawl's rhythm. The drive throws its punches back to back
            // (deliberately -- no steps between blows, so the bouncer cannot
            // cross the floor mid-beat); spacing them over render-only frames
            // shows the exchange without simulating a single extra step.
            return 9;
        case WatchOpKind::Interact:
            return 150;  // TAKE HIM UP -- the errand's one added press
        case WatchOpKind::Examine:
            return 150;  // the lead-read plate wants reading
        case WatchOpKind::CourierNow:
            return 150;  // the hail and the sheet plate
        case WatchOpKind::Letters:
            // Opening the sheet is an entrance; putting it down is not.
            return lettersOpen ? 30 : 60;
        case WatchOpKind::Topic:
            return 240;  // Maell's sheet, the case's central document, read
        case WatchOpKind::Casebook:
            return 90;
        case WatchOpKind::SkipHour:
            return 150;  // the dark room after the cut -- see the file header
        case WatchOpKind::Crouch:
            return 45;
        case WatchOpKind::Chapter:
            // The caption reads before the beat moves; the wait's gets longer
            // because its whole beat is one instant.
            return op.a == 3 ? 90 : 60;
        case WatchOpKind::BeatLanded:
            return kShotDelay + 6;  // the payoff frame, plate eased in, shot taken
    }
    return 0;
}

std::int32_t CaseWatchDirector::execute(const WatchOp& op, Session& session) {
    const std::int32_t pause = holdFor(op, lettersOpen_);
    switch (op.kind) {
        case WatchOpKind::Step:
            break;  // never reaches execute -- advance() owns Step
        case WatchOpKind::Climb:
            session.climb();
            break;
        case WatchOpKind::Drop:
            session.dropDown();
            break;
        case WatchOpKind::Punch:
            session.punch();
            break;
        case WatchOpKind::Interact:
            session.interact();
            break;
        case WatchOpKind::Examine:
            session.examine();
            break;
        case WatchOpKind::CourierNow:
            session.courierDeliverNow();
            break;
        case WatchOpKind::Letters:
            session.toggleLetters();
            lettersOpen_ = !lettersOpen_;
            break;
        case WatchOpKind::Topic:
            session.chooseVisibleTopic(static_cast<int>(op.a));
            break;
        case WatchOpKind::Casebook:
            session.toggleCasebook();
            break;
        case WatchOpKind::SkipHour:
            // THE CUT, DRESSED. The skip is the watch's one hard cut -- noon
            // to a barred, blacked-out taproom in a single frame, which the
            // owner reads as "teleporting ... double vision" however clean
            // the simulation underneath is. So the frame the clock jumps on
            // is already fully black and the dark room eases up over the
            // start of the hold that exists to look at it. The watch's own
            // veil_ and not Session::dressInstantCut, because this hold steps
            // the simulation zero times a frame -- see veil()'s header.
            veil_.snapTo(true);
            veil_.setTarget(false);
            session.skipToHour(static_cast<int>(op.a));
            break;
        case WatchOpKind::Crouch:
            session.toggleCrouch();
            break;
        case WatchOpKind::Chapter:
            if (op.a >= 0 && op.a < 8) {
                caption_ = kChapterCaptions[op.a];
            }
            break;
        case WatchOpKind::BeatLanded:
            if (op.a >= 0 && op.a < 8) {
                shotName_ = kBeatShots[op.a];
                shotIn_ = kShotDelay;
            }
            break;
    }
    return pause;
}

CaseWatchDirector::Tick CaseWatchDirector::advance(Session& session) {
    Tick tick;
    if (phase_ == Phase::Done) {
        tick.running = false;
        return tick;
    }
    ++frame_;
    if (!eyeSeeded_) {
        // The tape's early ops (the courier's hail, the sheet) run before its
        // first Step carries a yaw, and the watcher's head has to start
        // somewhere: both start where the session spawned looking.
        tapeYaw_ = session.body().yaw();
        eyeYaw_ = tapeYaw_;
        eyeSeeded_ = true;
    }

    // ARM THE SHUTTER for this frame, if a landed beat's delay just ran out --
    // ahead of everything else, so the frame that gets written is the frame
    // being composed right now, plate up.
    pending_ = nullptr;
    if (shotIn_ > 0 && --shotIn_ == 0) {
        pending_ = shotName_;
        shotName_ = nullptr;
    }

    // The card and the caption are STATE, eased -- the build's own overlay
    // convention, and the demo's exact targets: the card up over the title and
    // the ending, the caption up whenever it has words and no page or plate
    // owns its pixels.
    card_.setTarget(phase_ == Phase::Title || (phase_ == Phase::Outro && held_ >= kOutroHold));
    captionFade_.setTarget(phase_ == Phase::Ops && !caption_.empty() &&
                           !routeOverlayStandsDown(session));
    card_.advance();
    captionFade_.advance();
    // The veil decays on the watch's own clock -- once per rendered frame --
    // for the reason veil()'s header states: a hold steps the simulation zero
    // times, and a black screen that waits for a step would hold for the
    // whole hold.
    veil_.advance();

    switch (phase_) {
        case Phase::Title:
            if (++held_ >= kTitleFrames) {
                phase_ = Phase::Ops;
                held_ = 0;
            }
            return tick;
        case Phase::Ops: {
            if (hold_ > 0) {
                --hold_;
                return tick;
            }
            while (at_ < drive_.ops.size()) {
                const WatchOp& op = drive_.ops[at_];
                ++at_;
                if (op.kind == WatchOpKind::Step) {
                    // THE DRIVE'S OWN STEP, one per rendered frame -- with the
                    // yaw the drive's walker had set, restored first, because
                    // stepToward steers the head outside the input struct.
                    // composeView() will put the watcher's eased eye back on
                    // after the step; the SIM always steps under the tape.
                    tapeYaw_ = op.a;
                    session.body().setYaw(op.a);
                    tick.move = op.move;
                    tick.steps = 1;
                    return tick;
                }
                // EVERY OP EXECUTES UNDER THE TAPE'S OWN HEADING, not the
                // eased eye's: an examine, a punch, an interact all resolve
                // by where the body FACES, and the drive resolved them under
                // tapeYaw_ -- an op run under the eye's lagging heading could
                // aim at different ground and break the twin. composeView()
                // re-applies the eye before anything is drawn.
                session.body().setYaw(tapeYaw_);
                const std::int32_t pause = execute(op, session);
                if (pause > 0) {
                    // This frame shows the op landing; pause-1 more follow.
                    // None of them steps the simulation, which is what keeps
                    // the replay the drive's twin.
                    hold_ = pause - 1;
                    return tick;
                }
                // A zero-hold op (Climb) chains straight into whatever the
                // tape says next, the way the press and the leap are one act.
            }
            // The tape is spent. The last plate holds, then the end card.
            phase_ = Phase::Outro;
            held_ = 0;
            caption_.clear();
            cardLine_ = "THE QUIET TENANT";
            cardSub_ = drive_.beats < drive_.beatsWanted ? "THE ERRAND FELL SHORT"
                       : drive_.mask == 0xFF             ? "THE ERRAND IS PAID"
                                                         : "STOPPED WHERE IT WAS TOLD";
            return tick;
        }
        case Phase::Outro:
            if (++held_ >= kOutroHold + kEndCardFrames) {
                phase_ = Phase::Done;
                finished_ = true;
                tick.running = false;
            }
            return tick;
        case Phase::Done:
            break;
    }
    tick.running = false;
    return tick;
}

void CaseWatchDirector::composeView(Session& session) {
    if (!eyeSeeded_) {
        return;  // nothing has run; the first advance() seeds both headings
    }
    // The shortest signed turn from the eye to the tape, in the sim's own
    // BAM units -- DemoDirector's turnDelta, restated on the same wrap
    // arithmetic because that helper is the demo's private business.
    const std::int32_t raw = (tapeYaw_ - eyeYaw_) & (sim::kTurnFull - 1);
    const std::int32_t delta = raw > sim::kTurnHalf ? raw - sim::kTurnFull : raw;
    // The demo walker's own human rate (~4 degrees a frame). On a dogleg,
    // where the tape alternates +-90 degrees every step, the eye holds the
    // mean heading with a rate-wide sway instead of strobing; on a real
    // corner it comes round in under half a second; and once within one
    // turn it SNAPS EXACTLY onto the tape -- the convergence twinMatched()
    // depends on.
    constexpr std::int32_t kTurnRate = sim::kTurnFull / 90;
    if (delta > kTurnRate) {
        eyeYaw_ += kTurnRate;
    } else if (delta < -kTurnRate) {
        eyeYaw_ -= kTurnRate;
    } else {
        eyeYaw_ = tapeYaw_;
    }
    // Kept on the dial. The chase arithmetic above tolerates an unnormalized
    // heading (raw is masked), but the yaw handed to the body should be the
    // same wrapped BAM every other setYaw hands it.
    eyeYaw_ &= sim::kTurnFull - 1;
    session.body().setYaw(eyeYaw_);
}

bool CaseWatchDirector::cardOwnsFrame() const noexcept {
    return card_.value() > 0.01F && !cardLine_.empty();
}

void CaseWatchDirector::drawOverlay(Framebuffer& target, const Session& session) const {
    // The veil goes UNDER the caption and the card -- the same layering the
    // demo gets for free (Session::drawFrame dips before the director draws),
    // so "THE WAIT..." stays readable while the room underneath cuts to two
    // in the morning.
    drawRouteVeil(target, veil_.value());
    drawRouteCaption(target, caption_, captionFade_.value());
    drawRouteCard(target, cardLine_, cardSub_, session.placeLabel(), card_.value());
}

void CaseWatchDirector::shutter(const Framebuffer& target) {
    if (pending_ == nullptr) {
        return;
    }
    const std::string name = pending_;
    pending_ = nullptr;
    if (shotDir_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(shotDir_, ec);
    (void)writePng(target, (shotDir_ / (name + ".png")).string());
}

std::int32_t CaseWatchDirector::plannedFrames() const noexcept {
    // The same walk advance() makes, dry: every Step is one frame, every op
    // with a hold is `hold` frames, a zero-hold op is free, and the title and
    // the ending bracket it. Letters parity is the one bit of state replayed.
    std::int32_t frames = kTitleFrames + kOutroHold + kEndCardFrames;
    bool lettersOpen = false;
    for (const WatchOp& op : drive_.ops) {
        if (op.kind == WatchOpKind::Step) {
            frames += 1;
            continue;
        }
        frames += holdFor(op, lettersOpen);
        if (op.kind == WatchOpKind::Letters) {
            lettersOpen = !lettersOpen;
        }
    }
    return frames;
}

std::string CaseWatchDirector::endSummary(const Session& session) const {
    // The identical fields, in the identical order, as runSmoke's `| case ...`
    // segment -- beats and mask off the drive's own marks, everything else off
    // the REPLAYED session, so the line is only right when the replay is.
    const sim::Casebook& book = session.sheetBook();
    std::ostringstream out;
    out << "case beats=" << drive_.beats << '/' << drive_.beatsWanted << " mask=" << drive_.mask
        << " read=" << book.readCount() << '/' << book.known().size()
        << " dread=" << book.dread() << " tenant=" << (session.tenantDown() ? "down" : "up")
        << " carry=" << (session.sheetCarry() ? "yes" : "no")
        << " closed=" << (book.closed() ? "yes" : "no")
        << " live=" << (session.sheetCaseLive() ? "yes" : "no")
        << " letters=" << session.unlockedLetters().size();
    return out.str();
}

bool CaseWatchDirector::twinMatched(const Session& session) const {
    return watchFingerprintOf(session) == drive_.end;
}

}  // namespace granadad::render
