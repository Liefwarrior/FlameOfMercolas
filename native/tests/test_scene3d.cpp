// THE SCENE DESCRIPTION, AS THE THING THAT GETS HASHED.
//
// The 3D renderer's determinism claim does not live in pixels -- a GPU frame
// is never hashed -- it lives here: the SceneDescription is a pure function
// of session state, its byte image is what sceneHash() digests, and two
// sessions driven by the same script must produce the same digest. These
// cases pin the digest's behaviour (identical in, identical out; any byte
// moved, digest moved), the frame convention every lane builds against
// (north is -Z, east is +X, up is +Y), the id spaces that keep the lanes'
// meshes apart, and the viewmodel machine's integer clock. No raylib, no
// window, no licensed asset: the starter scene is procedural.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>

#include "granadad/render/session.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/actor_instances.hpp"
#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/starter_scene.hpp"
#include "granadad/render3d/viewmodel.hpp"
#include "granadad/sim/human_scale.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;
namespace sim = granadad::sim;

namespace {

StarterSceneParams noonParams() {
    StarterSceneParams params;
    params.groundY = 0.0F;
    params.centre = Vec3{0.0F, 0.0F, 0.0F};
    params.cube = Vec3{0.0F, 0.0F, -4.0F};
    params.halfExtent = 16.0F;
    params.timeOfDaySeconds = 12 * 3600;
    return params;
}

render::Camera eyeAt(float x, float y, float z, float yaw) {
    render::Camera camera;
    camera.x = x;
    camera.y = y;
    camera.z = z;
    camera.yaw = yaw;
    camera.pitch = 0.0F;
    camera.hfovTan = 1.0F;
    return camera;
}

SceneDescription starterAt(float yaw) {
    SceneDescription scene;
    buildStarterScene(scene, noonParams());
    scene.camera = cameraFrom(eyeAt(0.0F, 0.0F, 1.7F, yaw), 320.0F / 180.0F);
    return scene;
}

}  // namespace

TEST_CASE("a SceneDescription hashes identically for identical state") {
    const SceneDescription a = starterAt(0.0F);
    const SceneDescription b = starterAt(0.0F);
    REQUIRE(a.meshes.size() == 2);
    REQUIRE(a.instances.size() == 2);
    CHECK(sceneHash(a) == sceneHash(b));
    // And it is a digest of the BYTES, not of the object: a copy hashes the
    // same, and the number is stable across calls.
    const SceneDescription c = a;
    CHECK(sceneHash(c) == sceneHash(a));
    CHECK(sceneHash(a) == sceneHash(a));
}

