#include "granadad/render3d/viewmodel.hpp"

#include <algorithm>
#include <cmath>

#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/scene_types.hpp"
#include "granadad/render3d/shapes.hpp"
#include "granadad/sim/tavern.hpp"

namespace granadad::render3d {

namespace {

constexpr float kPi = 3.14159265358979323846F;

// The placeholder's palette: the player's own skin and coat as the sprite
// sheet reads a serf, plus wood and steel for what the hand holds. Nothing
// here is art; it is what stands in for it until the export runs.
constexpr Rgba8 kSkin{214, 170, 130, 255};
constexpr Rgba8 kSleeve{104, 84, 60, 255};
constexpr Rgba8 kWood{118, 86, 52, 255};
constexpr Rgba8 kIron{96, 100, 108, 255};
constexpr Rgba8 kSteel{178, 184, 194, 255};
constexpr Rgba8 kCastGlow{255, 232, 180, 255};

[[nodiscard]] Rgba8 shade(const Rgba8& base, float factor) noexcept {
    const auto ch = [factor](std::uint8_t c) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<float>(c) * factor, 0.0F, 255.0F));
    };
    return Rgba8{ch(base.r), ch(base.g), ch(base.b), base.a};
}

using shapes::Builder;
using shapes::Ring;

[[nodiscard]] Ring ring(float y, float rx, float rz, const Rgba8& c) noexcept {
    Ring r;
    r.y = y;
    r.rx = rx;
    r.rz = rz;
    r.colour = c;
    return r;
}

/// A solid of rings standing along the line from `a` to `b` (shapes.hpp
/// alongAxis). `rings` are (distance along it, rx, rz, colour).
void along(MeshData& mesh, const Vec3& a, const Vec3& b, const std::vector<Ring>& rings, int sides,
           bool capBottom = true, bool capTop = true) {
    float length = 0.0F;
    Builder builder;
    builder.mesh = &mesh;
    builder.xf = shapes::alongAxis(a, b, Vec3{0.0F, 1.0F, 0.0F}, length);
    shapes::loft(builder, rings, sides, capBottom, capTop);
}

void block(MeshData& mesh, float x0, float y0, float z0, float x1, float y1, float z1, const Rgba8& base) {
    Builder builder;
    builder.mesh = &mesh;
    shapes::block(builder, Vec3{(x0 + x1) * 0.5F, (y0 + y1) * 0.5F, (z0 + z1) * 0.5F},
                  Vec3{(x1 - x0) * 0.5F, (y1 - y0) * 0.5F, (z1 - z0) * 0.5F}, base);
}

/// The fist, authored about the origin, knuckles towards -Z (the player's
/// own hand seen from behind): the back of the hand, a row of four knuckles
/// with the four fingers curled under them, and the thumb laid across the
/// front on the inner side (towards the other hand).
void pushFist(MeshData& mesh, float side) {
    block(mesh, -0.060F, -0.046F, -0.020F, 0.060F, 0.050F, 0.100F, kSkin);
    const float xs[4] = {-0.0435F, -0.0145F, 0.0145F, 0.0435F};
    for (int i = 0; i < 4; ++i) {
        const float x = xs[i];
        block(mesh, x - 0.0145F, -0.014F, -0.074F, x + 0.0145F, 0.040F, -0.014F, shade(kSkin, 1.02F));
        block(mesh, x - 0.0135F, -0.060F, -0.088F, x + 0.0135F, -0.010F, -0.050F, shade(kSkin, 0.88F));
    }
    // The inner side is the right hand's -X: the thumb crosses the fingers.
    const float inner = -side;
    block(mesh, inner * 0.028F - 0.034F, -0.030F, -0.098F, inner * 0.028F + 0.034F, -0.004F, -0.066F,
          shade(kSkin, 0.94F));
}

/// The forearm: from the wrist just behind the fist (the origin, at z 0.05)
/// back, down and out to the elbow, which sits off the bottom corner of the
/// frame -- 0.30 closer to the eye than the fist, never near the pass's 0.05
/// near plane (a fist rests 0.66 out), so the sleeve reads as a band from the
/// corner rather than a wall of cloth. A bare wrist, a cuff, the sleeve.
void pushForearm(MeshData& mesh, float side) {
    const Vec3 wrist{0.0F, 0.0F, 0.05F};
    const Vec3 elbow{side * 0.30F, -0.55F, 0.30F};
    const float length = std::sqrt((elbow.x - wrist.x) * (elbow.x - wrist.x) +
                                   (elbow.y - wrist.y) * (elbow.y - wrist.y) +
                                   (elbow.z - wrist.z) * (elbow.z - wrist.z));
    const Rgba8 cuff = shade(kSleeve, 0.72F);
    along(mesh, wrist, elbow,
          {ring(0.0F, 0.052F, 0.048F, kSkin), ring(0.10F * length, 0.058F, 0.054F, kSkin),
           ring(0.10F * length, 0.074F, 0.070F, cuff), ring(0.20F * length, 0.078F, 0.074F, cuff),
           ring(0.20F * length, 0.080F, 0.076F, kSleeve), ring(0.60F * length, 0.092F, 0.088F, kSleeve),
           ring(length, 0.108F, 0.100F, kSleeve)},
          8);
}

