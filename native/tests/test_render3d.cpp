// THE 3D FRAME, ASSERTED ON THE PIXELS IT ACTUALLY PRODUCES -- HEADLESS.
//
// raylib 6.0 ships a software rasterizer (rlsw) and a windowless platform
// (PLATFORM_MEMORY); the docker host check builds the adapter on those, so a
// real 3D frame of a real description can be drawn INSIDE the test suite with
// no window, no GPU and no display server, and facts about it checked on
// every build. That is the same discipline the software renderer bought S1
// through S13, carried into 3D.
//
// The shipped .exe builds the adapter on GLFW + OpenGL 3.3 instead. Under
// that build these frame cases SKIP rather than open a window inside the
// test suite: a GPU frame is proved by the exe's own --screenshot on the host
// and by similarity, never by a hash -- see backend.hpp. The
// Framebuffer-alpha cases below run everywhere.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/actor_instances.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/starter_scene.hpp"
#include "granadad/render3d/static_pieces.hpp"
#include "granadad/render3d/world_scene.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 180;

BackendConfig headlessConfig() {
    BackendConfig config;
    config.width = kWidth;
    config.height = kHeight;
    config.windowScale = 1;
    config.vsync = false;
    return config;
}

/// The starter scene seen from one tile up, looking north at the cube four
/// tiles away, with a 90-degree horizontal field.
SceneDescription starterScene() {
    StarterSceneParams params;
    params.groundY = 0.0F;
    params.centre = Vec3{0.0F, 0.0F, 0.0F};
    params.cube = Vec3{0.0F, 0.0F, -4.0F};
    params.halfExtent = 16.0F;
    params.timeOfDaySeconds = 12 * 3600;
    SceneDescription scene;
    buildStarterScene(scene, params);
    render::Camera eye;
    eye.x = 0.0F;
    eye.y = 0.0F;
    eye.z = 1.7F;
    eye.yaw = 0.0F;
    eye.pitch = 0.0F;
    eye.hfovTan = 1.0F;
    scene.camera = cameraFrom(eye, static_cast<float>(kWidth) / static_cast<float>(kHeight));
    return scene;
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

int channelDistance(std::uint32_t a, std::uint32_t b) {
    int worst = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        const int d = std::abs(static_cast<int>((a >> shift) & 0xFFU) -
                               static_cast<int>((b >> shift) & 0xFFU));
        worst = d > worst ? d : worst;
    }
    return worst;
}

std::uint32_t packed(const Rgba8& c) {
    return static_cast<std::uint32_t>(c.r) | (static_cast<std::uint32_t>(c.g) << 8) |
           (static_cast<std::uint32_t>(c.b) << 16) | (static_cast<std::uint32_t>(c.a) << 24);
}

/// The flat colour of one face of a mesh pushed four vertices per face.
Rgba8 faceColour(const MeshData& mesh, std::size_t face) {
    const std::size_t at = face * 4 * 4;
    return Rgba8{mesh.colours[at], mesh.colours[at + 1], mesh.colours[at + 2],
                 mesh.colours[at + 3]};
}

}  // namespace

