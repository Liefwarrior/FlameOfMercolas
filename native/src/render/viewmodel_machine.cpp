#include "granadad/render/viewmodel_machine.hpp"

namespace granadad::render {

ViewmodelPose ViewmodelMachine::step(const ViewmodelInputs& in) noexcept {
    // Priority, highest first: a hit interrupts anything; a release or a
    // cast starts a one-shot; a running one-shot plays out; then the held
    // verbs -- block over charge -- and idle when nothing is held.
    ViewmodelState next = pose_.state;
    std::int32_t oneShot = oneShotLeft_;
    bool swingStarted = false;

    if (in.hit) {
        next = ViewmodelState::Hit;
        oneShot = kHitSteps;
    } else if (in.releasedHard) {
        next = ViewmodelState::SwingHard;
        oneShot = kSwingSteps;
        swingStarted = true;
    } else if (in.releasedLight) {
        next = ViewmodelState::SwingLight;
        oneShot = kSwingSteps;
        swingStarted = true;
    } else if (in.cast && !viewmodelStateOneShot(pose_.state)) {
        next = ViewmodelState::Cast;
        oneShot = kCastSteps;
    } else if (viewmodelStateOneShot(pose_.state) && oneShot > 0) {
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
    if (swingStarted) {
        // A swing thrown on top of a swing still in flight (impossible from
        // the sim -- recovery outlasts kSwingSteps -- but the machine does
        // not rely on it) restarts the clip on the other hand.
        pose_.stateSteps = 0;
        ++pose_.swingSeq;
    }
    pose_.chargeSteps = in.chargeSteps;
    if (viewmodelStateOneShot(next) && oneShot > 0) {
        --oneShot;
    }
    oneShotLeft_ = oneShot;
    return pose_;
}

std::string_view viewmodelStateName(ViewmodelState state) noexcept {
    switch (state) {
        case ViewmodelState::Idle: return "idle";
        case ViewmodelState::Charging: return "charging";
        case ViewmodelState::ChargedHard: return "charged_hard";
        case ViewmodelState::SwingLight: return "swing_light";
        case ViewmodelState::SwingHard: return "swing_hard";
        case ViewmodelState::Block: return "block";
        case ViewmodelState::Cast: return "cast";
        case ViewmodelState::Hit: return "hit";
    }
    return "idle";
}

}  // namespace granadad::render