void pushClub(MeshData& mesh) {
    // A wrapped grip through the fist, the shaft swelling to a studded head
    // out past it, an iron collar, a leather-wrapped butt.
    const Vec3 butt{0.0F, 0.0F, 0.11F};
    const Vec3 tip{0.0F, 0.0F, -0.62F};
    const Rgba8 wrap = shade(kWood, 0.55F);
    along(mesh, butt, tip,
          {ring(0.0F, 0.020F, 0.020F, kIron), ring(0.020F, 0.032F, 0.032F, kIron),
           ring(0.020F, 0.022F, 0.022F, wrap), ring(0.20F, 0.022F, 0.022F, wrap),
           ring(0.20F, 0.021F, 0.021F, kWood), ring(0.36F, 0.026F, 0.026F, kWood),
           ring(0.47F, 0.036F, 0.036F, kIron), ring(0.49F, 0.036F, 0.036F, kWood),
           ring(0.56F, 0.052F, 0.052F, shade(kWood, 0.9F)), ring(0.66F, 0.054F, 0.054F, shade(kWood, 0.82F)),
           ring(0.73F, 0.040F, 0.040F, shade(kWood, 0.78F))},
          6);
    // Iron studs round the head.
    for (const float sx : {-1.0F, 1.0F}) {
        block(mesh, sx * 0.050F - 0.008F, -0.008F, -0.50F, sx * 0.050F + 0.008F, 0.008F, -0.48F, kIron);
        block(mesh, -0.008F, sx * 0.050F - 0.008F, -0.54F, 0.008F, sx * 0.050F + 0.008F, -0.52F, kIron);
    }
}

void pushDagger(MeshData& mesh) {
    // A short double-edged blade from a crossguard, a wrapped grip, a pommel.
    const Rgba8 wrap = shade(kWood, 0.55F);
    along(mesh, Vec3{0.0F, 0.0F, 0.12F}, Vec3{0.0F, 0.0F, -0.09F},
          {ring(0.0F, 0.016F, 0.016F, kIron), ring(0.012F, 0.022F, 0.022F, kIron), ring(0.012F, 0.016F, 0.016F, wrap),
           ring(0.19F, 0.017F, 0.017F, wrap), ring(0.21F, 0.019F, 0.019F, kIron)},
          6);
    block(mesh, -0.050F, -0.012F, -0.105F, 0.050F, 0.012F, -0.085F, kIron);
    along(mesh, Vec3{0.0F, 0.0F, -0.10F}, Vec3{0.0F, 0.0F, -0.42F},
          {ring(0.0F, 0.016F, 0.0045F, kSteel), ring(0.15F, 0.0145F, 0.0050F, kSteel),
           ring(0.26F, 0.009F, 0.0040F, shade(kSteel, 1.06F)), ring(0.32F, 0.0015F, 0.0015F, kSteel)},
          4);
}

void pushSword(MeshData& mesh) {
    // The docks' cutlass: a broad blade with a fuller, a disc guard and
    // knuckle bow, a wrapped grip, an iron pommel.
    const Rgba8 wrap = shade(kWood, 0.5F);
    along(mesh, Vec3{0.0F, 0.0F, 0.14F}, Vec3{0.0F, 0.0F, -0.10F},
          {ring(0.0F, 0.020F, 0.020F, kIron), ring(0.016F, 0.028F, 0.028F, kIron), ring(0.016F, 0.019F, 0.019F, wrap),
           ring(0.20F, 0.020F, 0.020F, wrap), ring(0.24F, 0.016F, 0.016F, kIron)},
          6);
    along(mesh, Vec3{0.0F, 0.0F, -0.10F}, Vec3{0.0F, 0.0F, -0.115F},
          {ring(0.0F, 0.085F, 0.085F, shade(kIron, 0.85F)), ring(0.015F, 0.085F, 0.085F, kIron)}, 8);
    block(mesh, -0.095F, -0.012F, -0.118F, 0.095F, 0.012F, -0.100F, kIron);
    along(mesh, Vec3{0.0F, 0.0F, -0.115F}, Vec3{0.0F, 0.0F, -0.96F},
          {ring(0.0F, 0.024F, 0.0065F, kSteel), ring(0.30F, 0.026F, 0.0065F, kSteel),
           ring(0.62F, 0.028F, 0.0060F, shade(kSteel, 1.04F)), ring(0.76F, 0.020F, 0.0045F, kSteel),
           ring(0.845F, 0.0015F, 0.0015F, kSteel)},
          4);
    // The fuller, a shade darker down the middle of the flat.
    along(mesh, Vec3{0.0F, 0.0F, -0.17F}, Vec3{0.0F, 0.0F, -0.82F},
          {ring(0.0F, 0.006F, 0.0072F, shade(kSteel, 0.72F)), ring(0.65F, 0.0045F, 0.0066F, shade(kSteel, 0.72F))},
          4);
}