TEST_CASE("a 3D frame of a lit plane and a cube renders headless through rlsw") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    CHECK(video->kind() == VideoKind::Software);
    CHECK(video->width() == kWidth);
    CHECK(video->height() == kHeight);

    const SceneDescription scene = starterScene();
    SceneStats stats;
    const render::Framebuffer frame = drawOnce(*video, scene, nullptr, &stats);
    REQUIRE(frame.width() == kWidth);
    REQUIRE(frame.height() == kHeight);
    CHECK(stats.instancesDrawn == 2);
    CHECK(stats.meshesUploaded == 2);
    CHECK(stats.trianglesDrawn > 12);

    // Pixels non-trivial: not all one colour. A cleared frame scores 1, a
    // frame with a plane and a lit cube in it scores at least sky + two
    // checker shades + the cube's lit and shaded faces.
    std::set<std::uint32_t> distinct;
    for (const std::uint32_t pixel : frame.pixels()) {
        distinct.insert(pixel);
    }
    CHECK(distinct.size() >= 5);

    // The top-left corner is sky -- the clear colour. Within one: rlsw
    // stores a channel as (uint8)(f * 255) with no rounding, and a byte that
    // went through /255 and back in float can land one short.
    CHECK(channelDistance(pixelAt(frame, 0, 0), packed(scene.clearColour)) <= 1);
    // The bottom-centre pixel is ground: one of the two checker shades. The
    // plane's mesh is one quad per square, all four vertices one colour, so
    // the rasterizer has no gradient to invent (a tolerance of two covers
    // its float interpolation rounding).
    const MeshData* ground = scene.findMesh(kStarterGroundMeshId);
    REQUIRE(ground != nullptr);
    const std::uint32_t groundPixel = pixelAt(frame, kWidth / 2, kHeight - 3);
    bool onGround = false;
    for (std::size_t face = 0; face < ground->vertexCount() / 4 && !onGround; ++face) {
        onGround = channelDistance(groundPixel, packed(faceColour(*ground, face))) <= 2;
    }
    CHECK(onGround);
    // The cube: centred four tiles north, one tile on a side, seen from 1.7
    // tiles up through a 90-degree horizontal field (focal length 160 px at
    // 320 wide, horizon at row 90). Its south face -- the one towards the
    // eye -- is 3.5 tiles away: its top edge sits 0.7 below the eye, 160 *
    // 0.7 / 3.5 = 32 px under the horizon (row 122), its base 1.7 below,
    // 78 px under (row 168). The top face runs from that edge back to 4.5
    // tiles, 160 * 0.7 / 4.5 = 25 px under (row 115). Rows are sampled well
    // inside both. Face order is top, bottom, south, north, east, west.
    const MeshData* cube = scene.findMesh(kStarterCubeMeshId);
    REQUIRE(cube != nullptr);
    const std::uint32_t southFace = packed(faceColour(*cube, 2));
    const std::uint32_t topFace = packed(faceColour(*cube, 0));
    CHECK(channelDistance(pixelAt(frame, kWidth / 2, 140), southFace) <= 2);
    // ...and just above it the top face, which the eye looks down onto.
    CHECK(channelDistance(pixelAt(frame, kWidth / 2, 118), topFace) <= 2);
    // The lit top is brighter than the shaded south face: the lighting is
    // in the pixels, not only in the description.
    CHECK((topFace & 0xFFU) > (southFace & 0xFFU));
    // And well to the side of the cube, at the same row, it is ground again.
    CHECK(channelDistance(pixelAt(frame, 40, 140), southFace) > 2);
}

TEST_CASE("the same 3D scene renders byte-identical twice") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    const SceneDescription scene = starterScene();
    render::Framebuffer first(1, 1);
    {
        std::unique_ptr<Backend> video = Backend::open(headlessConfig());
        REQUIRE(video != nullptr);
        first = drawOnce(*video, scene, nullptr, nullptr);
        // Twice in one backend: the mesh cache is warm the second time and
        // the picture must not care.
        const render::Framebuffer second = drawOnce(*video, scene, nullptr, nullptr);
        CHECK(first.pixels() == second.pixels());
    }
    // And again in a fresh backend, from a cold cache, in the same process:
    // the frame is a function of the description and of nothing else.
    std::unique_ptr<Backend> again = Backend::open(headlessConfig());
    REQUIRE(again != nullptr);
    const render::Framebuffer third = drawOnce(*again, scene, nullptr, nullptr);
    CHECK(first.pixels() == third.pixels());
    // The description itself hashed the same all along.
    CHECK(sceneHash(scene) == sceneHash(starterScene()));
}

TEST_CASE("the HUD overlay composites over the 3D frame and leaves the sky alone where it is clear") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    const SceneDescription scene = starterScene();

    render::Framebuffer overlay(kWidth, kHeight);
    overlay.clearTransparent();
    // An opaque plate, the way drawTextPlate draws one...
    overlay.fillRect(10, 10, 40, 20, render::Rgb{1.0F, 0.0F, 0.0F}, 1.0F);
    // ...and a half-covered wash over the sky, the way a punch wash draws.
    overlay.fillRect(200, 0, 40, 20, render::Rgb{0.0F, 0.0F, 1.0F}, 0.5F);

    const render::Framebuffer bare = drawOnce(*video, scene, nullptr, nullptr);
    const render::Framebuffer composed = drawOnce(*video, scene, &overlay, nullptr);
    REQUIRE(composed.width() == kWidth);

    // Under the plate: red, opaque.
    CHECK(channelDistance(pixelAt(composed, 20, 15), 0xFF0000FFU) <= 2);
    // Under the half wash: half way from sky to blue.
    const std::uint32_t sky = pixelAt(bare, 210, 5);
    const std::uint32_t washed = pixelAt(composed, 210, 5);
    const int skyBlue = static_cast<int>((sky >> 16) & 0xFFU);
    const int washedBlue = static_cast<int>((washed >> 16) & 0xFFU);
    const int skyRed = static_cast<int>(sky & 0xFFU);
    const int washedRed = static_cast<int>(washed & 0xFFU);
    CHECK(std::abs(washedBlue - (skyBlue + 255) / 2) <= 3);
    CHECK(std::abs(washedRed - skyRed / 2) <= 3);
    // Where the overlay is clear, the frame is the bare frame -- within one,
    // because a clear overlay pixel still goes through the blend (dst * 1 +
    // src * 0) and rlsw's byte store truncates.
    CHECK(channelDistance(pixelAt(composed, 300, 5), pixelAt(bare, 300, 5)) <= 1);
    CHECK(channelDistance(pixelAt(composed, kWidth / 2, 140), pixelAt(bare, kWidth / 2, 140)) <=
          1);
    // The overlay's own placement at a window the frame's own size is 1:1.
    const OverlayPlacement placement = video->overlayPlacement(kWidth, kHeight);
    CHECK(placement.scale == 1);
    CHECK(placement.offsetX == 0);
    CHECK(placement.offsetY == 0);
}

