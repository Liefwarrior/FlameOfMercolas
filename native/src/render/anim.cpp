#include "granadad/render/anim.hpp"

#include <algorithm>

namespace granadad::render {

EasedToggle::EasedToggle(std::int32_t riseSteps, std::int32_t fallSteps) noexcept
    : riseSteps_(std::max<std::int32_t>(1, riseSteps)),
      fallSteps_(std::max<std::int32_t>(1, fallSteps)) {}

void EasedToggle::snapTo(bool open) noexcept {
    target_ = open;
    value_ = open ? 1.0F : 0.0F;
}

void EasedToggle::setTarget(bool open) noexcept {
    if (target_ == open) {
        return;
    }
    target_ = open;
    // THE BUMP. Opening from a dead stop (value_ exactly 0) carries the first
    // tick of its own rise immediately, so the very first frame drawn after
    // the key that opened it already shows something rather than nothing --
    // see the header on why a fade that starts invisible is a flash.
    // Reopening mid-close (value_ already above 0) needs no such bump: it is
    // already visible and simply changes direction from wherever it was.
    if (target_ && value_ <= 0.0F) {
        value_ = 1.0F / static_cast<float>(riseSteps_);
    }
}

void EasedToggle::advance() noexcept {
    // SNAPPED TO THE BOUNDARY, NOT CLAMPED TO IT. 1.0F / riseSteps_ is not
    // exactly representable for most step counts (a fifth, a seventh, ...),
    // so riseSteps_ additions of it land a float epsilon short of or past 1 --
    // ctest caught this directly: five steps of 1.0F/5 subtracted from 1.0F
    // left 2.98e-08F, which is neither <= 0 nor == 0. std::min/std::max alone
    // clamp a value that OVERSHOOTS the boundary; they do nothing for one
    // that undershoots it by a rounding error, which is the failure here. So
    // this checks for "close enough" on both sides and snaps to the exact
    // boundary, which is also the only value settled() below can ever trust.
    constexpr float kEpsilon = 1.0F / 4096.0F;
    if (target_) {
        value_ += 1.0F / static_cast<float>(riseSteps_);
        value_ = value_ >= 1.0F - kEpsilon ? 1.0F : std::max(0.0F, value_);
    } else {
        value_ -= 1.0F / static_cast<float>(fallSteps_);
        value_ = value_ <= kEpsilon ? 0.0F : std::min(1.0F, value_);
    }
}

bool EasedToggle::settled() const noexcept {
    return target_ ? value_ >= 1.0F : value_ <= 0.0F;
}

}  // namespace granadad::render