/// The light where the body stands, the actor rule: ambient + max(baked,
/// dynamic), clamped near 1 so a hand in a lamp pool is lit, not blown out.
[[nodiscard]] Rgba8 standingTint(const render::SkyState& sky, const render::Rgb& baked,
                                 const render::Rgb& dynamic) noexcept {
    const auto ch = [](float ambient, float b, float d) {
        const float light = std::min(1.15F, ambient + std::max(b, d));
        return static_cast<std::uint8_t>(std::clamp(light * 255.0F, 0.0F, 255.0F));
    };
    return Rgba8{ch(sky.ambient.r, baked.r, dynamic.r), ch(sky.ambient.g, baked.g, dynamic.g),
                 ch(sky.ambient.b, baked.b, dynamic.b), 255};
}

[[nodiscard]] Vec3 lerp(const Vec3& a, const Vec3& b, float t) noexcept {
    return Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

[[nodiscard]] Vec3 add(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] float clamp01(float v) noexcept { return std::clamp(v, 0.0F, 1.0F); }

[[nodiscard]] float easeOut(float k) noexcept { return 1.0F - (1.0F - k) * (1.0F - k); }

[[nodiscard]] float smooth(float k) noexcept { return k * k * (3.0F - 2.0F * k); }

/// One hand's transform: where the fist is and how it is turned.
struct HandPose {
    Vec3 position;
    Vec3 rotation;
};

/// The rest pose for a side (-1 left, +1 right): the fists raised at the
/// lower corners of the view, knuckles up, turned a little in.
[[nodiscard]] HandPose idlePose(float side) noexcept {
    HandPose pose;
    pose.position = Vec3{side * 0.36F, side < 0.0F ? -0.29F : -0.27F, -0.66F};
    pose.rotation = Vec3{0.35F, side * 0.25F, 0.0F};
    return pose;
}

/// The wind-up of the striking hand at a charge fraction: pulled back
/// towards the eye, up and out, cocked at the wrist.
[[nodiscard]] HandPose windupPose(float side, float f) noexcept {
    HandPose pose = idlePose(side);
    pose.position = add(pose.position, Vec3{side * 0.10F * f, 0.10F * f, 0.18F * f});
    pose.rotation = add(pose.rotation, Vec3{0.55F * f, 0.0F, side * 0.15F * f});
    return pose;
}

/// The other hand while one charges: comes in a little, as a guard.
[[nodiscard]] HandPose guardPose(float side, float f) noexcept {
    HandPose pose = idlePose(side);
    pose.position = add(pose.position, Vec3{-side * 0.05F * f, 0.05F * f, -0.02F * f});
    return pose;
}

/// STANCE (combat feel): the hands DOWN -- fighting mode off. The arms hang
/// at the sides, dropped and let out, so only the top of each fist shows at
/// the bottom edge of the frame (the fist's top-front corner lands near row
/// 169 of 180 under the 55-degree field): the tell that the hands exist, and
/// the raise on the first Attack press is the whole fighting-mode moment.
[[nodiscard]] HandPose loweredPose(float side) noexcept {
    HandPose pose;
    pose.position = Vec3{side * 0.34F, side < 0.0F ? -0.39F : -0.375F, -0.64F};
    pose.rotation = Vec3{0.10F, side * 0.25F, side * 0.12F};
    return pose;
}

constexpr float kDegToRad = kPi / 180.0F;

/// The framings, solved off the clips' joint positions (the block clip's
/// guard frame for the fists, its blade-across frame for the sword, the
/// spell clip's raised off hand for the cast) and tuned on frames. Yaw is
/// the half turn less the body's own turn to the left: the sword's hip
/// stance and the cast's thrown hand both come round into frame when the
/// body faces a little left of the eye's line.
[[nodiscard]] constexpr ViewmodelRigPlacement placementOf(Vec3 offset, float turnLeftDegrees,
                                                          float pitchDegrees) noexcept {
    return ViewmodelRigPlacement{offset, kViewmodelRigYaw - turnLeftDegrees * kDegToRad,
                                 pitchDegrees * kDegToRad};
}
constexpr ViewmodelRigPlacement kFistsGuard = placementOf(Vec3{0.10F, -1.34F, 0.14F}, 8.0F, 35.0F);
constexpr ViewmodelRigPlacement kFistsBlock = placementOf(Vec3{0.0F, -1.22F, -0.41F}, 0.0F, 10.0F);
/// The punches lunge: the shoulder comes half a metre forward through the
/// clip, so the rig sits further back and barely leans for them, the
/// shoulders behind the near plane in every frame and the fist flying in
/// from the lower right to the middle rather than up through the eye.
constexpr ViewmodelRigPlacement kFistsPunch = placementOf(Vec3{0.0F, -1.27F, 0.45F}, 0.0F, 10.0F);
constexpr ViewmodelRigPlacement kSwordGuard = placementOf(Vec3{0.40F, -2.04F, 0.28F}, 10.0F, 70.0F);
constexpr ViewmodelRigPlacement kSwordBlock = placementOf(Vec3{0.30F, -1.96F, 0.28F}, 10.0F, 75.0F);
constexpr ViewmodelRigPlacement kCastPlacement = placementOf(Vec3{0.30F, -1.88F, -0.24F}, 20.0F, -10.0F);
/// The swing's last third eases the placement back to the guard, so the
/// fall back to Idle lands on the framing it left.
constexpr float kSwingReturnFrom = 0.7F;
/// HANDS DOWN: the rig drops this far below its guard framing, so the arms
/// at the hips are under the bottom edge -- Oblivion's sheathed state shows
/// nothing, and the block clip's first frame at the guard framing put a
/// forearm across the near plane instead. The stance flip eases the rig
/// back up while the clip raises the hands, so the guard rises into view.
constexpr float kLoweredDrop = 0.9F;

[[nodiscard]] ViewmodelRigPlacement lerp(const ViewmodelRigPlacement& a, const ViewmodelRigPlacement& b,
                                         float t) noexcept {
    ViewmodelRigPlacement out;
    out.offset = Vec3{a.offset.x + (b.offset.x - a.offset.x) * t, a.offset.y + (b.offset.y - a.offset.y) * t,
                      a.offset.z + (b.offset.z - a.offset.z) * t};
    out.yaw = a.yaw + (b.yaw - a.yaw) * t;
    out.pitch = a.pitch + (b.pitch - a.pitch) * t;
    return out;
}

/// The sword's socket, and the clubs' and the dagger's: the same grip.
constexpr ViewmodelSocket kHammerGrip{Vec3{-0.09F, 0.03F, -0.10F}, Vec3{-kPi * 0.5F, 0.0F, 0.0F}};

/// How far down the hands are, 0 raised .. 1 hanging: non-zero only in
/// Idle (every other state has them up by the sim's own rules), easing
/// across a stance flip over ViewmodelMachine::kStanceSteps.
[[nodiscard]] float loweredAmount(const ViewmodelPose& pose) noexcept {
    if (pose.state != ViewmodelState::Idle) {
        return 0.0F;
    }
    const float target = pose.handsUp ? 0.0F : 1.0F;
    if (pose.stanceSteps >= ViewmodelMachine::kStanceSteps) {
        return target;
    }
    const float e = smooth(static_cast<float>(pose.stanceSteps) /
                           static_cast<float>(ViewmodelMachine::kStanceSteps));
    const float from = 1.0F - target;
    return from + (target - from) * e;
}

}  // namespace