TEST_CASE("any byte of the description moved is a different hash") {
    const SceneDescription base = starterAt(0.0F);
    const std::uint64_t reference = sceneHash(base);

    SUBCASE("the camera turned") {
        const SceneDescription turned = starterAt(0.01F);
        CHECK(sceneHash(turned) != reference);
    }
    SUBCASE("an instance moved by one sub-tile") {
        SceneDescription moved = base;
        moved.instances[1].position.x += 1.0F / 256.0F;
        CHECK(sceneHash(moved) != reference);
    }
    SUBCASE("one vertex colour off by one") {
        SceneDescription tinted = base;
        REQUIRE(!tinted.meshes[1].colours.empty());
        tinted.meshes[1].colours[0] = static_cast<std::uint8_t>(tinted.meshes[1].colours[0] ^ 1U);
        CHECK(sceneHash(tinted) != reference);
    }
    SUBCASE("a mesh version bumped with the same bytes") {
        SceneDescription bumped = base;
        bumped.meshes[0].version += 1;
        CHECK(sceneHash(bumped) != reference);
    }
    SUBCASE("the sky changed") {
        SceneDescription dusk = base;
        dusk.clearColour.r = static_cast<std::uint8_t>(dusk.clearColour.r ^ 1U);
        CHECK(sceneHash(dusk) != reference);
    }
    SUBCASE("an instance removed") {
        SceneDescription fewer = base;
        fewer.instances.pop_back();
        CHECK(sceneHash(fewer) != reference);
    }
    SUBCASE("a body added, and then its clip changed") {
        // A LANE: the actor list is part of the digest, and so is what a
        // body is doing -- a punch and an idle are different pictures.
        SceneDescription crowd = base;
        ActorInstance body;
        body.rig = 0;
        body.instance.meshId = actorRigMeshId(0);
        body.instance.position = Vec3{1.0F, 0.0F, -3.0F};
        body.clip = ActorClip::Idle;
        body.clipFrame = 7;
        crowd.actors.push_back(body);
        const std::uint64_t withBody = sceneHash(crowd);
        CHECK(withBody != reference);
        crowd.actors[0].clip = ActorClip::PunchLeft;
        CHECK(sceneHash(crowd) != withBody);
        crowd.actors[0].clip = ActorClip::Idle;
        crowd.actors[0].clipFrame = 8;
        CHECK(sceneHash(crowd) != withBody);
        crowd.actors[0].clipFrame = 7;
        crowd.actors[0].skinned = true;
        CHECK(sceneHash(crowd) != withBody);
    }
    SUBCASE("a building piece placed, then stretched, then its file renamed") {
        // S LANE: the static pieces and the table they index are part of
        // the digest -- a wall a hair longer, or the catalogue pointing the
        // same placement at another file, is a different picture.
        SceneDescription dressed = base;
        dressed.pieces.push_back(StaticPieceRef{"PolygonGeneric/SM_Bld_Base_Wall_01.gltf"});
        StaticInstance wall;
        wall.piece = 0;
        wall.role = 1;
        wall.position = Vec3{4.0F, 0.0F, -2.0F};
        wall.scale = Vec3{0.8F, 1.0F, 1.0F};
        dressed.statics.push_back(wall);
        const std::uint64_t withWall = sceneHash(dressed);
        CHECK(withWall != reference);
        dressed.statics[0].scale.x = 0.8F + 1.0F / 1024.0F;
        CHECK(sceneHash(dressed) != withWall);
        dressed.statics[0].scale.x = 0.8F;
        CHECK(sceneHash(dressed) == withWall);
        dressed.pieces[0].file = "PolygonGeneric/SM_Bld_Base_Wall_Window_01.gltf";
        CHECK(sceneHash(dressed) != withWall);
    }
}

TEST_CASE("the same params refresh the same scene and re-upload nothing") {
    SceneDescription scene;
    buildStarterScene(scene, noonParams());
    const std::uint32_t version = scene.meshes[0].version;
    const std::uint64_t first = sceneHash(scene);
    // A second refresh with the same minute keeps the meshes' versions -- the
    // adapter's cache key -- and the bytes.
    buildStarterScene(scene, noonParams());
    CHECK(scene.meshes[0].version == version);
    CHECK(sceneHash(scene) == first);
    // A minute later the light moved: new version, new bytes.
    StarterSceneParams later = noonParams();
    later.timeOfDaySeconds += 60;
    buildStarterScene(scene, later);
    CHECK(scene.meshes[0].version != version);
    CHECK(sceneHash(scene) != first);
}

TEST_CASE("the starter scene is lit: a face towards the sun is brighter than one away") {
    SceneDescription scene;
    buildStarterScene(scene, noonParams());
    const MeshData* cube = scene.findMesh(kStarterCubeMeshId);
    REQUIRE(cube != nullptr);
    REQUIRE(cube->vertexCount() == 24);
    REQUIRE(cube->triangleCount() == 12);
    // Faces are pushed top, bottom, south, north, east, west, four vertices
    // each. The top faces the sun (it is high); the bottom faces away.
    const auto luma = [&](std::size_t face) {
        const std::size_t at = face * 4 * 4;
        return static_cast<int>(cube->colours[at]) + static_cast<int>(cube->colours[at + 1]) +
               static_cast<int>(cube->colours[at + 2]);
    };
    CHECK(luma(0) > luma(1));
    // East (face 4) is towards the sun's x; west (face 5) is away.
    CHECK(luma(4) > luma(5));
    // Every vertex of one face carries one colour: flat shading, so the
    // rasterizer cannot invent a gradient a hash would not see.
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t v = 1; v < 4; ++v) {
            for (std::size_t ch = 0; ch < 4; ++ch) {
                CHECK(cube->colours[(face * 4 + v) * 4 + ch] == cube->colours[face * 4 * 4 + ch]);
            }
        }
    }
    // Front faces are counter-clockwise: every index in range, three per triangle.
    for (const std::uint16_t index : cube->indices) {
        CHECK(index < cube->vertexCount());
    }
}

