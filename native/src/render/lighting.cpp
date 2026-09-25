#include "granadad/render/lighting.hpp"

#include <algorithm>
#include <cmath>

#include "granadad/sim/vertical_scale.hpp"

namespace granadad::render {

namespace {

/// The two authored warmths, as colours.
constexpr Rgb kFireColour{1.00F, 0.58F, 0.24F};
constexpr Rgb kLanternColour{1.00F, 0.82F, 0.58F};

/// A lamp lights the level it sits on and its immediate neighbours; vertical
/// distance counts for more than horizontal so a lamp does not shine through
/// two floors.
///
/// THE WEIGHT IS NOW THE TRUTH RATHER THAN A FUDGE. It was 2.2 — a made-up
/// number whose only job was to stop a lamp bleeding through a ceiling back
/// when a level was drawn one tile tall and honest geometry would have said
/// 1.0. A level is three tiles now (sim/vertical_scale.hpp), so the distance
/// from a lamp to the cell above it really is three tiles, and saying so is
/// both simpler and stricter than the fudge was.
///
/// What it costs: the glow no longer carries two levels. A lamp's own falloff
/// radius tops out at 5.5 tiles (falloffOf), so dz = 2 is 6.0 tiles away and
/// drops out entirely, and dz = 1 keeps about 4.6 tiles of horizontal spread.
/// That is the right answer — a lantern on the quay should light the wall it
/// hangs on and the first floor above it, and should NOT light the roof slum
/// two storeys up. kVerticalReach stays at 2 because it is the loop bound the
/// falloff is evaluated inside, not a claim that the light gets there.
constexpr float kVerticalWeight = static_cast<float>(sim::kTilesPerBand);
constexpr std::int32_t kVerticalReach = 2;

[[nodiscard]] float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

/// Radius and peak from an authored luminance. ONE definition, used by the
/// baked field and by the per-cell dynamic path, so a hearth and a street
/// lantern of equal luminance cannot light a room differently.
struct LampFalloff {
    float radius;
    float peak;
};

[[nodiscard]] LampFalloff falloffOf(std::int32_t luminance) noexcept {
    const float lum = static_cast<float>(luminance);
    return LampFalloff{std::clamp(4.0F + (lum - 8.0F) / 12.0F, 3.5F, 5.5F),
                       0.55F + 0.45F * lum / 26.0F};
}

[[nodiscard]] Rgb tintOf(LampWarmth warmth) noexcept {
    return warmth == LampWarmth::Fire ? kFireColour : kLanternColour;
}

}  // namespace

Rgb dynamicGlowAt(const std::vector<Lamp>& lamps, std::int32_t x, std::int32_t y,
                  std::int32_t z) noexcept {
    if (lamps.empty()) {
        return Rgb{};
    }
    float intensity = 0.0F;
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    for (const Lamp& lamp : lamps) {
        const std::int32_t dz = z - lamp.z;
        if (dz < -kVerticalReach || dz > kVerticalReach) {
            continue;
        }
        const LampFalloff shape = falloffOf(lamp.luminance);
        const float fx = static_cast<float>(x - lamp.x);
        const float fy = static_cast<float>(y - lamp.y);
        const float fz = static_cast<float>(dz) * kVerticalWeight;
        const float distance = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (distance >= shape.radius) {
            continue;
        }
        const float fall = 1.0F - (distance * distance) / (shape.radius * shape.radius);
        const float value = shape.peak * fall * fall;
        if (value <= 0.002F) {
            continue;
        }
        const Rgb tint = tintOf(lamp.warmth);
        red += tint.r * value;
        green += tint.g * value;
        blue += tint.b * value;
        // Saturating union, same as the baked field: two lamps overlapping pool
        // rather than adding up to white.
        intensity = std::max(intensity, value);
    }
    const float weight = red + green + blue;
    if (intensity <= 0.0F || weight <= 0.0F) {
        return Rgb{};
    }
    const float norm = 3.0F / weight;
    return Rgb{red * norm * intensity, green * norm * intensity, blue * norm * intensity};
}

// ---------------------------------------------------------------------------
// weather
// ---------------------------------------------------------------------------

namespace {

/// splitmix64's finaliser: the one integer mixer this file draws with. Every
/// draw is this over (seed, day, salt), so the same three numbers give the
/// same draw on every machine -- there is no stream and no position.
[[nodiscard]] constexpr std::uint64_t splitmix(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

[[nodiscard]] constexpr std::uint64_t weatherDraw(std::uint64_t seed, std::int32_t day,
                                                  std::uint32_t salt) noexcept {
    const auto d = static_cast<std::uint64_t>(static_cast<std::int64_t>(day));
    return splitmix(seed + d * 0x9E3779B97F4A7C15ULL +
                    static_cast<std::uint64_t>(salt) * 0xD1B54A32D192ED03ULL);
}

/// The salts, named. A period's own draws are salted by its index too.
constexpr std::uint32_t kSaltPeriodCount = 1;
constexpr std::uint32_t kSaltPeriodEdge = 16;
constexpr std::uint32_t kSaltPeriodKind = 32;
constexpr std::uint32_t kSaltPeriodPeak = 48;

constexpr std::int32_t kMinutesPerDay = 1440;
/// A period edge lands within an hour either side of its even split.
constexpr std::int32_t kEdgeJitterMinutes = 60;
/// A period eases in over its first hour and out over its last -- or a
/// third of itself, when it is short.
constexpr std::int32_t kRampMinutes = 60;

/// How many periods START in `day`, 2..4. A day has as many cuts as
/// periods, and the stretch before its first cut is the tail of the day
/// before's last period.
[[nodiscard]] std::int32_t periodCount(std::uint64_t seed, std::int32_t day) noexcept {
    return 2 + static_cast<std::int32_t>(weatherDraw(seed, day, kSaltPeriodCount) % 3ULL);
}

/// The minute the day's `index`-th cut falls at (index 0..count-1): the even
/// split on the half-offsets (two periods cut at 06:00 and 18:00, four at
/// 03:00, 09:00, 15:00 and 21:00), jittered within an hour either side.
/// Strictly inside the day, strictly increasing in index.
[[nodiscard]] std::int32_t periodEdge(std::uint64_t seed, std::int32_t day, std::int32_t count,
                                      std::int32_t index) noexcept {
    const std::int32_t even = (kMinutesPerDay * (2 * index + 1)) / (2 * count);
    const std::int32_t jitter =
        static_cast<std::int32_t>(weatherDraw(seed, day, kSaltPeriodEdge + static_cast<std::uint32_t>(index)) %
                                  static_cast<std::uint64_t>(2 * kEdgeJitterMinutes + 1)) -
        kEdgeJitterMinutes;
    return even + jitter;
}

/// The kind a period gets, by the hour its middle sits in. The bands and
/// their weights (out of 100). Clear leads everywhere; fog is the most
/// common thing that is not, and it is a dawn, dusk and night thing.
///
///                 clear  overcast  fog  wind
///   night  21-05    42      16      32    10
///   edges  05-09    34      18      38    10     dawn and dusk: the fog's hours
///          17-21
///   day    09-17    48      22      12    18
[[nodiscard]] WeatherKind periodKind(std::uint64_t seed, std::int32_t day, std::int32_t index,
                                     std::int32_t midMinute) noexcept {
    const std::int32_t hour = ((midMinute % kMinutesPerDay) + kMinutesPerDay) % kMinutesPerDay / 60;
    int clear = 48;
    int overcast = 22;
    int fog = 12;
    if (hour < 5 || hour >= 21) {
        clear = 42;
        overcast = 16;
        fog = 32;
    } else if (hour < 9 || hour >= 17) {
        clear = 34;
        overcast = 18;
        fog = 38;
    }
    const int roll = static_cast<int>(
        weatherDraw(seed, day, kSaltPeriodKind + static_cast<std::uint32_t>(index)) % 100ULL);
    if (roll < clear) {
        return WeatherKind::Clear;
    }
    if (roll < clear + overcast) {
        return WeatherKind::Overcast;
    }
    if (roll < clear + overcast + fog) {
        return WeatherKind::Fog;
    }
    return WeatherKind::Wind;
}

/// How strong a period gets, 0.55..1.0. Zero for a clear one.
[[nodiscard]] float periodPeak(std::uint64_t seed, std::int32_t day, std::int32_t index,
                               WeatherKind kind) noexcept {
    if (kind == WeatherKind::Clear) {
        return 0.0F;
    }
    const auto draw = static_cast<float>(
        weatherDraw(seed, day, kSaltPeriodPeak + static_cast<std::uint32_t>(index)) % 1000ULL);
    return 0.55F + 0.45F * (draw / 999.0F);
}

}  // namespace

WeatherPeriod weatherPeriodAt(std::uint64_t worldSeed, std::int32_t day,
                              int timeOfDaySeconds) noexcept {
    const int wrapped = ((timeOfDaySeconds % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;
    const std::int32_t minute = wrapped / 60;

    // The cuts of this day, and the one at or before `minute`.
    const std::int32_t count = periodCount(worldSeed, day);
    std::int32_t startDay = day;
    std::int32_t startIndex = 0;
    std::int32_t startMinute = 0;
    std::int32_t endMinute = 0;
    bool found = false;
    for (std::int32_t i = count - 1; i >= 0; --i) {
        const std::int32_t edge = periodEdge(worldSeed, day, count, i);
        if (edge <= minute) {
            found = true;
            startIndex = i;
            startMinute = edge;
            endMinute = i + 1 < count
                            ? periodEdge(worldSeed, day, count, i + 1)
                            : kMinutesPerDay + periodEdge(worldSeed, day + 1, periodCount(worldSeed, day + 1), 0);
            break;
        }
    }
    if (!found) {
        // Before the day's first cut: the period is yesterday's last, run
        // on through midnight.
        startDay = day - 1;
        const std::int32_t yesterday = periodCount(worldSeed, startDay);
        startIndex = yesterday - 1;
        startMinute = periodEdge(worldSeed, startDay, yesterday, startIndex) - kMinutesPerDay;
        endMinute = periodEdge(worldSeed, day, count, 0);
    }

    WeatherPeriod period;
    period.startMinute = startMinute;
    period.endMinute = endMinute;
    period.kind = periodKind(worldSeed, startDay, startIndex, (startMinute + endMinute) / 2);
    period.peak = periodPeak(worldSeed, startDay, startIndex, period.kind);
    return period;
}

Weather weatherFor(std::uint64_t worldSeed, std::int32_t day, int timeOfDaySeconds) noexcept {
    const WeatherPeriod period = weatherPeriodAt(worldSeed, day, timeOfDaySeconds);
    Weather weather;
    weather.kind = period.kind;
    if (period.kind == WeatherKind::Clear) {
        return weather;
    }
    const int wrapped = ((timeOfDaySeconds % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;
    const float now = static_cast<float>(wrapped) / 60.0F;
    const float start = static_cast<float>(period.startMinute);
    const float end = static_cast<float>(period.endMinute);
    const float ramp =
        std::min(static_cast<float>(kRampMinutes), std::max(1.0F, (end - start) / 3.0F));
    // A trapezoid: up over the first ramp, flat, down over the last.
    const float envelope =
        smoothstep(start, start + ramp, now) * (1.0F - smoothstep(end - ramp, end, now));
    weather.intensity = std::clamp(period.peak * envelope, 0.0F, 1.0F);
    return weather;
}

float Weather::windGain() const noexcept {
    switch (kind) {
        case WeatherKind::Wind:
            return 1.0F + 1.4F * intensity;
        case WeatherKind::Fog:
            return 1.0F - 0.35F * intensity;
        case WeatherKind::Overcast:
            return 1.0F + 0.15F * intensity;
        case WeatherKind::Clear:
            break;
    }
    return 1.0F;
}

std::string_view weatherKindName(WeatherKind kind) noexcept {
    switch (kind) {
        case WeatherKind::Clear:
            return "clear";
        case WeatherKind::Overcast:
            return "overcast";
        case WeatherKind::Fog:
            return "fog";
        case WeatherKind::Wind:
            return "wind";
    }
    return "clear";
}

bool parseWeatherKind(std::string_view name, WeatherKind& out) noexcept {
    for (int i = 0; i < kWeatherKindCount; ++i) {
        const auto kind = static_cast<WeatherKind>(i);
        if (name == weatherKindName(kind)) {
            out = kind;
            return true;
        }
    }
    return false;
}

SkyState skyAt(int timeOfDaySeconds) { return skyAt(timeOfDaySeconds, Weather{}); }

SkyState skyAt(int timeOfDaySeconds, const Weather& weather) {
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
    sky.fogDistance = 17.0F + 24.0F * daylight;

    // THE CLEAR SKY ENDS HERE, and everything pinned before the weather lane
    // is a picture of it. A clear Weather returns it untouched -- not
    // "nearly": the same bytes -- which is what keeps every existing frame
    // and every sky test where it was.
    if (weather.clear()) {
        return sky;
    }

    // ---- the weather, by intensity ---------------------------------------
    //
    // Everything below is a lerp from the clear sky by `i`, so a period that
    // is easing in eases the whole picture in with it and no consumer sees
    // a step. The clear fogDistance is kept aside: the 3D veil is what the
    // weather adds OVER it (SkyState::veil), never the clear day's own fog.
    const float i = std::clamp(weather.intensity, 0.0F, 1.0F);
    const float clearDensity = 1.0F / sky.fogDistance;
    switch (weather.kind) {
        case WeatherKind::Fog: {
            // Harbour fog. Milky by day, a cold grey-blue at night that
            // lifts the blacks a shade -- the lamps' own light scattered
            // back -- and at last light it takes a little of the sodium
            // wash. The fog IS the sky at full: the horizon goes to it and
            // the zenith most of the way.
            const Rgb nightFog{0.085F, 0.100F, 0.135F};
            const Rgb dayFog{0.70F, 0.72F, 0.74F};
            Rgb fogTarget = lerp(nightFog, dayFog, daylight);
            fogTarget = lerp(fogTarget, Rgb{0.42F, 0.24F, 0.15F}, duskness * 0.25F);
            sky.fog = lerp(sky.fog, fogTarget, i);
            sky.skyHorizon = lerp(sky.skyHorizon, fogTarget, i);
            sky.skyTop = lerp(sky.skyTop, fogTarget, 0.85F * i);
            // Flatter light: a shade down by day, a shade up and cold at
            // night, so a lit face and a shaded one sit closer together.
            const Rgb flat = sky.ambient * (0.92F + 0.12F * (1.0F - daylight)) +
                             Rgb{0.0F, 0.006F, 0.012F} * (1.0F - daylight);
            sky.ambient = lerp(sky.ambient, flat, i);
            // Eight tiles by day, six and a half at night, at full -- lerped
            // as DENSITY so half a fog is half the fog and not a tenth.
            const float fullDistance = 6.5F + 1.5F * daylight;
            const float density = clearDensity + (1.0F / fullDistance - clearDensity) * i;
            sky.fogDistance = 1.0F / density;
            sky.daylight = daylight * (1.0F - 0.20F * i);
            break;
        }
        case WeatherKind::Overcast: {
            // A lid on the sky: the band flattened to one cool grey between
            // the zenith and the horizon, the dusk wash mostly gone under it,
            // the light cooler and a touch dimmer -- which is what makes a
            // lit window read warm by contrast.
            const Rgb mid = lerp(sky.skyTop, sky.skyHorizon, 0.55F);
            const Rgb flat{mid.r * 0.93F * 0.94F, mid.g * 0.955F * 0.94F, mid.b * 0.94F};
            sky.skyTop = lerp(sky.skyTop, flat, i);
            sky.skyHorizon = lerp(sky.skyHorizon, flat, 0.75F * i);
            const Rgb cool{sky.ambient.r * 0.86F, sky.ambient.g * 0.90F, sky.ambient.b * 0.97F};
            sky.ambient = lerp(sky.ambient, cool, i);
            sky.fog = lerp(sky.skyHorizon * 0.75F, sky.skyHorizon, 0.5F);
            // A little more haze, no more than that.
            const float density = clearDensity * (1.0F + 0.25F * i);
            sky.fogDistance = 1.0F / density;
            sky.daylight = daylight * (1.0F - 0.15F * i);
            break;
        }
        case WeatherKind::Wind: {
            // Clear-ish and blustering: the air scoured a little clearer,
            // the zenith a touch deeper, the lanterns guttering. Mostly
            // heard -- see Weather::windGain.
            const float density = clearDensity / (1.0F + 0.3F * i);
            sky.fogDistance = 1.0F / density;
            sky.skyTop = sky.skyTop * (1.0F - 0.06F * i);
            sky.haloScale = 1.0F - 0.30F * i;
            break;
        }
        case WeatherKind::Clear:
            break;
    }
    // What the 3D pass adds round the eye: the density over the clear
    // day's, never less than nothing (wind clears, it does not veil).
    sky.veil = std::max(0.0F, 1.0F / sky.fogDistance - clearDensity);
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
        const LampFalloff shape = falloffOf(lamp.luminance);
        const float radius = shape.radius;
        const float peak = shape.peak;
        const Rgb tint = tintOf(lamp.warmth);
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
