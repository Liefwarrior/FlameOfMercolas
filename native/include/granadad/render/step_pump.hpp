#pragma once

// Wall-clock in, a whole number of movement steps out — and not one mouse count
// lost on the way.
//
// THE BUG THIS EXISTS TO NOT HAVE
//
// The body advances on a fixed 60 Hz cadence whatever the frame rate does, so
// the client runs an accumulator and calls step() a whole number of times. A
// frame's mouse motion is a per-FRAME quantity, so it must be applied to
// exactly ONE of those steps: apply it to all of them and a slow frame
// multiplies the sensitivity by the frame time, which is the classic mouse-look
// bug.
//
// S1 solved that half and opened the other. `input` was a frame-local, the
// deltas accumulated into it, and the step loop `while (accumulator >= step)`
// may not run at all — at any frame rate above 60 fps it frequently does not.
// The delta then died with the frame. No vsync was set, so this fired the
// moment the renderer got faster than 60 fps; it went unseen only because the
// measurements were taken at 25–47 fps.
//
// So the look delta is PENDING STATE, not a frame local. It is consumed by the
// first step of a batch and carried across frames that produce no step. Turning
// the mouse for a tenth of a second at 200 fps therefore produces the same
// total rotation as turning it at 30 fps, which is the whole contract.
//
// This lives in granadad-render rather than in the client for one reason: a
// loop that cannot be tested is a loop that gets this wrong again. The client
// keeps SDL and nothing else.

#include <cstdint>

#include "granadad/sim/angle.hpp"
#include "granadad/sim/player.hpp"

namespace granadad::render {

class StepPump {
public:
    /// `stepsPerSecond` is the body's own cadence; the default is the only
    /// value the game ever uses.
    explicit StepPump(std::int32_t stepsPerSecond = sim::kStepsPerSecond) noexcept;

    /// Adds mouse motion. May be called any number of times before advance();
    /// deltas accumulate.
    void addLook(sim::Angle yawDelta, sim::Angle pitchDelta) noexcept;

    /// Advances the clock by one frame and returns how many movement steps are
    /// due. Never negative; capped at maxStepsPerFrame() so a stalled window or
    /// a breakpoint does not fast-forward the game.
    [[nodiscard]] std::int32_t advance(double frameSeconds) noexcept;

    /// The input for the next step of the batch. `held` carries the keyboard
    /// state; the pending look is folded into the FIRST call after an advance
    /// and cleared, so later steps in the same batch turn by the keyboard rate
    /// alone.
    [[nodiscard]] sim::MoveInput nextStepInput(const sim::MoveInput& held) noexcept;

    /// Look that no step has consumed yet. Non-zero here across a frame
    /// boundary is the carry that S1 dropped.
    [[nodiscard]] sim::Angle pendingYaw() const noexcept { return pendingYaw_; }
    [[nodiscard]] sim::Angle pendingPitch() const noexcept { return pendingPitch_; }

    /// Longest stretch of simulated time one frame may catch up on, in seconds.
    /// After a window drag, dropping the missing steps is better than
    /// replaying them in a burst.
    [[nodiscard]] static constexpr double maxCatchUpSeconds() noexcept { return 0.25; }

private:
    double secondsPerStep_;
    double accumulator_ = 0.0;
    sim::Angle pendingYaw_ = 0;
    sim::Angle pendingPitch_ = 0;
};

}  // namespace granadad::render
