#pragma once

// THE VIEWMODEL -- the player's own hands, drawn.
//
// The MACHINE (what the hands are doing) is render/viewmodel_machine.hpp,
// owned and stepped by render::Session once per movement step off the
// combat sim's public getters; this header is the 3D half: what the hands
// LOOK like, as a function of Session::viewmodel(), Tavern::playerHeldWeapon()
// and the light where the body stands.
//
// A camera-attached rig drawn LAST, in a second 3D pass of its own (see
// backend.hpp): a camera at the origin looking down -Z, so every part here
// is placed in VIEW SPACE -- +X right, +Y up, -Z forward, tiles -- and rides
// the eye wherever it turns, jolting WITH the camera impulses rather than
// against them. The adapter squeezes that pass's depth into the front of
// the depth range, so a wall the body stands against never cuts the fists
// off, and the fists still occlude each other correctly.
//
// TWO LOOKS, ONE DESCRIPTION. Every build -- every test, the docker gate,
// today's client -- draws the PLACEHOLDER: two box forearms and two box
// fists (plus a box weapon) posed by the tables in viewmodel.cpp, in the
// viewmodel mesh id range, through the generic mesh path. When the licensed
// export exists under content/art/lot-3d/ the adapter loads
// characters/<viewmodelRigFile(kind)> -- the FantasyHero arms with the
// clips at index == ViewmodelState -- and, for a kind whose weapon is not
// fused into the rig, static/<viewmodelWeaponFile(kind)> hung on the Hand_R
// bone. The description carries both: the posed parts AND the rig's
// placement/clip/phase, so the hash covers whatever the adapter ends up
// drawing.
//
// HELD WEAPON -> HAND MODEL. sim::Weapon is the room's word for what the
// hand holds; ViewmodelKind is the renderer's:
//
//     Fists       -> Fists     bare knuckles
//     Improvised  -> Club      a stool leg, a bottle: something swung
//     Blunt       -> Club      the cudgel
//     Evictor     -> Club      the headed cudgel (the same silhouette; a
//                              different static file when it exists)
//     Edged       -> Sword     the docks' blade is a cutlass
//
// Dagger is a kind with a placeholder and a file and NO Weapon that maps to
// it today -- the knife the nemesis draws is Edged, and Edged is the
// cutlass. Kept on the table, said out loud, so the day a knife enters the
// sim the hand model is one row away.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "granadad/render/viewmodel_machine.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/sim/brawl.hpp"

namespace granadad::render {
class Session;
}

namespace granadad::render3d {

// The machine's names, in this namespace as the toolchain lane declared them
// (test_scene3d.cpp exercises the machine through these).
using render::ViewmodelInputs;
using render::ViewmodelMachine;
using render::ViewmodelPose;
using render::ViewmodelState;

/// Which hand model. The byte ViewmodelInstance::kind carries.
enum class ViewmodelKind : std::uint8_t { Fists = 0, Club = 1, Dagger = 2, Sword = 3 };
inline constexpr std::size_t kViewmodelKindCount = 4;

[[nodiscard]] ViewmodelKind viewmodelKindOf(sim::Weapon weapon) noexcept;
[[nodiscard]] std::string_view viewmodelKindName(ViewmodelKind kind) noexcept;

/// The arms glb the adapter looks for under BackendConfig::modelDir
/// (content/art/lot-3d/characters): viewmodel_fists.glb for every kind but
/// the sword, whose export has the blade fused into the skin
/// (viewmodel_sword.glb). Clips at index 0..7 == ViewmodelState.
[[nodiscard]] std::string_view viewmodelRigFile(ViewmodelKind kind) noexcept;

/// The static weapon .gltf hung on the arms' Hand_R bone, relative to
/// BackendConfig::weaponDir (content/art/lot-3d/static); empty for a kind
/// with nothing to hang (fists) or with the weapon already in the rig
/// (sword). The asset lane's names, verbatim.
[[nodiscard]] std::string_view viewmodelWeaponFile(ViewmodelKind kind) noexcept;

/// The placeholder's parts. A kind's set is always the four arm parts; the
/// weapon part exists for every kind but Fists.
enum class ViewmodelPartId : std::uint8_t {
    LeftArm = 0,
    LeftFist = 1,
    RightArm = 2,
    RightFist = 3,
    Weapon = 4,
};
inline constexpr std::size_t kViewmodelPartCount = 5;

/// Mesh ids: kViewmodelMeshIdBase + kind * 8 + part. 200000..200036.
[[nodiscard]] constexpr std::uint32_t viewmodelMeshId(ViewmodelKind kind,
                                                      ViewmodelPartId part) noexcept {
    return kViewmodelMeshIdBase + static_cast<std::uint32_t>(kind) * 8U +
           static_cast<std::uint32_t>(part);
}

/// True when this kind has this part (Fists has no Weapon part).
[[nodiscard]] constexpr bool viewmodelHasPart(ViewmodelKind kind, ViewmodelPartId part) noexcept {
    return part != ViewmodelPartId::Weapon || kind != ViewmodelKind::Fists;
}

/// One placeholder part, authored in the part's own frame: a fist is a box
/// about the origin (knuckles towards -Z); a forearm runs from the fist at
/// the origin back and down to an elbow at +Z; a weapon has its grip at the
/// origin and points down -Z. Vertex colours carry the fixed key light,
/// id viewmodelMeshId(kind, part), version 1, under 100 vertices.
[[nodiscard]] MeshData buildViewmodelPart(ViewmodelKind kind, ViewmodelPartId part);

/// Every kind's parts into `scene`, idempotent by (id, version).
void putViewmodelMeshes(SceneDescription& scene);

/// Where the licensed rig's feet go in view space so the eye sits at its
/// head: the FantasyHero arms are exported at 1 m = 1 tile with the eye
/// ~1.65 up; the body's eye is 435/256 tiles above its feet. Half a turn so
/// the glTF +Z front faces the scene's -Z. The two constants a real export
/// may move, by design in one place.
inline constexpr Vec3 kViewmodelRigOffset{0.0F, -1.65F, 0.12F};
inline constexpr float kViewmodelRigYaw = 3.14159265358979323846F;
inline constexpr float kViewmodelFovyDegrees = 55.0F;

/// THE POSE TABLES. Fills `out.parts` (and state/stateSteps/phase/kind) for
/// a kind and a machine pose -- a pure function, so a test can pose the
/// hands without a session. `tint` is the standing light, applied to every
/// part (the cast hand adds its own glow on top).
void poseViewmodel(ViewmodelInstance& out, ViewmodelKind kind, const ViewmodelPose& pose,
                   const Rgba8& tint);

/// THE COMPOSER: the description of the hands for this session -- kind off
/// Tavern::playerHeldWeapon(), pose off Session::viewmodel(), light off the
/// body's own tile (ambient + baked + the Gull's flames, the actor rule).
/// Always visible; a caller that wants no hands clears `visible`.
[[nodiscard]] ViewmodelInstance viewmodelInstance(const render::Session& session);

}  // namespace granadad::render3d
