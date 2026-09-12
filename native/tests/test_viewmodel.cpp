// THE VIEWMODEL + HUD LANE, ASSERTED.
//
// Three claims, each a case the docker gate names:
//
//   1. THE HANDS FOLLOW THE FIGHT. Session::viewmodel() -- the machine
//      stepped beside the sim -- reads Idle at rest, Charging with the real
//      charge count while the Attack key is held, ChargedHard at the hold
//      threshold, the swing at the tier the ROOM resolved (hard off a held
//      release, light off a tap) for exactly kSwingSteps, the guard while
//      the room says the guard is up, the cast when the room let the hand
//      try (and NOT on a refusal), and the flinch on an unguarded blow.
//   2. THE HUD COMPOSITES BYTE FOR BYTE. The terminal register drawn into
//      its Framebuffer, uploaded and drawn over a real 3D frame of the
//      Docks through rlsw: every pixel the overlay covers opaquely is the
//      overlay's own bytes, every clear pixel is the world's own bytes, and
//      a wash in between is the lerp within rlsw's truncation. The eighteen
//      pixel-exact HUD/page files never see the 3D pass at all -- they keep
//      drawing into the Framebuffer -- so this is the one place the
//      COMPOSITE is held to the same standard.
//   3. THE FISTS DRAW OVER THE WORLD. Headless through rlsw: the raised
//      placeholder fists put pixels in the lower corners of the Docks frame,
//      and in front of a wall thirty hundredths of a tile from the eye --
//      the depth squeeze the adapter does in the projection, proved on the
//      pixels rather than trusted.
//
// Plus the description side (kinds off the held weapon, mesh ids in their
// range, the hash moving with the hands) and the washes riding the overlay.
// Placeholder arms throughout: no licensed glb exists in this container.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/viewmodel_machine.hpp"
#include "granadad/render3d/actor_instances.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/viewmodel.hpp"
#include "granadad/render3d/world_scene.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/spellforge.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;
namespace sim = granadad::sim;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 180;

render::SessionConfig sessionConfig(int hour) {
    render::SessionConfig config;
    config.width = kWidth;
    config.height = kHeight;
    config.timeOfDay = hour * 3600;
    config.timeOfDayGiven = true;
    return config;
}

BackendConfig headlessConfig() {
    BackendConfig config;
    config.width = kWidth;
    config.height = kHeight;
    config.windowScale = 1;
    config.vsync = false;
    return config;
}

render::Framebuffer drawOnce(Backend& video, const SceneDescription& scene,
                             const render::Framebuffer* overlay, SceneStats* stats) {
    video.beginFrame(scene.clearColour);
    const SceneStats drawn = video.drawScene(scene);
    if (stats != nullptr) {
        *stats = drawn;
    }
    if (overlay != nullptr) {
        video.drawOverlay(*overlay);
    }
    render::Framebuffer shot(1, 1);
    video.endFrame(&shot);
    return shot;
}

std::uint32_t pixelAt(const render::Framebuffer& frame, int x, int y) {
    return frame.pixels()[frame.index(x, y)];
}

int channel(std::uint32_t pixel, int shift) { return static_cast<int>((pixel >> shift) & 0xFFU); }

int channelDistance(std::uint32_t a, std::uint32_t b) {
    int worst = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        const int d = std::abs(channel(a, shift) - channel(b, shift));
        worst = d > worst ? d : worst;
    }
    return worst;
}

/// The whole 3D stack for a session, as the client composes it: the Docks
/// by chunk, the sky, the people, the hands, the camera off the body.
struct Stack {
    std::unique_ptr<WorldScene> docks;
    SceneDescription scene;

    explicit Stack(const render::Session& session) {
        docks = std::make_unique<WorldScene>(session.tiles(), session.atlas(),
                                             &session.renderer().glow());
        refresh(session);
    }

    void refresh(const render::Session& session) {
        WorldSceneParams params;
        params.timeOfDaySeconds = session.timeOfDay();
        params.dynamicLamps = session.tavernLights();
        docks->refresh(scene, session.camera(), static_cast<float>(kWidth) / static_cast<float>(kHeight),
                       params);
        putActorRigs(scene);
        scene.actors = actorInstances(session, session.camera());
        putViewmodelMeshes(scene);
        scene.viewmodel = viewmodelInstance(session);
    }
};

/// The overlay the client draws: the terminal register on a transparent
/// ground, with the world pass left out.
render::Framebuffer overlayOf(const render::Session& session) {
    render::Framebuffer overlay(kWidth, kHeight);
    (void)session.drawFrame(overlay, render::Session::FramePasses{.world = false});
    return overlay;
}

/// Classifies every pixel of a composite against the overlay and the bare
/// world: opaque overlay pixels must be the overlay's bytes EXACTLY, clear
/// pixels the world's EXACTLY, and partial ones the straight-alpha lerp
/// within rlsw's byte truncation. Returns the worst miss in each class.
struct CompositeReport {
    std::size_t opaque = 0;
    std::size_t clear = 0;
    std::size_t partial = 0;
    int worstOpaque = 0;
    int worstClear = 0;
    int worstPartial = 0;
};

CompositeReport classify(const render::Framebuffer& composite, const render::Framebuffer& world,
                         const render::Framebuffer& overlay) {
    CompositeReport report;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::uint32_t ov = pixelAt(overlay, x, y);
            const std::uint32_t got = pixelAt(composite, x, y) & 0x00FFFFFFU;
            const std::uint32_t under = pixelAt(world, x, y) & 0x00FFFFFFU;
            const int alpha = channel(ov, 24);
            if (alpha == 255) {
                ++report.opaque;
                report.worstOpaque =
                    std::max(report.worstOpaque, channelDistance(got, ov & 0x00FFFFFFU));
            } else if (alpha == 0) {
                ++report.clear;
                report.worstClear = std::max(report.worstClear, channelDistance(got, under));
            } else {
                ++report.partial;
                int worst = 0;
                for (int shift = 0; shift < 24; shift += 8) {
                    const int expected =
                        (channel(ov, shift) * alpha + channel(under, shift) * (255 - alpha)) / 255;
                    worst = std::max(worst, std::abs(channel(got, shift) - expected));
                }
                report.worstPartial = std::max(report.worstPartial, worst);
            }
        }
    }
    return report;
}

