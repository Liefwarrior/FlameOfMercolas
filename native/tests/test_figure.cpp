// PHASE 6 -- THE PEOPLE (include/granadad/render3d/figure.hpp).
//
// A person is three pure functions: who they are (actorLookFor: a function of
// the body's kind, id and sex), what they look like at rest (buildFigure: one
// rigged mesh, a function of the look), and how they stand now
// (evaluateFigurePose: a function of the clip and the integer frame the sim
// derived). These cases pin each of them with no renderer, and pin the claim
// the plan makes of the animation: it is a pure function of sim integers, so
// two sessions driven the same way describe the same crowd to the byte.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

#include "granadad/render/session.hpp"
#include "granadad/render3d/actor_instances.hpp"
#include "granadad/render3d/figure.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/sim/ward_actors.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;
namespace sim = granadad::sim;

namespace {

constexpr sim::WardType kPeople[] = {
    sim::WardType::Serf,         sim::WardType::Shopkeeper,   sim::WardType::Sailor,
    sim::WardType::Fisher,       sim::WardType::Carter,       sim::WardType::MilitiaWatch,
    sim::WardType::Wastrel,      sim::WardType::Urchin,       sim::WardType::Thief,
    sim::WardType::PriestOfTheFlame, sim::WardType::DiscipleOfTheFlame, sim::WardType::AnimalKeeper,
};

std::array<Vec3, kFigureBoneCount + 1> restOf(const ActorLook& look) { return figureRestJoints(look); }

float signedVolume(const MeshData& mesh) {
    double volume = 0.0;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const std::size_t a = mesh.indices[t] * 3U;
        const std::size_t b = mesh.indices[t + 1] * 3U;
        const std::size_t c = mesh.indices[t + 2] * 3U;
        const double ax = mesh.positions[a], ay = mesh.positions[a + 1], az = mesh.positions[a + 2];
        const double bx = mesh.positions[b], by = mesh.positions[b + 1], bz = mesh.positions[b + 2];
        const double cx = mesh.positions[c], cy = mesh.positions[c + 1], cz = mesh.positions[c + 2];
        volume += ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx);
    }
    return static_cast<float>(volume / 6.0);
}

struct Bounds {
    float minX = 1e9F, minY = 1e9F, minZ = 1e9F, maxX = -1e9F, maxY = -1e9F, maxZ = -1e9F;
};

Bounds boundsOf(const std::vector<float>& positions) {
    Bounds b;
    for (std::size_t v = 0; v + 2 < positions.size(); v += 3) {
        b.minX = std::min(b.minX, positions[v]);
        b.maxX = std::max(b.maxX, positions[v]);
        b.minY = std::min(b.minY, positions[v + 1]);
        b.maxY = std::max(b.maxY, positions[v + 1]);
        b.minZ = std::min(b.minZ, positions[v + 2]);
        b.maxZ = std::max(b.maxZ, positions[v + 2]);
    }
    return b;
}

std::vector<float> posedPositions(const MeshData& mesh, ActorClip clip, std::uint32_t frame) {
    const auto rest = restOf(ActorLook::fromKey(mesh.id - kFigureMeshIdBase));
    const FigurePose pose = evaluateFigurePose(rest.data(), clip, frame);
    std::vector<float> positions;
    std::vector<float> normals;
    skinFigure(mesh, pose, positions, normals);
    return positions;
}

constexpr ActorClip kClips[] = {ActorClip::Idle,  ActorClip::Walk,    ActorClip::PunchLeft, ActorClip::PunchRight,
                                ActorClip::Block, ActorClip::Hit,     ActorClip::Recover,   ActorClip::Death,
                                ActorClip::Talk};

}  // namespace

