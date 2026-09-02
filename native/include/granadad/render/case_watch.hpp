#pragma once

// CASE WATCH -- `--case-watch`: THE QUIET TENANT, played where a human can see
// it.
//
// WHY THIS EXISTS. `--case` is a drive harness: thousands of simulation steps
// at CPU speed, a summary line, exit. Run windowed it finishes in an instant --
// the owner asked to WATCH the courier case and saw nothing. The demo is the
// build's one watchable artefact, and it is watchable by construction: ONE
// simulation step per rendered frame, throttled on a self-correcting frame
// deadline (main.cpp's demo gate), so the route plays at human speed and demo
// frame N is the same picture on every machine.
//
// SO THE CASE BORROWS EXACTLY THAT, AND INVENTS NO SECOND PACING SYSTEM. The
// scripted drive runs once, headless, with Session's recorder attached
// (recordCaseDrive, session.hpp) -- the identical runCaseLine `--case` runs,
// down to the step. This director then REPLAYS the tape through main.cpp's own
// demo gate: a Step op is one simulation step on one rendered frame; an
// instantaneous op (a punch, the letters toggle, the clock skip) executes
// between steps and buys itself a few RENDER-ONLY frames to be seen in. Those
// hold frames step the simulation zero times, which is the whole determinism
// argument: the replayed session receives EXACTLY the drive's op sequence, so
// it ends where the drive ended -- twinMatched() checks, the client prints the
// verdict, and test_case_watch.cpp gates it.
//
// THE WAIT IS COMPRESSED BUT VISIBLE, on the demo's own precedent. The drive
// waits to two in the morning with one skipToHour -- the same call a WAIT pick
// spends, and the same act the demo's Clock beat performs instantly before its
// night section. Playing eight hours for real is not a tutorial, it is a
// vigil; so the watch does what the demo does: says what is about to happen
// (the caption), jumps, and then HOLDS on the darkened room so the cut reads
// as a cut and not as a glitch.

#include <cstdint>
#include <filesystem>
#include <string>

#include "granadad/render/anim.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/session.hpp"

namespace granadad::render {

class CaseWatchDirector {
  public:
    /// `drive`: the recorded tape (recordCaseDrive). `shotDir` empty takes no
    /// pictures; named, each landed beat writes one PNG into it.
    CaseWatchDirector(CaseWatchDrive drive, std::filesystem::path shotDir);

    /// What the frame loop should do this frame: step the simulation `steps`
    /// times (always 0 or 1) with `move`, or -- when `running` goes false --
    /// close the window, exactly the demo's own ending.
    struct Tick {
        sim::MoveInput move;
        std::int32_t steps = 0;
        bool running = true;
    };
    /// Once per rendered frame, BEFORE the session is stepped -- the
    /// DemoDirector contract, kept so main.cpp routes both through one gate.
    [[nodiscard]] Tick advance(Session& session);

    /// THE CARD OWNS THE FRAME while this is true -- DemoDirector's own rule,
    /// for the client's setHudStandDown.
    [[nodiscard]] bool cardOwnsFrame() const noexcept;

    /// The card and the caption, over the finished frame -- the demo's own
    /// panes (drawRouteCaption/drawRouteCard), not a second vocabulary.
    void drawOverlay(Framebuffer& target, const Session& session) const;

    /// Writes the PNG the last advance armed, if any. After drawOverlay, so a
    /// captured frame is exactly the frame presented.
    void shutter(const Framebuffer& target);

    /// The route ended by its own hand (as opposed to ESC).
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] const CaseWatchDrive& drive() const noexcept { return drive_; }
    [[nodiscard]] std::int64_t frameIndex() const noexcept { return frame_; }

    /// How many frames the whole watch will spend, holds and cards included --
    /// a pure function of the tape, computable before a window exists, so the
    /// banner can promise a runtime and test_case_watch can bound one.
    [[nodiscard]] std::int32_t plannedFrames() const noexcept;

    /// The same fields the harness's `| case ...` segment prints, read off the
    /// REPLAYED session (beats/mask off the drive's own marks) -- so the line
    /// the watch ends on is comparable, word for word, with what `--case`
    /// prints for the same errand.
    [[nodiscard]] std::string endSummary(const Session& session) const;

    /// The replayed session ended exactly where the drive ended.
    [[nodiscard]] bool twinMatched(const Session& session) const;

  private:
    /// Executes one non-Step op on the session and answers how many
    /// render-only frames it buys itself to be seen in.
    std::int32_t execute(const WatchOp& op, Session& session);
    /// The pacing table, stateless half: what a given op holds for. `letters
    /// open` differs from `letters closed`, which is the one bit of state the
    /// planner below replays for itself.
    [[nodiscard]] static std::int32_t holdFor(const WatchOp& op, bool lettersOpen) noexcept;

    CaseWatchDrive drive_;
    std::filesystem::path shotDir_;

    enum class Phase : std::uint8_t { Title, Ops, Outro, Done };
    Phase phase_ = Phase::Title;
    std::size_t at_ = 0;
    std::int32_t held_ = 0;
    std::int32_t hold_ = 0;
    std::int64_t frame_ = 0;
    bool finished_ = false;
    /// Letters-tile parity: runCaseLine opens the sheet and closes it again
    /// with the same toggle, and the two deserve different holds.
    bool lettersOpen_ = false;

    /// The shutter: armed by a BeatLanded op, fired a few frames later so the
    /// plate the beat raised has eased in -- DemoBeat::shotAt's lesson.
    const char* shotName_ = nullptr;
    std::int32_t shotIn_ = 0;
    const char* pending_ = nullptr;

    std::string caption_;
    std::string cardLine_;
    std::string cardSub_;
    EasedToggle card_{18, 18};
    EasedToggle captionFade_{14, 14};
};

}  // namespace granadad::render
