#include "granadad/render3d/viewmodel.hpp"

namespace granadad::render3d {

namespace {

[[nodiscard]] bool isOneShot(ViewmodelState state) noexcept {
    return state == ViewmodelState::SwingLight || state == ViewmodelState::SwingHard ||
           state == ViewmodelState::Cast || state == ViewmodelState::Hit;
}

}  // namespace

ViewmodelPose ViewmodelMachine::step(const ViewmodelInputs& in) noexcept {
    // Priority, highest first: a hit interrupts anything; a release or a
    // cast starts a one-shot; a running one-shot plays out; then the held
    // verbs -- block over charge -- and idle when nothing is held.
    ViewmodelState next = pose_.state;
    std::int32_t oneShot = oneShotLeft_;

    if (in.hit) {
        next = ViewmodelState::Hit;
        oneShot = kHitSteps;
    } else if (in.releasedHard) {
        next = ViewmodelState::SwingHard;
        oneShot = kSwingSteps;
    } else if (in.releasedLight) {
        next = ViewmodelState::SwingLight;
        oneShot = kSwingSteps;
    } else if (in.cast && !isOneShot(pose_.state)) {
        next = ViewmodelState::Cast;
        oneShot = kCastSteps;
    } else if (isOneShot(pose_.state) && oneShot > 0) {
        next = pose_.state;
    } else if (in.blocking) {
        next = ViewmodelState::Block;
        oneShot = 0;
    } else if (in.chargeSteps > 0) {
        next = in.chargeHard ? ViewmodelState::ChargedHard : ViewmodelState::Charging;
        oneShot = 0;
    } else {
        next = ViewmodelState::Idle;
        oneShot = 0;
    }

    if (next != pose_.state) {
        pose_.state = next;
        pose_.stateSteps = 0;
    } else {
        ++pose_.stateSteps;
    }
    if (isOneShot(next) && oneShot > 0) {
        --oneShot;
    }
    oneShotLeft_ = oneShot;
    return pose_;
}

}  // namespace granadad::render3d
