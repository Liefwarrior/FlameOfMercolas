// LIGHTING AND ATMOSPHERE -- phase 5 of docs/design/OBLIVION-PLAN.md.
//
// Two kinds of case. The PURE ones need no renderer at all: the lamp curve the
// shader and the simulation's integer light field both evaluate (pinned to one
// another here), the lights, the sun and the moon from the clock, the sky and
// the fog, and the description the world scene writes when the light is per
// pixel -- all hashed, all a function of the clock and the weather. The FRAME
// ones draw that description headless on Mesa's llvmpipe and judge it by
// tolerance against committed goldens (tests/support/golden.hpp) and by what is
// in the picture.
//
// SHADOWS ARE COSMETIC (ruling 9): nothing here, and nothing in the renderer,
// reaches the simulation. The sim's light field is evaluated below only to be
// compared with the curve.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/render/atlas.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/vertical.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/light_model.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/world_scene.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/stealth.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/vertical_scale.hpp"
#include "support/golden.hpp"

using namespace granadad::render3d;
namespace render = granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;
namespace testing = granadad::testing;

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr int kWidth = 320;
constexpr int kHeight = 180;

float lumaOf(const Vec3& c) { return 0.30F * c.x + 0.59F * c.y + 0.11F * c.z; }

/// The Docks, built once for the file.
struct Docks {
    content::World world = content::loadWorldFile(content::bakedMap(sim::docks::kWorldName));
    sim::TileQuery tiles{world};
    render::TileAtlas atlas = render::TileAtlas::load(content::contentDir());
    std::vector<render::Lamp> lamps = render::loadLamps(content::contentDir(), sim::docks::kWorldName);
    render::LampGlow glow = render::LampGlow::build(tiles, lamps);
};

Docks& docks() {
    static Docks instance;
    return instance;
}

render::Camera spawnEye(float pitchDegrees = 0.0F) {
    render::Camera eye;
    eye.x = static_cast<float>(sim::docks::kSpawnTileX) + 0.5F;
    eye.y = static_cast<float>(sim::docks::kSpawnTileY) + 0.5F;
    eye.z = render::bandSurface(sim::docks::kSpawnBand) + static_cast<float>(sim::kEyeHeightTilesQ8) / 256.0F;
    eye.yaw = 265.0F * kPi / 180.0F;
    eye.pitch = pitchDegrees * kPi / 180.0F;
    eye.hfovTan = 1.0F;
    return eye;
}

WorldSceneParams litParams(int seconds) {
    WorldSceneParams params;
    params.timeOfDaySeconds = seconds;
    params.perPixel = true;
    return params;
}

/// The district's geometry is built once for the file (it is the slow part).
WorldScene& docksWorld() {
    static WorldScene world(docks().tiles, docks().atlas, &docks().glow, nullptr, &docks().lamps);
    return world;
}

SceneDescription litScene(int seconds, const render::Weather& weather = {}) {
    WorldScene& world = docksWorld();
    WorldSceneParams params = litParams(seconds);
    params.weather = weather;
    SceneDescription scene;
    world.refresh(scene, spawnEye(), static_cast<float>(kWidth) / static_cast<float>(kHeight), params);
    return scene;
}

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
    video.beginFrame(scene.clearColour);
    (void)video.drawScene(scene);
    render::Framebuffer shot(1, 1);
    video.endFrame(&shot);
    return shot;
}

double meanLuma(const render::Framebuffer& frame) {
    double sum = 0.0;
    for (const std::uint32_t px : frame.pixels()) {
        sum += 0.30 * static_cast<double>(px & 0xFFU) + 0.59 * static_cast<double>((px >> 8) & 0xFFU) +
               0.11 * static_cast<double>((px >> 16) & 0xFFU);
    }
    return sum / static_cast<double>(frame.pixels().size());
}

}  // namespace

