#include "granadad/render3d/light_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "granadad/render/vertical.hpp"
#include "granadad/sim/stealth.hpp"

namespace granadad::render3d {

namespace {

constexpr float kPi = 3.14159265358979323846F;

/// Sixteenths of a tile, the sim's radius unit.
constexpr float kRadiusUnitF = 16.0F;

/// The hours the sun and the moon keep (see Heavens).
constexpr float kSunrise = 6.25F;
constexpr float kSunset = 20.25F;
constexpr float kSunMaxElevation = 55.0F * kPi / 180.0F;
constexpr float kMoonMaxElevation = 48.0F * kPi / 180.0F;

[[nodiscard]] float smooth(float a, float b, float x) noexcept {
    const float t = std::clamp((x - a) / (b - a), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] Vec3 mixV(const Vec3& a, const Vec3& b, float t) noexcept {
    return Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

[[nodiscard]] Vec3 scaleV(const Vec3& a, float k) noexcept { return Vec3{a.x * k, a.y * k, a.z * k}; }

[[nodiscard]] Vec3 rgbV(const render::Rgb& c) noexcept { return Vec3{c.r, c.g, c.b}; }

[[nodiscard]] float luma(const Vec3& c) noexcept { return 0.30F * c.x + 0.59F * c.y + 0.11F * c.z; }

[[nodiscard]] std::uint32_t hashLamp(const render::Lamp& lamp) noexcept {
    std::uint32_t h = 2166136261U;
    const auto mix = [&h](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (8 * i)) & 0xFFU;
            h *= 16777619U;
        }
    };
    for (const char c : lamp.name) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619U;
    }
    mix(static_cast<std::uint32_t>(lamp.x));
    mix(static_cast<std::uint32_t>(lamp.y));
    mix(static_cast<std::uint32_t>(lamp.z));
    return h == 0U ? 1U : h;
}

}  // namespace

float lampFalloff(float distance, float radius, float peak) noexcept {
    if (!(radius > 0.0F) || distance >= radius) {
        return 0.0F;
    }
    const float t = distance / radius;
    const float k = 1.0F - t * t;
    return peak * k * k;
}

ScenePointLight pointLightOf(const render::Lamp& lamp, float gutter) noexcept {
    ScenePointLight light;
    const bool fire = lamp.warmth == render::LampWarmth::Fire;
    light.position = toScene(static_cast<float>(lamp.x) + 0.5F, static_cast<float>(lamp.y) + 0.5F,
                             render::bandSurface(lamp.z) + (fire ? kFireHeight : kLanternHeight));
    light.radius =
        static_cast<float>(sim::lightRadiusQ4(lamp.luminance)) / kRadiusUnitF;
    light.peak = static_cast<float>(sim::lightPeak(lamp.luminance)) / 100.0F * gutter;
    // An open flame is ember orange; a shielded lantern burns whiter.
    light.colour = fire ? Vec3{1.0F, 0.50F, 0.18F} : Vec3{1.0F, 0.74F, 0.42F};
    light.flicker = hashLamp(lamp);
    light.flickerDepth = fire ? 0.16F : 0.05F;
    return light;
}

std::vector<ScenePointLight> pointLightsOf(const std::vector<render::Lamp>* baked,
                                           const std::vector<render::Lamp>& live, float gutter) {
    std::vector<ScenePointLight> out;
    out.reserve((baked != nullptr ? baked->size() : 0U) + live.size());
    if (baked != nullptr) {
        for (const render::Lamp& lamp : *baked) {
            if (lamp.luminance > 0) {
                out.push_back(pointLightOf(lamp, gutter));
            }
        }
    }
    for (const render::Lamp& lamp : live) {
        if (lamp.luminance > 0) {
            out.push_back(pointLightOf(lamp, gutter));
        }
    }
    return out;
}

