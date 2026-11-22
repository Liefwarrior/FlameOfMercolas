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
//
// PHASE 9: KEYED BY ITEM. The Kit puts a THING in the hand, and the hand
// model is now the thing's (viewmodelKindForItem): its render key -- the
// raws' "model", or its id where it has none -- through the table below,
// and only a row the table does not hold falls back to the class above.
//
//     cudgel        -> Club         (the existing club and mace file)
//     knife, dagger -> Dagger       (the day the knife entered the sim)
//     cutlass       -> Cutlass      SM_Wep_Sword_Small_01, the ground's own
//     hatchet       -> Hatchet      SM_Wep_Axe_Small_04, the cleaver head
//     mace          -> Mace         SM_Wep_Mace_01, the existing mace
//     maul          -> Maul         SM_Wep_WarHammer_Large_01, two hands
//     boarding_axe  -> BoardingAxe  SM_Wep_Axe_01, the bearded axe, two hands
//     anything else -> viewmodelKindOf(its Weapon class)
//
// A two-handed kind puts the LEFT fist on the haft above the right, in every
// state but the cast. The other hand's SHIELD (the buckler, the round
// shield) is a part of its own on the left fist, in the shield id range, and
// is drawn over the licensed arms too.

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

namespace granadad::sim {
struct ItemDef;
}

