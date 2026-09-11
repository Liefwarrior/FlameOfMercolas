#pragma once

// ACTOR INSTANCING -- the people, as the A lane will fill it in.
//
// Every body in the ward (WardPopulation's ~600) and the Gilded Gull's
// seventeen becomes one Instance: a rig mesh id per WardType, a position
// interpolated the way session.cpp's wardSprites already interpolates
// (prevX -> x by stepsThisSecond / kStepsPerSecond, render-only floats,
// never written back), a yaw from the BAM facing, and an animation clip +
// frame the adapter plays. Only bodies inside the fog radius are skinned;
// the rest draw in a rest pose.
//
// THIS LANE SHIPS THE CONTRACT AND A STUB. actorInstances() returns nothing;
// the A lane fills it from Session::people() / tavern() and proves "an actor
// is drawn where the simulation says the actor is" on the rlsw frame.

#include <cstdint>
#include <vector>

#include "granadad/render3d/scene.hpp"

namespace granadad::render {
class Session;
struct Camera;
}  // namespace granadad::render

namespace granadad::render3d {

/// The rig a body is drawn with, one per WardType (16) plus the Gull's own.
/// Ids in the actor range of scene.hpp.
[[nodiscard]] constexpr std::uint32_t actorRigMeshId(std::uint32_t rig) noexcept {
    return kActorMeshIdBase + rig;
}

/// Which clip a body plays, decided off sim state alone:
/// (x, y) != (prevX, prevY) walks, otherwise idles; combat verbs on top.
enum class ActorClip : std::uint8_t { Idle, Walk, PunchLeft, PunchRight, Block, Hit, Recover, Death };

struct ActorInstance {
    Instance instance;
    ActorClip clip = ActorClip::Idle;
    /// Clip frame, derived from body().stepCount() and the actor id (so a
    /// crowd does not march in step). Render-only.
    std::uint32_t clipFrame = 0;
    /// Inside the fog radius: skinned. Outside: rest pose, no animation.
    bool skinned = false;
};

/// The instances for one frame. STUB: empty. The A lane fills it.
[[nodiscard]] std::vector<ActorInstance> actorInstances(const render::Session& session,
                                                        const render::Camera& view);

}  // namespace granadad::render3d