TEST_CASE("a look is a pure function of the kind, the id and the sex, and packs losslessly") {
    for (const sim::WardType type : kPeople) {
        for (std::int32_t id = 0; id < 60; ++id) {
            for (const bool female : {false, true}) {
                const ActorLook a = actorLookFor(type, id, female);
                const ActorLook b = actorLookFor(type, id, female);
                CHECK(a.key() == b.key());
                CHECK(ActorLook::fromKey(a.key()).key() == a.key());
                CHECK(a.key() < kFigureFarOffset);
                CHECK(a.sex == (female ? 1 : 0));
                CHECK(a.type == static_cast<std::uint8_t>(type));
                // The mesh ids of a look and of its far twin never meet any other lane's.
                CHECK(figureMeshId(a) >= kFigureMeshIdBase);
                CHECK(figureFarMeshId(a) == figureMeshId(a) + kFigureFarOffset);
            }
        }
    }
    // The height jitter is the id's and nothing else, inside its band.
    for (std::int32_t id = -5; id < 400; ++id) {
        const float h = actorHeightJitter(id);
        CHECK(h >= 0.95F);
        CHECK(h <= 1.05F + 1e-5F);
        CHECK(h == actorHeightJitter(id));
    }
}

TEST_CASE("appearance varies per person, and a kind is still its kind at a glance") {
    // A street of serfs is not one person: builds, skins, hair and garments all vary.
    std::set<std::uint32_t> keys;
    std::set<int> builds, skins, hairs, coats, styles;
    for (std::int32_t id = 0; id < 400; ++id) {
        const ActorLook look = actorLookFor(sim::WardType::Serf, id, (id % 3) == 0);
        keys.insert(look.key());
        builds.insert(look.build);
        skins.insert(look.skin);
        hairs.insert(look.hairColour);
        coats.insert(look.coatColour);
        styles.insert(look.hairStyle);
    }
    CHECK(keys.size() > 150U);
    CHECK(builds.size() == 4U);
    CHECK(skins.size() >= 6U);
    CHECK(hairs.size() >= 6U);
    CHECK(coats.size() == 8U);
    CHECK(styles.size() >= 3U);

    for (std::int32_t id = 0; id < 100; ++id) {
        // The Watch is always helmeted, in a coat, in blue; the Priest in a robe, bald or grey.
        const ActorLook watch = actorLookFor(sim::WardType::MilitiaWatch, id, false);
        CHECK(watch.headGear == static_cast<std::uint8_t>(HeadGear::Helmet));
        CHECK(watch.coatStyle == static_cast<std::uint8_t>(CoatStyle::Coat));
        const ActorLook priest = actorLookFor(sim::WardType::PriestOfTheFlame, id, false);
        CHECK(priest.coatStyle == static_cast<std::uint8_t>(CoatStyle::Robe));
        CHECK(priest.hairStyle == 0);
        const ActorLook child = actorLookFor(sim::WardType::Urchin, id, id % 2 == 0);
        CHECK(child.build == 0);
    }
    // A woman and a man of the same id are drawn differently.
    for (const sim::WardType type : kPeople) {
        CHECK(actorLookFor(type, 7, true).key() != actorLookFor(type, 7, false).key());
    }
}