namespace granadad::render3d {

// The machine's names, in this namespace as the toolchain lane declared them
// (test_scene3d.cpp exercises the machine through these).
using render::ViewmodelInputs;
using render::ViewmodelMachine;
using render::ViewmodelPose;
using render::ViewmodelState;

/// The one-handed grip's hard hold (sim::kHardSwingHoldSteps, static_asserted
/// equal in viewmodel.cpp): the pose functions' default, so a caller with no
/// grip in hand poses exactly as before phase 9.
inline constexpr std::int32_t kViewmodelHardHoldSteps = 15;

/// Which hand model. The byte ViewmodelInstance::kind carries. Append-only:
/// the first four are phase 6's, phase 9 appended one per thing a hand can
/// hold that none of them is.
enum class ViewmodelKind : std::uint8_t {
    Fists = 0,
    Club = 1,
    Dagger = 2,
    Sword = 3,
    Cutlass = 4,
    Hatchet = 5,
    Mace = 6,
    Maul = 7,
    BoardingAxe = 8,
};
inline constexpr std::size_t kViewmodelKindCount = 9;

[[nodiscard]] ViewmodelKind viewmodelKindOf(sim::Weapon weapon) noexcept;
/// PHASE 9: the hand model for the row worn in the hand (nullptr: bare,
/// Fists). The render key is the row's model, or its id when it has none;
/// a key the table does not hold falls back to viewmodelKindOf(class).
[[nodiscard]] ViewmodelKind viewmodelKindForItem(const sim::ItemDef* item) noexcept;
/// The kind a render key names, or false for a key the table does not hold.
[[nodiscard]] bool viewmodelKindOfModel(std::string_view model, ViewmodelKind& out) noexcept;
[[nodiscard]] std::string_view viewmodelKindName(ViewmodelKind kind) noexcept;
/// The kind viewmodelKindName names ("maul", "boarding_axe"), or -1: the
/// client's --hands= reads its names through this.
[[nodiscard]] int viewmodelKindFromName(std::string_view name) noexcept;
/// True for a kind held in both hands (the left fist on the haft).
[[nodiscard]] bool viewmodelTwoHanded(ViewmodelKind kind) noexcept;

/// PHASE 9: what the other hand carries. The byte ViewmodelInstance::shield
/// carries. Append-only.
enum class ViewmodelShield : std::uint8_t { None = 0, Buckler = 1, RoundShield = 2 };
inline constexpr std::size_t kViewmodelShieldCount = 2;  // past None
/// The shield for the row worn in the other hand (nullptr: none): its
/// render key through the table, any other other-hand row the round one.
[[nodiscard]] ViewmodelShield viewmodelShieldForItem(const sim::ItemDef* item) noexcept;
/// Mesh ids: kViewmodelShieldMeshIdBase + shield (200257, 200258).
[[nodiscard]] constexpr std::uint32_t viewmodelShieldMeshId(ViewmodelShield shield) noexcept {
    return kViewmodelShieldMeshIdBase + static_cast<std::uint32_t>(shield);
}
/// The placeholder shield, authored in the left fist's frame: a disc facing
/// -Z (away from the eye), its boss forward, the grip at the fist; a buckler
/// held in the fist, a round shield strapped along the forearm so most of it
/// lies below and left of the fist. Version 1, vertex colours, under 200
/// vertices. Empty for None.
[[nodiscard]] MeshData buildViewmodelShield(ViewmodelShield shield);

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

/// Mesh ids: kViewmodelMeshIdBase + kind * 8 + part. 200000..200068 for the
/// nine kinds; thirty-two kinds fit under the shields at +256.
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

/// A half turn faces the glTF +Z front down the scene's -Z; the hands'
/// pass keeps its own vertical field, whatever the world's slider says.
inline constexpr float kViewmodelRigYaw = 3.14159265358979323846F;
inline constexpr float kViewmodelFovyDegrees = 55.0F;

/// THE RIG'S FRAMING. The exported arms are a standing body's (the Malbers
/// clips hold a guard at the hips, a block at the face, a cast overhead),
/// and at a true first-person placement -- the eye 1.65 up the rig, the
/// body under it -- none of that is in a 55-degree frame: the fists sit
/// fifty degrees below the axis and only a pauldron shows at the edge. So
/// the rig is FRAMED per kind and per state the way a first-person rig is:
/// pushed up and forward and leaned back about the eye (rigPitch), so the
/// forearms enter from the bottom corners, the shoulders stay behind the
/// near plane, and the fists (or the blade) sit in the lower third. The
/// numbers were solved off the clips' joint positions and then tuned on
/// frames; each one is here, in one place, so a re-export moves them once.
struct ViewmodelRigPlacement {
    /// The rig's feet in view space before the lean.
    Vec3 offset;
    /// Radians clockwise about +Y, the scene's convention, the half turn
    /// included.
    float yaw = kViewmodelRigYaw;
    /// Radians about the eye's X: positive lifts what is in front of the eye
    /// (the body leans back under the camera).
    float pitch = 0.0F;
};

/// The guard: where the arms sit whenever the hands are up and nothing else
/// is asked of them (Idle up, and the base every other state pushes off).
[[nodiscard]] ViewmodelRigPlacement viewmodelGuardPlacement(ViewmodelKind kind) noexcept;
/// The block: the rig pushed toward the eye so the raised forearms (bare)
/// or the raised blade (a weapon) cover the middle of the frame.
[[nodiscard]] ViewmodelRigPlacement viewmodelBlockPlacement(ViewmodelKind kind) noexcept;
/// The cast: the rig dropped and turned so the off hand, thrown overhead in
/// the spell clip, lands centre-frame with its glow. RAISES THE STANCE by
/// construction -- a cast with the hands down still poses up here.
[[nodiscard]] ViewmodelRigPlacement viewmodelCastPlacement(ViewmodelKind kind) noexcept;
/// The swing: where the rig sits for a charge, a hold and a swing -- the
/// punches lunge half a metre, so bare fists step the rig back and level
/// it; a weapon keeps its guard. Eased to over a charge's first steps and
/// eased back to the guard over a swing's last third.
[[nodiscard]] ViewmodelRigPlacement viewmodelSwingPlacement(ViewmodelKind kind) noexcept;

/// THE CLIP POLICY: which of the arms glb's eight clips a state plays and
/// where in it (0..1 over the clip). The export's clip per state index is
/// a whole third-person clip, and the V lane picks the frames that read
/// in first person:
///
///   Idle, hands up     the BLOCK clip held at its guard frame (both fists
///                      forward and up); hands down, its first frame (the
///                      hips) so the raise is the clip's own motion, eased
///                      over the stance flip.
///   Charging           the swing clip scrubbed over its cock (the first
///                      sixth) by the charge fraction; ChargedHard holds
///                      there.
///   SwingLight/Hard    the swing clip from the cock to its end over the
///                      swing's steps. Bare fists alternate the punch clip
///                      by swing parity (clip 1 right, clip 2 left); a
///                      weapon plays the hard clip (index 4) for both tiers.
///   Block              the block clip from the guard frame to its hold,
///                      eased over the first steps, then held.
///   Cast               the cast clip through its window.
///   Hit                the hit clip through its window.
struct ViewmodelRigClip {
    std::uint8_t clip = 0;
    float frame = 0.0F;
};
/// `hardHoldSteps` is the hold that makes a swing hard for the grip in the
/// hand (Tavern::playerHardHoldSteps: 15 one-handed, 21 two-handed), so a
/// two-hander's wind-up fills at its own count. The default is the shipped
/// one-handed hold.
[[nodiscard]] ViewmodelRigClip viewmodelRigClip(ViewmodelKind kind, const ViewmodelPose& pose,
                                                std::int32_t hardHoldSteps = kViewmodelHardHoldSteps) noexcept;

/// The guard frame of the block clip (0..1), and its hold frame: the two
/// the policy above stands on.
inline constexpr float kViewmodelGuardFrame = 0.13F;
inline constexpr float kViewmodelBlockHoldFrame = 0.40F;
/// The sword's guard is later in ITS block clip (H_Block_Axe: the blade
/// across the chest as the arms come down), and its hold earlier.
inline constexpr float kViewmodelSwordGuardFrame = 0.56F;
inline constexpr float kViewmodelSwordBlockHoldFrame = 0.44F;
/// The cock: how far into a swing clip the wind-up runs before the strike.
inline constexpr float kViewmodelCockFrame = 0.16F;
/// A block, and a placement, eases over this many steps.
inline constexpr std::int32_t kViewmodelEaseSteps = 6;

/// THE WEAPON SOCKET per hand model (scene.hpp's ViewmodelSocket): the
/// export parents every weapon to Hand_R with no offset, so a blade runs
/// down the bone's own +Y -- through the wrist. In the FantasyHero hand's
/// bone frame the fingers run down -X, the palm faces +Y and the thumb
/// side is -Z, so a hammer grip turns the weapon's +Y onto -Z (a quarter
/// turn about X the negative way) and slides its grip centre into the
/// curled fingers, nine centimetres down the hand and three into the palm.
/// Fists have no socket (zero).
[[nodiscard]] ViewmodelSocket viewmodelSocket(ViewmodelKind kind) noexcept;
/// True when the kind's weapon is baked into the arms glb (the sword).
[[nodiscard]] bool viewmodelWeaponFused(ViewmodelKind kind) noexcept;

/// THE POSE TABLES. Fills `out.parts` (and state/stateSteps/phase/kind) for
/// a kind and a machine pose -- a pure function, so a test can pose the
/// hands without a session. `tint` is the standing light, applied to every
/// part (the cast hand adds its own glow on top). PHASE 9: `shield` puts its
/// part on the left fist (and in out.shield); `hardHoldSteps` is the held
/// grip's hard hold (viewmodelRigClip).
void poseViewmodel(ViewmodelInstance& out, ViewmodelKind kind, const ViewmodelPose& pose,
                   const Rgba8& tint, ViewmodelShield shield = ViewmodelShield::None,
                   std::int32_t hardHoldSteps = kViewmodelHardHoldSteps);

/// THE COMPOSER: the description of the hands for this session -- kind off
/// the Kit's worn HAND row (viewmodelKindForItem), or off
/// Tavern::playerHeldWeapon() when the hand row is bare (a weapon granted as
/// a bare byte, with no registry), the shield off the Kit's OFF-HAND row,
/// the hard hold off the held grip, pose off Session::viewmodel(), light off
/// the body's own tile (ambient + baked + the Gull's flames, the actor rule).
/// Always visible; a caller that wants no hands clears `visible`.
///
/// `forceKind`, when it names a ViewmodelKind (0..kViewmodelKindCount-1), puts
/// that hand model in the hands whatever the sim holds: a verification aid
/// for the captures (the client's --hands=), render-only like everything
/// here. The shield stays the Kit's.
[[nodiscard]] ViewmodelInstance viewmodelInstance(const render::Session& session, int forceKind = -1);

}  // namespace granadad::render3d
