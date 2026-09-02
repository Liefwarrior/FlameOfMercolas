#pragma once

// TASK #83: MENU AND HUD TRANSITION POLISH.
//
// Everything in this game's menus used to be a light switch: a panel was
// fully on the frame the step its `open` flag went true, and fully gone the
// step it went false. That reads fine for a HUD row that is either true or
// not -- the health bar does not need to ease in -- but a whole panel
// (dialogue, the casebook, the keys page, options, the pause menu, the
// character sheet -- they are all one widget, see dialogue_view.hpp) popping
// in and out is the exact "menus and HUD elements that pop in/out with no
// transition" complaint the task names. And the alert row has the identical
// problem the other way round: it is on screen at full strength for six
// seconds and then gone on the seventh frame, which is a a prompt that
// disappears rather than one that was dismissed.
//
// EasedToggle is the one small piece of state that fixes both: a value that
// eases toward an open or closed target instead of snapping to it.
//
// STEPS, NOT SECONDS -- same reason render::HoldToggle counts movement steps
// and not milliseconds: identical behaviour at 30 frames a second and at 300,
// and identical behaviour on a machine three times as fast, which is the only
// way a captured frame stays reproducible and the twin-run gate stays honest.
// Nothing in this file is simulation state -- it never reaches PhasedEngine
// and it is never hashed -- but it IS drawn, and a captured frame is only
// evidence if the same scripted session produces the same frame every time it
// is asked to. See body_->stepCount()/60.0F in Session::drawFrame and
// Session::dialogueView, which already make the identical promise for the
// lamp flicker and the cursor's own breathing highlight.

#include <cstdint>

namespace granadad::render {

// ---------------------------------------------------------------------------
// UI-EA-SPEC sec. 3: THE ONE TRANSITION GRAMMAR'S DURATIONS, NAMED ONCE.
//
// "Everything is a veil or a plate; nothing snaps" -- and every duration in
// that grammar is one of the four constants below, shared across the three
// lanes (HUD's plates and tutor bands, PAGES' page compositions, FLOW's
// open/close and commit routing) so no surface can drift onto its own private
// timing. All of them are STEPS, per this file's own header: render-side,
// never hashed, identical at every frame rate.
// ---------------------------------------------------------------------------

/// One page open or close: eight steps, a touch over an eighth of a second at
/// kStepsPerSecond (60). This has been EasedToggle's shipped default since
/// task #83; naming it makes the spec's "durations are constants, named once,
/// shared" true by construction -- and pins it against a well-meaning tune-up
/// that would move every committed frame in the game at once.
inline constexpr std::int32_t kPageEaseSteps = 8;

/// An EVENT-tier plate (place, case, clock, purse, room) holds this long
/// after its edge before easing back down -- ~2.5 seconds.
inline constexpr std::int32_t kPlateHoldSteps = 150;

/// A TUTOR-tier band (nav bands, key legends, instruction copy) holds this
/// long when raised -- on page open, on device change, on an unrecognized
/// press -- before easing down to bare keycaps. ~3 seconds.
inline constexpr std::int32_t kTutorHoldSteps = 180;

/// Hesitation is the request for help: this much idle on a page re-raises its
/// tutor band. ~5 seconds.
inline constexpr std::int32_t kIdleWakeSteps = 300;

/// The at-rest shutter: how many zero-input steps a default screenshot runs
/// before capturing, so the frame it takes is the one the word budgets bind
/// -- every tutor band raised at page-open has held its kTutorHoldSteps and
/// eased fully down (180 + 8), and the kIdleWakeSteps re-raise has not yet
/// arrived (300). 240 sits squarely inside that window: ~4 seconds.
/// `--settle-steps=0` photographs the raised state instead.
inline constexpr std::int32_t kCaptureRestSteps = 240;

/// A steps-based ease between closed (0) and open (1).
///
/// NEVER EXACTLY 0 THE INSTANT setTarget(true) OPENS SOMETHING THAT WAS FULLY
/// CLOSED. A fade that starts invisible is a flash on the very first frame,
/// not a fade -- so opening from rest carries the first tick of its own rise
/// for free, and the frame drawn the instant a key is pressed already shows
/// something. See the .cpp for the exact amount.
class EasedToggle {
public:
    /// riseSteps/fallSteps: how many step()/advance() calls a full open or
    /// close takes. kPageEaseSteps (eight) is a touch over a tenth of a
    /// second at kStepsPerSecond (60) -- long enough to read as motion, short
    /// enough that a menu never feels laggy to open, which is the one thing
    /// polish must never cost a player who is trying to play the game. The
    /// default IS the named constant now, so the one transition grammar and
    /// this class cannot quietly disagree.
    explicit EasedToggle(std::int32_t riseSteps = kPageEaseSteps,
                         std::int32_t fallSteps = kPageEaseSteps) noexcept;