const ViewmodelPart* partOf(const ViewmodelInstance& hands, ViewmodelPartId part) {
    for (const ViewmodelPart& p : hands.parts) {
        if (p.meshId == viewmodelMeshId(static_cast<ViewmodelKind>(hands.kind), part)) {
            return &p;
        }
    }
    return nullptr;
}

/// A big grey wall quad `distance` tiles north of the origin, facing the
/// eye, in the starter/debug id range.
MeshData wallMesh(float distance) {
    MeshData wall;
    wall.id = 950;
    wall.version = 1;
    const float z = -distance;
    const Vec3 corners[4] = {{-3.0F, -3.0F, z}, {3.0F, -3.0F, z}, {3.0F, 3.0F, z}, {-3.0F, 3.0F, z}};
    for (const Vec3& c : corners) {
        wall.positions.push_back(c.x);
        wall.positions.push_back(c.y);
        wall.positions.push_back(c.z);
        wall.texcoords.push_back(0.0F);
        wall.texcoords.push_back(0.0F);
        wall.colours.push_back(120);
        wall.colours.push_back(120);
        wall.colours.push_back(124);
        wall.colours.push_back(255);
    }
    // CCW seen from the eye at the origin looking down -Z.
    wall.indices = {0, 1, 2, 0, 2, 3};
    return wall;
}

}  // namespace

TEST_CASE("the viewmodel clip follows the combat state machine") {
    render::Session session(sessionConfig(8));
    const sim::MoveInput still{};
    const sim::Tavern& room = session.tavern();

    // At rest: idle, and the counter runs.
    session.stepMany(still, 3);
    CHECK(session.viewmodel().state == ViewmodelState::Idle);
    CHECK(session.viewmodel().stateSteps >= 2);
    CHECK(session.viewmodel().swingSeq == 0);

    // The hold: Charging with the room's own charge count, step for step.
    session.attackDown();
    session.stepMany(still, 3);
    CHECK(room.playerChargeSteps() == 3);
    CHECK(session.viewmodel().state == ViewmodelState::Charging);
    CHECK(session.viewmodel().chargeSteps == 3);
    CHECK(session.viewmodel().stateSteps == 2);
    // ...up to the hard threshold, where it reads ChargedHard and holds.
    session.stepMany(still, sim::kHardSwingHoldSteps - 3);
    REQUIRE(room.playerChargeHard());
    CHECK(session.viewmodel().state == ViewmodelState::ChargedHard);
    CHECK(session.viewmodel().chargeSteps == sim::kHardSwingHoldSteps);
    session.stepMany(still, 5);
    CHECK(session.viewmodel().state == ViewmodelState::ChargedHard);
    CHECK(session.viewmodel().chargeSteps == sim::kHardSwingHoldSteps);

    // The release: the HARD swing, for exactly kSwingSteps, then the hand
    // falls back to idle (the room is in recovery: no charge, no guard).
    session.attackUp();
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::SwingHard);
    CHECK(session.viewmodel().stateSteps == 0);
    CHECK(session.viewmodel().swingSeq == 1);
    session.stepMany(still, ViewmodelMachine::kSwingSteps - 1);
    CHECK(session.viewmodel().state == ViewmodelState::SwingHard);
    CHECK(session.viewmodel().stateSteps == ViewmodelMachine::kSwingSteps - 1);
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::Idle);
    CHECK(session.viewmodel().chargeSteps == 0);

    // A tap after the lockout: the LIGHT swing, the other hand.
    session.stepMany(still, sim::kHardSwingRecoverySteps);
    REQUIRE(room.playerCombatIdle());
    session.punch();
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::SwingLight);
    CHECK(session.viewmodel().swingSeq == 2);
    {
        // Swing one was the right hand, swing two is the left: the left
        // fist is the one out towards the centre now.
        ViewmodelInstance hands;
        poseViewmodel(hands, ViewmodelKind::Fists, session.viewmodel(), Rgba8{});
        const ViewmodelPart* left = partOf(hands, ViewmodelPartId::LeftFist);
        const ViewmodelPart* right = partOf(hands, ViewmodelPartId::RightFist);
        REQUIRE(left != nullptr);
        REQUIRE(right != nullptr);
        // Six steps in: the striking fist is out past the resting one.
        session.stepMany(still, 6);
        poseViewmodel(hands, ViewmodelKind::Fists, session.viewmodel(), Rgba8{});
        left = partOf(hands, ViewmodelPartId::LeftFist);
        right = partOf(hands, ViewmodelPartId::RightFist);
        CHECK(left->position.z < right->position.z);
        CHECK(std::fabs(left->position.x) < std::fabs(right->position.x));
    }
    session.stepMany(still, ViewmodelMachine::kSwingSteps);
    CHECK(session.viewmodel().state == ViewmodelState::Idle);

    // The guard: held while the room says so, and the room only says so
    // once the hand is free again.
    session.stepMany(still, sim::kSwingRecoverySteps);
    REQUIRE(room.playerCombatIdle());
    session.setBlocking(true);
    session.step(still);
    REQUIRE(room.playerBlocking());
    CHECK(session.viewmodel().state == ViewmodelState::Block);
    session.stepMany(still, 10);
    CHECK(session.viewmodel().state == ViewmodelState::Block);
    CHECK(session.viewmodel().stateSteps == 10);
    session.setBlocking(false);
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::Idle);

    // A blow taken, unguarded: the flinch, over everything, for kHitSteps.
    const std::int32_t hpBefore = room.playerHp();
    session.tavern().injurePlayer(3);
    REQUIRE(room.playerHp() < hpBefore);
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::Hit);
    session.stepMany(still, ViewmodelMachine::kHitSteps - 1);
    CHECK(session.viewmodel().state == ViewmodelState::Hit);
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::Idle);

    // A cast REFUSED (the grimoire is empty at spawn) moves nothing...
    session.castEquipped();
    CHECK_FALSE(session.lastMessage().empty());
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::Idle);
    // ...and a cast the room let the hand try -- opened or slipped, the
    // recovery clock says it was thrown -- plays the gesture.
    const sim::Spell* spell = room.spellbook().find("steady_the_hand");
    REQUIRE(spell != nullptr);
    REQUIRE(session.tavern().dialogue().grimoire().learn(*spell));
    REQUIRE(session.tavern().equipSpellAt(0));
    session.castEquipped();
    REQUIRE(room.castCooldownLeft() > 0);
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::Cast);
    session.stepMany(still, ViewmodelMachine::kCastSteps - 1);
    CHECK(session.viewmodel().state == ViewmodelState::Cast);
    session.step(still);
    CHECK(session.viewmodel().state == ViewmodelState::Idle);
}