ViewmodelKind viewmodelKindOf(sim::Weapon weapon) noexcept {
    switch (weapon) {
        case sim::Weapon::Improvised:
        case sim::Weapon::Blunt:
        case sim::Weapon::Evictor:
            return ViewmodelKind::Club;
        case sim::Weapon::Edged:
            return ViewmodelKind::Sword;
        case sim::Weapon::Fists:
        default:
            return ViewmodelKind::Fists;
    }
}

std::string_view viewmodelKindName(ViewmodelKind kind) noexcept {
    switch (kind) {
        case ViewmodelKind::Fists: return "fists";
        case ViewmodelKind::Club: return "club";
        case ViewmodelKind::Dagger: return "dagger";
        case ViewmodelKind::Sword: return "sword";
    }
    return "fists";
}

std::string_view viewmodelRigFile(ViewmodelKind kind) noexcept {
    return kind == ViewmodelKind::Sword ? "viewmodel_sword.glb" : "viewmodel_fists.glb";
}

std::string_view viewmodelWeaponFile(ViewmodelKind kind) noexcept {
    switch (kind) {
        case ViewmodelKind::Club:
            return "PolygonFantasyHeroCharacters/SM_Wep_Mace_01.gltf";
        case ViewmodelKind::Dagger:
            return "PolygonFantasyHeroCharacters/SM_Wep_Dagger_01.gltf";
        case ViewmodelKind::Fists:
        case ViewmodelKind::Sword:
        default:
            return {};
    }
}

