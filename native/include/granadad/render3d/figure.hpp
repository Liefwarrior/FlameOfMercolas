#pragma once

// THE PEOPLE -- PHASE 6 of docs/design/OBLIVION-PLAN.md (section 6.9, and the
// procedural humanoid of section 7, item 4).
//
// A low-poly person with a 15-bone skeleton, built here from a parameter set
// and animated by hand-written clips. Nothing in it is a model file: a clean
// checkout draws these people, and the licensed glb rigs (Synty, Malbers),
// when they are on disk, still win exactly as they did.
//
// THREE PURE FUNCTIONS, each of integers or of the one before:
//
//   actorLookFor(kind, id, female)   -> ActorLook   WHO this is: build, skin,
//        hair, garments, headgear, apron. A function of the body's kind and
//        id (and the sex its name says) and of nothing else: no position, no
//        clock, no RNG. Packed to a 26-bit key that is part of the mesh id.
//   buildFigure(look)                -> MeshData    WHAT they look like at rest:
//        one mesh, ~500-700 triangles, vertex colours from the look, every
//        vertex weighted to one bone, the skeleton's rest joints in boneRest.
//   evaluateFigurePose(rest, clip, frame)           HOW they stand now: a
//        function of the clip and the integer frame the sim derived
//        (actorClipFrame: the player's step count shifted by the actor id).
//        The same arguments give the same pose, always; the test pins it.
//
// The adapter poses a copy of the rest mesh with skinFigure() for the bodies
// near the eye and draws the rest mesh as it is for the far ones -- the same
// split the glb rigs already had (ActorSceneParams::skinDistance).
//
// THE SKELETON (kFigureBoneCount = 15), parent first:
//
//   Pelvis (the root)  -> Torso -> Head
//                      -> Thigh L/R -> Shin L/R -> Foot L/R
//   Torso              -> UpperArm L/R -> Forearm L/R -> Hand L/R
//
// L is the figure's left, -X; the figure faces -Z. A bone's local rotation is
// pitch (about X), yaw (about Y), roll (about Z), applied in that order
// (shapes.hpp euler). A POSITIVE PITCH swings a hanging limb FORWARD (a thigh,
// a raised arm, a flexed elbow) and tips the top of a standing part BACK; a
// knee flexes with a negative one.
//
// The skeleton is a fixed tree of rigid parts: every part is capped with a
// small dome past its joint, so a bent knee or elbow shows no gap. That is the
// look (a 2006 low-poly figure seen at street distance), and it keeps the CPU
// skinning to one matrix per vertex.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/shapes.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace granadad::render3d {

inline constexpr std::size_t kFigureBoneCount = 15;

enum class FigureBone : std::uint8_t {
    Pelvis = 0,
    Torso,
    Head,
    UpperArmL,
    ForearmL,
    HandL,
    UpperArmR,
    ForearmR,
    HandR,
    ThighL,
    ShinL,
    FootL,
    ThighR,
    ShinR,
    FootR,
};

/// The bone a bone hangs from (the pelvis, the root, is its own).
[[nodiscard]] FigureBone figureParentOf(FigureBone bone) noexcept;

/// Headgear a look can wear (ActorLook::headGear).
enum class HeadGear : std::uint8_t {
    None = 0,
    FlatCap,
    Helmet,
    Hood,
    BrimHat,
    KnitCap,
    Headscarf,
    Coif,
};

/// How a garment hangs (ActorLook::coatStyle): a tunic to the hip, a coat to
/// the knee, a robe or a dress to the shin, or shirt sleeves under a vest.
enum class CoatStyle : std::uint8_t { Tunic = 0, Coat, Robe, Shirt };