TEST_CASE("the lamp curve is the simulation's own: the shader's function and the integer field agree") {
    // lampFalloff is the shader's term; sim::glowFrom is the stealth field's
    // integer evaluation of the same expression. At a grid of points round a
    // lamp of each luminance the two agree to the integer's own resolution
    // (hundredths: the field truncates twice), and both are exactly zero at and
    // beyond the radius.
    for (const std::int32_t luminance : {4, 8, 12, 18, 26, 31}) {
        const sim::SimLight light{40, 40, 3, luminance};
        const float radius = static_cast<float>(sim::lightRadiusQ4(luminance)) / 16.0F;
        const float peak = static_cast<float>(sim::lightPeak(luminance)) / 100.0F;
        int compared = 0;
        for (std::int32_t dy = -7; dy <= 7; ++dy) {
            for (std::int32_t dx = -7; dx <= 7; ++dx) {
                const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                const float curve = 100.0F * lampFalloff(distance, radius, peak);
                const std::int32_t field = sim::glowFrom(light, 40 + dx, 40 + dy, 3);
                CHECK(std::abs(curve - static_cast<float>(field)) <= 2.0F);
                if (distance >= radius) {
                    CHECK(curve == 0.0F);
                    CHECK(field == 0);
                }
                ++compared;
            }
        }
        CHECK(compared == 225);
        // The peak is the lamp's own; the curve is monotone down to nothing.
        CHECK(lampFalloff(0.0F, radius, peak) == doctest::Approx(peak));
        float last = peak;
        for (int k = 1; k <= 40; ++k) {
            const float v = lampFalloff(radius * static_cast<float>(k) / 40.0F, radius, peak);
            CHECK(v <= last);
            last = v;
        }
        CHECK(last == 0.0F);
    }
    // No radius, no light.
    CHECK(lampFalloff(1.0F, 0.0F, 1.0F) == 0.0F);
}

TEST_CASE("a lamp is a point light with the sim's radius and peak, its warmth and a steady identity") {
    render::Lamp lantern;
    lantern.name = "lantern";
    lantern.x = 20;
    lantern.y = 30;
    lantern.z = 4;
    lantern.luminance = 18;
    lantern.warmth = render::LampWarmth::Lantern;
    render::Lamp brazier = lantern;
    brazier.name = "brazier";
    brazier.warmth = render::LampWarmth::Fire;
    const ScenePointLight a = pointLightOf(lantern);
    const ScenePointLight b = pointLightOf(brazier);
    CHECK(a.radius == doctest::Approx(static_cast<float>(sim::lightRadiusQ4(18)) / 16.0F));
    CHECK(a.peak == doctest::Approx(static_cast<float>(sim::lightPeak(18)) / 100.0F));
    CHECK(a.position.x == doctest::Approx(20.5F));
    CHECK(a.position.z == doctest::Approx(30.5F));
    CHECK(a.position.y == doctest::Approx(render::bandSurface(4) + kLanternHeight));
    CHECK(b.position.y == doctest::Approx(render::bandSurface(4) + kFireHeight));
    // A fire is warmer (redder) and wavers more than a shielded lantern.
    CHECK(b.colour.z / b.colour.x < a.colour.z / a.colour.x);
    CHECK(b.flickerDepth > a.flickerDepth);
    CHECK(a.flicker != 0U);
    CHECK(a.flicker == pointLightOf(lantern).flicker);
    CHECK(a.flicker != b.flicker);
    // A lamp with no luminance is no light; the wind takes its share off a flame.
    render::Lamp dead = lantern;
    dead.luminance = 0;
    CHECK(pointLightsOf(nullptr, {dead, lantern}).size() == 1);
    CHECK(pointLightOf(lantern, 0.5F).peak == doctest::Approx(a.peak * 0.5F));
}