TEST_CASE("the viewmodel is described from the sim's own hand and hashes") {
    // The kind table and the asset lane's file names.
    CHECK(viewmodelKindOf(sim::Weapon::Fists) == ViewmodelKind::Fists);
    CHECK(viewmodelKindOf(sim::Weapon::Improvised) == ViewmodelKind::Club);
    CHECK(viewmodelKindOf(sim::Weapon::Blunt) == ViewmodelKind::Club);
    CHECK(viewmodelKindOf(sim::Weapon::Evictor) == ViewmodelKind::Club);
    CHECK(viewmodelKindOf(sim::Weapon::Edged) == ViewmodelKind::Sword);
    CHECK(viewmodelRigFile(ViewmodelKind::Fists) == "viewmodel_fists.glb");
    CHECK(viewmodelRigFile(ViewmodelKind::Club) == "viewmodel_fists.glb");
    CHECK(viewmodelRigFile(ViewmodelKind::Dagger) == "viewmodel_fists.glb");
    CHECK(viewmodelRigFile(ViewmodelKind::Sword) == "viewmodel_sword.glb");
    CHECK(viewmodelWeaponFile(ViewmodelKind::Fists).empty());
    CHECK(viewmodelWeaponFile(ViewmodelKind::Sword).empty());
    CHECK(viewmodelWeaponFile(ViewmodelKind::Club) ==
          "PolygonFantasyHeroCharacters/SM_Wep_Mace_01.gltf");
    CHECK(viewmodelWeaponFile(ViewmodelKind::Dagger) ==
          "PolygonFantasyHeroCharacters/SM_Wep_Dagger_01.gltf");
    for (std::size_t k = 0; k < kViewmodelKindCount; ++k) {
        const auto kind = static_cast<std::uint8_t>(k);
        CHECK(viewmodelRigFileOf(kind) == viewmodelRigFile(static_cast<ViewmodelKind>(kind)));
        CHECK(viewmodelWeaponFileOf(kind) ==
              viewmodelWeaponFile(static_cast<ViewmodelKind>(kind)));
    }
    CHECK(viewmodelRigFileOf(200).empty());
    CHECK(viewmodelWeaponFileOf(200).empty());
    for (std::size_t s = 0; s < render::kViewmodelStateCount; ++s) {
        CHECK_FALSE(
            render::viewmodelStateName(static_cast<ViewmodelState>(static_cast<std::uint8_t>(s)))
                .empty());
    }
    CHECK(render::viewmodelStateName(ViewmodelState::ChargedHard) == "charged_hard");
    CHECK(render::viewmodelStateName(ViewmodelState::SwingHard) == "swing_hard");

    // Every kind's parts, once, in the viewmodel range, closed and small.
    SceneDescription scene;
    putViewmodelMeshes(scene);
    const std::size_t meshCount = scene.meshes.size();
    CHECK(meshCount == 4U * 4U + 3U);  // four arm parts per kind, a weapon for all but fists
    std::set<std::uint32_t> ids;
    for (const MeshData& mesh : scene.meshes) {
        CHECK(mesh.id >= kViewmodelMeshIdBase);
        CHECK(mesh.id < kViewmodelMeshIdBase + 40U);
        CHECK(mesh.version == 1);
        CHECK(ids.insert(mesh.id).second);
        CHECK(mesh.triangleCount() > 0);
        CHECK(mesh.vertexCount() < 100U);
        CHECK(mesh.colours.size() == mesh.vertexCount() * 4);
        for (const std::uint16_t index : mesh.indices) {
            CHECK(index < mesh.vertexCount());
        }
    }
    putViewmodelMeshes(scene);
    CHECK(scene.meshes.size() == meshCount);  // idempotent

    // A session's hands: bare at spawn, four parts, fists below the eye
    // line and in front of it, right on the right -- and DOWN, because the
    // room's own stance (Tavern::playerHandsUp, STANCE & ROOM BUILD) is
    // down until the first Attack press: the fists hang below the raised
    // rest the pose tables call idle, and the licensed rig drops with them.
    render::Session session(sessionConfig(8));
    session.step(sim::MoveInput{});
    CHECK_FALSE(session.tavern().playerHandsUp());
    CHECK_FALSE(session.viewmodel().handsUp);
    ViewmodelInstance hands = viewmodelInstance(session);
    CHECK(hands.visible);
    CHECK(hands.kind == static_cast<std::uint8_t>(ViewmodelKind::Fists));
    CHECK(hands.state == ViewmodelState::Idle);
    CHECK(hands.parts.size() == 4U);
    CHECK(hands.fovyDegrees == doctest::Approx(kViewmodelFovyDegrees));
    CHECK(hands.tint.a == 255);
    for (const ViewmodelPart& part : hands.parts) {
        CHECK(scene.findMesh(part.meshId) != nullptr);
        CHECK(part.position.y < 0.0F);
        CHECK(part.position.z < 0.0F);
    }
    REQUIRE(partOf(hands, ViewmodelPartId::RightFist) != nullptr);
    REQUIRE(partOf(hands, ViewmodelPartId::LeftFist) != nullptr);
    const Vec3 downRight = partOf(hands, ViewmodelPartId::RightFist)->position;
    const Vec3 downLeft = partOf(hands, ViewmodelPartId::LeftFist)->position;
    CHECK(downRight.x > 0.0F);
    CHECK(downLeft.x < 0.0F);
    CHECK(partOf(hands, ViewmodelPartId::Weapon) == nullptr);
    // The glb path with the hands down: the block clip at its first frame
    // (the hips), at the guard framing -- the clip's own motion lowers them.
    CHECK(hands.rigClip == 5U);
    CHECK(hands.rigFrame == doctest::Approx(0.0F));
    CHECK(hands.rigPitch == doctest::Approx(viewmodelGuardPlacement(ViewmodelKind::Fists).pitch));

    // THE RAISED REST: the pose tables' own idle with the hands up, at
    // re-entry -- what a hand-built pose is (handsUp defaults to up, the
    // stance ease saturated), and what every comparison below is against.
    ViewmodelPose raised;
    ViewmodelInstance rest;
    poseViewmodel(rest, ViewmodelKind::Fists, raised, Rgba8{});
    CHECK(rest.rigOffset.y == doctest::Approx(viewmodelGuardPlacement(ViewmodelKind::Fists).offset.y));
    CHECK(rest.rigClip == 5U);
    CHECK(rest.rigFrame == doctest::Approx(kViewmodelGuardFrame));
    REQUIRE(partOf(rest, ViewmodelPartId::RightFist) != nullptr);
    REQUIRE(partOf(rest, ViewmodelPartId::LeftFist) != nullptr);
    const Vec3 restRight = partOf(rest, ViewmodelPartId::RightFist)->position;
    const Vec3 restLeft = partOf(rest, ViewmodelPartId::LeftFist)->position;
    CHECK(restRight.x > 0.0F);
    CHECK(restLeft.x < 0.0F);
    CHECK(downRight.y < restRight.y - 0.08F);
    CHECK(downLeft.y < restLeft.y - 0.08F);

    // A GUARD PRESSED AND RELEASED puts the hands UP by the room's own rule
    // (a guard raises them, a release does not lower them): the fists rise
    // to the raised rest over the stance ease and stay there. LOWER HANDS
    // drops them again the same way. The row and the hands read one bit.
    session.setBlocking(true);
    session.step(sim::MoveInput{});
    CHECK(session.tavern().playerHandsUp());
    CHECK(session.viewmodel().handsUp);
    session.setBlocking(false);
    session.stepMany(sim::MoveInput{}, ViewmodelMachine::kStanceSteps + 4);
    CHECK(session.viewmodel().state == ViewmodelState::Idle);
    CHECK(session.viewmodel().handsUp);
    hands = viewmodelInstance(session);
    REQUIRE(partOf(hands, ViewmodelPartId::RightFist) != nullptr);
    CHECK(std::fabs(partOf(hands, ViewmodelPartId::RightFist)->position.y - restRight.y) < 0.02F);
    CHECK(hands.rigFrame == doctest::Approx(kViewmodelGuardFrame));
    CHECK(std::fabs(hands.rigOffset.y - viewmodelGuardPlacement(ViewmodelKind::Fists).offset.y) < 0.01F);
    session.tavern().lowerPlayerHands();
    session.step(sim::MoveInput{});
    CHECK_FALSE(session.viewmodel().handsUp);
    CHECK(session.viewmodel().stanceSteps == 0);
    // Half way through the ease the fist is between the two rests.
    session.stepMany(sim::MoveInput{}, ViewmodelMachine::kStanceSteps / 2);
    hands = viewmodelInstance(session);
    REQUIRE(partOf(hands, ViewmodelPartId::RightFist) != nullptr);
    CHECK(partOf(hands, ViewmodelPartId::RightFist)->position.y < restRight.y - 0.02F);
    CHECK(partOf(hands, ViewmodelPartId::RightFist)->position.y > downRight.y + 0.02F);
    session.stepMany(sim::MoveInput{}, ViewmodelMachine::kStanceSteps);
    hands = viewmodelInstance(session);
    REQUIRE(partOf(hands, ViewmodelPartId::RightFist) != nullptr);
    CHECK(partOf(hands, ViewmodelPartId::RightFist)->position.y < restRight.y - 0.08F);
    CHECK(hands.rigFrame == doctest::Approx(0.0F));

    // The hash covers the hands: the same session twice is the same digest,
    // one step of a charge is another.
    scene.viewmodel = hands;
    const std::uint64_t restA = sceneHash(scene);
    scene.viewmodel = viewmodelInstance(session);
    CHECK(sceneHash(scene) == restA);
    session.attackDown();
    session.step(sim::MoveInput{});
    scene.viewmodel = viewmodelInstance(session);
    CHECK(scene.viewmodel.state == ViewmodelState::Charging);
    CHECK(sceneHash(scene) != restA);
    scene.viewmodel.visible = false;
    const std::uint64_t hidden = sceneHash(scene);
    scene.viewmodel.visible = true;
    CHECK(sceneHash(scene) != hidden);

    // The held weapon swaps the hand model: the Evictor is a club in the
    // right hand, sharing the right fist's place, and a fifth part.
    REQUIRE(session.tavern().grantPlayerWeapon(sim::kEvictorWeaponId));
    hands = viewmodelInstance(session);
    CHECK(hands.kind == static_cast<std::uint8_t>(ViewmodelKind::Club));
    CHECK(hands.parts.size() == 5U);
    const ViewmodelPart* weapon = partOf(hands, ViewmodelPartId::Weapon);
    const ViewmodelPart* armedRight = partOf(hands, ViewmodelPartId::RightFist);
    REQUIRE(weapon != nullptr);
    REQUIRE(armedRight != nullptr);
    CHECK(weapon->position.x == armedRight->position.x);
    CHECK(weapon->position.y == armedRight->position.y);
    CHECK(weapon->position.z == armedRight->position.z);
    CHECK(weapon->rotation.x == armedRight->rotation.x);

    // The pose tables, on their own clock: a hard swing at its far point
    // reaches further than a light one; the guard brings both fists up
    // to the middle; the flinch drops them; the cast reaches the off hand
    // out and lights it.
    ViewmodelPose pose;
    pose.swingSeq = 1;
    pose.state = ViewmodelState::SwingLight;
    pose.stateSteps = 6;
    ViewmodelInstance light;
    poseViewmodel(light, ViewmodelKind::Fists, pose, Rgba8{});
    pose.state = ViewmodelState::SwingHard;
    ViewmodelInstance hard;
    poseViewmodel(hard, ViewmodelKind::Fists, pose, Rgba8{});
    CHECK(partOf(hard, ViewmodelPartId::RightFist)->position.z <
          partOf(light, ViewmodelPartId::RightFist)->position.z);
    CHECK(partOf(light, ViewmodelPartId::RightFist)->position.z < restRight.z);
    pose.state = ViewmodelState::Block;
    pose.stateSteps = 6;
    ViewmodelInstance guard;
    poseViewmodel(guard, ViewmodelKind::Fists, pose, Rgba8{});
    CHECK(std::fabs(partOf(guard, ViewmodelPartId::LeftFist)->position.x) < 0.2F);
    CHECK(std::fabs(partOf(guard, ViewmodelPartId::RightFist)->position.x) < 0.2F);
    CHECK(partOf(guard, ViewmodelPartId::RightFist)->position.y > restRight.y);
    pose.state = ViewmodelState::Hit;
    pose.stateSteps = 0;
    ViewmodelInstance flinch;
    poseViewmodel(flinch, ViewmodelKind::Fists, pose, Rgba8{});
    CHECK(partOf(flinch, ViewmodelPartId::RightFist)->position.y < restRight.y - 0.1F);
    pose.state = ViewmodelState::Cast;
    pose.stateSteps = 8;
    ViewmodelInstance cast;
    poseViewmodel(cast, ViewmodelKind::Fists, pose, Rgba8{100, 100, 100, 255});
    CHECK(partOf(cast, ViewmodelPartId::LeftFist)->position.z < restLeft.z);
    CHECK(partOf(cast, ViewmodelPartId::LeftFist)->tint.r > 100);  // the glow
    CHECK(partOf(cast, ViewmodelPartId::RightFist)->tint.r == 100);
}