Heavens heavensAt(int timeOfDaySeconds) noexcept {
    const int wrapped =
        ((timeOfDaySeconds % render::kSecondsPerDay) + render::kSecondsPerDay) % render::kSecondsPerDay;
    const float hour = static_cast<float>(wrapped) / 3600.0F;
    Heavens out;
    // The sun's road: a half turn from the east at sunrise through the
    // south to the west at sunset. Outside the day the same sine is negative,
    // which is the sun under the horizon, where it is.
    {
        const float a = kPi * (hour - kSunrise) / (kSunset - kSunrise);
        const float elevation = kSunMaxElevation * std::sin(a);
        const float cosE = std::cos(elevation);
        out.sun = Vec3{std::cos(a) * cosE, std::sin(elevation), std::sin(a) * cosE};
        out.sunElevation = std::sin(elevation);
    }
    // The moon keeps the night, the same road in the ten hours that are left.
    {
        float since = hour - kSunset;
        if (since < 0.0F) {
            since += 24.0F;
        }
        const float a = kPi * since / (24.0F - (kSunset - kSunrise));
        const float elevation = kMoonMaxElevation * std::sin(a);
        const float cosE = std::cos(elevation);
        out.moon = Vec3{std::cos(a) * cosE, std::sin(elevation), std::sin(a) * cosE};
        out.moonElevation = std::sin(elevation);
    }
    return out;
}

void atmosphereAt(SceneShading& shading, int timeOfDaySeconds, const render::Weather& weather) {
    const render::SkyState sky = render::skyAt(timeOfDaySeconds, weather);
    const Heavens sky3 = heavensAt(timeOfDaySeconds);
    const float day = sky.daylight;
    const float night = 1.0F - day;
    const float i = std::clamp(weather.intensity, 0.0F, 1.0F);
    const bool overcast = weather.kind == render::WeatherKind::Overcast;
    const bool fog = weather.kind == render::WeatherKind::Fog;
    // How much direct light the weather lets through.
    const float sunShare = 1.0F - (overcast ? 0.78F * i : 0.0F) - (fog ? 0.55F * i : 0.0F);

    // ---- the direct light: the sun, or the moon when the sun is down ------
    const float sunHeight = std::max(sky3.sunElevation, 0.0F);
    const float sunStrength = smooth(0.0F, 0.30F, sunHeight) * (0.25F + 0.75F * day) * sunShare;
    const Vec3 lowSun{1.0F, 0.58F, 0.30F};
    const Vec3 highSun{1.0F, 0.94F, 0.82F};
    const Vec3 sunTint = mixV(lowSun, highSun, smooth(0.08F, 0.55F, sunHeight));
    const Vec3 sunLight = scaleV(sunTint, 0.95F * sunStrength);
    const float moonHeight = std::max(sky3.moonElevation, 0.0F);
    const float moonStrength = smooth(0.0F, 0.25F, moonHeight) * night * night * (1.0F - 0.6F * i);
    const Vec3 moonLight = scaleV(Vec3{0.50F, 0.62F, 1.0F}, 0.34F * moonStrength);
    if (luma(sunLight) >= luma(moonLight) && sunStrength > 0.0F) {
        shading.sunDirection = sky3.sun;
        shading.sunColour = sunLight;
    } else if (moonStrength > 0.0F) {
        shading.sunDirection = sky3.moon;
        shading.sunColour = moonLight;
    } else {
        shading.sunDirection = Vec3{0.0F, 1.0F, 0.0F};
        shading.sunColour = Vec3{0.0F, 0.0F, 0.0F};
    }
    shading.lightMix = 1.0F;

    // ---- the hemisphere: the sky's own colour above, the ground's below ---
    const Vec3 ambient = rgbV(sky.ambient);
    const float up = 0.45F + 1.50F * night;
    shading.ambientColour = mixV(scaleV(ambient, up), rgbV(sky.skyTop), 0.18F);
    shading.ambientGround = mixV(scaleV(ambient, 0.30F + 0.10F * night), Vec3{0.30F, 0.24F, 0.18F}, 0.10F * day);

    // ---- fog: the clear day's haze, a third of the software pass's, plus
    //      what the weather adds; the colour is the horizon's -----------------
    const float total = 1.0F / std::max(sky.fogDistance, 0.01F);
    const float clearPart = std::max(0.0F, total - sky.veil);
    shading.fogDensity = 0.33F * clearPart + sky.veil;
    shading.fogColour = rgbV(sky.skyHorizon);

    // ---- the lamps, the flicker's clock is the caller's ---------------------
    shading.lightGain = 1.15F;

    // ---- shadows: cosmetic, soft, only while a light is up ------------------
    shading.shadowStrength = luma(shading.sunColour) > 0.02F ? 0.82F : 0.0F;
    shading.shadowReach = 48.0F;

    // ---- the sky -----------------------------------------------------------
    shading.skyShader = true;
    shading.skyZenith = rgbV(sky.skyTop);
    shading.skyHorizon = rgbV(sky.skyHorizon);
    shading.sunSkyDirection = sky3.sun;
    shading.sunDisc = sky3.sunElevation > -0.05F
                          ? scaleV(mixV(Vec3{1.0F, 0.50F, 0.22F}, Vec3{1.0F, 0.96F, 0.86F},
                                        smooth(0.05F, 0.5F, sunHeight)),
                                   2.4F * std::max(sunShare, 0.0F) * (0.3F + 0.7F * smooth(-0.05F, 0.1F, sky3.sunElevation)))
                          : Vec3{0.0F, 0.0F, 0.0F};
    shading.moonDirection = sky3.moon;
    shading.moonDisc = sky3.moonElevation > -0.05F
                           ? scaleV(Vec3{0.80F, 0.88F, 1.0F}, 1.5F * night * (1.0F - 0.8F * i))
                           : Vec3{0.0F, 0.0F, 0.0F};
    shading.stars = night * night * (1.0F - 0.9F * i);
    const float cover = 0.30F + (overcast ? 0.62F * i : 0.0F) + (fog ? 0.5F * i : 0.0F);
    shading.cloudCover = std::clamp(cover, 0.0F, 1.0F);
    shading.cloudColour =
        mixV(scaleV(rgbV(sky.skyHorizon), 1.15F), scaleV(Vec3{1.0F, 0.95F, 0.90F}, 0.55F + 0.45F * day), 0.4F);
    shading.cloudClock = static_cast<float>(((timeOfDaySeconds % render::kSecondsPerDay) + render::kSecondsPerDay) %
                                            render::kSecondsPerDay);

    // ---- the harbour -------------------------------------------------------
    shading.waterShallow = Vec3{0.10F, 0.20F, 0.23F};
    shading.waterDeep = Vec3{0.015F, 0.045F, 0.07F};
    shading.waterClock = shading.cloudClock;

    // ---- post: the eye's adaptation, a shoulder, a bloom, a grade ------------
    shading.exposure = 1.0F + 0.45F * night;
    shading.toneCurve = 1.0F;
    shading.bloom = 0.30F + 0.30F * night;
    shading.bloomThreshold = 0.72F;
    shading.vignette = 0.22F;
    shading.saturation = 1.0F - 0.18F * night;
    shading.contrast = 1.06F;
    shading.gradeTint = mixV(Vec3{1.0F, 0.97F, 0.92F}, Vec3{0.86F, 0.95F, 1.10F}, night);
}

