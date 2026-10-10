#pragma once

// THE VIEWMODEL MACHINE -- what the player's own hands are doing.
//
// Render-side state, exactly like the ImpactPulses beside it in Session:
// stepped ONCE PER MOVEMENT STEP by Session::step(), fed only public sim
// getters (Tavern::playerChargeSteps / playerChargeHard / playerBlocking) and
// the three events the session already sees on their own edges -- a swing
// released (Session::attackUp, with the tier the room resolved), a cast
// thrown (Session::castEquipped, attempted rather than refused) and a blow
// landing on the player (the hp comparison in step()). Never hashed: the
// hands are a courtesy to the eye, and the sim never reads them back.
//
// WHY IT LIVES IN granadad-render AND NOT render3d. The 3D pass draws the
// pose (render3d/viewmodel.hpp), but the pose has to be a deterministic
// function of the SESSION -- a --screenshot taken after a scripted punch
// must photograph the same frame of the swing on every machine, and the
// smoke path never runs a client loop. So the machine is stepped where the
// sim is stepped, and render3d reads Session::viewmodel() like any other
// getter. (granadad-render cannot link render3d-core; render3d-core links
// this.)
//
// THE CLIP TABLE, in enum order, IS THE ASSET CONTRACT: the exported
// viewmodel glb files carry their animations at index 0..7 in exactly this
// order (idle, charging, charged_hard, swing_light, swing_hard, block,
// cast, hit -- the asset lane's job file), so static_cast<int>(state) is
// the animation the adapter plays. Nothing may be inserted in the middle.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace granadad::render {

enum class ViewmodelState : std::uint8_t {
    /// Fists raised, loop.
    Idle,
    /// The wind-up, scrubbed by the charge fraction (chargeSteps over
    /// sim::kHardSwingHoldSteps) rather than played on its own clock.
    Charging,
    /// Held at the hard-tier frame, with a tremor.
    ChargedHard,
    /// The tap's punch, one shot over kSwingSteps.
    SwingLight,
    /// The hard swing -- further, with a hook -- one shot over kSwingSteps.
    SwingHard,
    /// The guard, held while Tavern::playerBlocking().
    Block,
    /// The touch-cast gesture, one shot over kCastSteps.
    Cast,
    /// The flinch, one shot over kHitSteps, over everything else.
    Hit,
};
inline constexpr std::size_t kViewmodelStateCount = 8;

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
    /// Cast thrown this step (attempted: a fizzle still throws the hand; a
    /// refusal does not).
    bool cast = false;
    /// A blow landed on the player this step and the guard did not catch it.
    bool hit = false;
    /// STANCE (combat feel): Tavern::playerHandsUp() -- fighting mode. NOT
    /// a state of this machine (the state enum is the glb clip order and
    /// may not grow): a fact the Idle pose reads -- raised fists while
    /// true, the arms hanging with the knuckles at the bottom edge while
    /// false. Every other state has the hands up by the sim's own rules (a
    /// charge, a guard and a caught blow all raise them). Defaults to up,
    /// so a machine fed without the stance poses as it did before it.
    bool handsUp = true;
};

struct ViewmodelPose {
    ViewmodelState state = ViewmodelState::Idle;
    /// Steps spent in the current state. What a clip scrubs by.
    std::int32_t stateSteps = 0;
    /// Swings started so far. Even swings throw the right hand, odd the
    /// left -- the alternation the punch clips need.
    std::int32_t swingSeq = 0;
    /// The sim's charge count this step, carried so the wind-up can scrub
    /// by the real fraction rather than by stateSteps.
    std::int32_t chargeSteps = 0;
    /// STANCE: ViewmodelInputs::handsUp, carried. Read by the Idle pose.
    bool handsUp = true;
    /// Steps since handsUp last flipped, saturating at kStanceSaturated --
    /// what the Idle pose eases the hands up or down over (kStanceSteps),
    /// so LOWER HANDS reads as a motion and not a cut. Saturated at rest
    /// and on an unstepped machine, so a hand-built pose is at its rest.
    std::int32_t stanceSteps = 1 << 20;
};

/// Integer-clocked, one step() per movement step, so a settled capture is
/// byte-identical on every machine. One-shot states (swings, cast, hit) run
/// for their own step counts and fall back to whatever the held inputs say.
class ViewmodelMachine {
public:
    static constexpr std::int32_t kSwingSteps = 18;
    static constexpr std::int32_t kCastSteps = 24;
    static constexpr std::int32_t kHitSteps = 12;
    /// The stance ease: steps the Idle pose takes to raise or lower the
    /// hands after playerHandsUp() flips.
    static constexpr std::int32_t kStanceSteps = 8;
    static constexpr std::int32_t kStanceSaturated = 1 << 20;

    ViewmodelPose step(const ViewmodelInputs& in) noexcept;
    [[nodiscard]] const ViewmodelPose& pose() const noexcept { return pose_; }

private:
    ViewmodelPose pose_;
    std::int32_t oneShotLeft_ = 0;
    /// False until the first step: that step ADOPTS the stance it is fed
    /// rather than easing to it, so a session boots with its hands where
    /// the sim says they are (down) instead of lowering them on frame one.
    bool primed_ = false;
};

/// The clip's name in the exported glb (and in the asset lane's job file):
/// idle, charging, charged_hard, swing_light, swing_hard, block, cast, hit.
[[nodiscard]] std::string_view viewmodelStateName(ViewmodelState state) noexcept;

/// True for a state that plays once over its own step count and holds its
/// last frame until the machine moves on; false for one that loops or is
/// scrubbed (Idle, Charging, ChargedHard, Block).
[[nodiscard]] constexpr bool viewmodelStateOneShot(ViewmodelState state) noexcept {
    return state == ViewmodelState::SwingLight || state == ViewmodelState::SwingHard ||
           state == ViewmodelState::Cast || state == ViewmodelState::Hit;
}

/// How many steps a one-shot state runs for; 0 for the held states.
[[nodiscard]] constexpr std::int32_t viewmodelStateSteps(ViewmodelState state) noexcept {
    switch (state) {
        case ViewmodelState::SwingLight:
        case ViewmodelState::SwingHard:
            return ViewmodelMachine::kSwingSteps;
        case ViewmodelState::Cast:
            return ViewmodelMachine::kCastSteps;
        case ViewmodelState::Hit:
            return ViewmodelMachine::kHitSteps;
        case ViewmodelState::Idle:
        case ViewmodelState::Charging:
        case ViewmodelState::ChargedHard:
        case ViewmodelState::Block:
        default:
            return 0;
    }
}

}  // namespace granadad::render