TEST_CASE("the HUD overlay composites byte-identically over a 3D frame") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    // A real session, walked one step (the opening page closes) and left
    // to settle, so the overlay is the HUD proper: the reticle, the
    // compass, the clock, the bottom band -- opaque glyphs and plates over
    // a mostly clear ground.
    render::Session session(sessionConfig(20));
    sim::MoveInput forward;
    forward.forward = 1;
    session.step(forward);
    session.stepMany(sim::MoveInput{}, 20);
    const render::Framebuffer overlay = overlayOf(session);
    Stack stack(session);

    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    const render::Framebuffer world = drawOnce(*video, stack.scene, nullptr, nullptr);
    SceneStats stats;
    const render::Framebuffer composite = drawOnce(*video, stack.scene, &overlay, &stats);
    REQUIRE(composite.width() == kWidth);
    REQUIRE(composite.height() == kHeight);
    CHECK(stats.viewmodelPartsDrawn == stack.scene.viewmodel.parts.size());

    const CompositeReport report = classify(composite, world, overlay);
    MESSAGE("HUD overlay: " << report.opaque << " opaque, " << report.clear << " clear, "
                            << report.partial << " partial; worst miss opaque "
                            << report.worstOpaque << ", clear " << report.worstClear
                            << ", partial " << report.worstPartial);
    // A HUD is on it, and most of the world shows through.
    CHECK(report.opaque + report.partial > 200U);
    CHECK(report.clear > static_cast<std::size_t>(kWidth * kHeight) / 2);
    // BYTE FOR BYTE where the overlay is opaque and where it is clear:
    // 255 * (1/255) is exactly 1.0f, so rlsw's src*a + dst*(1-a) is src
    // there and dst here, and its byte store truncates back to the byte it
    // started from -- checked for every value, not assumed.
    CHECK(report.worstOpaque == 0);
    CHECK(report.worstClear == 0);
    CHECK(report.worstPartial <= 2);
    // The overlay's own placement at a window the frame's own size is 1:1.
    const OverlayPlacement placement = video->overlayPlacement(kWidth, kHeight);
    CHECK(placement.scale == 1);
    CHECK(placement.offsetX == 0);
    CHECK(placement.offsetY == 0);
    // And the same composite draws the same bytes again.
    const render::Framebuffer again = drawOnce(*video, stack.scene, &overlay, nullptr);
    CHECK(again.pixels() == composite.pixels());

    // THE SAME CLAIM WHERE IT CANNOT PASS BY ACCIDENT: a page's plate and
    // its glyphs, drawn at full alpha through the Framebuffer's own
    // primitives (the ones every HUD/page drawer uses), over the same
    // Docks frame -- thousands of opaque pixels, every one the overlay's
    // own bytes, whatever the world under them was.
    render::Framebuffer page(kWidth, kHeight);
    page.clearTransparent();
    page.fillRect(40, 30, 240, 120, render::Rgb{0.05F, 0.05F, 0.06F}, 1.0F);
    page.fillRect(40, 30, 240, 2, render::Rgb{0.86F, 0.82F, 0.72F}, 1.0F);
    page.fillRect(40, 148, 240, 2, render::Rgb{0.86F, 0.82F, 0.72F}, 1.0F);
    const render::Rgb bone{0.86F, 0.82F, 0.72F};
    const render::Rgb warm{0.95F, 0.55F, 0.20F};
    (void)render::drawText(page, 52, 44, "PUT DOWN IN THE GILDED GULL.", bone, 1.0F, 1);
    (void)render::drawText(page, 52, 56, "BY CANNIC. BY STEEL.", warm, 1.0F, 1);
    // A half wash beside it, the way a punch wash draws.
    page.fillRect(0, 160, kWidth, 20, render::Rgb{0.58F, 0.10F, 0.08F}, 0.5F);
    const render::Framebuffer paged = drawOnce(*video, stack.scene, &page, nullptr);
    const CompositeReport plate = classify(paged, world, page);
    MESSAGE("plate overlay: " << plate.opaque << " opaque, " << plate.partial
                              << " partial; worst miss opaque " << plate.worstOpaque
                              << ", clear " << plate.worstClear << ", partial "
                              << plate.worstPartial);
    CHECK(plate.opaque > 20000U);
    CHECK(plate.partial > 5000U);
    CHECK(plate.worstOpaque == 0);
    CHECK(plate.worstClear == 0);
    CHECK(plate.worstPartial <= 2);
}