TEST_CASE("the sun and the moon keep the clock: east at sunrise, south at midday, west at sunset") {
    const Heavens dawn = heavensAt(6 * 3600 + 15 * 60);
    CHECK(dawn.sunElevation == doctest::Approx(0.0F).epsilon(0.01).scale(1.0));
    CHECK(dawn.sun.x > 0.99F);  // east
    const Heavens noon = heavensAt(13 * 3600 + 15 * 60);
    CHECK(noon.sunElevation > 0.7F);
    CHECK(noon.sun.z > 0.2F);  // from the south, a harbour on the north shore
    CHECK(std::abs(noon.sun.x) < 0.05F);
    const Heavens dusk = heavensAt(20 * 3600 + 15 * 60);
    CHECK(dusk.sun.x < -0.99F);  // west
    // Under the horizon all night; the moon is up for it, and the sun is not.
    const Heavens midnight = heavensAt(1 * 3600);
    CHECK(midnight.sunElevation < -0.3F);
    CHECK(midnight.moonElevation > 0.3F);
    // Every direction is a unit vector, and the day is a pure function of the clock.
    for (int hour = 0; hour < 24; ++hour) {
        const Heavens h = heavensAt(hour * 3600);
        CHECK(std::sqrt(h.sun.x * h.sun.x + h.sun.y * h.sun.y + h.sun.z * h.sun.z) == doctest::Approx(1.0F));
        CHECK(std::sqrt(h.moon.x * h.moon.x + h.moon.y * h.moon.y + h.moon.z * h.moon.z) == doctest::Approx(1.0F));
        CHECK(heavensAt(hour * 3600).sun.y == h.sun.y);
    }
    // And it wraps with the day.
    CHECK(heavensAt(8 * 3600).sun.x == doctest::Approx(heavensAt(8 * 3600 + 86400).sun.x));
}

TEST_CASE("one clock drives the sky: the light, the fog and the sky meet, and the night is dark") {
    SceneShading morning;
    SceneShading dusk;
    SceneShading night;
    atmosphereAt(morning, 8 * 3600, {});
    atmosphereAt(dusk, 19 * 3600, {});
    atmosphereAt(night, 22 * 3600, {});
    // The direct light: the sun by day, the moon after dark, far dimmer.
    CHECK(lumaOf(morning.sunColour) > 0.4F);
    CHECK(lumaOf(night.sunColour) < 0.5F * lumaOf(morning.sunColour));
    CHECK(morning.sunDirection.y > 0.0F);
    CHECK(night.sunDirection.y > 0.0F);
    // The low sun is redder than the high one.
    SceneShading noon;
    atmosphereAt(noon, 13 * 3600, {});
    CHECK(dusk.sunColour.z / std::max(dusk.sunColour.x, 1e-3F) < noon.sunColour.z / std::max(noon.sunColour.x, 1e-3F));
    // The fog is the horizon's colour, so the far street and the sky meet there.
    for (const SceneShading* s : {&morning, &dusk, &night}) {
        CHECK(s->fogColour.x == s->skyHorizon.x);
        CHECK(s->fogColour.y == s->skyHorizon.y);
        CHECK(s->fogColour.z == s->skyHorizon.z);
        CHECK(s->fogDensity > 0.0F);
        CHECK(s->lightMix == 1.0F);
        CHECK(s->skyShader);
    }
    // The harbour fog closes in at night, and the stars are out only in the dark.
    CHECK(night.fogDensity > morning.fogDensity);
    CHECK(night.stars > 0.9F);
    CHECK(morning.stars == 0.0F);
    // The weather is what the fog adds, and a fog is thicker than a clear day.
    SceneShading fog;
    atmosphereAt(fog, 8 * 3600, render::Weather{render::WeatherKind::Fog, 1.0F});
    CHECK(fog.fogDensity > 4.0F * morning.fogDensity);
    SceneShading overcast;
    atmosphereAt(overcast, 8 * 3600, render::Weather{render::WeatherKind::Overcast, 1.0F});
    CHECK(lumaOf(overcast.sunColour) < 0.4F * lumaOf(morning.sunColour));
    CHECK(overcast.cloudCover > morning.cloudCover);
    // Pure: the same moment is the same bytes.
    SceneShading again;
    atmosphereAt(again, 8 * 3600, {});
    CHECK(again.sunDirection.x == morning.sunDirection.x);
    CHECK(again.fogDensity == morning.fogDensity);
    CHECK(again.cloudClock == morning.cloudClock);
}

