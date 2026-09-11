#pragma once

// THE VIEWMODEL -- the player's own hands, as the V lane will fill it in.
//
// A camera-attached rig (arms, and later a weapon) drawn LAST in a second
// 3D pass with its own near clip, so it never pokes through a wall. Its
// state machine reads only public sim getters -- Tavern::playerCombatIdle(),
// playerChargeSteps(), playerChargeHard(), playerBlocking() and the
// release edge the client already sends -- and maps the four combat verbs
// onto clips:
//
//     idle      loop
//     charging  scrub into the punch wind-up by chargeSteps; hold at the
//               hard-tier frame once playerChargeHard
//     release   play the light or the hard swing at 1.0x
//     block     the block loop while playerBlocking
//     cast      the touch-cast gesture
//     hit       the flinch, over everything else
//
// THIS LANE SHIPS THE MACHINE'S SHAPE with a deterministic, integer-clocked
// stub body; the V lane wires the sim getters, the clip table and the arms
// glb, and adds tests/test_viewmodel.cpp.

#include <cstdint>

#include "granadad/render3d/scene.hpp"

namespace granadad::render3d {

enum class ViewmodelState : std::uint8_t {
    Idle,
    Charging,
    ChargedHard,
    SwingLight,
    SwingHard,
    Block,
    Cast,
    Hit,
};

/// What the machine is told each MOVEMENT STEP, read off the sim.
struct ViewmodelInputs {
    /// Tavern::playerChargeSteps() -- 0 when the hand is not held down.
    std::int32_t chargeSteps = 0;
    /// Tavern::playerChargeHard().
    bool chargeHard = false;
    /// Tavern::playerBlocking().
    bool blocking = false;
    /// The release edge this step, and which tier it resolved to.
    bool releasedLight = false;
    bool releasedHard = false;
    /// Cast fired this step.
    bool cast = false;
    /// A blow landed on the player this step.
    bool hit = false;
};

struct ViewmodelPose {
    ViewmodelState state = ViewmodelState::Idle;
    /// Steps spent in the current state. What a clip scrubs by.
    std::int32_t stateSteps = 0;
};

/// Integer-clocked, one step() per movement step, so a settled capture is
/// byte-identical on every machine. One-shot states (swings, cast, hit) run
/// for their own step counts and fall back to whatever the held inputs say.
class ViewmodelMachine {
public:
    static constexpr std::int32_t kSwingSteps = 18;
    static constexpr std::int32_t kCastSteps = 24;
    static constexpr std::int32_t kHitSteps = 12;

    ViewmodelPose step(const ViewmodelInputs& in) noexcept;
    [[nodiscard]] const ViewmodelPose& pose() const noexcept { return pose_; }

private:
    ViewmodelPose pose_;
    std::int32_t oneShotLeft_ = 0;
};

}  // namespace granadad::render3d