TEST_CASE("the raised fists render over the Docks and in front of the nearest wall") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);

    // THE WALL. A grey plane thirty hundredths of a tile from the eye,
    // filling the view: without the hands the frame is nothing but wall;
    // with them the fists show in the lower corners -- at 0.66 tiles they
    // are BEHIND the wall by distance, and in front of it on the picture,
    // which is the depth squeeze doing its one job.
    SceneDescription near;
    near.clearColour = Rgba8{10, 10, 10, 255};
    render::Camera eye;
    eye.x = 0.0F;
    eye.y = 0.0F;
    eye.z = 0.0F;
    eye.yaw = 0.0F;
    eye.pitch = 0.0F;
    eye.hfovTan = 1.0F;
    near.camera = cameraFrom(eye, static_cast<float>(kWidth) / static_cast<float>(kHeight));
    near.putMesh(wallMesh(0.30F));
    Instance wall;
    wall.meshId = 950;
    near.instances.push_back(wall);
    const render::Framebuffer wallOnly = drawOnce(*video, near, nullptr, nullptr);
    const std::uint32_t wallColour = pixelAt(wallOnly, kWidth / 2, kHeight / 2);
    CHECK(channelDistance(wallColour, 0xFF7C7878U) <= 1);
    for (int y = 0; y < kHeight; y += 9) {
        for (int x = 0; x < kWidth; x += 16) {
            CHECK(channelDistance(pixelAt(wallOnly, x, y), wallColour) <= 1);
        }
    }

    putViewmodelMeshes(near);
    ViewmodelPose rest;
    poseViewmodel(near.viewmodel, ViewmodelKind::Fists, rest, Rgba8{});
    near.viewmodel.visible = true;
    SceneStats stats;
    const render::Framebuffer withHands = drawOnce(*video, near, nullptr, &stats);
    CHECK(stats.viewmodelPartsDrawn == 4U);
    CHECK_FALSE(stats.viewmodelSkinned);
    std::size_t handPixels = 0;
    std::size_t upperPixels = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (pixelAt(withHands, x, y) != pixelAt(wallOnly, x, y)) {
                ++handPixels;
                if (y < kHeight / 3) {
                    ++upperPixels;
                }
            }
        }
    }
    MESSAGE("hand pixels over the wall: " << handPixels << " (" << upperPixels << " in the top third)");
    CHECK(handPixels > 1500U);
    CHECK(handPixels < static_cast<std::size_t>(kWidth * kHeight) / 3);
    CHECK(upperPixels == 0U);
    // The fists themselves, where the pose puts them: a 55-degree vertical
    // field at 16:9, the right fist at view (0.36, -0.27, -0.66) projects
    // to about (254, 161) and the left to about (66, 163). Skin, not wall.
    for (const auto& at : {std::pair<int, int>{254, 161}, std::pair<int, int>{66, 163}}) {
        const std::uint32_t p = pixelAt(withHands, at.first, at.second);
        CHECK(channelDistance(p, wallColour) > 20);
        CHECK(channel(p, 0) > channel(p, 8));   // r > g
        CHECK(channel(p, 8) > channel(p, 16));  // g > b
    }
    // Byte for byte again.
    const render::Framebuffer again = drawOnce(*video, near, nullptr, nullptr);
    CHECK(again.pixels() == withHands.pixels());

    // THE DOCKS. The real district at eight in the morning from the spawn,
    // with the people, with the hands: the fists put pixels in the bottom
    // half that the world and the crowd alone did not.
    render::Session session(sessionConfig(8));
    session.stepMany(sim::MoveInput{}, 2);
    // STANCE: the hands come UP by the room's own rule (a guard raises
    // them) and settle at the raised rest before the picture is taken --
    // this case is about the RAISED fists; the lowered ones are measured
    // after it.
    session.setBlocking(true);
    session.step(sim::MoveInput{});
    session.setBlocking(false);
    session.stepMany(sim::MoveInput{}, ViewmodelMachine::kStanceSteps + 4);
    REQUIRE(session.tavern().playerHandsUp());
    REQUIRE(session.viewmodel().state == ViewmodelState::Idle);
    REQUIRE(session.viewmodel().handsUp);
    Stack stack(session);
    REQUIRE(stack.scene.viewmodel.visible);
    SceneStats docksStats;
    const render::Framebuffer docks = drawOnce(*video, stack.scene, nullptr, &docksStats);
    CHECK(docksStats.viewmodelPartsDrawn == 4U);
    SceneDescription bare = stack.scene;
    bare.viewmodel.visible = false;
    SceneStats bareStats;
    const render::Framebuffer noHands = drawOnce(*video, bare, nullptr, &bareStats);
    CHECK(bareStats.viewmodelPartsDrawn == 0U);
    CHECK(bareStats.instancesDrawn + 4U == docksStats.instancesDrawn);
    std::size_t lower = 0;
    std::size_t upper = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (pixelAt(docks, x, y) != pixelAt(noHands, x, y)) {
                if (y >= kHeight / 2) {
                    ++lower;
                } else {
                    ++upper;
                }
            }
        }
    }
    MESSAGE("hand pixels over the Docks: " << lower << " in the lower half, " << upper << " above");
    CHECK(lower > 1500U);
    CHECK(upper < 200U);

    // THE HANDS DOWN. LOWER HANDS, the stance ease played out, the same
    // picture: the fists hang below the frame with only the knuckles
    // showing at the bottom edge -- fewer pixels than raised, every one of
    // them in the bottom quarter, none above it.
    session.tavern().lowerPlayerHands();
    session.stepMany(sim::MoveInput{}, ViewmodelMachine::kStanceSteps + 4);
    REQUIRE_FALSE(session.viewmodel().handsUp);
    stack.refresh(session);
    REQUIRE(stack.scene.viewmodel.visible);
    const render::Framebuffer docksDown = drawOnce(*video, stack.scene, nullptr, nullptr);
    SceneDescription bareDown = stack.scene;
    bareDown.viewmodel.visible = false;
    const render::Framebuffer noHandsDown = drawOnce(*video, bareDown, nullptr, nullptr);
    std::size_t down = 0;
    std::size_t downHigh = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (pixelAt(docksDown, x, y) != pixelAt(noHandsDown, x, y)) {
                ++down;
                if (y < kHeight * 3 / 4) {
                    ++downHigh;
                }
            }
        }
    }
    MESSAGE("hand pixels over the Docks, hands down: " << down << " (" << downHigh
                                                        << " above the bottom quarter)");
    CHECK(down > 0U);
    CHECK(down < lower);
    CHECK(downHigh == 0U);
}

