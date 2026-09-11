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

#include <cmath>
#include <cstdint>
#include <set>

#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/chunk_mesher.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/starter_scene.hpp"
#include "granadad/render3d/viewmodel.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;

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