Vec3 litSurface(const SceneShading& shading, const std::vector<ScenePointLight>& lights,
                const Vec3& position, const Vec3& normal) noexcept {
    const float hemi = std::clamp(normal.y * 0.5F + 0.5F, 0.0F, 1.0F);
    const Vec3 ground = shading.ambientGround.x < 0.0F ? shading.ambientColour : shading.ambientGround;
    Vec3 total = mixV(ground, shading.ambientColour, hemi);
    const float sunDot = std::max(normal.x * shading.sunDirection.x + normal.y * shading.sunDirection.y +
                                      normal.z * shading.sunDirection.z,
                                  0.0F);
    total.x += shading.sunColour.x * sunDot;
    total.y += shading.sunColour.y * sunDot;
    total.z += shading.sunColour.z * sunDot;
    for (const ScenePointLight& light : lights) {
        const Vec3 d{light.position.x - position.x, light.position.y - position.y,
                     light.position.z - position.z};
        const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const float fall = lampFalloff(dist, light.radius, light.peak);
        if (fall <= 0.0F) {
            continue;
        }
        const float inv = dist > 1e-4F ? 1.0F / dist : 0.0F;
        const float facing =
            std::max(normal.x * d.x * inv + normal.y * d.y * inv + normal.z * d.z * inv, 0.0F);
        const float k = shading.lightGain * fall * (0.25F + 0.75F * facing);
        total.x += light.colour.x * k;
        total.y += light.colour.y * k;
        total.z += light.colour.z * k;
    }
    return total;
}

}  // namespace granadad::render3d