TEST_CASE("blend over an opaque pixel is the lerp it always was") {
    // The eighteen pixel-exact HUD/page test files rest on this: a frame
    // cleared to a colour, then blended on, produces the identical bytes the
    // software renderer produced before the overlay existed.
    render::Framebuffer frame(4, 1);
    frame.clear(render::Rgb{0.2F, 0.4F, 0.6F});
    const std::uint32_t before = pixelAt(frame, 0, 0);
    CHECK((before >> 24) == 0xFFU);
    frame.blend(0, 0, render::Rgb{1.0F, 0.0F, 0.0F}, 0.25F);
    const std::uint32_t expected =
        render::packRgb(render::lerp(render::unpackRgb(before), render::Rgb{1.0F, 0.0F, 0.0F}, 0.25F));
    CHECK(pixelAt(frame, 0, 0) == expected);
    CHECK((pixelAt(frame, 0, 0) >> 24) == 0xFFU);
    // Alpha one writes the colour outright, alpha zero writes nothing.
    frame.blend(1, 0, render::Rgb{0.0F, 1.0F, 0.0F}, 1.0F);
    CHECK(pixelAt(frame, 1, 0) == render::packRgb(render::Rgb{0.0F, 1.0F, 0.0F}));
    frame.blend(2, 0, render::Rgb{0.0F, 1.0F, 0.0F}, 0.0F);
    CHECK(pixelAt(frame, 2, 0) == before);
}

TEST_CASE("blend over a transparent pixel accumulates coverage") {
    render::Framebuffer frame(3, 1);
    frame.clearTransparent();
    CHECK(pixelAt(frame, 0, 0) == 0U);
    CHECK(render::unpackAlpha(pixelAt(frame, 0, 0)) == 0.0F);
    // A first quarter-wash over nothing IS the colour, at a quarter coverage.
    frame.blend(0, 0, render::Rgb{1.0F, 0.0F, 0.0F}, 0.25F);
    const std::uint32_t once = pixelAt(frame, 0, 0);
    CHECK((once & 0xFFU) == 255U);
    CHECK(((once >> 8) & 0xFFU) == 0U);
    CHECK(((once >> 24) & 0xFFU) == 64U);
    // A second quarter over it: coverage a + A(1 - a) = 0.4375.
    frame.blend(0, 0, render::Rgb{1.0F, 0.0F, 0.0F}, 0.25F);
    CHECK(((pixelAt(frame, 0, 0) >> 24) & 0xFFU) == 112U);
    // Opaque over transparent is simply opaque.
    frame.blend(1, 0, render::Rgb{0.0F, 0.0F, 1.0F}, 1.0F);
    CHECK(pixelAt(frame, 1, 0) == render::packRgb(render::Rgb{0.0F, 0.0F, 1.0F}));
    // set() writes opaque too, exactly as before.
    frame.set(2, 0, render::Rgb{0.5F, 0.5F, 0.5F}, 1.0F);
    CHECK((pixelAt(frame, 2, 0) >> 24) == 0xFFU);
    CHECK(render::packRgba(render::Rgb{1.0F, 1.0F, 1.0F}, 1.0F) == 0xFFFFFFFFU);
    CHECK(render::packRgba(render::Rgb{1.0F, 1.0F, 1.0F}, 0.0F) == 0x00FFFFFFU);
}