TEST_CASE("a figure is a closed, rigged, low-poly person standing on the ground facing north") {
    std::size_t built = 0;
    for (const sim::WardType type : kPeople) {
        for (std::int32_t id = 1; id < 40; id += 3) {
            for (const bool female : {false, true}) {
                const ActorLook look = actorLookFor(type, id, female);
                const MeshData mesh = buildFigure(look);
                ++built;
                CHECK(mesh.id == figureMeshId(look));
                CHECK(mesh.version == 1);
                CHECK(mesh.triangleCount() > 300U);
                CHECK(mesh.triangleCount() <= kFigureTriangleBudget);
                CHECK(mesh.vertexCount() < 65535U);
                CHECK(mesh.normals.size() == mesh.positions.size());
                CHECK(mesh.colours.size() == mesh.vertexCount() * 4U);
                REQUIRE(mesh.bones.size() == mesh.vertexCount());
                CHECK(mesh.boneRest.size() == (kFigureBoneCount + 1) * 3U);
                for (const std::uint8_t bone : mesh.bones) {
                    CHECK(bone < kFigureBoneCount);
                }
                for (const std::uint16_t index : mesh.indices) {
                    CHECK(index < mesh.vertexCount());
                }
                // Every bone has something to move.
                std::set<std::uint8_t> used(mesh.bones.begin(), mesh.bones.end());
                CHECK(used.size() == kFigureBoneCount);
                // Outward counter-clockwise: the signed volume is positive.
                CHECK(signedVolume(mesh) > 0.0F);
                // Unit normals.
                for (std::size_t v = 0; v < mesh.vertexCount(); ++v) {
                    const float nx = mesh.normals[v * 3], ny = mesh.normals[v * 3 + 1], nz = mesh.normals[v * 3 + 2];
                    CHECK(std::sqrt(nx * nx + ny * ny + nz * nz) == doctest::Approx(1.0F).epsilon(1e-3));
                }
                const Bounds b = boundsOf(mesh.positions);
                CHECK(b.minY >= -0.001F);
                CHECK(b.minY <= 0.01F);               // on its feet
                CHECK(b.maxY >= 1.85F);               // the reference person, 1.875 tall
                CHECK(b.maxY <= 2.0F);
                CHECK(b.minZ < -0.13F);               // a nose and a toe in front (-Z)
                CHECK(b.minZ < -b.maxZ + 0.02F);
                CHECK(b.maxX - b.minX < 0.95F);
            }
        }
    }
    CHECK(built == 12U * 13U * 2U);
}

TEST_CASE("the far figure is the same person at a fraction of the triangles") {
    for (const sim::WardType type : kPeople) {
        for (std::int32_t id = 2; id < 30; id += 5) {
            const ActorLook look = actorLookFor(type, id, id % 2 == 0);
            const MeshData near = buildFigure(look, false);
            const MeshData far = buildFigure(look, true);
            CHECK(far.id == figureFarMeshId(look));
            CHECK(far.triangleCount() <= kFarFigureTriangleBudget);
            CHECK(far.triangleCount() * 2U < near.triangleCount());
            CHECK(far.bones.empty());
            CHECK(signedVolume(far) > 0.0F);
            const Bounds a = boundsOf(near.positions);
            const Bounds b = boundsOf(far.positions);
            CHECK(std::fabs(a.maxY - b.maxY) < 0.06F);
            CHECK(std::fabs(a.minZ - b.minZ) < 0.06F);
        }
    }
}

TEST_CASE("building a figure is deterministic to the byte, and the description holds one mesh per look") {
    const ActorLook look = actorLookFor(sim::WardType::Shopkeeper, 31, true);
    const MeshData a = buildFigure(look);
    const MeshData b = buildFigure(look);
    CHECK(a.positions == b.positions);
    CHECK(a.normals == b.normals);
    CHECK(a.colours == b.colours);
    CHECK(a.indices == b.indices);
    CHECK(a.bones == b.bones);
    CHECK(a.boneRest == b.boneRest);

    SceneDescription scene;
    putFigure(scene, look);
    putFigure(scene, look);  // idempotent
    CHECK(scene.meshes.size() == 1U);
    putFigure(scene, look, true);
    CHECK(scene.meshes.size() == 2U);
    const ActorLook other = actorLookFor(sim::WardType::Shopkeeper, 32, false);
    putFigure(scene, other);
    CHECK(scene.meshes.size() == 3U);

    // The rig is part of the hash: the same mesh without its bones is a different description.
    SceneDescription rigged;
    putFigure(rigged, look);
    SceneDescription bare = rigged;
    bare.meshes[0].bones.clear();
    bare.meshes[0].boneRest.clear();
    CHECK(sceneHash(rigged) != sceneHash(bare));
    SceneDescription again;
    putFigure(again, look);
    CHECK(sceneHash(rigged) == sceneHash(again));
}