TEST_CASE("a surface near a lamp is lit by it, and the light dies at its radius") {
    SceneShading shading;
    atmosphereAt(shading, 22 * 3600, {});
    render::Lamp lamp;
    lamp.name = "lantern";
    lamp.x = 50;
    lamp.y = 50;
    lamp.z = 3;
    lamp.luminance = 20;
    const std::vector<ScenePointLight> lights = pointLightsOf(nullptr, {lamp});
    REQUIRE(lights.size() == 1);
    const Vec3 lampAt = lights[0].position;
    const Vec3 up{0.0F, 1.0F, 0.0F};
    const Vec3 nearGround{lampAt.x + 1.0F, render::bandSurface(3), lampAt.z};
    const Vec3 farGround{lampAt.x + 12.0F, render::bandSurface(3), lampAt.z};
    const Vec3 lit = litSurface(shading, lights, nearGround, up);
    const Vec3 dark = litSurface(shading, lights, farGround, up);
    const Vec3 darkNoLamp = litSurface(shading, {}, farGround, up);
    CHECK(lumaOf(lit) > 2.0F * lumaOf(dark));
    // Past its radius a lamp adds nothing at all.
    CHECK(dark.x == darkNoLamp.x);
    CHECK(dark.y == darkNoLamp.y);
    CHECK(dark.z == darkNoLamp.z);
    // A fire-lit surface leans warm: red over blue.
    CHECK(lit.x > lit.z);
    // A face turned from the lamp is lit less than one that looks at it.
    const Vec3 wall{lampAt.x + 2.0F, lampAt.y, lampAt.z};
    const Vec3 towards = litSurface(shading, lights, wall, Vec3{-1.0F, 0.0F, 0.0F});
    const Vec3 away = litSurface(shading, lights, wall, Vec3{1.0F, 0.0F, 0.0F});
    CHECK(lumaOf(towards) > lumaOf(away));
}

TEST_CASE("the world scene lit per pixel: lights, a sky of its own, the harbour apart, no hour in a vertex") {
    const SceneDescription morning = litScene(8 * 3600);
    const SceneDescription night = litScene(22 * 3600);
    // The lamps are lights, all of them with a radius.
    CHECK(morning.lights.size() >= docks().lamps.size());
    for (const ScenePointLight& light : morning.lights) {
        CHECK(light.radius > 0.0F);
        CHECK(light.peak > 0.0F);
    }
    // At night the lit windows add candles to the lamps.
    CHECK(night.lights.size() >= morning.lights.size());
    // The sky is the sky shader's, the ring has its own colours, and the
    // harbour is a mesh of its own for the water shader.
    int sky = 0;
    int ring = 0;
    int water = 0;
    for (const Instance& instance : morning.instances) {
        sky += instance.surface == kSurfaceSky ? 1 : 0;
        ring += instance.surface == kSurfaceFar ? 1 : 0;
        if (instance.surface == kSurfaceWater) {
            ++water;
            const MeshData* mesh = morning.findMesh(instance.meshId);
            REQUIRE(mesh != nullptr);
            CHECK(mesh->triangleCount() > 0);
            // Red is the shore distance over three tiles, never past one.
            for (std::size_t v = 0; v < mesh->vertexCount(); ++v) {
                CHECK(mesh->colours[v * 4 + 3] == 255);
            }
        }
    }
    CHECK(sky == 1);
    CHECK(ring == 1);
    CHECK(water > 3);
    // The vertex colour is what the surface is and nothing of the hour: every
    // chunk mesh is the same bytes at eight in the morning and at ten at night.
    int compared = 0;
    for (const MeshData& mesh : morning.meshes) {
        if (mesh.id < kChunkMeshIdBase || mesh.id >= kActorMeshIdBase || mesh.id == kSkylineMeshId) {
            continue;
        }
        const MeshData* other = night.findMesh(mesh.id);
        if (other == nullptr) {
            continue;
        }
        CHECK(mesh.version == other->version);
        CHECK(mesh.colours == other->colours);
        ++compared;
    }
    CHECK(compared > 20);
    // And a room is a room: some faces are closed off from the sky (alpha 0).
    bool anyIndoor = false;
    bool anyOpen = false;
    for (const MeshData& mesh : morning.meshes) {
        if (mesh.id < kChunkMeshIdBase || mesh.id >= 20000 || mesh.colours.empty()) {
            continue;
        }
        for (std::size_t v = 0; v < mesh.vertexCount(); ++v) {
            anyIndoor = anyIndoor || mesh.colours[v * 4 + 3] == 0;
            anyOpen = anyOpen || mesh.colours[v * 4 + 3] == 255;
        }
    }
    CHECK(anyIndoor);
    CHECK(anyOpen);
}