/// WHO A PERSON IS, as small indices into tables (figure.cpp). The packed
/// key is the mesh id's low bits, so two bodies with the same look share one
/// mesh and the description holds a mesh per look in sight, not per body.
struct ActorLook {
    /// sim::WardType value, 0..15.
    std::uint8_t type = 0;
    /// 0 a man, 1 a woman.
    std::uint8_t sex = 0;
    /// 0 slight, 1 average, 2 broad, 3 heavy.
    std::uint8_t build = 1;
    /// 0..7, pale to dark.
    std::uint8_t skin = 2;
    /// 0 shaved, 1 cropped, 2 short (a bun on a woman), 3 long.
    std::uint8_t hairStyle = 1;
    /// 0..7: black, dark brown, brown, auburn, fair, ginger, grey, white.
    std::uint8_t hairColour = 2;
    /// 0..7, into the kind's coat table.
    std::uint8_t coatColour = 0;
    /// 0..3, trousers.
    std::uint8_t legColour = 0;
    std::uint8_t headGear = 0;
    std::uint8_t coatStyle = 0;
    /// 1 wears an apron.
    std::uint8_t apron = 0;

    [[nodiscard]] constexpr std::uint32_t key() const noexcept {
        return static_cast<std::uint32_t>(type & 15U) | static_cast<std::uint32_t>(sex & 1U) << 4U |
               static_cast<std::uint32_t>(build & 3U) << 5U | static_cast<std::uint32_t>(skin & 7U) << 7U |
               static_cast<std::uint32_t>(hairStyle & 3U) << 10U |
               static_cast<std::uint32_t>(hairColour & 7U) << 12U |
               static_cast<std::uint32_t>(coatColour & 7U) << 15U |
               static_cast<std::uint32_t>(legColour & 3U) << 18U |
               static_cast<std::uint32_t>(headGear & 7U) << 20U |
               static_cast<std::uint32_t>(coatStyle & 3U) << 23U |
               static_cast<std::uint32_t>(apron & 1U) << 25U;
    }
    [[nodiscard]] static constexpr ActorLook fromKey(std::uint32_t k) noexcept {
        ActorLook look;
        look.type = static_cast<std::uint8_t>(k & 15U);
        look.sex = static_cast<std::uint8_t>((k >> 4U) & 1U);
        look.build = static_cast<std::uint8_t>((k >> 5U) & 3U);
        look.skin = static_cast<std::uint8_t>((k >> 7U) & 7U);
        look.hairStyle = static_cast<std::uint8_t>((k >> 10U) & 3U);
        look.hairColour = static_cast<std::uint8_t>((k >> 12U) & 7U);
        look.coatColour = static_cast<std::uint8_t>((k >> 15U) & 7U);
        look.legColour = static_cast<std::uint8_t>((k >> 18U) & 3U);
        look.headGear = static_cast<std::uint8_t>((k >> 20U) & 7U);
        look.coatStyle = static_cast<std::uint8_t>((k >> 23U) & 3U);
        look.apron = static_cast<std::uint8_t>((k >> 25U) & 1U);
        return look;
    }
    [[nodiscard]] constexpr bool operator==(const ActorLook& o) const noexcept {
        return key() == o.key();
    }
};

/// The mesh id of a look's rest mesh.
[[nodiscard]] constexpr std::uint32_t figureMeshId(const ActorLook& look) noexcept {
    return kFigureMeshIdBase + look.key();
}
/// The FAR figure of a look: the same person at a third of the triangles
/// (no face, a mitten for a hand, four-sided limbs), drawn at rest past the
/// skin radius. Its id sits one key-space above the near one.
inline constexpr std::uint32_t kFigureFarOffset = 1U << 26U;
[[nodiscard]] constexpr std::uint32_t figureFarMeshId(const ActorLook& look) noexcept {
    return kFigureMeshIdBase + kFigureFarOffset + look.key();
}
[[nodiscard]] constexpr bool isFigureMeshId(std::uint32_t id) noexcept {
    return id >= kFigureMeshIdBase;
}
[[nodiscard]] constexpr bool isFarFigureMeshId(std::uint32_t id) noexcept {
    return id >= kFigureMeshIdBase + kFigureFarOffset;
}