TEST_CASE("the rest pose skins to the rest mesh, and every bone moves what it should") {
    const ActorLook look = actorLookFor(sim::WardType::Sailor, 12, false);
    const MeshData mesh = buildFigure(look);
    const auto rest = restOf(look);
    const FigurePose pose = figureRestPose(rest.data());
    std::vector<float> positions;
    std::vector<float> normals;
    skinFigure(mesh, pose, positions, normals);
    REQUIRE(positions.size() == mesh.positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        CHECK(positions[i] == doctest::Approx(mesh.positions[i]).epsilon(1e-5));
        CHECK(normals[i] == doctest::Approx(mesh.normals[i]).epsilon(1e-5));
    }
    // The skeleton: a bone's parent is earlier in the order, so evaluation is one pass.
    for (std::size_t b = 1; b < kFigureBoneCount; ++b) {
        CHECK(static_cast<std::size_t>(figureParentOf(static_cast<FigureBone>(b))) < b);
    }
    // A raised right arm moves right-arm vertices and nothing of the left leg.
    std::array<Vec3, kFigureBoneCount> joints = pose.joint;
    (void)joints;
    const FigurePose talk = evaluateFigurePose(rest.data(), ActorClip::Talk, 30);
    std::vector<float> posed;
    std::vector<float> posedNormals;
    skinFigure(mesh, talk, posed, posedNormals);
    float armMoved = 0.0F;
    float legMoved = 0.0F;
    for (std::size_t v = 0; v < mesh.vertexCount(); ++v) {
        const float d = std::fabs(posed[v * 3] - mesh.positions[v * 3]) + std::fabs(posed[v * 3 + 1] - mesh.positions[v * 3 + 1]) +
                        std::fabs(posed[v * 3 + 2] - mesh.positions[v * 3 + 2]);
        if (mesh.bones[v] == static_cast<std::uint8_t>(FigureBone::ForearmR)) {
            armMoved = std::max(armMoved, d);
        }
        if (mesh.bones[v] == static_cast<std::uint8_t>(FigureBone::FootL)) {
            legMoved = std::max(legMoved, d);
        }
    }
    CHECK(armMoved > 0.15F);
    CHECK(legMoved < 0.05F);
}

TEST_CASE("animation is a pure function of the clip and the integer frame") {
    const ActorLook look = actorLookFor(sim::WardType::Carter, 44, false);
    const auto rest = restOf(look);
    for (const ActorClip clip : kClips) {
        const std::uint32_t length = figureClipLength(clip);
        CHECK(length > 0U);
        for (std::uint32_t frame : {0U, 1U, 7U, 31U, 59U, 90U, 1234U}) {
            const FigurePose a = evaluateFigurePose(rest.data(), clip, frame);
            const FigurePose b = evaluateFigurePose(rest.data(), clip, frame);
            CHECK(std::memcmp(&a, &b, sizeof(FigurePose)) == 0);
            if (!actorClipOneShot(clip)) {
                // A loop wraps by its own length.
                const FigurePose wrapped = evaluateFigurePose(rest.data(), clip, frame + length * 3U);
                CHECK(std::memcmp(&a, &wrapped, sizeof(FigurePose)) == 0);
            }
        }
        if (actorClipOneShot(clip)) {
            // A one-shot plays to its length and holds: where the sim leaves a body that is down.
            const FigurePose end = evaluateFigurePose(rest.data(), clip, length);
            const FigurePose later = evaluateFigurePose(rest.data(), clip, length + 5000U);
            CHECK(std::memcmp(&end, &later, sizeof(FigurePose)) == 0);
        }
    }
    // Different clips are different people-in-motion.
    const FigurePose idle = evaluateFigurePose(rest.data(), ActorClip::Idle, 10);
    const FigurePose walk = evaluateFigurePose(rest.data(), ActorClip::Walk, 10);
    CHECK(std::memcmp(&idle, &walk, sizeof(FigurePose)) != 0);
}