    /// Snaps straight to fully open or fully closed, with no animation to
    /// play. What a Session uses at construction: a session built with the
    /// casebook already open (SessionConfig::openingPage) draws it at full
    /// strength on frame one, because nobody pressed a key to open it and
    /// there is nothing to ease from.
    void snapTo(bool open) noexcept;

    /// Starts easing toward `open`. IDEMPOTENT: calling it again with the
    /// current target changes nothing, so a caller that re-asserts "still
    /// open" every frame (which conversingNow()-driven code does) never
    /// resets the animation it is already mid-way through. Reversing
    /// direction resumes from wherever the fade had got to, so a panel
    /// closed and immediately reopened does not pop back to fully open --
    /// it keeps easing from whatever it was showing.
    void setTarget(bool open) noexcept;

    /// One step's worth of easing toward the current target. Call once per
    /// Session::step(), never from a const drawing path -- see the file
    /// header on why this is counted in steps and not frames drawn.
    void advance() noexcept;

    /// 0 (closed) .. 1 (open).
    [[nodiscard]] float value() const noexcept { return value_; }

    [[nodiscard]] bool target() const noexcept { return target_; }

    /// True once value() has reached its target exactly and there is nothing
    /// left for advance() to do.
    [[nodiscard]] bool settled() const noexcept;

private:
    std::int32_t riseSteps_;
    std::int32_t fallSteps_;
    bool target_ = false;
    float value_ = 0.0F;
};

// INNOVATION SPRINT (item #3): SOME IMPACT, TASTEFULLY. Grep confirmed this
// build has no screen shake, flash or hit-stop anywhere: a punch lands, a
// blow lands on the player, a bouncer shouts across the room, and nothing on
// screen has ever weighed any of it. EasedToggle is the wrong shape for that
// -- it is a STATE (open or closed, held until told otherwise) and a punch
// landing is an EVENT (happens once, at an instant, and is over). Restating
// it as an EasedToggle you flip on and then immediately flip back off would
// work by accident and read as a hack to the next person who has to
// understand why.
//
// ImpactPulse is the one small piece of state that fixes that: it jumps to
// full strength the instant trigger() is called and eases back down to
// nothing over a short, fixed run of advance() calls, with nothing holding
// it open. RESTRAINED BY CONSTRUCTION, not by convention: there is no way to
// call this that produces a loop or a hold -- the peak is a single instant
// and the decay is the whole of what happens after it, which is what keeps
// every caller's flash "a few frames" rather than "however long I forget to
// turn it off".
class ImpactPulse {
public:
    /// decaySteps: how many advance() calls the pulse takes to reach zero
    /// from full strength. Short on purpose -- this is punctuation for a
    /// moody, text-forward investigation game, not a fighting game's hit
    /// spark, and every caller of this class picks a value on the order of a
    /// tenth of a second at kStepsPerSecond (60), not longer.
    explicit ImpactPulse(std::int32_t decaySteps = kPageEaseSteps) noexcept;

    /// Jumps straight back to full strength, even mid-decay. A flurry of
    /// punches restacks the flash rather than waiting for an earlier one to
    /// finish fading -- the same "the newest thing wins" rule a fresh say()
    /// already gives Session::message_.
    void trigger() noexcept;

    /// One step's worth of decay. Call once per Session::step(), same as
    /// EasedToggle::advance() and for the identical reason -- see anim.hpp's
    /// own header on why this is counted in steps and not draw calls.
    void advance() noexcept;

    /// 1 the instant trigger() is called, easing down to 0 and sitting there
    /// until triggered again.
    [[nodiscard]] float value() const noexcept { return value_; }

private:
    std::int32_t decaySteps_;
    float value_ = 0.0F;
};

}  // namespace granadad::render
