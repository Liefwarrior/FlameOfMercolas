// THE HEADLESS 3D FRAME AGAINST COMMITTED GOLDEN IMAGES, WITH A STATED TOLERANCE.
//
// Phase 3 of docs/design/OBLIVION-PLAN.md retired raylib's rlsw rasterizer (no
// shader, byte-exact everywhere) for Mesa's llvmpipe (the same GL 3.3 shaders
// as the .exe, deterministic in-process, but Mesa and LLVM move low bits
// between versions). A frame is therefore judged against a committed PNG by
// tolerance -- tests/support/golden.hpp says which numbers -- and the scene
// hash stays the exact cross-machine proof of what was drawn.
//
// What the goldens show, and so what a regression looks like:
//   starter     a lit plane and a cube: the depth buffer, perspective-correct
//               checker, the lit and shaded faces;
//   docks-noon  the real district from the spawn at midday;
//   docks-night the same eye at night, the lamps' pools;
//   look-up /   the same eye pitched +-45 degrees: TRUE pitch (the horizon
//   look-down   leaves the middle of the frame; a shear never would).
//
// A golden is regenerated, never hand-edited: GRANADAD_BLESS_GOLDEN=1, see
// support/golden.hpp. Under the .exe's GLFW build these cases skip, like every
// frame case.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/scene_types.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/voxel_classify.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/starter_scene.hpp"
#include "granadad/render3d/world_scene.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"
#include "support/golden.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;
namespace testing = granadad::testing;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 180;
constexpr float kPi = 3.14159265358979323846F;

BackendConfig headlessConfig() {
    BackendConfig config;
    config.width = kWidth;
    config.height = kHeight;
    config.windowScale = 1;
    config.headless = true;
    config.vsync = false;
    return config;
}

render::Framebuffer drawOnce(Backend& video, const SceneDescription& scene) {
    if (std::getenv("DBG_TWICE") != nullptr) {
        video.beginFrame(scene.clearColour);
        (void)video.drawScene(scene);
        render::Framebuffer junk(1, 1);
        video.endFrame(&junk);
    }
    video.beginFrame(scene.clearColour);
    (void)video.drawScene(scene);
    render::Framebuffer shot(1, 1);
    video.endFrame(&shot);
    return shot;
}

/// The Docks from the authored spawn on the Tarwalk, at `seconds` into the
/// day, the head pitched by `pitchDegrees`.
render::Framebuffer docksFrame(Backend& video, int seconds, float pitchDegrees) {
    namespace content = granadad::content;
    namespace sim = granadad::sim;
    const content::World world =
        content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    const sim::TileQuery tiles(world);
    const render::TileAtlas atlas = render::TileAtlas::load(content::contentDir());
    const std::vector<render::Lamp> lamps =
        render::loadLamps(content::contentDir(), sim::docks::kWorldName);
    const render::LampGlow glow = render::LampGlow::build(tiles, lamps);

    render::Camera eye;
    eye.x = static_cast<float>(sim::docks::kSpawnTileX) + 0.5F;
    eye.y = static_cast<float>(sim::docks::kSpawnTileY) + 0.5F;
    eye.z = render::bandSurface(sim::docks::kSpawnBand) +
            static_cast<float>(sim::kEyeHeightTilesQ8) / 256.0F;
    eye.yaw = 265.0F * kPi / 180.0F;
    eye.pitch = pitchDegrees * kPi / 180.0F;
    eye.hfovTan = 1.0F;

    WorldSceneParams params;
    params.timeOfDaySeconds = seconds;
    WorldScene docks(tiles, atlas, &glow);
    SceneDescription scene;
    docks.refresh(scene, eye, static_cast<float>(kWidth) / static_cast<float>(kHeight), params);
    return drawOnce(video, scene);
}

void expectGolden(const render::Framebuffer& frame, const std::string& name) {
    const testing::GoldenReport report = testing::compareWithGolden(frame, name);
    MESSAGE(report.why);
    CHECK_MESSAGE(report.ok, report.why);
}

}  // namespace

TEST_CASE("the starter scene matches its golden image within tolerance") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build is not on raylib's SDL platform, so it has no headless context");
        return;
    }
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
    eye.hfovTan = 1.0F;
    scene.camera = cameraFrom(eye, static_cast<float>(kWidth) / static_cast<float>(kHeight));

    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    expectGolden(drawOnce(*video, scene), "starter");
}

TEST_CASE("the Docks at noon and at night match their golden images within tolerance") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build is not on raylib's SDL platform, so it has no headless context");
        return;
    }
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    expectGolden(docksFrame(*video, 12 * 3600, 0.0F), "docks-noon");
    expectGolden(docksFrame(*video, 22 * 3600, 0.0F), "docks-night");
}

TEST_CASE("looking up and looking down is true pitch, matching the golden images") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build is not on raylib's SDL platform, so it has no headless context");
        return;
    }
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    const render::Framebuffer level = docksFrame(*video, 12 * 3600, 0.0F);
    const render::Framebuffer up = docksFrame(*video, 12 * 3600, 45.0F);
    const render::Framebuffer down = docksFrame(*video, 12 * 3600, -45.0F);
    expectGolden(up, "docks-look-up");
    expectGolden(down, "docks-look-down");

    // TRUE PITCH, not a horizon shear: looking 45 degrees up turns the
    // picture into sky over a few roofs, looking 45 degrees down into ground
    // -- the three frames are three different pictures, and the upward one
    // has a good deal more of the one sky colour in it than the level one.
    CHECK(up.pixels() != level.pixels());
    CHECK(down.pixels() != level.pixels());
    const auto mostCommon = [](const render::Framebuffer& frame) {
        std::map<std::uint32_t, std::size_t> counts;
        for (const std::uint32_t pixel : frame.pixels()) {
            ++counts[pixel];
        }
        std::size_t best = 0;
        for (const auto& [colour, n] : counts) {
            (void)colour;
            best = n > best ? n : best;
        }
        return best;
    };
    CHECK(mostCommon(up) > mostCommon(level));
}
