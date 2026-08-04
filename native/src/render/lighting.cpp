#include "granadad/render/lighting.hpp"

#include <algorithm>
#include <cmath>

namespace granadad::render {

namespace {

/// The two authored warmths, as colours.
constexpr Rgb kFireColour{1.00F, 0.58F, 0.24F};
constexpr Rgb kLanternColour{1.00F, 0.82F, 0.58F};

/// A lamp lights the level it sits on and its immediate neighbours; vertical
/// distance counts for more than horizontal so a lamp does not shine through
/// two floors.
constexpr float kVerticalWeight = 2.2F;
constexpr std::int32_t kVerticalReach = 2;

[[nodiscard]] float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

}  // namespace

SkyState skyAt(int timeOfDaySeconds) {
    const int wrapped = ((timeOfDaySeconds % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;
    const float hour = static_cast<float>(wrapped) / 3600.0F;

    // Dawn 05:00-07:30, dusk 19:00-21:30. Between them, daylight; outside,
    // night. A cosine would be prettier and this is easier to reason about
    // when someone asks why a screenshot looks the way it does.
    float daylight = 0.0F;
    if (hour < 5.0F || hour >= 21.5F) {
        daylight = 0.0F;
    } else if (hour < 7.5F) {
        daylight = smoothstep(5.0F, 7.5F, hour);
    } else if (hour < 19.0F) {
        daylight = 1.0F;
    } else {
        daylight = 1.0F - smoothstep(19.0F, 21.5F, hour);
    }

    SkyState sky;
    sky.daylight = daylight;

    // Committed dark. Night is a cold near-black; day is overcast harbour
    // light, never a bright blue sky — this is a fogged working port on the
    // north shore, and the art register is luminous-on-black.
    const Rgb nightAmbient{0.050F, 0.058F, 0.086F};
    const Rgb dayAmbient{0.62F, 0.63F, 0.60F};
    sky.ambient = lerp(nightAmbient, dayAmbient, daylight);

    const Rgb nightTop{0.014F, 0.017F, 0.030F};
    const Rgb dayTop{0.44F, 0.50F, 0.56F};
    sky.skyTop = lerp(nightTop, dayTop, daylight);

    const Rgb nightHorizon{0.055F, 0.048F, 0.062F};
    const Rgb dayHorizon{0.66F, 0.68F, 0.68F};
    sky.skyHorizon = lerp(nightHorizon, dayHorizon, daylight);

    // A dusk band gets a sodium wash along the horizon, which is what sells
    // "harbour at last light" more than any amount of geometry.
    const float duskness = (hour >= 17.5F && hour < 21.5F)
                               ? smoothstep(17.5F, 19.5F, hour) *
                                     (1.0F - smoothstep(20.0F, 21.5F, hour))
                               : ((hour >= 4.5F && hour < 8.0F)
                                      ? smoothstep(4.5F, 6.0F, hour) *
                                            (1.0F - smoothstep(6.5F, 8.0F, hour))
                                      : 0.0F);
    sky.skyHorizon = lerp(sky.skyHorizon, Rgb{0.42F, 0.24F, 0.15F}, duskness * 0.75F);

    sky.fog = lerp(sky.skyHorizon * 0.75F, sky.skyHorizon, 0.5F);
    // Fog closes in at night: the district should swallow its own far end.
    sky.fogDistance = 14.0F + 20.0F * daylight;
    return sky;
}

LampGlow LampGlow::build(const sim::TileQuery& tiles, const std::vector<Lamp>& lamps) {
    LampGlow glow;
    glow.sizeX_ = tiles.sizeX();
    glow.sizeY_ = tiles.sizeY();
    glow.sizeZ_ = tiles.sizeZ();
    const std::size_t cells = static_cast<std::size_t>(glow.sizeX_) *
                              static_cast<std::size_t>(glow.sizeY_) *
                              static_cast<std::size_t>(glow.sizeZ_);
    glow.field_.assign(cells, 0U);
    if (lamps.empty()) {
        return glow;
    }

    // Accumulate in float, then pack. Saturating union on intensity with a
    // weighted-average colour, which is the Java's rule: two lamps overlapping
    // do not add up to white, they pool.
    std::vector<float> intensity(cells, 0.0F);
    std::vector<float> red(cells, 0.0F);
    std::vector<float> green(cells, 0.0F);
    std::vector<float> blue(cells, 0.0F);

    for (const Lamp& lamp : lamps) {
        const float lum = static_cast<float>(lamp.luminance);
        const float radius = std::clamp(4.0F + (lum - 8.0F) / 12.0F, 3.5F, 5.5F);
        const float peak = 0.55F + 0.45F * lum / 26.0F;
        const Rgb tint = lamp.warmth == LampWarmth::Fire ? kFireColour : kLanternColour;
        const std::int32_t reach = static_cast<std::int32_t>(std::ceil(radius));

        for (std::int32_t dz = -kVerticalReach; dz <= kVerticalReach; ++dz) {
            const std::int32_t z = lamp.z + dz;
            if (z < 0 || z >= glow.sizeZ_) {
                continue;
            }
            for (std::int32_t dy = -reach; dy <= reach; ++dy) {
                const std::int32_t y = lamp.y + dy;
                if (y < 0 || y >= glow.sizeY_) {
                    continue;
                }
                for (std::int32_t dx = -reach; dx <= reach; ++dx) {
                    const std::int32_t x = lamp.x + dx;
                    if (x < 0 || x >= glow.sizeX_) {
                        continue;
                    }
                    const float fx = static_cast<float>(dx);
                    const float fy = static_cast<float>(dy);
                    const float fz = static_cast<float>(dz) * kVerticalWeight;
                    const float distance = std::sqrt(fx * fx + fy * fy + fz * fz);
                    if (distance >= radius) {
                        continue;
                    }
                    const float fall = 1.0F - (distance * distance) / (radius * radius);
                    const float value = peak * fall * fall;
                    if (value <= 0.002F) {
                        continue;
                    }
                    const std::size_t at =
                        (static_cast<std::size_t>(z) * static_cast<std::size_t>(glow.sizeY_) +
                         static_cast<std::size_t>(y)) *
                            static_cast<std::size_t>(glow.sizeX_) +
                        static_cast<std::size_t>(x);
                    red[at] += tint.r * value;
                    green[at] += tint.g * value;
                    blue[at] += tint.b * value;
                    intensity[at] = std::max(intensity[at], value);
                }
            }
        }
    }

    for (std::size_t at = 0; at < cells; ++at) {
        if (intensity[at] <= 0.0F) {
            continue;
        }
        const float weight = red[at] + green[at] + blue[at];
        if (weight <= 0.0F) {
            continue;
        }
        // Normalise the summed colour back to a hue, then scale by the
        // saturating intensity.
        const float norm = 3.0F / weight;
        const Rgb colour{red[at] * norm * intensity[at], green[at] * norm * intensity[at],
                         blue[at] * norm * intensity[at]};
        glow.field_[at] = packRgb(colour);
        ++glow.litCells_;
    }
    return glow;
}

Rgb LampGlow::at(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept {
    if (field_.empty() || x < 0 || y < 0 || z < 0 || x >= sizeX_ || y >= sizeY_ || z >= sizeZ_) {
        return Rgb{};
    }
    const std::size_t index =
        (static_cast<std::size_t>(z) * static_cast<std::size_t>(sizeY_) +
         static_cast<std::size_t>(y)) *
            static_cast<std::size_t>(sizeX_) +
        static_cast<std::size_t>(x);
    const std::uint32_t packed = field_[index];
    if ((packed & 0x00FFFFFFU) == 0U) {
        return Rgb{};
    }
    return unpackRgb(packed);
}

}  // namespace granadad::render