TEST_CASE("a walker's legs alternate, its arms swing against them, and a robe takes shorter steps") {
    const ActorLook look = actorLookFor(sim::WardType::Carter, 5, false);
    ActorLook skirted = look;
    skirted.coatStyle = static_cast<std::uint8_t>(CoatStyle::Robe);
    const auto rest = restOf(look);
    const auto robeRest = restOf(skirted);
    const std::uint32_t length = figureClipLength(ActorClip::Walk);
    const auto zOf = [](const FigurePose& p, FigureBone b) { return p.joint[static_cast<std::size_t>(b)].z; };
    // A quarter into the stride the left foot is forward (-Z) and the right back; half a stride on, swapped.
    const FigurePose q1 = evaluateFigurePose(rest.data(), ActorClip::Walk, length / 4);
    const FigurePose q3 = evaluateFigurePose(rest.data(), ActorClip::Walk, 3 * length / 4);
    CHECK(zOf(q1, FigureBone::FootL) < zOf(q1, FigureBone::FootR) - 0.2F);
    CHECK(zOf(q3, FigureBone::FootL) > zOf(q3, FigureBone::FootR) + 0.2F);
    // The arms swing against the legs: the right hand forward while the left foot is.
    CHECK(zOf(q1, FigureBone::HandR) < zOf(q1, FigureBone::HandL) - 0.1F);
    CHECK(zOf(q3, FigureBone::HandR) > zOf(q3, FigureBone::HandL) + 0.1F);
    const FigurePose r1 = evaluateFigurePose(robeRest.data(), ActorClip::Walk, length / 4);
    const float stride = zOf(q1, FigureBone::FootR) - zOf(q1, FigureBone::FootL);
    const float robeStride = zOf(r1, FigureBone::FootR) - zOf(r1, FigureBone::FootL);
    CHECK(robeStride > 0.1F);
    CHECK(robeStride < stride * 0.8F);
    // Never through the ground, over a whole stride, by more than a hair.
    const MeshData mesh = buildFigure(look);
    for (std::uint32_t frame = 0; frame < length; frame += 4) {
        const Bounds b = boundsOf(posedPositions(mesh, ActorClip::Walk, frame));
        CHECK(b.minY > -0.04F);
        CHECK(b.minY < 0.14F);
    }
}

TEST_CASE("the fight, talk and fall clips pose the body as the name says") {
    const ActorLook look = actorLookFor(sim::WardType::Sailor, 21, false);
    const MeshData mesh = buildFigure(look);
    const auto rest = restOf(look);
    const auto zOf = [](const FigurePose& p, FigureBone b) { return p.joint[static_cast<std::size_t>(b)].z; };
    const auto yOf = [](const FigurePose& p, FigureBone b) { return p.joint[static_cast<std::size_t>(b)].y; };
    const auto xOf = [](const FigurePose& p, FigureBone b) { return p.joint[static_cast<std::size_t>(b)].x; };

    // A right punch at full reach (0.42 of the loop) puts the right fist well out in front of the
    // shoulder; the left punch does the same with the left.
    const FigurePose right = evaluateFigurePose(rest.data(), ActorClip::PunchRight, 17);
    const FigurePose left = evaluateFigurePose(rest.data(), ActorClip::PunchLeft, 17);
    CHECK(zOf(right, FigureBone::HandR) < zOf(right, FigureBone::UpperArmR) - 0.35F);
    CHECK(zOf(left, FigureBone::HandL) < zOf(left, FigureBone::UpperArmL) - 0.35F);
    CHECK(zOf(right, FigureBone::HandR) < zOf(right, FigureBone::HandL) - 0.2F);
    CHECK(zOf(left, FigureBone::HandL) < zOf(left, FigureBone::HandR) - 0.2F);
    // The guard hand is up by the chin, not down at the hip.
    CHECK(yOf(right, FigureBone::HandL) > 1.3F);

    // A block raises both fists in front of the face.
    const FigurePose block = evaluateFigurePose(rest.data(), ActorClip::Block, 10);
    CHECK(yOf(block, FigureBone::HandL) > 1.3F);
    CHECK(yOf(block, FigureBone::HandR) > 1.3F);
    CHECK(zOf(block, FigureBone::HandL) < -0.15F);

    // A talker's right hand is lifted and moves from frame to frame; the head turns.
    const FigurePose t0 = evaluateFigurePose(rest.data(), ActorClip::Talk, 0);
    const FigurePose t1 = evaluateFigurePose(rest.data(), ActorClip::Talk, 12);
    CHECK(yOf(t0, FigureBone::HandR) > 1.15F);
    CHECK((std::fabs(zOf(t0, FigureBone::HandR) - zOf(t1, FigureBone::HandR)) +
           std::fabs(yOf(t0, FigureBone::HandR) - yOf(t1, FigureBone::HandR))) > 0.04F);
    const FigurePose idle = evaluateFigurePose(rest.data(), ActorClip::Idle, 12);
    CHECK(yOf(idle, FigureBone::HandR) < 1.0F);

    // A body struck down sits on the floor; a body slain lies on its back with its head south
    // (+Z, behind the way it faced) and its feet north.
    const Bounds down = boundsOf(posedPositions(mesh, ActorClip::Recover, 9999));
    CHECK(down.maxY < 1.45F);
    CHECK(down.minY > -0.05F);
    const Bounds dead = boundsOf(posedPositions(mesh, ActorClip::Death, 9999));
    CHECK(dead.maxY < 0.45F);
    CHECK(dead.minY > -0.05F);
    const FigurePose lying = evaluateFigurePose(rest.data(), ActorClip::Death, 9999);
    CHECK(zOf(lying, FigureBone::Head) > zOf(lying, FigureBone::Pelvis) + 0.4F);
    CHECK(zOf(lying, FigureBone::FootL) < zOf(lying, FigureBone::Pelvis) - 0.3F);
    CHECK(std::fabs(xOf(lying, FigureBone::Head)) < 0.1F);
    // A flinch: the head snaps back (away from the way it faced) and settles.
    const FigurePose hit = evaluateFigurePose(rest.data(), ActorClip::Hit, 6);
    CHECK(zOf(hit, FigureBone::Head) > zOf(idle, FigureBone::Head) + 0.01F);
}