TEST_CASE("the camera adapter keeps north at -Z, east at +X and up at +Y") {
    constexpr float kPi = 3.14159265358979323846F;
    const float aspect = 640.0F / 360.0F;

    const SceneCamera north = cameraFrom(eyeAt(10.0F, 20.0F, 1.7F, 0.0F), aspect);
    // World (x east, y south, z up) -> scene (X, Y up, Z south).
    CHECK(north.position.x == doctest::Approx(10.0F));
    CHECK(north.position.y == doctest::Approx(1.7F));
    CHECK(north.position.z == doctest::Approx(20.0F));
    CHECK(north.target.x - north.position.x == doctest::Approx(0.0F).epsilon(1e-5));
    CHECK(north.target.y - north.position.y == doctest::Approx(0.0F).epsilon(1e-5));
    CHECK(north.target.z - north.position.z == doctest::Approx(-1.0F));

    const SceneCamera east = cameraFrom(eyeAt(0.0F, 0.0F, 0.0F, kPi * 0.5F), aspect);
    CHECK(east.target.x == doctest::Approx(1.0F));
    CHECK(std::fabs(east.target.z) < 1e-5F);

    // Pitch up tilts the target up.
    render::Camera tilted = eyeAt(0.0F, 0.0F, 0.0F, 0.0F);
    tilted.pitch = 0.5F;
    const SceneCamera up = cameraFrom(tilted, aspect);
    CHECK(up.target.y > 0.0F);
    CHECK(up.up.y == doctest::Approx(1.0F));

    // A 90-degree horizontal field (hfovTan 1) at 16:9 is the vertical field
    // raylib is handed: 2 * atan(9/16) = 58.7 degrees.
    CHECK(north.fovyDegrees == doctest::Approx(58.716F).epsilon(0.01));
}

TEST_CASE("mesh ids never collide across the lanes' ranges") {
    std::set<std::uint32_t> ids;
    for (std::int32_t cy = 0; cy < kChunksDown; ++cy) {
        for (std::int32_t cx = 0; cx < kChunksAcross; ++cx) {
            const std::uint32_t id = chunkMeshId(ChunkKey{cx, cy});
            CHECK(id >= kChunkMeshIdBase);
            CHECK(id < kActorMeshIdBase);
            CHECK(ids.insert(id).second);
        }
    }
    CHECK(ids.size() == static_cast<std::size_t>(kChunksDown * kChunksAcross));
    CHECK(kStarterCubeMeshId < kChunkMeshIdBase);
    CHECK(kActorMeshIdBase < kViewmodelMeshIdBase);
    // And a tile maps into its chunk.
    CHECK(chunkOf(0, 0).cx == 0);
    CHECK(chunkOf(15, 15).cy == 0);
    CHECK(chunkOf(16, 31).cx == 1);
    CHECK(chunkOf(16, 31).cy == 1);
    CHECK(chunkOf(255, 191).cx == kChunksAcross - 1);
    CHECK(chunkOf(255, 191).cy == kChunksDown - 1);
}

TEST_CASE("a chunk's version moves on a rebuild and on the light, and on nothing else") {
    ChunkLighting noon;
    noon.timeOfDaySeconds = 12 * 3600;
    ChunkLighting sameMinute;
    sameMinute.timeOfDaySeconds = 12 * 3600 + 30;
    ChunkLighting nextMinute;
    nextMinute.timeOfDaySeconds = 12 * 3600 + 60;
    CHECK(chunkVersion(0, noon) == chunkVersion(0, sameMinute));
    CHECK(chunkVersion(0, noon) != chunkVersion(0, nextMinute));
    CHECK(chunkVersion(0, noon) != chunkVersion(1, noon));
    CHECK(chunkVersion(0, noon) != 0);
}

TEST_CASE("the viewmodel machine plays a swing for its own steps and falls back to the hand") {
    ViewmodelMachine arms;
    ViewmodelInputs rest;
    CHECK(arms.step(rest).state == ViewmodelState::Idle);

    ViewmodelInputs charging;
    charging.chargeSteps = 3;
    CHECK(arms.step(charging).state == ViewmodelState::Charging);
    charging.chargeSteps = 40;
    charging.chargeHard = true;
    CHECK(arms.step(charging).state == ViewmodelState::ChargedHard);

    ViewmodelInputs release;
    release.releasedHard = true;
    CHECK(arms.step(release).state == ViewmodelState::SwingHard);
    // The swing plays out on its own clock whatever the hand does now.
    ViewmodelInputs blocking;
    blocking.blocking = true;
    for (int i = 1; i < ViewmodelMachine::kSwingSteps; ++i) {
        CHECK(arms.step(blocking).state == ViewmodelState::SwingHard);
    }
    // ...then the held verb takes over.
    CHECK(arms.step(blocking).state == ViewmodelState::Block);
    CHECK(arms.step(rest).state == ViewmodelState::Idle);

    // A hit interrupts anything, including a swing in flight.
    CHECK(arms.step(release).state == ViewmodelState::SwingHard);
    ViewmodelInputs hit;
    hit.hit = true;
    CHECK(arms.step(hit).state == ViewmodelState::Hit);
    CHECK(arms.pose().stateSteps == 0);
}