TEST_CASE("the washes are overlay pixels over the 3D frame") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    // A blow landing on the player fires the blooded wash -- a low-alpha
    // fill of the whole frame drawn into the Framebuffer before the HUD --
    // which is now an overlay pixel with partial alpha over the world, and
    // the composite honours it. The death ceremony's veil and epitaph draw
    // into the same target (Session::composeDeathCeremony, last in
    // drawFrame), so they ride the same pass.
    render::Session session(sessionConfig(20));
    sim::MoveInput forward;
    forward.forward = 1;
    session.step(forward);
    session.stepMany(sim::MoveInput{}, 20);
    const render::Framebuffer calm = overlayOf(session);
    session.tavern().injurePlayer(4);
    session.step(sim::MoveInput{});
    CHECK(session.viewmodel().state == ViewmodelState::Hit);
    const render::Framebuffer washed = overlayOf(session);
    // An open patch of the frame: the first pixel in the middle rows that
    // the calm overlay left clear, which the wash must now cover partway.
    int px = -1;
    int py = -1;
    for (int y = kHeight / 3; y < kHeight * 2 / 3 && px < 0; ++y) {
        for (int x = 8; x < kWidth / 3; ++x) {
            if (channel(pixelAt(calm, x, y), 24) == 0) {
                px = x;
                py = y;
                break;
            }
        }
    }
    REQUIRE(px >= 0);
    const std::uint32_t wash = pixelAt(washed, px, py);
    const int washAlpha = channel(wash, 24);
    CHECK(washAlpha > 0);
    CHECK(washAlpha < 255);
    // ...and it is the blooded wash's own colour (0.58, 0.10, 0.08).
    CHECK(std::abs(channel(wash, 0) - 148) <= 2);
    CHECK(std::abs(channel(wash, 8) - 26) <= 2);
    CHECK(std::abs(channel(wash, 16) - 20) <= 2);

    Stack stack(session);
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    const render::Framebuffer world = drawOnce(*video, stack.scene, nullptr, nullptr);
    const render::Framebuffer composite = drawOnce(*video, stack.scene, &washed, nullptr);
    const CompositeReport report = classify(composite, world, washed);
    MESSAGE("washed overlay: " << report.partial << " partial pixels, worst partial miss "
                               << report.worstPartial);
    CHECK(report.partial > static_cast<std::size_t>(kWidth * kHeight) / 2);
    CHECK(report.worstOpaque == 0);
    CHECK(report.worstPartial <= 2);
    // And the composite moved under the wash at the open patch.
    CHECK(pixelAt(composite, px, py) != pixelAt(world, px, py));
}