std::string_view viewmodelRigFileOf(std::uint8_t kind) noexcept {
    return kind < kViewmodelKindCount ? viewmodelRigFile(static_cast<ViewmodelKind>(kind))
                                      : std::string_view{};
}

std::string_view viewmodelWeaponFileOf(std::uint8_t kind) noexcept {
    return kind < kViewmodelKindCount ? viewmodelWeaponFile(static_cast<ViewmodelKind>(kind))
                                      : std::string_view{};
}

ViewmodelSocket viewmodelSocket(ViewmodelKind kind) noexcept {
    switch (kind) {
        case ViewmodelKind::Club:
        case ViewmodelKind::Dagger:
        case ViewmodelKind::Sword:
            return kHammerGrip;
        case ViewmodelKind::Fists:
        default:
            return ViewmodelSocket{};
    }
}

bool viewmodelWeaponFused(ViewmodelKind kind) noexcept { return kind == ViewmodelKind::Sword; }

ViewmodelSocket viewmodelSocketOf(std::uint8_t kind) noexcept {
    return kind < kViewmodelKindCount ? viewmodelSocket(static_cast<ViewmodelKind>(kind))
                                      : ViewmodelSocket{};
}

bool viewmodelWeaponFusedOf(std::uint8_t kind) noexcept {
    return kind < kViewmodelKindCount && viewmodelWeaponFused(static_cast<ViewmodelKind>(kind));
}

ViewmodelRigPlacement viewmodelGuardPlacement(ViewmodelKind kind) noexcept {
    return kind == ViewmodelKind::Fists ? kFistsGuard : kSwordGuard;
}

ViewmodelRigPlacement viewmodelBlockPlacement(ViewmodelKind kind) noexcept {
    return kind == ViewmodelKind::Fists ? kFistsBlock : kSwordBlock;
}

ViewmodelRigPlacement viewmodelCastPlacement(ViewmodelKind kind) noexcept {
    (void)kind;
    return kCastPlacement;
}

ViewmodelRigPlacement viewmodelSwingPlacement(ViewmodelKind kind) noexcept {
    return kind == ViewmodelKind::Fists ? kFistsPunch : kSwordGuard;
}

ViewmodelRigClip viewmodelRigClip(ViewmodelKind kind, const ViewmodelPose& pose) noexcept {
    const bool armed = kind != ViewmodelKind::Fists;
    const float guard = armed ? kViewmodelSwordGuardFrame : kViewmodelGuardFrame;
    const float hold = armed ? kViewmodelSwordBlockHoldFrame : kViewmodelBlockHoldFrame;
    // The striking hand, the placeholder's own rule: a weapon is always in
    // the right hand; bare fists alternate, the first swing the right.
    const bool inFlight =
        pose.state == ViewmodelState::SwingLight || pose.state == ViewmodelState::SwingHard;
    const std::int32_t swingNumber = inFlight ? pose.swingSeq : pose.swingSeq + 1;
    const bool rightHand = armed || (swingNumber & 1) != 0;
    // The clip indices are the export's: 1/2 the two punches (right, left)
    // for the fists and a charge pose / the sword cut for the sword; 3/4
    // the same two punches again, or the sword cut and the swipe up. A
    // weapon's cock and swing are one clip (the swipe up, index 4) so the
    // strike carries straight on from the hold.
    const std::uint8_t swingClip = armed ? 4U : (rightHand ? 3U : 4U);
    const std::uint8_t cockClip = armed ? 4U : (rightHand ? 1U : 2U);
    const std::int32_t length = render::viewmodelStateSteps(pose.state);
    const float progress =
        length > 0 ? clamp01(static_cast<float>(pose.stateSteps) / static_cast<float>(length)) : 0.0F;
    ViewmodelRigClip out;
    switch (pose.state) {
        case ViewmodelState::Idle: {
            // The guard frame with the hands up, the first frame (the hips)
            // with them down, the clip's own motion between.
            out.clip = 5U;
            out.frame = guard * (1.0F - loweredAmount(pose));
            break;
        }
        case ViewmodelState::Charging: {
            out.clip = cockClip;
            out.frame = kViewmodelCockFrame *
                        clamp01(static_cast<float>(pose.chargeSteps) /
                                static_cast<float>(sim::kHardSwingHoldSteps));
            break;
        }
        case ViewmodelState::ChargedHard:
            out.clip = cockClip;
            out.frame = kViewmodelCockFrame;
            break;
        case ViewmodelState::SwingLight:
        case ViewmodelState::SwingHard:
            out.clip = swingClip;
            out.frame = kViewmodelCockFrame + (1.0F - kViewmodelCockFrame) * progress;
            break;
        case ViewmodelState::Block: {
            const float e = smooth(clamp01(static_cast<float>(pose.stateSteps) /
                                           static_cast<float>(kViewmodelEaseSteps)));
            out.clip = 5U;
            out.frame = guard + (hold - guard) * e;
            break;
        }
        case ViewmodelState::Cast:
            out.clip = 6U;
            out.frame = progress;
            break;
        case ViewmodelState::Hit:
            out.clip = 7U;
            out.frame = progress;
            break;
    }
    return out;
}