// ---------------------------------------------------------------------------
// A LANE -- the people, as instances
// ---------------------------------------------------------------------------

namespace {

constexpr float kTestPi = 3.14159265358979323846F;

/// A session at eight in the morning, small frame (the frame size only
/// sizes the HUD; the people are the same).
render::SessionConfig morningConfig() {
    render::SessionConfig config;
    config.width = 160;
    config.height = 90;
    config.timeOfDay = 8 * 3600;
    config.timeOfDayGiven = true;
    return config;
}

/// The actor half of the description: rigs, bodies, camera. The world's
/// chunks are the W lane's own proof and are left out to keep this cheap.
SceneDescription crowdScene(const render::Session& session) {
    SceneDescription scene;
    putActorRigs(scene);
    scene.camera = cameraFrom(session.camera(), 16.0F / 9.0F);
    scene.actors = actorInstances(session, session.camera());
    return scene;
}

[[nodiscard]] bool nearly(float a, float b, float eps = 1e-4F) { return std::fabs(a - b) <= eps; }

}  // namespace

TEST_CASE("an actor is drawn where the simulation says the actor is") {
    // THE A LANE'S FIRST CLAIM. For every body the sim holds within the
    // instancing radius there is exactly one ActorInstance, and it stands
    // on the tile the sim put it on: the ward's slide between prevX and x
    // (wardSprites' own arithmetic, off the same counter), the Gull's Q8
    // straight out of the actor, the band through bandSurface, the BAM
    // facing as the yaw, and the rig that is its WardType. Nothing here is
    // drawn -- the description is the claim, and it is what gets hashed.
    render::Session session(morningConfig());
    const render::Camera eye = session.camera();
    const SceneDescription scene = crowdScene(session);

    // Every KIND's placeholder is in the description, in the actor id range,
    // closed and small, with its nose in FRONT (-Z) of its head. A variant
    // rig (the townswoman) has none of its own.
    for (std::uint32_t rig = 0; rig < kActorKindCount; ++rig) {
        const MeshData* mesh = scene.findMesh(actorRigMeshId(rig));
        REQUIRE(mesh != nullptr);
        CHECK(mesh->id >= kActorMeshIdBase);
        CHECK(mesh->id < kViewmodelMeshIdBase);
        CHECK(mesh->version == 1);
        CHECK(mesh->triangleCount() > 0);
        CHECK(mesh->vertexCount() < 200U);
        float minZ = 1e9F;
        float minY = 1e9F;
        for (std::size_t v = 0; v < mesh->vertexCount(); ++v) {
            minY = std::min(minY, mesh->positions[v * 3 + 1]);
            minZ = std::min(minZ, mesh->positions[v * 3 + 2]);
        }
        CHECK(minY == doctest::Approx(0.0F));  // stands on its feet
        CHECK(minZ < 0.0F);                    // and has a front
        for (const std::uint16_t index : mesh->indices) {
            CHECK(index < mesh->vertexCount());
        }
    }

    // The ward: one instance per visible body within the radius, at the
    // tile centre (stepsThisSecond is 0 at spawn, so no slide yet).
    REQUIRE(session.stepsThisSecond() == 0);
    std::size_t expected = 0;
    std::size_t checked = 0;
    for (const sim::WardActor& actor : session.people().actors()) {
        if (!actor.visible()) {
            continue;
        }
        const float px = static_cast<float>(actor.x) + 0.5F;
        const float py = static_cast<float>(actor.y) + 0.5F;
        const float dx = px - eye.x;
        const float dy = py - eye.y;
        if (std::sqrt(dx * dx + dy * dy) > 64.0F) {
            continue;
        }
        ++expected;
        const Vec3 where = toScene(px, py, render::bandSurface(actor.band));
        bool found = false;
        for (const ActorInstance& body : scene.actors) {
            if (nearly(body.instance.position.x, where.x) &&
                nearly(body.instance.position.y, where.y) &&
                nearly(body.instance.position.z, where.z) &&
                body.instance.meshId == actorRigMeshId(actorRigOf(actor.type))) {
                found = true;
                CHECK(body.rig == actorRigFor(actor.type, actor.id,
                                              session.people().identity(actor.id).name));
                CHECK(body.instance.yaw ==
                      doctest::Approx(static_cast<float>(actor.facing) * (2.0F * kTestPi / 65536.0F)));
                CHECK(body.clip == wardClip(actor.x != actor.prevX || actor.y != actor.prevY));
                CHECK(body.clipFrame == actorClipFrame(session.body().stepCount(), actor.id));
                CHECK(body.instance.tint.a == 255);
                if (sim::isPerson(actor.type)) {
                    CHECK(body.instance.scale > 0.5F);
                    CHECK(body.instance.scale < 1.2F);
                } else {
                    CHECK(body.instance.scale == 1.0F);
                }
                ++checked;
                break;
            }
        }
        CHECK(found);
    }
    // The Gull's present bodies within the radius, at their Q8 position.
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (!actor.present()) {
            continue;
        }
        const float px = static_cast<float>(actor.x()) / 256.0F;
        const float py = static_cast<float>(actor.y()) / 256.0F;
        const float dx = px - eye.x;
        const float dy = py - eye.y;
        if (std::sqrt(dx * dx + dy * dy) > 64.0F) {
            continue;
        }
        ++expected;
        const Vec3 where = toScene(px, py, render::bandSurface(actor.band()));
        bool found = false;
        for (const ActorInstance& body : scene.actors) {
            if (nearly(body.instance.position.x, where.x) &&
                nearly(body.instance.position.y, where.y) &&
                nearly(body.instance.position.z, where.z)) {
                found = true;
                CHECK(body.rig == actorRigFor(render::figureForRole(actor.role(), actor.id()),
                                              actor.id(), actor.name()));
                CHECK(body.clip == clipForActivity(actor.activity(), actor.npcSwingSeq()));
                break;
            }
        }
        CHECK(found);
    }
    CHECK(scene.actors.size() == expected);
    CHECK(expected > 0);
    CHECK(checked > 0);
    MESSAGE("crowd at 08:00 from the spawn: " << scene.actors.size() << " bodies within 64 tiles");

    // Now half a second into the next second: a body that stepped this
    // tick is drawn HALF WAY between its two tiles -- the slide, not the
    // sim -- and plays the walk. Run one second (so the ward ticks and
    // some prevX != x) plus thirty steps.
    sim::MoveInput hold;
    session.stepMany(hold, sim::kStepsPerSecond + sim::kStepsPerSecond / 2);
    REQUIRE(session.stepsThisSecond() == sim::kStepsPerSecond / 2);
    const SceneDescription later = crowdScene(session);
    const render::Camera eyeLater = session.camera();
    std::size_t walkers = 0;
    for (const sim::WardActor& actor : session.people().actors()) {
        if (!actor.visible() || (actor.x == actor.prevX && actor.y == actor.prevY)) {
            continue;
        }
        const float px = static_cast<float>(actor.prevX) +
                         static_cast<float>(actor.x - actor.prevX) * 0.5F + 0.5F;
        const float py = static_cast<float>(actor.prevY) +
                         static_cast<float>(actor.y - actor.prevY) * 0.5F + 0.5F;
        const float dx = px - eyeLater.x;
        const float dy = py - eyeLater.y;
        if (std::sqrt(dx * dx + dy * dy) > 64.0F) {
            continue;
        }
        const Vec3 where = toScene(px, py, render::bandSurface(actor.band));
        bool found = false;
        for (const ActorInstance& body : later.actors) {
            if (nearly(body.instance.position.x, where.x) &&
                nearly(body.instance.position.z, where.z) &&
                body.instance.meshId == actorRigMeshId(actorRigOf(actor.type))) {
                found = true;
                CHECK(body.clip == ActorClip::Walk);
                break;
            }
        }
        CHECK(found);
        ++walkers;
    }
    MESSAGE("walkers mid-stride within 64 tiles: " << walkers);
    // And the same session described twice is the same bytes.
    CHECK(sceneHash(later) == sceneHash(crowdScene(session)));
}