TEST_CASE("the weapon socket, the framing and the clip policy are pure per kind and state") {
    // THE SOCKET PER CLASS. Bare fists hold nothing: zero. Every weapon --
    // the club, the dagger and the sword -- hangs by one hammer grip in the
    // Hand_R bone's own frame: a quarter turn the negative way about X, so
    // the weapon's +Y (its blade, its head) leaves along the bone's -Z (the
    // thumb side of the fist) instead of down +Y (the wrist), and a slide of
    // nine centimetres down the fingers, three into the palm, ten along the
    // grip so the grip's centre lies in the curled fingers. The byte-keyed
    // twins the adapter calls agree with the typed ones, and only the sword
    // is fused into its arms glb.
    const ViewmodelSocket none = viewmodelSocket(ViewmodelKind::Fists);
    CHECK(none.offset.x == 0.0F);
    CHECK(none.offset.y == 0.0F);
    CHECK(none.offset.z == 0.0F);
    CHECK(none.rotation.x == 0.0F);
    for (const ViewmodelKind kind : {ViewmodelKind::Club, ViewmodelKind::Dagger, ViewmodelKind::Sword}) {
        const ViewmodelSocket socket = viewmodelSocket(kind);
        CHECK(socket.rotation.x == doctest::Approx(-3.14159265358979323846F * 0.5F));
        CHECK(socket.rotation.y == 0.0F);
        CHECK(socket.rotation.z == 0.0F);
        CHECK(socket.offset.x == doctest::Approx(-0.09F));
        CHECK(socket.offset.y == doctest::Approx(0.03F));
        CHECK(socket.offset.z == doctest::Approx(-0.10F));
        const ViewmodelSocket byByte = viewmodelSocketOf(static_cast<std::uint8_t>(kind));
        CHECK(byByte.offset.x == socket.offset.x);
        CHECK(byByte.rotation.x == socket.rotation.x);
    }
    CHECK(viewmodelWeaponFused(ViewmodelKind::Sword));
    CHECK_FALSE(viewmodelWeaponFused(ViewmodelKind::Club));
    CHECK_FALSE(viewmodelWeaponFused(ViewmodelKind::Fists));
    CHECK(viewmodelWeaponFusedOf(3));
    CHECK_FALSE(viewmodelWeaponFusedOf(200));
    CHECK(viewmodelSocketOf(200).offset.x == 0.0F);

    // THE FRAMING. The guard leans the rig back about the eye (a positive
    // pitch: what is in front lifts) with its feet below and a little
    // behind the eye for the fists, and further below, turned a hair left,
    // for the sword, whose guard is the blade across the chest. The block
    // pushes the fists' rig toward the eye; the cast drops and turns it so
    // the thrown off hand lands centre-frame. Each is its own place.
    const ViewmodelRigPlacement fistsGuard = viewmodelGuardPlacement(ViewmodelKind::Fists);
    CHECK(fistsGuard.pitch > 0.5F);
    CHECK(fistsGuard.offset.y < -1.0F);
    CHECK(fistsGuard.yaw == doctest::Approx(kViewmodelRigYaw));
    const ViewmodelRigPlacement fistsBlock = viewmodelBlockPlacement(ViewmodelKind::Fists);
    CHECK(fistsBlock.offset.z < fistsGuard.offset.z - 0.3F);
    const ViewmodelRigPlacement swordGuard = viewmodelGuardPlacement(ViewmodelKind::Sword);
    CHECK(swordGuard.pitch > fistsGuard.pitch);
    CHECK(swordGuard.offset.y < fistsGuard.offset.y);
    CHECK(swordGuard.yaw < kViewmodelRigYaw);
    const ViewmodelRigPlacement cast = viewmodelCastPlacement(ViewmodelKind::Fists);
    CHECK(cast.yaw < kViewmodelRigYaw);
    CHECK(cast.offset.y < -1.5F);
    CHECK(viewmodelGuardPlacement(ViewmodelKind::Club).offset.y ==
          viewmodelGuardPlacement(ViewmodelKind::Sword).offset.y);

    // THE CLIP POLICY, state by state, off hand-built poses. Idle up holds
    // the block clip's guard frame; Idle down its first frame (the hips);
    // half way through a stance flip, half way between. A charge scrubs the
    // punch's cock by the real charge fraction and the hold sits at the
    // cock; the swing runs from the cock to the clip's end over its own
    // steps; the block eases from the guard frame to the hold; a cast and
    // a hit play their own clips through. Bare fists alternate the two
    // punches by swing parity; a weapon's cock and swing are the one
    // swipe-up clip.
    ViewmodelPose pose;
    ViewmodelRigClip clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 5U);
    CHECK(clip.frame == doctest::Approx(kViewmodelGuardFrame));
    pose.handsUp = false;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 5U);
    CHECK(clip.frame == doctest::Approx(0.0F));
    pose.stanceSteps = ViewmodelMachine::kStanceSteps / 2;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.frame > 0.0F);
    CHECK(clip.frame < kViewmodelGuardFrame);
    pose = ViewmodelPose{};
    clip = viewmodelRigClip(ViewmodelKind::Sword, pose);
    CHECK(clip.clip == 5U);
    CHECK(clip.frame == doctest::Approx(kViewmodelSwordGuardFrame));

    pose.state = ViewmodelState::Charging;
    pose.chargeSteps = sim::kHardSwingHoldSteps / 2;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 1U);  // the right punch: the first swing is the right
    CHECK(clip.frame == doctest::Approx(kViewmodelCockFrame * 0.5F));
    pose.swingSeq = 1;       // the second swing will be the left
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 2U);
    pose.state = ViewmodelState::ChargedHard;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.frame == doctest::Approx(kViewmodelCockFrame));
    clip = viewmodelRigClip(ViewmodelKind::Sword, pose);
    CHECK(clip.clip == 4U);
    CHECK(clip.frame == doctest::Approx(kViewmodelCockFrame));

    pose = ViewmodelPose{};
    pose.state = ViewmodelState::SwingLight;
    pose.swingSeq = 1;  // in flight: this IS the first (right) swing
    pose.stateSteps = 0;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 3U);
    CHECK(clip.frame == doctest::Approx(kViewmodelCockFrame));
    pose.stateSteps = ViewmodelMachine::kSwingSteps;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.frame == doctest::Approx(1.0F));
    pose.swingSeq = 2;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 4U);
    pose.state = ViewmodelState::SwingHard;
    clip = viewmodelRigClip(ViewmodelKind::Sword, pose);
    CHECK(clip.clip == 4U);
    CHECK(clip.frame == doctest::Approx(1.0F));

    pose = ViewmodelPose{};
    pose.state = ViewmodelState::Block;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 5U);
    CHECK(clip.frame == doctest::Approx(kViewmodelGuardFrame));
    pose.stateSteps = kViewmodelEaseSteps;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.frame == doctest::Approx(kViewmodelBlockHoldFrame));
    clip = viewmodelRigClip(ViewmodelKind::Sword, pose);
    CHECK(clip.frame == doctest::Approx(kViewmodelSwordBlockHoldFrame));

    pose = ViewmodelPose{};
    pose.state = ViewmodelState::Cast;
    pose.stateSteps = ViewmodelMachine::kCastSteps / 2;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 6U);
    CHECK(clip.frame == doctest::Approx(0.5F));
    pose.state = ViewmodelState::Hit;
    pose.stateSteps = ViewmodelMachine::kHitSteps;
    clip = viewmodelRigClip(ViewmodelKind::Fists, pose);
    CHECK(clip.clip == 7U);
    CHECK(clip.frame == doctest::Approx(1.0F));

    // THE CAST RAISES THE STANCE: posed with the hands DOWN, a cast at its
    // middle still sits at the cast framing, not at the guard, and a cast
    // at its end has come back to the guard so Idle lands without a jump.
    ViewmodelPose castDown;
    castDown.handsUp = false;
    castDown.state = ViewmodelState::Cast;
    castDown.stateSteps = ViewmodelMachine::kCastSteps / 2;
    ViewmodelInstance mid;
    poseViewmodel(mid, ViewmodelKind::Fists, castDown, Rgba8{});
    CHECK(mid.rigOffset.y == doctest::Approx(cast.offset.y));
    CHECK(mid.rigYaw == doctest::Approx(cast.yaw));
    CHECK(mid.rigPitch == doctest::Approx(cast.pitch));
    CHECK(mid.rigClip == 6U);
    castDown.stateSteps = ViewmodelMachine::kCastSteps;
    ViewmodelInstance end;
    poseViewmodel(end, ViewmodelKind::Fists, castDown, Rgba8{});
    CHECK(end.rigOffset.y == doctest::Approx(fistsGuard.offset.y));
    CHECK(end.rigPitch == doctest::Approx(fistsGuard.pitch));

    // The block's push eases in over its steps and the socket rides every
    // pose of an armed kind.
    ViewmodelPose blocking;
    blocking.state = ViewmodelState::Block;
    blocking.stateSteps = kViewmodelEaseSteps;
    ViewmodelInstance held;
    poseViewmodel(held, ViewmodelKind::Sword, blocking, Rgba8{});
    CHECK(held.rigOffset.y == doctest::Approx(viewmodelBlockPlacement(ViewmodelKind::Sword).offset.y));
    CHECK(held.socketRotation.x == doctest::Approx(-3.14159265358979323846F * 0.5F));
    CHECK(held.socketOffset.x == doctest::Approx(-0.09F));
    ViewmodelInstance bare;
    poseViewmodel(bare, ViewmodelKind::Fists, blocking, Rgba8{});
    CHECK(bare.socketRotation.x == 0.0F);
    CHECK(bare.rigOffset.z == doctest::Approx(fistsBlock.offset.z));

    // And the hash reads all of it: a pitch, a clip, a frame or a socket
    // moved is a different digest.
    SceneDescription scene;
    scene.viewmodel = held;
    const std::uint64_t base = sceneHash(scene);
    scene.viewmodel.rigPitch += 0.01F;
    CHECK(sceneHash(scene) != base);
    scene.viewmodel = held;
    scene.viewmodel.rigClip = 6U;
    CHECK(sceneHash(scene) != base);
    scene.viewmodel = held;
    scene.viewmodel.rigFrame += 0.01F;
    CHECK(sceneHash(scene) != base);
    scene.viewmodel = held;
    scene.viewmodel.socketOffset.z += 0.01F;
    CHECK(sceneHash(scene) != base);
    scene.viewmodel = held;
    CHECK(sceneHash(scene) == base);
}
