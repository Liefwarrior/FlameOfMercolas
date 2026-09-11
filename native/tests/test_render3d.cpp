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

#include <cstdint>
#include <cstdlib>
#include <set>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/starter_scene.hpp"

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