TEST_CASE("the scene description twin-runs identical") {
    // THE A LANE'S DETERMINISM CLAIM, in the 3D renderer's own currency: two
    // sessions built from one config and driven by one script describe the
    // same crowd -- every position, yaw, clip and frame -- byte for byte, so
    // the digest is equal. And a third session driven differently does not,
    // which is what makes the equality mean something.
    const auto drive = [](render::Session& session) {
        sim::MoveInput forward;
        forward.forward = 1;
        session.stepMany(forward, 40);
        sim::MoveInput turn;
        turn.forward = 1;
        turn.turn = 1;
        session.stepMany(turn, 20);
        session.stepMany(forward, 25);
    };
    render::Session a(morningConfig());
    render::Session b(morningConfig());
    drive(a);
    drive(b);
    const SceneDescription sceneA = crowdScene(a);
    const SceneDescription sceneB = crowdScene(b);
    REQUIRE(sceneA.actors.size() == sceneB.actors.size());
    CHECK(sceneA.actors.size() > 0);
    CHECK(sceneHash(sceneA) == sceneHash(sceneB));
    CHECK(a.stepsThisSecond() == b.stepsThisSecond());
    CHECK(a.body().stepCount() == b.body().stepCount());

    render::Session c(morningConfig());
    sim::MoveInput forward;
    forward.forward = 1;
    c.stepMany(forward, 85);
    CHECK(sceneHash(crowdScene(c)) != sceneHash(sceneA));
}