MeshData buildViewmodelPart(ViewmodelKind kind, ViewmodelPartId part) {
    MeshData mesh;
    mesh.id = viewmodelMeshId(kind, part);
    mesh.version = 1;
    switch (part) {
        case ViewmodelPartId::LeftArm:
            pushForearm(mesh, -1.0F);
            break;
        case ViewmodelPartId::RightArm:
            pushForearm(mesh, 1.0F);
            break;
        case ViewmodelPartId::LeftFist:
            pushFist(mesh, -1.0F);
            break;
        case ViewmodelPartId::RightFist:
            pushFist(mesh, 1.0F);
            break;
        case ViewmodelPartId::Weapon:
            switch (kind) {
                case ViewmodelKind::Club:
                    pushClub(mesh);
                    break;
                case ViewmodelKind::Dagger:
                    pushDagger(mesh);
                    break;
                case ViewmodelKind::Sword:
                    pushSword(mesh);
                    break;
                case ViewmodelKind::Fists:
                default:
                    break;  // bare hands: an empty mesh, never put
            }
            break;
    }
    return mesh;
}

void putViewmodelMeshes(SceneDescription& scene) {
    for (std::uint32_t k = 0; k < kViewmodelKindCount; ++k) {
        const auto kind = static_cast<ViewmodelKind>(k);
        for (std::uint32_t p = 0; p < kViewmodelPartCount; ++p) {
            const auto part = static_cast<ViewmodelPartId>(p);
            if (!viewmodelHasPart(kind, part)) {
                continue;
            }
            const MeshData* present = scene.findMesh(viewmodelMeshId(kind, part));
            if (present != nullptr && present->version == 1) {
                continue;
            }
            scene.putMesh(buildViewmodelPart(kind, part));
        }
    }
}

