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
// THE RIG IS THE WARD TYPE, SPLIT BY ID. One placeholder per kind of body,
// sixteen of them, and the taproom's roles borrow a WardType through
// render::figureForRole -- the same rule the sprite sheet applies, so there
// is one look for a person in this game rather than two. The LOOK a body
// wears on top of that (the rig byte) is a pure function of its kind and
// its id: the sim carries no sex, so the working kinds split roughly in half
// between the townsman and the townswoman rig by an id hash (actorRigFor),
// the ward's poor and its thieves wear the wastrel rig, and a street child
// is a townsman or a townswoman drawn at the urchin's own height. Each
// KIND has:
//
//   - a PLACEHOLDER mesh, procedural, built here: a box figure (legs, torso,
//     head, and a nose block on the -Z face so the facing reads) sized off
//     render::figureScaleOf, shaded per face like the starter cube. This is
//     what every test and the docker gate draw, since content/art/lot-3d/ is
//     licensed and absent there. Its id is actorRigMeshId(kind) and it is
//     what instance.meshId names WHATEVER the rig byte says: a box crowd does
//     not split by sex, so the placeholder frame is the same bytes before and
//     after the split (only the rig byte in the hash moves).
//   - a FILE NAME per rig, actorRigFile(rig): the glb the adapter looks for
//     under BackendConfig::modelDir at runtime (townsman / townswoman /
//     dockhand / wastrel / watchman -- the asset lane's humanoids; beasts have
//     none yet and keep the box). Present -> the skinned model is drawn in its
//     place, animated by clip index; absent -> the placeholder, silently.
//
// DECLARED, NOT MAPPED: knight.glb is on disk but the sim has no Watch rank
// to hang it on (every MilitiaWatch body is the watchman); no robed preset
// was exported, so the Priest and the Disciple of the Flame keep the
// townsman until one is.
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

/// One placeholder rig per WardType (sim/ward_actors.hpp), sixteen of them,
/// then the export's VARIANT looks, which share a kind's placeholder box.
inline constexpr std::uint32_t kActorKindCount = static_cast<std::uint32_t>(sim::kWardTypeCount);
/// The townswoman: the look half the working kinds (and half the urchins)
/// wear. The one variant rig id today.
inline constexpr std::uint8_t kActorRigTownswoman = static_cast<std::uint8_t>(kActorKindCount);
inline constexpr std::uint32_t kActorRigVariantCount = 1;
inline constexpr std::uint32_t kActorRigCount = kActorKindCount + kActorRigVariantCount;

/// The placeholder mesh for a kind of body. Ids in the actor range of
/// scene.hpp. A variant rig has no mesh of its own: a body wearing one
/// carries its KIND's placeholder in instance.meshId.
[[nodiscard]] constexpr std::uint32_t actorRigMeshId(std::uint32_t kind) noexcept {
    return kActorMeshIdBase + kind;
}

/// The default rig for a kind of body: its WardType value.
[[nodiscard]] constexpr std::uint8_t actorRigOf(sim::WardType type) noexcept {
    return static_cast<std::uint8_t>(type);
}

/// THE SPLIT. The named salt every look draw goes through, so a reader can
/// tell this lot from any other drawn on an actor id.
inline constexpr std::uint32_t kSaltLook = 0x4C4F4F4BU;  // "LOOK"

/// The id's look draw: a mixed hash of the actor id under the salt, so the
/// bit it takes is independent of the id's own parity (which
/// render::figureForRole already spends on the Gull's patrons).
[[nodiscard]] constexpr std::uint32_t actorLookDraw(std::int32_t actorId) noexcept {
    std::uint32_t v = static_cast<std::uint32_t>(actorId) * 0x9E3779B1U ^ kSaltLook;
    v ^= v >> 16;
    v *= 0x7FEB352DU;
    v ^= v >> 15;
    v *= 0x846CA68BU;
    v ^= v >> 16;
    return v;
}

/// True for a kind whose bodies split between the two peasant rigs: the
/// ward's working folk. Sailors keep the dockhand (a working quay's crew),
/// the Watch its soldier, the poor and the thieves the wastrel, the clergy
/// the townsman (no robed preset), beasts their boxes.
[[nodiscard]] constexpr bool actorKindSplits(sim::WardType type) noexcept {
    return type == sim::WardType::Serf || type == sim::WardType::Shopkeeper ||
           type == sim::WardType::AnimalKeeper || type == sim::WardType::Fisher ||
           type == sim::WardType::Carter || type == sim::WardType::Urchin;
}

/// THE RIG A BODY WEARS: a pure function of its kind and its id. A splitting
/// kind takes the townswoman when the look draw's low bit is set (roughly
/// half of them, by id, never by position or by clock); everything else
/// wears its kind's own rig. An urchin's townswoman is the same rig at the
/// urchin's own instance scale.
[[nodiscard]] constexpr std::uint8_t actorRigFor(sim::WardType type, std::int32_t actorId) noexcept {
    if (actorKindSplits(type) && (actorLookDraw(actorId) & 1U) != 0U) {
        return kActorRigTownswoman;
    }
    return actorRigOf(type);
}

/// THE NAME'S SAY. Every body in this game has a name the player can read
/// -- the ward's out of content/raws/names/names.json by id, the Gull's
/// roster and the Forty authored -- and a name says what it says: Gerta
/// Saltcotte is a woman, Tarn Wrenhale a man, and a body drawn otherwise
/// is a defect anybody with the E prompt open can see. So a splitting
/// kind's body follows its name where the name is on the pools' own lists
/// (a woman's name -> the townswoman, a man's -> the kind's own rig), and
/// the id draw decides only where the name says nothing (a wastrel's
/// nickname, a keeper called Fodder). The name is itself a pure function
/// of the id and the raws, so this is the same claim as actorRigFor's.
/// +1 a woman's name, -1 a man's, 0 unlisted -- any word of the name.
[[nodiscard]] int actorNameSays(std::string_view name) noexcept;

/// The rig for a NAMED body: the name's say first, the id draw after.
[[nodiscard]] std::uint8_t actorRigFor(sim::WardType type, std::int32_t actorId,
                                       std::string_view name) noexcept;

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

/// The placeholder figure for a kind: a closed box figure standing on y = 0,
/// centred on the origin in x and z, facing -Z (north) at yaw 0 with a nose
/// block on the front of the head. People are built 1.875 tiles tall (the
/// reference figure; Instance::scale carries the type's ratio, as it does for
/// a glb) and beasts at their own size. id = actorRigMeshId(kind), version 1.
[[nodiscard]] MeshData buildActorPlaceholder(std::uint8_t kind);

/// Puts every kind's placeholder into the description if it is not there
/// already (checked by id + version, so a per-frame call costs sixteen
/// lookups).
void putActorRigs(SceneDescription& scene);

/// The instance scale of a person of this kind: the figure table's height
/// over the reference person's, the same ratio the sprite sheet draws at --
/// an urchin is 1.30 / 1.875 of a grown body (a child against a 1.8 m adult),
/// the Watch a shade over one. Beasts scale 1 (no glb to scale against).
[[nodiscard]] float actorInstanceScale(sim::WardType type) noexcept;

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