TEST_CASE("the fog is a shader, not a veil, and the lit description is a pure function of the clock") {
    const render::Weather fog{render::WeatherKind::Fog, 1.0F};
    const SceneDescription foggy = litScene(8 * 3600, fog);
    CHECK(foggy.veils.empty());
    CHECK(foggy.findMesh(kVeilMeshId) == nullptr);
    CHECK(foggy.shading.fogDensity > 0.1F);
    // The legacy path still draws its veil from the same weather.
    {
        WorldScene& world = docksWorld();
        WorldSceneParams legacy;
        legacy.timeOfDaySeconds = 8 * 3600;
        legacy.weather = fog;
        SceneDescription scene;
        world.refresh(scene, spawnEye(), 1.78F, legacy);
        CHECK_FALSE(scene.veils.empty());
        CHECK(scene.shading.lightMix == 0.0F);
        CHECK(scene.lights.empty());
        // And the two looks are two descriptions.
        CHECK(sceneHash(scene) != sceneHash(foggy));
    }
    // Twice is the same bytes; another minute is not.
    CHECK(sceneHash(litScene(8 * 3600)) == sceneHash(litScene(8 * 3600)));
    CHECK(sceneHash(litScene(8 * 3600)) != sceneHash(litScene(8 * 3600 + 60 * 12)));
    CHECK(sceneHash(litScene(8 * 3600)) != sceneHash(litScene(22 * 3600)));
    // The render clock (the flicker's) is part of the picture.
    WorldScene& world = docksWorld();
    WorldSceneParams params = litParams(22 * 3600);
    SceneDescription a;
    SceneDescription b;
    world.refresh(a, spawnEye(), 1.78F, params);
    params.renderClock = 3.5F;
    world.refresh(b, spawnEye(), 1.78F, params);
    CHECK(sceneHash(a) != sceneHash(b));
}

TEST_CASE("lit frames: the day, the dusk and the night, headless on llvmpipe") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build is not on raylib's SDL platform, so it has no headless context");
        return;
    }
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    const render::Framebuffer morning = drawOnce(*video, litScene(8 * 3600));
    const render::Framebuffer dusk = drawOnce(*video, litScene(19 * 3600));
    const render::Framebuffer night = drawOnce(*video, litScene(22 * 3600));
    // Brightest by day, darkest at night, the dusk between.
    const double day = meanLuma(morning);
    const double eve = meanLuma(dusk);
    const double dark = meanLuma(night);
    MESSAGE("mean luma: morning " << day << ", dusk " << eve << ", night " << dark);
    CHECK(day > eve);
    CHECK(eve > dark);
    CHECK(dark > 3.0);  // not black: the lamps, the moon, the stars
    CHECK(day < 220.0);
    // The same description is the same frame twice, byte for byte.
    const SceneDescription again = litScene(22 * 3600);
    CHECK(drawOnce(*video, again).pixels() == drawOnce(*video, again).pixels());
    // The goldens, by tolerance.
    for (const auto& [frame, name] : {std::pair<const render::Framebuffer*, const char*>{&morning, "lit-morning"},
                                      {&dusk, "lit-dusk"},
                                      {&night, "lit-night"}}) {
        const testing::GoldenReport report = testing::compareWithGolden(*frame, name);
        MESSAGE(report.why);
        CHECK_MESSAGE(report.ok, report.why);
    }
}

