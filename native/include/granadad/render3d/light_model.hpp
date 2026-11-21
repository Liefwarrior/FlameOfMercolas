#pragma once

// THE LIGHT MODEL OF THE 3D PASS -- lighting and atmosphere, phase 5 of
// docs/design/OBLIVION-PLAN.md, as pure C++.
//
// The GPU only draws. Everything here is a pure function of the clock, the
// weather and the lamp list, written into the SceneDescription (scene.hpp:
// SceneShading and ScenePointLight) and hashed with the rest; the adapter
// (render3d/rl/) reads it and evaluates the same functions per pixel. Nothing
// here reads a pixel back, and nothing in sim/ reads anything from here.
//
//   lampFalloff      the one curve a lamp is lit by: peak * (1 - (d/R)^2)^2.
//                    The simulation's integer light field (sim/stealth.cpp,
//                    glowFrom) evaluates the same expression in integers, the
//                    shader evaluates it per pixel, and test_light_model.cpp
//                    pins the three to one another at a grid of points. What
//                    the player sees lit and what stealth reads are one idea.
//   pointLightsOf    the 27 baked lamps and the tavern's live ones as lights.
//   atmosphereAt     one clock, one sky: sun and moon from the hour, the
//                    hemisphere ambient, sky, fog, water, the post grade.
//   litSurface       the shader's diffuse term in C++ (no shadow map), for the
//                    first-person hands and for the tests.
//
// SHADOWS ARE COSMETIC (owner's ruling 9, 2026-11-13): the sun or moon casts
// into a map the adapter draws and a dark patch under an eave is not cover.
// The integer light field decides who sees whom, always.

#include <vector>

#include "granadad/render/lamps.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render3d/scene.hpp"

namespace granadad::render3d {

/// `peak * (1 - (distance / radius)^2)^2`, and zero at or past the radius
/// (and for a radius that is not over zero). The shader's lamp term.
[[nodiscard]] float lampFalloff(float distance, float radius, float peak) noexcept;

/// How high a lamp hangs over its band's floor, in tiles: a lantern at head
/// height and a bit, an open fire (brazier, hearth, torch in a basket) low.
inline constexpr float kLanternHeight = 2.2F;
inline constexpr float kFireHeight = 0.9F;

/// One lamp as a point light: the tile's centre at its hanging height, the
/// radius and peak the sim's own curve gives the authored luminance
/// (sim::lightRadiusQ4, sim::lightPeak), the colour of its warmth, and a
/// flicker seed hashed from its identity. `gutter` is the weather's share
/// off a flame in the wind (SkyState::haloScale).
[[nodiscard]] ScenePointLight pointLightOf(const render::Lamp& lamp, float gutter = 1.0F) noexcept;

/// The frame's lights: the baked lamps (may be null), then the live ones.
[[nodiscard]] std::vector<ScenePointLight> pointLightsOf(const std::vector<render::Lamp>* baked,
                                                         const std::vector<render::Lamp>& live,
                                                         float gutter = 1.0F);

/// The sun's and the moon's direction (unit, toward the body, scene space:
/// east +X, up +Y, south +Z) for a time of day, and how far above the horizon
/// each is as the sine of its elevation (negative below it). The sun rises in
/// the east at about 06:15, stands in the south at 13:15 and sets in the west
/// at 20:15, a harbour on the north shore; the moon keeps the night, from
/// 20:15 to 06:15, the same road.
struct Heavens {
    Vec3 sun;
    float sunElevation = 0.0F;
    Vec3 moon;
    float moonElevation = 0.0F;
};
[[nodiscard]] Heavens heavensAt(int timeOfDaySeconds) noexcept;

/// Everything the scene shader, the sky, the water and the post pass read for
/// a moment of the day under a weather: written over `shading` (the lights
/// themselves are separate). Distance fog is the clear day's own haze (a third
/// of the software pass's, whose district is a few tiles deep) plus what the
/// weather adds, mixed toward the colour of the sky at the horizon so the two
/// meet there.
void atmosphereAt(SceneShading& shading, int timeOfDaySeconds, const render::Weather& weather);

/// The diffuse light of a surface at `position` with unit `normal`, as the
/// scene shader computes it with no shadow: hemisphere ambient, the sun or
/// moon by Lambert, and every light in range by its own falloff and a wrapped
/// Lambert. Flicker is left out (it is the shader's, by the render clock).
[[nodiscard]] Vec3 litSurface(const SceneShading& shading, const std::vector<ScenePointLight>& lights,
                              const Vec3& position, const Vec3& normal) noexcept;

}  // namespace granadad::render3d