TEST_CASE("the Docks render to a 3D frame with a world in it") {
    // THE WORLD LANE'S FRAME: the real district, meshed by chunk from its
    // baked tiles, lit by its own lamps and the noon curve, seen from the
    // authored spawn on the quayside looking west down the Tarwalk with the
    // Gilded Gull on the left -- drawn headless through rlsw. What is
    // asserted is that a WORLD is in the picture: thousands of pixels that
    // the sky alone would not have put there, many colours, and the same
    // bytes when drawn again.
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    namespace content = granadad::content;
    namespace sim = granadad::sim;
    const content::World world =
        content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    const sim::TileQuery tiles(world);
    // The real pack when the checkout has it, the procedural one when it
    // does not: the assertions hold on either, which is the point of them.
    const render::TileAtlas atlas = render::TileAtlas::load(content::contentDir());
    const std::vector<render::Lamp> lamps =
        render::loadLamps(content::contentDir(), sim::docks::kWorldName);
    const render::LampGlow glow = render::LampGlow::build(tiles, lamps);

    render::Camera eye;
    eye.x = static_cast<float>(sim::docks::kSpawnTileX) + 0.5F;
    eye.y = static_cast<float>(sim::docks::kSpawnTileY) + 0.5F;
    eye.z = render::bandSurface(sim::docks::kSpawnBand) +
            static_cast<float>(sim::kEyeHeightTilesQ8) / 256.0F;
    eye.yaw = 265.0F * 3.14159265358979323846F / 180.0F;
    eye.pitch = 0.0F;
    eye.hfovTan = 1.0F;

    WorldSceneParams params;
    params.timeOfDaySeconds = 12 * 3600;
    WorldScene docks(tiles, atlas, &glow);
    SceneDescription scene;
    docks.refresh(scene, eye, static_cast<float>(kWidth) / static_cast<float>(kHeight), params);
    REQUIRE(scene.instances.size() >= 2);
    REQUIRE(scene.instances[0].meshId == kSkyMeshId);

    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    SceneStats stats;
    const render::Framebuffer frame = drawOnce(*video, scene, nullptr, &stats);
    REQUIRE(frame.width() == kWidth);
    REQUIRE(frame.height() == kHeight);
    CHECK(stats.instancesDrawn == scene.instances.size());
    CHECK(stats.texturesUploaded == 1);
    CHECK(stats.trianglesDrawn > 10000);

    // The sky alone, for the difference: everything the world put there.
    SceneDescription skyOnly = scene;
    skyOnly.instances.resize(1);
    const render::Framebuffer skyFrame = drawOnce(*video, skyOnly, nullptr, nullptr);
    std::size_t worldPixels = 0;
    for (std::size_t i = 0; i < frame.pixels().size(); ++i) {
        if (frame.pixels()[i] != skyFrame.pixels()[i]) {
            ++worldPixels;
        }
    }
    CHECK(worldPixels > 8000);
    std::set<std::uint32_t> distinct;
    for (const std::uint32_t pixel : frame.pixels()) {
        distinct.insert(pixel);
    }
    CHECK(distinct.size() >= 16);
    // And some sky is left: the Gull's two storeys reach the top-left of
    // the frame from here, but the district does not fill the whole sky.
    CHECK(worldPixels < frame.pixels().size());
    // Ground under the eye: the quayside is not sky.
    CHECK(pixelAt(frame, kWidth / 2, kHeight - 2) != pixelAt(skyFrame, kWidth / 2, kHeight - 2));
    MESSAGE("Docks frame: " << worldPixels << " world pixels, " << distinct.size()
                            << " colours, " << stats.instancesDrawn << " instances, "
                            << stats.trianglesDrawn << " triangles");

    // And again, byte for byte -- the frame is a function of the description.
    const render::Framebuffer second = drawOnce(*video, scene, nullptr, nullptr);
    CHECK(frame.pixels() == second.pixels());
}