/// A salted draw of the actor id, independent of the id's parity and of
/// every other salt (the same mixer as actorLookDraw, other salt).
[[nodiscard]] constexpr std::uint32_t figureDraw(std::int32_t actorId, std::uint32_t salt) noexcept {
    std::uint32_t v = (static_cast<std::uint32_t>(actorId) + 0x6D2B79F5U) * 0x9E3779B1U ^ salt;
    v ^= v >> 16;
    v *= 0x7FEB352DU;
    v ^= v >> 15;
    v *= 0x846CA68BU;
    v ^= v >> 16;
    return v;
}

/// WHO a body of this kind and id is. `female` is the sex the body's NAME
/// says (actor_instances.hpp actorNameSays) or, where it says nothing, the
/// id draw the rig split already uses: the caller decides, so this stays a
/// function of its arguments. Beasts (not sim::isPerson) get a default look
/// and are never built.
[[nodiscard]] ActorLook actorLookFor(sim::WardType type, std::int32_t actorId, bool female) noexcept;

/// A person's height against their kind's, 0.95 to 1.05, in steps of a
/// hundredth: another thing that varies with the id and nothing else.
[[nodiscard]] float actorHeightJitter(std::int32_t actorId) noexcept;

/// The look's rest skeleton: kFigureBoneCount joints (xyz, the joint each
/// bone turns about, in the rest pose, feet on y = 0) and one trailing
/// entry whose x is the STRIDE (how far the legs may swing: a robe or a dress
/// takes less than trousers).
[[nodiscard]] std::array<Vec3, kFigureBoneCount + 1> figureRestJoints(const ActorLook& look) noexcept;

/// The rest mesh of a look: id figureMeshId(look), version 1, triangles under
/// kFigureTriangleBudget, normals filled, every vertex weighted to a bone.
[[nodiscard]] MeshData buildFigure(const ActorLook& look, bool far = false);
inline constexpr std::size_t kFigureTriangleBudget = 1500;
inline constexpr std::size_t kFarFigureTriangleBudget = 650;

/// Puts the look's mesh (the far one when `far`) into the description unless
/// it is already there.
void putFigure(SceneDescription& scene, const ActorLook& look, bool far = false);

/// A pose: the world rotation of every bone and where its joint is, in the
/// figure's own space (the adapter's instance matrix places it).
struct FigurePose {
    std::array<Vec3, kFigureBoneCount> joint;
    std::array<shapes::Mat3, kFigureBoneCount> rotation;
};

/// Frames in a clip's loop (60 to the second), or in a one-shot's run.
[[nodiscard]] std::uint32_t figureClipLength(ActorClip clip) noexcept;

/// THE ANIMATION. A pure function of the rest joints, the clip and the
/// frame. A looping clip wraps its frame by the clip's length; a one-shot
/// (actorClipOneShot: death, the recovery, a flinch) plays to its length and
/// holds its last pose, which is where the sim leaves a body that is down.
[[nodiscard]] FigurePose evaluateFigurePose(const Vec3* restJoints, ActorClip clip,
                                            std::uint32_t frame) noexcept;

/// The rest pose, for a test and for the far draw.
[[nodiscard]] FigurePose figureRestPose(const Vec3* restJoints) noexcept;

/// Poses `rest`'s vertices and normals by `pose` (positions and normals out,
/// the same length as the mesh's). Each vertex follows its own bone.
void skinFigure(const MeshData& rest, const FigurePose& pose, std::vector<float>& positions,
                std::vector<float>& normals);

/// The same, from the bare arrays the adapter keeps (no MeshData copy).
void skinFigure(const float* restPositions, const float* restNormals, const std::uint8_t* bones,
                std::size_t vertexCount, const Vec3* restJoints, const FigurePose& pose,
                float* positionsOut, float* normalsOut) noexcept;

}  // namespace granadad::render3d
