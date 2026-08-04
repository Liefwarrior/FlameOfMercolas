#include "granadad/render/step_pump.hpp"

#include <algorithm>

#include "granadad/sim/fixed.hpp"

namespace granadad::render {

StepPump::StepPump(std::int32_t stepsPerSecond) noexcept
    : secondsPerStep_(1.0 / static_cast<double>(std::max<std::int32_t>(1, stepsPerSecond))) {}

void StepPump::addLook(sim::Angle yawDelta, sim::Angle pitchDelta) noexcept {
    // wrap_add, not +: yaw is a BAM and a player spinning for long enough with
    // a stalled renderer could otherwise overflow a signed int, which is UB in
    // C++ even though the whole codebase is compiled -fwrapv.
    pendingYaw_ = sim::wrap_add(pendingYaw_, yawDelta);
    pendingPitch_ = sim::wrap_add(pendingPitch_, pitchDelta);
}

std::int32_t StepPump::advance(double frameSeconds) noexcept {
    if (frameSeconds > 0.0) {
        accumulator_ += frameSeconds;
    }
    accumulator_ = std::min(accumulator_, maxCatchUpSeconds());
    std::int32_t steps = 0;
    while (accumulator_ >= secondsPerStep_) {
        accumulator_ -= secondsPerStep_;
        ++steps;
    }
    return steps;
}

sim::MoveInput StepPump::nextStepInput(const sim::MoveInput& held) noexcept {
    sim::MoveInput input = held;
    input.yawDelta = pendingYaw_;
    input.pitchDelta = pendingPitch_;
    pendingYaw_ = 0;
    pendingPitch_ = 0;
    return input;
}

}  // namespace granadad::render
