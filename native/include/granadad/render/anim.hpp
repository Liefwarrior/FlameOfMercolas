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
    /// close takes. Eight is a touch over a tenth of a second at
    /// kStepsPerSecond (60) -- long enough to read as motion, short enough
    /// that a menu never feels laggy to open, which is the one thing polish
    /// must never cost a player who is trying to play the game.
    explicit EasedToggle(std::int32_t riseSteps = 8, std::int32_t fallSteps = 8) noexcept;

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

}  // namespace granadad::render