TEST_CASE("the lit frame's budget on llvmpipe, written down") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build is not on raylib's SDL platform, so it has no headless context");
        return;
    }
    // OBLIVION-PLAN section 9, phase 5: "the budget is measured and written down
    // in the PR". One frame at the capture size, drawn after a warm-up, legacy
    // (vertex colour) against lit (lights, shadow map, sky, water, bloom). The
    // numbers are a message, not an assertion: a CPU rasterizer's speed says
    // nothing about a GPU's (OBLIVION-PLAN 10.1, item 2).
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    SceneDescription lit = litScene(22 * 3600);
    SceneDescription flat = lit;
    flat.shading = SceneShading{};
    flat.lights.clear();
    for (Instance& instance : flat.instances) {
        instance.surface = kSurfaceLit;
    }
    const auto timeOf = [&](const SceneDescription& scene) {
        (void)drawOnce(*video, scene);  // warm-up: uploads, shader compile
        const auto start = std::chrono::steady_clock::now();
        constexpr int kFrames = 5;
        for (int i = 0; i < kFrames; ++i) {
            (void)drawOnce(*video, scene);
        }
        const auto stop = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(stop - start).count() / kFrames;
    };
    const double flatMs = timeOf(flat);
    const double litMs = timeOf(lit);
    MESSAGE("per frame at " << kWidth << "x" << kHeight << ": vertex-colour " << flatMs << " ms, lit " << litMs
                            << " ms, " << lit.lights.size() << " lights");
    CHECK(litMs > 0.0);
}

TEST_CASE("shadows are in the lit frame, and switching them off changes it") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build is not on raylib's SDL platform, so it has no headless context");
        return;
    }
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    SceneDescription scene = litScene(10 * 3600);
    REQUIRE(scene.shading.shadowStrength > 0.0F);
    const render::Framebuffer shadowed = drawOnce(*video, scene);
    scene.shading.shadowStrength = 0.0F;
    const render::Framebuffer unshadowed = drawOnce(*video, scene);
    REQUIRE(shadowed.pixels().size() == unshadowed.pixels().size());
    std::size_t darker = 0;
    std::size_t brighter = 0;
    for (std::size_t i = 0; i < shadowed.pixels().size(); ++i) {
        const std::uint32_t a = shadowed.pixels()[i];
        const std::uint32_t b = unshadowed.pixels()[i];
        const int la = static_cast<int>(a & 0xFFU) + static_cast<int>((a >> 8) & 0xFFU) + static_cast<int>((a >> 16) & 0xFFU);
        const int lb = static_cast<int>(b & 0xFFU) + static_cast<int>((b >> 8) & 0xFFU) + static_cast<int>((b >> 16) & 0xFFU);
        darker += la < lb - 12 ? 1 : 0;
        brighter += la > lb + 12 ? 1 : 0;
    }
    MESSAGE("shadowed darker: " << darker << ", lighter: " << brighter);
    // A shadow only ever takes light away: a real share of the frame went darker.
    CHECK(darker > shadowed.pixels().size() / 100);
    CHECK(brighter < darker / 20 + 10);
}

TEST_CASE("the lit pass is the legacy pass when the description asks for nothing") {
    if (!Backend::headlessCapable()) {
        MESSAGE("skipped: this build is not on raylib's SDL platform, so it has no headless context");
        return;
    }
    std::unique_ptr<Backend> video = Backend::open(headlessConfig());
    REQUIRE(video != nullptr);
    // With the shading at its defaults and no lights, the lit shaders return
    // texture x tint x vertex colour: --flat-light is the frame it always was.
    SceneDescription scene;
    scene.clearColour = Rgba8{40, 60, 90, 255};
    const render::Framebuffer frame = drawOnce(*video, scene);
    const std::uint32_t px = frame.pixels()[0];
    CHECK((px & 0xFFU) == 40);
    CHECK(((px >> 8) & 0xFFU) == 60);
    CHECK(((px >> 16) & 0xFFU) == 90);
}