TEST_CASE("the ward's people render as figures in the 3D frame") {
    // THE A LANE'S FRAME. A real session at eight in the morning, the real
    // Docks meshed by chunk, and the ward's own roster instanced into the
    // description -- then the eye is stood two tiles from the nearest body
    // on the same storey, looking straight at it, and the frame is drawn
    // headless through rlsw. What is asserted: the body put pixels in the
    // picture that the world alone did not, near the middle of the frame
    // where a figure two tiles away stands, the stats count it as a body
    // drawn through its placeholder (no glb in this container, ever), and
    // the same description draws the same bytes again.
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    namespace sim = granadad::sim;
    render::SessionConfig config;
    config.width = kWidth;
    config.height = kHeight;
    config.timeOfDay = 8 * 3600;
    config.timeOfDayGiven = true;
    render::Session session(config);

    // The nearest visible person on the body's own band, and a standable
    // tile two steps from it (orthogonal first) to put the eye on.
    const render::Camera spawn = session.camera();
    const sim::WardActor* subject = nullptr;
    float best = 1e9F;
    for (const sim::WardActor& actor : session.people().actors()) {
        if (!actor.visible() || !sim::isPerson(actor.type) || actor.band != session.body().band()) {
            continue;
        }
        const float dx = static_cast<float>(actor.x) + 0.5F - spawn.x;
        const float dy = static_cast<float>(actor.y) + 0.5F - spawn.y;
        const float d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            subject = &actor;
        }
    }
    REQUIRE(subject != nullptr);
    const std::int32_t offsets[4][2] = {{0, 2}, {2, 0}, {0, -2}, {-2, 0}};
    render::Camera eye = spawn;
    bool placed = false;
    for (const auto& offset : offsets) {
        const std::int32_t ex = subject->x + offset[0];
        const std::int32_t ey = subject->y + offset[1];
        const std::int32_t mx = subject->x + offset[0] / 2;
        const std::int32_t my = subject->y + offset[1] / 2;
        if (session.tiles().standable(ex, ey, subject->band) &&
            session.tiles().standable(mx, my, subject->band)) {
            eye.x = static_cast<float>(ex) + 0.5F;
            eye.y = static_cast<float>(ey) + 0.5F;
            eye.z = render::bandSurface(subject->band) +
                    static_cast<float>(sim::kEyeHeightTilesQ8) / 256.0F;
            // Yaw clockwise from north (-y): atan2 of east over north.
            eye.yaw = std::atan2(static_cast<float>(-offset[0]), static_cast<float>(offset[1]));
            eye.pitch = 0.0F;
            placed = true;
            break;
        }
    }
    REQUIRE(placed);
    MESSAGE("subject: " << sim::wardTypeName(subject->type) << " #" << subject->id << " at ("
                        << subject->x << "," << subject->y << ") band " << subject->band
                        << "; eye at (" << eye.x << "," << eye.y << ") yaw " << eye.yaw);

    WorldSceneParams params;
    params.timeOfDaySeconds = session.timeOfDay();
    WorldScene docks(session.tiles(), session.atlas(), &session.renderer().glow());
    SceneDescription scene;
    docks.refresh(scene, eye, static_cast<float>(kWidth) / static_cast<float>(kHeight), params);
    putActorRigs(scene);
    scene.actors = actorInstances(session, eye);
    REQUIRE(!scene.actors.empty());
    // The subject is among them, two tiles ahead of the eye.
    bool subjectInstanced = false;
    for (const ActorInstance& body : scene.actors) {
        if (std::fabs(body.instance.position.x - (static_cast<float>(subject->x) + 0.5F)) < 1e-4F &&
            std::fabs(body.instance.position.z - (static_cast<float>(subject->y) + 0.5F)) < 1e-4F) {
            subjectInstanced = true;
            CHECK(body.skinned);
        }
    }
    CHECK(subjectInstanced);

    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    SceneStats stats;
    const render::Framebuffer frame = drawOnce(*video, scene, nullptr, &stats);
    CHECK(stats.actorsDrawn == scene.actors.size());
    CHECK(stats.actorsSkinned == 0);  // no glb here: every body is its placeholder
    CHECK(stats.rigModelsLoaded == 0);
    CHECK(stats.instancesDrawn == scene.instances.size() + scene.actors.size());

    // The world alone, for the difference: everything the people put there.
    SceneDescription empty = scene;
    empty.actors.clear();
    SceneStats emptyStats;
    const render::Framebuffer world = drawOnce(*video, empty, nullptr, &emptyStats);
    CHECK(emptyStats.actorsDrawn == 0);
    std::size_t peoplePixels = 0;
    std::size_t centrePixels = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (pixelAt(frame, x, y) != pixelAt(world, x, y)) {
                ++peoplePixels;
                if (x > kWidth / 4 && x < kWidth * 3 / 4 && y > kHeight / 6 && y < kHeight) {
                    ++centrePixels;
                }
            }
        }
    }
    MESSAGE("people pixels: " << peoplePixels << " (" << centrePixels << " in the centre), "
                              << stats.actorsDrawn << " bodies, " << stats.trianglesDrawn
                              << " triangles");
    // A figure 1.875 tiles tall two tiles away spans most of the frame's
    // height at a 90-degree field: hundreds of pixels at the least.
    CHECK(peoplePixels > 300U);
    CHECK(centrePixels > 200U);
    CHECK(peoplePixels < frame.pixels().size() / 2);

    // And again, byte for byte.
    const render::Framebuffer second = drawOnce(*video, scene, nullptr, nullptr);
    CHECK(frame.pixels() == second.pixels());
}