TEST_CASE("the clip table follows the activity and the rigs name the asset lane's files") {
    // Walking walks; a brawl alternates hands on the swing sequence; a man
    // on the floor recovers and a dead one dies; everything done standing
    // still idles -- and nothing the sim has today reaches Block or Hit.
    CHECK(clipForActivity(sim::Activity::Walking, 0) == ActorClip::Walk);
    CHECK(clipForActivity(sim::Activity::Brawling, 0) == ActorClip::PunchRight);
    CHECK(clipForActivity(sim::Activity::Brawling, 1) == ActorClip::PunchLeft);
    CHECK(clipForActivity(sim::Activity::Ejecting, 2) == ActorClip::PunchRight);
    CHECK(clipForActivity(sim::Activity::Downed, 0) == ActorClip::Recover);
    CHECK(clipForActivity(sim::Activity::Dead, 0) == ActorClip::Death);
    CHECK(clipForActivity(sim::Activity::Working, 0) == ActorClip::Idle);
    CHECK(clipForActivity(sim::Activity::Drinking, 0) == ActorClip::Idle);
    CHECK(clipForActivity(sim::Activity::Watching, 0) == ActorClip::Idle);
    CHECK(clipForActivity(sim::Activity::Warning, 0) == ActorClip::Idle);
    CHECK(wardClip(true) == ActorClip::Walk);
    CHECK(wardClip(false) == ActorClip::Idle);
    CHECK(actorClipOneShot(ActorClip::Death));
    CHECK(actorClipOneShot(ActorClip::Recover));
    CHECK(!actorClipOneShot(ActorClip::Walk));

    // The clip index IS the glb animation index: the names, in order, are
    // what the asset lane's job file writes at 0..7.
    const char* const names[] = {"idle", "walk", "punch_l", "punch_r",
                                 "block", "hit", "recover", "death"};
    for (std::size_t i = 0; i < kActorClipCount; ++i) {
        CHECK(std::string(actorClipName(static_cast<ActorClip>(i))) == names[i]);
    }

    // The humanoid files, by kind; beasts have none and keep the box.
    CHECK(actorRigFile(actorRigOf(sim::WardType::MilitiaWatch)) == "watchman.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::Sailor)) == "dockhand.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::Fisher)) == "dockhand.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::Serf)) == "townsman.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::Urchin)) == "townsman.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::PriestOfTheFlame)) == "townsman.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::DiscipleOfTheFlame)) == "townsman.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::Wastrel)) == "wastrel.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::Thief)) == "wastrel.glb");
    CHECK(actorRigFile(kActorRigTownswoman) == "townswoman.glb");
    CHECK(actorRigFile(actorRigOf(sim::WardType::Dog)).empty());
    CHECK(actorRigFile(actorRigOf(sim::WardType::Mouse)).empty());
    CHECK(actorRigFile(static_cast<std::uint8_t>(kActorRigCount)).empty());
    CHECK(actorRigFile(200).empty());
    // A frame moves with the step and is phase-shifted per body.
    CHECK(actorClipFrame(10, 0) == 10);
    CHECK(actorClipFrame(10, 1) != actorClipFrame(10, 2));
    CHECK(actorClipFrame(11, 3) == actorClipFrame(10, 3) + 1);
}