TEST_CASE("the ward is drawn as figures, and the crowd's description is the same twice") {
    render::SessionConfig config;
    config.width = 160;
    config.height = 90;
    config.timeOfDay = 8 * 3600;
    config.timeOfDayGiven = true;
    render::Session first(config);
    render::Session second(config);
    const auto build = [](const render::Session& session) {
        SceneDescription scene;
        putActorRigs(scene);
        scene.actors = actorInstances(session, session.camera());
        putActorFigures(scene, scene.actors);
        return scene;
    };
    const SceneDescription a = build(first);
    const SceneDescription b = build(second);
    CHECK(sceneHash(a) == sceneHash(b));
    std::size_t people = 0;
    std::size_t skinned = 0;
    std::size_t far = 0;
    std::set<std::uint32_t> looks;
    for (const ActorInstance& body : a.actors) {
        if (!isFigureMeshId(body.instance.meshId)) {
            continue;
        }
        ++people;
        const ActorLook look = ActorLook::fromKey(body.lookKey);
        // A near body wears the near mesh; a far one its far twin: the same look either way.
        CHECK(body.instance.meshId == (body.skinned ? figureMeshId(look) : figureFarMeshId(look)));
        CHECK(a.findMesh(body.instance.meshId) != nullptr);
        looks.insert(body.lookKey);
        skinned += body.skinned ? 1U : 0U;
        far += body.skinned ? 0U : 1U;
        // The scale is the kind's height times the id's own jitter.
        CHECK(body.instance.scale > 0.6F);
        CHECK(body.instance.scale < 1.3F);
    }
    CHECK(people > 100U);
    CHECK(skinned > 0U);
    CHECK(far > 0U);
    CHECK(looks.size() > 40U);  // a crowd of people, not of one person

    // Stepping the world moves the frame the poses read, and nothing else about who they are.
    for (int i = 0; i < 30; ++i) {
        first.step(granadad::sim::MoveInput{});
    }
    const SceneDescription c = build(first);
    CHECK(sceneHash(c) != sceneHash(a));
    std::size_t same = 0;
    for (const ActorInstance& body : c.actors) {
        for (const ActorInstance& old : a.actors) {
            if (isFigureMeshId(old.instance.meshId) && old.lookKey == body.lookKey) {
                ++same;
                break;
            }
        }
    }
    CHECK(same > 50U);
}