void poseViewmodel(ViewmodelInstance& out, ViewmodelKind kind, const ViewmodelPose& pose,
                   const Rgba8& tint) {
    out.kind = static_cast<std::uint8_t>(kind);
    out.state = pose.state;
    out.stateSteps = pose.stateSteps;
    out.fovyDegrees = kViewmodelFovyDegrees;
    const float down = loweredAmount(pose);
    out.tint = tint;
    out.parts.clear();

    // THE GLB PATH: the framing for the state (the guard, pushed to the
    // block over its ease, swung to the cast and back over its window --
    // a one-shot returns to the guard by its own end, so the fall back to
    // Idle lands without a jump), the clip and the frame the policy picks,
    // the socket the kind's weapon hangs by.
    {
        const ViewmodelRigPlacement guard = viewmodelGuardPlacement(kind);
        ViewmodelRigPlacement at = guard;
        if (pose.state == ViewmodelState::Block) {
            const float e = smooth(clamp01(static_cast<float>(pose.stateSteps) /
                                           static_cast<float>(kViewmodelEaseSteps)));
            at = lerp(guard, viewmodelBlockPlacement(kind), e);
        } else if (pose.state == ViewmodelState::Charging) {
            const float e = smooth(clamp01(static_cast<float>(pose.stateSteps) /
                                           static_cast<float>(kViewmodelEaseSteps)));
            at = lerp(guard, viewmodelSwingPlacement(kind), e);
        } else if (pose.state == ViewmodelState::ChargedHard) {
            at = viewmodelSwingPlacement(kind);
        } else if (pose.state == ViewmodelState::SwingLight || pose.state == ViewmodelState::SwingHard) {
            const std::int32_t window = render::viewmodelStateSteps(pose.state);
            const float k = window > 0 ? clamp01(static_cast<float>(pose.stateSteps) /
                                                 static_cast<float>(window))
                                       : 0.0F;
            const float back = k > kSwingReturnFrom
                                   ? smooth((k - kSwingReturnFrom) / (1.0F - kSwingReturnFrom))
                                   : 0.0F;
            at = lerp(viewmodelSwingPlacement(kind), guard, back);
        } else if (pose.state == ViewmodelState::Cast) {
            const std::int32_t window = render::viewmodelStateSteps(pose.state);
            const float k = window > 0 ? clamp01(static_cast<float>(pose.stateSteps) /
                                                 static_cast<float>(window))
                                       : 0.0F;
            float e = 1.0F;
            if (k < 0.3F) {
                e = easeOut(k / 0.3F);
            } else if (k > 0.6F) {
                e = 1.0F - smooth((k - 0.6F) / 0.4F);
            }
            at = lerp(guard, viewmodelCastPlacement(kind), e);
        } else if (pose.state == ViewmodelState::Idle) {
            // Down: the rig sinks under the bottom edge with the stance,
            // eased like the clip. Up: the breathing sway, from rest at
            // re-entry.
            at.offset.y -= kLoweredDrop * down;
            const float t = static_cast<float>(pose.stateSteps);
            at.offset.y += (1.0F - down) * 0.006F * std::sin(t * (2.0F * kPi / 96.0F));
            at.offset.x += (1.0F - down) * 0.003F * std::sin(t * (2.0F * kPi / 192.0F));
        }
        out.rigOffset = at.offset;
        out.rigYaw = at.yaw;
        out.rigPitch = at.pitch;
        out.rigScale = 1.0F;
        const ViewmodelRigClip clip = viewmodelRigClip(kind, pose);
        out.rigClip = clip.clip;
        out.rigFrame = clip.frame;
        const ViewmodelSocket socket = viewmodelSocket(kind);
        out.socketOffset = socket.offset;
        out.socketRotation = socket.rotation;
    }

    // Which hand strikes: bare fists alternate per swing -- the first swing
    // is the right, the second the left -- and a wind-up belongs to the
    // swing it is about to throw, so the charge before swing N cocks the
    // hand swing N will use. A held weapon is always in the right hand, and
    // the left does the guarding and the casting.
    const bool armed = kind != ViewmodelKind::Fists;
    const bool inFlight =
        pose.state == ViewmodelState::SwingLight || pose.state == ViewmodelState::SwingHard;
    const std::int32_t swingNumber = inFlight ? pose.swingSeq : pose.swingSeq + 1;
    const float strike = (armed || (swingNumber & 1) != 0) ? 1.0F : -1.0F;
    const float chargeFraction =
        clamp01(static_cast<float>(pose.chargeSteps) / static_cast<float>(sim::kHardSwingHoldSteps));
    const std::int32_t length = render::viewmodelStateSteps(pose.state);
    float phase = 0.0F;
    if (pose.state == ViewmodelState::Charging || pose.state == ViewmodelState::ChargedHard) {
        phase = pose.state == ViewmodelState::ChargedHard ? 1.0F : chargeFraction;
    } else if (length > 0) {
        phase = clamp01(static_cast<float>(pose.stateSteps) / static_cast<float>(length));
    }
    out.phase = phase;

    HandPose hands[2];  // [0] = left, [1] = right
    Rgba8 handTint[2] = {tint, tint};
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? -1.0F : 1.0F;
        const bool striking = side == strike;
        const HandPose idle = idlePose(side);
        HandPose hand = idle;
        switch (pose.state) {
            case ViewmodelState::Idle: {
                // STANCE: the raised rest with the hands up, the arms hanging
                // with them down, eased across a flip (loweredAmount). Then a
                // slow breathing sway, from rest at re-entry (sin 0 = 0) so a
                // swing's return lands on it without a jump.
                if (down > 0.0F) {
                    const HandPose lowered = loweredPose(side);
                    hand.position = lerp(idle.position, lowered.position, down);
                    hand.rotation = lerp(idle.rotation, lowered.rotation, down);
                }
                const float t = static_cast<float>(pose.stateSteps);
                hand.position.y += 0.008F * std::sin(t * (2.0F * kPi / 96.0F));
                hand.position.x += side * 0.004F * std::sin(t * (2.0F * kPi / 192.0F));
                break;
            }
            case ViewmodelState::Charging:
                hand = striking ? windupPose(side, chargeFraction) : guardPose(side, chargeFraction);
                break;
            case ViewmodelState::ChargedHard: {
                hand = striking ? windupPose(side, 1.0F) : guardPose(side, 1.0F);
                // The tremor of a hold at the top: a hair up and down, by step.
                const float tremor = (pose.stateSteps & 1) != 0 ? 0.005F : -0.005F;
                hand.position.y += striking ? tremor : 0.0F;
                break;
            }
            case ViewmodelState::SwingLight:
            case ViewmodelState::SwingHard: {
                const bool hard = pose.state == ViewmodelState::SwingHard;
                if (striking) {
                    const HandPose from = windupPose(side, hard ? 1.0F : 0.4F);
                    HandPose target;
                    target.position = hard ? Vec3{side * 0.03F, 0.01F, -1.22F}
                                           : Vec3{side * 0.06F, -0.04F, -1.05F};
                    target.rotation = hard ? Vec3{-0.20F, 0.0F, side * 0.70F}
                                           : Vec3{-0.15F, 0.0F, 0.0F};
                    constexpr float kOut = 0.33F;
                    if (phase < kOut) {
                        const float e = easeOut(phase / kOut);
                        hand.position = lerp(from.position, target.position, e);
                        hand.rotation = lerp(from.rotation, target.rotation, e);
                    } else {
                        const float e = smooth((phase - kOut) / (1.0F - kOut));
                        hand.position = lerp(target.position, idle.position, e);
                        hand.rotation = lerp(target.rotation, idle.rotation, e);
                    }
                } else {
                    // The other hand rides up a touch through the middle of it.
                    hand.position.y += 0.03F * (1.0F - std::fabs(2.0F * phase - 1.0F));
                }
                break;
            }
            case ViewmodelState::Block: {
                const float e = smooth(clamp01(static_cast<float>(pose.stateSteps) / 6.0F));
                HandPose target;
                target.position = Vec3{side * 0.17F, 0.02F, -0.56F};
                target.rotation = Vec3{1.0F, side * 0.15F, side * 0.45F};
                hand.position = lerp(idle.position, target.position, e);
                hand.rotation = lerp(idle.rotation, target.rotation, e);
                break;
            }
            case ViewmodelState::Cast: {
                // The off hand opens forward at the centre and glows; the
                // weapon hand keeps its rest.
                if (side < 0.0F) {
                    HandPose target;
                    target.position = Vec3{-0.11F, -0.05F, -0.86F};
                    target.rotation = Vec3{0.15F, -0.10F, -0.20F};
                    float e = 1.0F;
                    if (phase < 0.3F) {
                        e = easeOut(phase / 0.3F);
                    } else if (phase > 0.6F) {
                        e = 1.0F - smooth((phase - 0.6F) / 0.4F);
                    }
                    hand.position = lerp(idle.position, target.position, e);
                    hand.rotation = lerp(idle.rotation, target.rotation, e);
                    const auto glow = [e](std::uint8_t lit, std::uint8_t warm) {
                        return static_cast<std::uint8_t>(
                            static_cast<float>(lit) + (static_cast<float>(warm) - static_cast<float>(lit)) * e);
                    };
                    handTint[i] = Rgba8{glow(tint.r, kCastGlow.r), glow(tint.g, kCastGlow.g),
                                        glow(tint.b, kCastGlow.b), 255};
                }
                break;
            }
            case ViewmodelState::Hit: {
                // Both hands jerk down and back, and settle over the flinch.
                const float k = 1.0F - phase;
                hand.position = add(idle.position, Vec3{0.0F, -0.16F * k, 0.12F * k});
                hand.rotation = add(idle.rotation, Vec3{-0.30F * k, 0.0F, side * 0.25F * k});
                break;
            }
        }
        hands[i] = hand;
    }

    const auto put = [&out](ViewmodelKind k, ViewmodelPartId part, const HandPose& hand,
                            const Rgba8& partTint) {
        ViewmodelPart p;
        p.meshId = viewmodelMeshId(k, part);
        p.position = hand.position;
        p.rotation = hand.rotation;
        p.scale = 1.0F;
        p.tint = partTint;
        out.parts.push_back(p);
    };
    // Far to near within each hand (the arm behind the fist), left before
    // right; the depth buffer sorts the rest.
    put(kind, ViewmodelPartId::LeftArm, hands[0], tint);
    put(kind, ViewmodelPartId::LeftFist, hands[0], handTint[0]);
    put(kind, ViewmodelPartId::RightArm, hands[1], tint);
    put(kind, ViewmodelPartId::RightFist, hands[1], handTint[1]);
    if (armed) {
        put(kind, ViewmodelPartId::Weapon, hands[1], tint);
    }
}

ViewmodelInstance viewmodelInstance(const render::Session& session, int forceKind) {
    const sim::PlayerBody& body = session.body();
    const render::SkyState sky = render::skyAt(session.timeOfDay());
    const render::Rgb baked = session.glow().at(body.tileX(), body.tileY(), body.band());
    const render::Rgb live =
        render::dynamicGlowAt(session.tavernLights(), body.tileX(), body.tileY(), body.band());
    ViewmodelInstance out;
    const ViewmodelKind kind =
        forceKind >= 0 && forceKind < static_cast<int>(kViewmodelKindCount)
            ? static_cast<ViewmodelKind>(forceKind)
            : viewmodelKindOf(session.tavern().playerHeldWeapon());
    poseViewmodel(out, kind, session.viewmodel(), standingTint(sky, baked, live));
    out.visible = true;
    return out;
}

}  // namespace granadad::render3d