TEST_CASE("the rig split is a pure function of kind and id, about half, and never by parity") {
    // THE SPLIT RULE. The sim carries no sex, so a working kind's body wears
    // the townswoman when its id's look draw has its low bit set -- a mixed
    // hash under a named salt, so the halves are near halves over any run
    // of ids and are NOT the id's own parity (figureForRole spends that bit
    // on the Gull's patrons: the innkeeper's roster would otherwise be all
    // of one sex). Every other kind wears its own rig whatever the id.
    const sim::WardType splitting[] = {sim::WardType::Serf, sim::WardType::Shopkeeper,
                                       sim::WardType::AnimalKeeper, sim::WardType::Fisher,
                                       sim::WardType::Carter, sim::WardType::Urchin};
    for (const sim::WardType kind : splitting) {
        CHECK(actorKindSplits(kind));
        int women = 0;
        int womenOdd = 0;
        int womenEven = 0;
        for (std::int32_t id = 0; id < 2000; ++id) {
            const std::uint8_t rig = actorRigFor(kind, id);
            CHECK((rig == kActorRigTownswoman || rig == actorRigOf(kind)));
            // Pure: the same id again is the same rig.
            CHECK(actorRigFor(kind, id) == rig);
            if (rig == kActorRigTownswoman) {
                ++women;
                ((id & 1) != 0 ? womenOdd : womenEven) += 1;
            }
        }
        // Roughly half, and independent of parity.
        CHECK(women > 800);
        CHECK(women < 1200);
        CHECK(womenOdd > 300);
        CHECK(womenEven > 300);
    }
    const sim::WardType fixed[] = {sim::WardType::Sailor,  sim::WardType::MilitiaWatch,
                                   sim::WardType::Wastrel, sim::WardType::Thief,
                                   sim::WardType::PriestOfTheFlame,
                                   sim::WardType::DiscipleOfTheFlame, sim::WardType::Dog,
                                   sim::WardType::Cat};
    for (const sim::WardType kind : fixed) {
        CHECK_FALSE(actorKindSplits(kind));
        for (std::int32_t id = 0; id < 500; ++id) {
            CHECK(actorRigFor(kind, id) == actorRigOf(kind));
        }
    }
    // The townswoman's id lies above every kind, so it never collides with
    // a kind's own rig, and it names the export's file.
    CHECK(kActorRigTownswoman == static_cast<std::uint8_t>(sim::kWardTypeCount));
    CHECK(kActorRigCount == kActorKindCount + kActorRigVariantCount);

    // THE URCHIN'S SCALE: a child's height against the reference body --
    // the sprite figure table's own 1.30 over 1.875 (about 0.69), and the
    // same number whichever of the two rigs the id draws. The Watch stands a
    // shade over one; a grown serf is exactly one; a beast scales one.
    CHECK(actorInstanceScale(sim::WardType::Urchin) == doctest::Approx(1.30F / 1.875F));
    CHECK(actorInstanceScale(sim::WardType::Urchin) < 0.75F);
    CHECK(actorInstanceScale(sim::WardType::Urchin) > 0.65F);
    CHECK(actorInstanceScale(sim::WardType::Serf) == doctest::Approx(1.0F));
    CHECK(actorInstanceScale(sim::WardType::MilitiaWatch) > 1.0F);
    CHECK(actorInstanceScale(sim::WardType::Dog) == doctest::Approx(1.0F));

    // AND IN A SCENE: an urchin wearing the townswoman carries the urchin's
    // placeholder and the urchin's scale -- the rig byte alone moves.
    render::Session session(morningConfig());
    const SceneDescription scene = crowdScene(session);
    int urchins = 0;
    int urchinWomen = 0;
    int women = 0;
    for (const ActorInstance& body : scene.actors) {
        if (body.rig == kActorRigTownswoman) {
            ++women;
            CHECK(body.instance.meshId != actorRigMeshId(kActorRigTownswoman));
        }
        if (body.instance.meshId == actorRigMeshId(actorRigOf(sim::WardType::Urchin))) {
            ++urchins;
            CHECK(body.instance.scale == doctest::Approx(1.30F / 1.875F));
            if (body.rig == kActorRigTownswoman) {
                ++urchinWomen;
            }
        }
    }
    CHECK(women > 0);
    MESSAGE("crowd from the spawn: " << women << " townswoman bodies of " << scene.actors.size()
                                     << ", " << urchinWomen << " of " << urchins << " urchins");
}

