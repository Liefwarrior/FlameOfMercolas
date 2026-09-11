#pragma once

// ACTOR INSTANCING -- the people, in 3D.
//
// Every body in the ward (WardPopulation's ~600) and the Gilded Gull's
// seventeen becomes one ActorInstance (scene.hpp): a rig per WardType, a
// position interpolated EXACTLY the way session.cpp's wardSprites already
// interpolates (prevX -> x by stepsThisSecond / kStepsPerSecond, +0.5 to the
// tile centre; the Gull's own are Q8 straight out of the sim), a yaw from
// the BAM facing, and an animation clip + frame decided off sim state alone.
// Render-only floats derived from integers, never written back -- the
// 2026-07-31 ruling read exactly as written -- and every one of them lands
// in the hashed SceneDescription, so two sessions driven by the same script
// describe the same crowd byte for byte.
//
// THE RIG IS THE WARD TYPE. One look per kind of body, sixteen of them, and
// the taproom's roles borrow a WardType through render::figureForRole -- the
// same rule the sprite sheet applies, so there is one look for a person in
// this game rather than two. Each rig has:
//
//   - a PLACEHOLDER mesh, procedural, built here: a box figure (legs, torso,
//     head, and a nose block on the -Z face so the facing reads) sized off
//     render::figureScaleOf, shaded per face like the starter cube. This is
//     what every test and the docker gate draw, since content/art/lot-3d/ is
//     licensed and absent there. Its id is actorRigMeshId(rig).
//   - a FILE NAME, actorRigFile(rig): the glb the adapter looks for under
//     BackendConfig::modelDir at runtime (townsman / dockhand / watchman --
//     the asset lane's three humanoids; beasts have none yet and keep the
//     box). Present -> the skinned model is drawn in its place, animated by
//     clip index; absent -> the placeholder, silently.
//
// THE CLIP TABLE (Activity -> ActorClip) is the one opinion of what a body
// is seen doing, and clipForActivity / wardClip are pure so a case can pin
// it without a session.

#include <cstdint>
#include <string_view>
#include <vector>

#include "granadad/render3d/scene.hpp"
#include "granadad/sim/actor.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace granadad::render {
class Session;
struct Camera;
}  // namespace granadad::render

namespace granadad::render3d {

/// One rig per WardType (sim/ward_actors.hpp), sixteen of them.
inline constexpr std::uint32_t kActorRigCount = static_cast<std::uint32_t>(sim::kWardTypeCount);

/// The rig a body is drawn with. Ids in the actor range of scene.hpp.
[[nodiscard]] constexpr std::uint32_t actorRigMeshId(std::uint32_t rig) noexcept {
    return kActorMeshIdBase + rig;
}

/// The rig for a kind of body: its WardType value.
[[nodiscard]] constexpr std::uint8_t actorRigOf(sim::WardType type) noexcept {
    return static_cast<std::uint8_t>(type);
}

// actorRigFile(rig) and actorClipOneShot(clip) -- the two facts the adapter
// needs about a body -- live on scene.hpp, so the one raylib TU sees them
// without pulling a sim header in. Defined in actor_instances.cpp.

/// The animation's name inside the glb, for a loader that would rather look
/// up by name than by index (the asset manifest carries both).
[[nodiscard]] std::string_view actorClipName(ActorClip clip) noexcept;

/// THE CLIP TABLE for the Gull's roster, off sim::Activity alone:
/// Walking -> Walk; Brawling / Ejecting -> PunchLeft or PunchRight by the
/// swing sequence's parity (so consecutive swings alternate hands);
/// Downed -> Recover; Dead -> Death; everything else a body does standing
/// (Working, Drinking, Watching, Warning) -> Idle. Away is never drawn.
/// The sim has no NPC guard state and no per-body "just hit" flag in v1,
/// so Block and Hit are reachable by the viewmodel only -- said out loud.
[[nodiscard]] ActorClip clipForActivity(sim::Activity activity, std::int32_t swingSeq) noexcept;

/// The ward's people have no Activity: a body whose tile moved this tick
/// walks, a body on the same tile idles.
[[nodiscard]] constexpr ActorClip wardClip(bool movedThisTick) noexcept {
    return movedThisTick ? ActorClip::Walk : ActorClip::Idle;
}

/// The frame a body's clip is on: the player's step count (one step = one
/// keyframe at the glb's 60 fps) phase-shifted by the actor id, so a street
/// of walkers is not a chorus line. Integer arithmetic; the adapter wraps or
/// clamps it by the clip's own length.
[[nodiscard]] constexpr std::uint32_t actorClipFrame(std::int64_t stepCount,
                                                     std::int32_t actorId) noexcept {
    return static_cast<std::uint32_t>(stepCount + static_cast<std::int64_t>(actorId) * 17);
}

/// The placeholder figure for a rig: a closed box figure standing on y = 0,
/// centred on the origin in x and z, facing -Z (north) at yaw 0 with a nose
/// block on the front of the head. People are built 1.875 tiles tall (the
/// reference figure; Instance::scale carries the type's ratio, as it does for
/// a glb) and beasts at their own size. id = actorRigMeshId(rig), version 1.
[[nodiscard]] MeshData buildActorPlaceholder(std::uint8_t rig);

/// Puts every rig's placeholder into the description if it is not there
/// already (checked by id + version, so a per-frame call costs sixteen
/// lookups).
void putActorRigs(SceneDescription& scene);

struct ActorSceneParams {
    /// Bodies further than this from the eye (XZ, tiles) are not instanced
    /// this frame. The chunk cull's own radius.
    float maxDistance = 64.0F;
    /// Bodies inside this radius are skinned (animated); outside they draw
    /// in a rest pose. The software fog's e-folding distance, roughly.
    float skinDistance = 24.0F;
};

/// The instances for one frame: every visible ward body and every present
/// tavern body within maxDistance of the eye, in roster order (ward first,
/// then the Gull), each lit by the light where it stands (the same
/// ambient + baked + dynamic glow wardSprites/actorSprites use, folded into
/// Instance::tint). Pure over the session; call putActorRigs on the
/// description once so the placeholders are there to draw.
[[nodiscard]] std::vector<ActorInstance> actorInstances(const render::Session& session,
                                                        const render::Camera& view,
                                                        const ActorSceneParams& params = {});

}  // namespace granadad::render3d
