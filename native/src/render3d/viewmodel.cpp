#include "granadad/render3d/viewmodel.hpp"

#include <algorithm>
#include <cmath>

#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/world_renderer.hpp"
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

void pushVertex(MeshData& mesh, const Vec3& p, const Rgba8& c) {
    mesh.positions.push_back(p.x);
    mesh.positions.push_back(p.y);
    mesh.positions.push_back(p.z);
    mesh.texcoords.push_back(0.0F);
    mesh.texcoords.push_back(0.0F);
    mesh.colours.push_back(c.r);
    mesh.colours.push_back(c.g);
    mesh.colours.push_back(c.b);
    mesh.colours.push_back(c.a);
}

/// A quad from four corners counter-clockwise as seen from outside.
void pushQuad(MeshData& mesh, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
              const Rgba8& colour) {
    const auto base = static_cast<std::uint16_t>(mesh.vertexCount());
    pushVertex(mesh, a, colour);
    pushVertex(mesh, b, colour);
    pushVertex(mesh, c, colour);
    pushVertex(mesh, d, colour);
    mesh.indices.push_back(base);
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 1));
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 2));
    mesh.indices.push_back(base);
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 2));
    mesh.indices.push_back(static_cast<std::uint16_t>(base + 3));
}

/// A closed hexahedron from its eight corners: n = the near rectangle
/// (towards -Z), f = the far one (towards +Z), each as (x0,y0) (x1,y0)
/// (x1,y1) (x0,y1) seen from -Z. The forearm is one of these with the far
/// end larger and displaced; a box is the special case of equal ends.
void pushHull(MeshData& mesh, const Vec3 n[4], const Vec3 f[4], const Rgba8& base) {
    pushQuad(mesh, n[0], n[1], n[2], n[3], shade(base, 0.90F));  // -Z (front / knuckles)
    pushQuad(mesh, f[1], f[0], f[3], f[2], shade(base, 0.60F));  // +Z (back)
    pushQuad(mesh, n[3], n[2], f[2], f[3], shade(base, 1.00F));  // +Y (top)
    pushQuad(mesh, f[0], f[1], n[1], n[0], shade(base, 0.45F));  // -Y (bottom)
    pushQuad(mesh, n[1], f[1], f[2], n[2], shade(base, 0.72F));  // +X
    pushQuad(mesh, f[0], n[0], n[3], f[3], shade(base, 0.62F));  // -X
}

void pushBox(MeshData& mesh, float x0, float y0, float z0, float x1, float y1, float z1,
             const Rgba8& base) {
    const Vec3 n[4] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}};
    const Vec3 f[4] = {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    pushHull(mesh, n, f, base);
}

/// The fist: a box about the origin, knuckles towards -Z, with the thumb
/// laid along the inner side (towards the other hand).
void pushFist(MeshData& mesh, float side) {
    pushBox(mesh, -0.08F, -0.065F, -0.09F, 0.08F, 0.065F, 0.09F, kSkin);
    const float inner = -side;  // the right hand's thumb is on its -X
    const float x0 = inner > 0.0F ? 0.08F : -0.11F;
    const float x1 = inner > 0.0F ? 0.11F : -0.08F;
    pushBox(mesh, x0, -0.01F, -0.07F, x1, 0.045F, 0.02F, shade(kSkin, 0.92F));
}

/// The forearm: from just behind the fist (the origin) back, down and out
/// to the elbow, which sits off the bottom corner of the frame -- 0.30
/// closer to the eye than the fist, never near the pass's 0.05 near plane
/// (a fist rests 0.66 out), so the sleeve reads as a band from the corner
/// rather than a wall of cloth.
void pushForearm(MeshData& mesh, float side) {
    const Vec3 n[4] = {{-0.07F, -0.07F, 0.05F}, {0.07F, -0.07F, 0.05F},
                       {0.07F, 0.05F, 0.05F}, {-0.07F, 0.05F, 0.05F}};
    const float ex = side * 0.30F;
    const float ey = -0.55F;
    const float ez = 0.30F;
    const Vec3 f[4] = {{ex - 0.10F, ey - 0.09F, ez}, {ex + 0.10F, ey - 0.09F, ez},
                       {ex + 0.10F, ey + 0.09F, ez}, {ex - 0.10F, ey + 0.09F, ez}};
    pushHull(mesh, n, f, kSleeve);
}

void pushClub(MeshData& mesh) {
    // The shaft through the fist, the head out past it.
    pushBox(mesh, -0.025F, -0.025F, -0.46F, 0.025F, 0.025F, 0.10F, kWood);
    pushBox(mesh, -0.05F, -0.05F, -0.60F, 0.05F, 0.05F, -0.42F, shade(kWood, 0.85F));
    pushBox(mesh, -0.055F, -0.055F, -0.50F, 0.055F, 0.055F, -0.47F, kIron);
}

void pushDagger(MeshData& mesh) {
    pushBox(mesh, -0.014F, -0.005F, -0.36F, 0.014F, 0.005F, -0.09F, kSteel);
    pushBox(mesh, -0.05F, -0.012F, -0.10F, 0.05F, 0.012F, -0.08F, kIron);
    pushBox(mesh, -0.018F, -0.018F, 0.09F, 0.018F, 0.018F, 0.11F, kIron);
}

void pushSword(MeshData& mesh) {
    pushBox(mesh, -0.022F, -0.006F, -0.90F, 0.022F, 0.006F, -0.10F, kSteel);
    // The fuller, a shade darker down the middle of the blade's flat.
    pushBox(mesh, -0.006F, -0.0065F, -0.82F, 0.006F, 0.0065F, -0.16F, shade(kSteel, 0.86F));
    pushBox(mesh, -0.09F, -0.014F, -0.11F, 0.09F, 0.014F, -0.08F, kIron);
    pushBox(mesh, -0.022F, -0.022F, 0.09F, 0.022F, 0.022F, 0.13F, kIron);
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
    out.rigOffset = kViewmodelRigOffset;
    out.rigYaw = kViewmodelRigYaw;
    out.rigScale = 1.0F;
    out.tint = tint;
    out.parts.clear();

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
                // A slow breathing sway, from rest at re-entry (sin 0 = 0) so
                // a swing's return lands on it without a jump.
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

ViewmodelInstance viewmodelInstance(const render::Session& session) {
    const sim::PlayerBody& body = session.body();
    const render::SkyState sky = render::skyAt(session.timeOfDay());
    const render::Rgb baked = session.renderer().glow().at(body.tileX(), body.tileY(), body.band());
    const render::Rgb live =
        render::dynamicGlowAt(session.tavernLights(), body.tileX(), body.tileY(), body.band());
    ViewmodelInstance out;
    poseViewmodel(out, viewmodelKindOf(session.tavern().playerHeldWeapon()), session.viewmodel(),
                  standingTint(sky, baked, live));
    out.visible = true;
    return out;
}

}  // namespace granadad::render3d