TEST_CASE("a named body follows its name, and the id draw only where the name says nothing") {
    // THE NAME'S SAY. The roster's and the pools' names read as they read:
    // a woman's name puts a splitting kind in the townswoman whatever the
    // id draws, a man's keeps the kind's rig, a title says it for a
    // notable, and a name on neither list leaves it to the id.
    CHECK(actorNameSays("Gerta Saltcotte") == 1);
    CHECK(actorNameSays("Tarn Wrenhale") == -1);
    CHECK(actorNameSays("Sella Brinewall") == 1);
    CHECK(actorNameSays("Watchman Cull") == -1);
    CHECK(actorNameSays("Master Venn") == -1);
    CHECK(actorNameSays("Widow Annis Netter") == 1);
    CHECK(actorNameSays("Mother Sethra") == 1);
    CHECK(actorNameSays("Gullet Mag") == 1);
    CHECK(actorNameSays("Captain Ivo Wake") == -1);
    CHECK(actorNameSays("Petra Barnacre") == 1);
    CHECK(actorNameSays("Fodder") == 0);
    CHECK(actorNameSays("Sniv") == 0);
    CHECK(actorNameSays("") == 0);
    // Whatever the id draws, the name wins for a splitting kind; a kind that
    // never splits ignores it (a bouncer called Gerta is still the Watch).
    for (std::int32_t id = 0; id < 64; ++id) {
        CHECK(actorRigFor(sim::WardType::Serf, id, "Tarn Wrenhale") == actorRigOf(sim::WardType::Serf));
        CHECK(actorRigFor(sim::WardType::Shopkeeper, id, "Gerta Saltcotte") == kActorRigTownswoman);
        CHECK(actorRigFor(sim::WardType::Fisher, id, "Sella Brinewall") == kActorRigTownswoman);
        CHECK(actorRigFor(sim::WardType::MilitiaWatch, id, "Gerta") == actorRigOf(sim::WardType::MilitiaWatch));
        CHECK(actorRigFor(sim::WardType::Serf, id, "Fodder") == actorRigFor(sim::WardType::Serf, id));
        CHECK(actorRigFor(sim::WardType::AnimalKeeper, id, "Drover") ==
              actorRigFor(sim::WardType::AnimalKeeper, id));
    }
    // The Gull's roster, in a session: every named patron and keeper wears
    // the body their name says.
    render::Session session(morningConfig());
    const SceneDescription scene = crowdScene(session);
    int checked = 0;
    for (const sim::Actor& actor : session.tavern().actors()) {
        const int says = actorNameSays(actor.name());
        if (says == 0) {
            continue;
        }
        const sim::WardType figure = render::figureForRole(actor.role(), actor.id());
        if (!actorKindSplits(figure)) {
            continue;
        }
        const std::uint8_t rig = actorRigFor(figure, actor.id(), actor.name());
        CHECK(rig == (says > 0 ? kActorRigTownswoman : actorRigOf(figure)));
        ++checked;
    }
    CHECK(checked >= 8);
    // And the ward: a body whose name the pools call a woman's wears the
    // townswoman; the crowd still holds both.
    int women = 0;
    int men = 0;
    for (const ActorInstance& body : scene.actors) {
        if (body.rig == kActorRigTownswoman) {
            ++women;
        } else if (body.instance.meshId == actorRigMeshId(actorRigOf(sim::WardType::Serf))) {
            ++men;
        }
    }
    CHECK(women > 0);
    CHECK(men > 0);
    for (const sim::WardActor& actor : session.people().actors()) {
        if (!actor.visible() || !actorKindSplits(actor.type)) {
            continue;
        }
        const int says = actorNameSays(session.people().identity(actor.id).name);
        if (says > 0) {
            CHECK(actorRigFor(actor.type, actor.id, session.people().identity(actor.id).name) ==
                  kActorRigTownswoman);
        } else if (says < 0) {
            CHECK(actorRigFor(actor.type, actor.id, session.people().identity(actor.id).name) ==
                  actorRigOf(actor.type));
        }
    }
    MESSAGE("crowd from the spawn, by name and draw: " << women << " women, " << men << " serf men");
}