TEST_CASE("a missing piece falls back to the placeholder and the scene still renders") {
    // THE S LANE'S FALLBACK, on the pixels. The Docks described WITH every
    // building piece the catalogue places near the spawn, drawn through a
    // backend that has no static model directory (this container never has
    // the licensed files): every placement counts as missing, nothing of it
    // is drawn, and the frame is BYTE-IDENTICAL to the same description
    // with its pieces stripped out -- the chunk mesh under the pieces is
    // the placeholder, always there. Then a directory that exists but holds
    // no .gltf: the same frame again, no file opened, no crash.
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build renders through a GPU window, not rlsw");
        return;
    }
    namespace content = granadad::content;
    namespace sim = granadad::sim;
    const content::World world =
        content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    const sim::TileQuery tiles(world);
    const render::TileAtlas atlas = render::TileAtlas::load(content::contentDir());
    const std::vector<render::Lamp> lamps =
        render::loadLamps(content::contentDir(), sim::docks::kWorldName);
    const render::LampGlow glow = render::LampGlow::build(tiles, lamps);
    const StaticCatalogue catalogue =
        StaticCatalogue::load(staticCataloguePath(content::contentDir()));
    REQUIRE_FALSE(catalogue.empty());

    render::Camera eye;
    eye.x = static_cast<float>(sim::docks::kSpawnTileX) + 0.5F;
    eye.y = static_cast<float>(sim::docks::kSpawnTileY) + 0.5F;
    eye.z = render::bandSurface(sim::docks::kSpawnBand) +
            static_cast<float>(sim::kEyeHeightTilesQ8) / 256.0F;
    eye.yaw = 265.0F * 3.14159265358979323846F / 180.0F;
    eye.pitch = 0.0F;
    eye.hfovTan = 1.0F;

    WorldSceneParams params;
    params.timeOfDaySeconds = 12 * 3600;
    WorldScene docks(tiles, atlas, &glow, &catalogue, &lamps);
    SceneDescription dressed;
    docks.refresh(dressed, eye, static_cast<float>(kWidth) / static_cast<float>(kHeight), params);
    REQUIRE(dressed.statics.size() > 100);
    REQUIRE(dressed.pieces.size() == catalogue.pieces().size());
    SceneDescription bare = dressed;
    bare.statics.clear();
    bare.pieces.clear();
    CHECK(sceneHash(bare) != sceneHash(dressed));

    BackendConfig config = headlessConfig();
    std::unique_ptr<Backend> video = Backend::open(config);
    REQUIRE(video != nullptr);
    SceneStats stats;
    const render::Framebuffer withPieces = drawOnce(*video, dressed, nullptr, &stats);
    CHECK(stats.staticsMissing == dressed.statics.size());
    CHECK(stats.staticsDrawn == 0);
    CHECK(stats.staticModelsLoaded == 0);
    CHECK(stats.instancesDrawn == dressed.instances.size());
    const render::Framebuffer withoutPieces = drawOnce(*video, bare, nullptr, nullptr);
    CHECK(withPieces.pixels() == withoutPieces.pixels());
    video.reset();

    // A directory with no pieces in it: looked in, nothing found, the same
    // picture.
    config.staticDir = (content::contentDir() / "maps").string();
    video = Backend::open(config);
    REQUIRE(video != nullptr);
    SceneStats again;
    const render::Framebuffer lookedFor = drawOnce(*video, dressed, nullptr, &again);
    CHECK(again.staticsMissing == dressed.statics.size());
    CHECK(again.staticsDrawn == 0);
    CHECK(again.staticModelsLoaded == 0);
    CHECK(lookedFor.pixels() == withoutPieces.pixels());
    MESSAGE("placeholder frame: " << dressed.statics.size() << " pieces described, none drawn, "
                                  << dressed.instances.size() << " chunk instances stand");
}
