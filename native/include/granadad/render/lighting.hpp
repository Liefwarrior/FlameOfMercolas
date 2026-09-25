#pragma once

// Light, which lives entirely on this side of the line and always has.
//
// ARCHITECTURE.md section 0.1 settles this at length: the simulation has no
// light dependents — actor sight radius by light was specified and never built,
// schedules are clock-driven, there is no stealth system — and the LIGHT and
// OPACITY lanes are all-zero in every shipped world. Putting glow accumulation
// in sim-core would break the no-float rule for zero simulation benefit. So
// lighting is a renderer feature, computed once at load from the baked lamps,
// and nothing here is ever read by the world hasher.
//
// Two contributions, combined per drawn face:
//
//   AMBIENT  a day/night curve. Committed dark, per the visual target: even
//            noon here is overcast harbour light, and night is near-black with
//            a cold cast so the lamp pools carry the frame.
//   LAMPS    a precomputed per-cell glow field. Radius and peak follow the
//            Java's LampGlowMap so the district lights the way it was authored
//            to: radius 4 + (lum-8)/12 clamped to 3.5..5.5 tiles, peak
//            0.55 + 0.45*lum/26, falloff P*(1-(d/R)^2)^2, overlaps combined by
//            saturating union with a weighted-average colour.

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/lamps.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::render {

/// Seconds in a day. The simulation ticks once a second, so a tick count is a
/// time of day with no conversion.
inline constexpr int kSecondsPerDay = 86400;

/// Everything the sky and the fog do at one moment.
struct SkyState {
    /// Multiplier applied to every unlit surface.
    Rgb ambient;
    /// Zenith and horizon colours of the sky band.
    Rgb skyTop;
    Rgb skyHorizon;
    /// What distance fades into.
    Rgb fog;
    /// e-folding distance of the harbour fog, in tiles.
    float fogDistance = 26.0F;
    /// 0 at midnight, 1 at noon. Drives the lamp mix.
    float daylight = 0.0F;
    /// WEATHER. The fog the 3D pass draws as a veil round the eye
    /// (render3d/world_scene.hpp, buildVeil), as an e-folding DENSITY per
    /// tile (1 / tiles) OVER the clear day's own fogDistance. The 3D pass has
    /// never drawn the clear day's fog -- the district reads sharp to its far
    /// end there, by that lane's own note -- so this is only ever what the
    /// weather ADDS, and a clear frame stays what it was. Zero in clear
    /// weather; the software pass ignores it (its fog is fogDistance).
    float veil = 0.0F;
    /// WEATHER. A multiplier on every flame's halo and glow: 1 in still air,
    /// under 1 when the wind is up and the lanterns gutter.
    float haloScale = 1.0F;
};

// ---------------------------------------------------------------------------
// WEATHER (roadmap 21a; D15 ruled it yes, render-only)
// ---------------------------------------------------------------------------
//
// Four states and an intensity, computed from (world seed, calendar day,
// clock) and NOTHING ELSE: no RNG stream, no wall clock, no sim field. It is a
// pure function, so two runs at one seed and one hour draw one sky, and the
// sim never reads it -- schedules, sight, prices and the twin-gate's hashes
// do not know the fog is there. Weather that touched a schedule or any hashed
// field would be a declared baseline move, and it is not this.
//
// A day has two to four PERIODS, drawn per day and cut at hashed minutes. Each
// period has one kind and one peak; its intensity eases in over its first
// hour and out over its last, so the fog rolls in and lifts rather than
// snapping at the top of the hour, and at a period's edge every kind is at
// zero -- where every kind IS clear -- so a period change is never a cut.
// The periods run through midnight: the last period of one day is the first
// of the next, so nothing resets at 00:00.
//
// The kind is biased by the hour the period sits in. Harbour fog rolls off
// the water at dawn, at dusk and through the night and is the most common
// thing that is not clear; overcast favours the day; wind the afternoon.

enum class WeatherKind : std::uint8_t {
    Clear = 0,
    Overcast = 1,
    /// Harbour fog off the water: the district swallowed to a few tiles,
    /// milky by day and a cold grey-blue at night.
    Fog = 2,
    /// Clear-ish and blustery: the wind bed up, the lanterns guttering.
    Wind = 3,
};
inline constexpr int kWeatherKindCount = 4;

struct Weather {
    WeatherKind kind = WeatherKind::Clear;
    /// How much of it, 0..1. Always 0 for Clear; a pinned state (--weather=)
    /// is 1.
    float intensity = 0.0F;

    /// True when this draws exactly the clear sky: Clear, or any kind at zero.
    [[nodiscard]] bool clear() const noexcept {
        return kind == WeatherKind::Clear || intensity <= 0.0F;
    }
    /// The audio wind bed's multiplier: 1 as authored, over 2 at a full blow,
    /// under 1 in a still fog. The mixer still caps the layer at one.
    [[nodiscard]] float windGain() const noexcept;

    [[nodiscard]] bool operator==(const Weather&) const = default;
};

/// One weather period, as weatherFor sees it: where it starts and ends in
/// MINUTES from the asked day's midnight (the start may be negative and the
/// end past 1440 -- periods run through midnight), what it is and how strong
/// it gets. Exposed so a test can pin the day's shape.
struct WeatherPeriod {
    std::int32_t startMinute = 0;
    std::int32_t endMinute = 1440;
    WeatherKind kind = WeatherKind::Clear;
    float peak = 0.0F;
};

/// The period covering `timeOfDaySeconds` of `day` at `worldSeed`. Pure.
[[nodiscard]] WeatherPeriod weatherPeriodAt(std::uint64_t worldSeed, std::int32_t day,
                                            int timeOfDaySeconds) noexcept;

/// THE WEATHER, as a pure function of the seed, the calendar day and the
/// clock. Same inputs, same answer, on every machine and every run.
[[nodiscard]] Weather weatherFor(std::uint64_t worldSeed, std::int32_t day,
                                 int timeOfDaySeconds) noexcept;

/// "clear", "overcast", "fog", "wind" -- what --weather= takes and what the
/// capture summary prints.
[[nodiscard]] std::string_view weatherKindName(WeatherKind kind) noexcept;
/// The reverse. False (and `out` untouched) for anything else.
[[nodiscard]] bool parseWeatherKind(std::string_view name, WeatherKind& out) noexcept;

/// The sky at a time of day, in seconds since midnight. THE CLEAR SKY: this is
/// skyAt(t, Weather{}) byte for byte, and every frame pinned before the
/// weather lane was drawn under it.
[[nodiscard]] SkyState skyAt(int timeOfDaySeconds);

/// The sky at a time of day under this weather. A clear Weather (any kind at
/// zero) returns exactly what skyAt(t) returns; anything else starts from it
/// and moves the fog, the sky band, the ambient and the daylight by the
/// intensity, so a state easing in eases the picture in with it.
[[nodiscard]] SkyState skyAt(int timeOfDaySeconds, const Weather& weather);

/// Additive glow at one cell from a HANDFUL of lights that are not in the baked
/// field: a tavern hearth that is banked at three in the morning, the candles
/// on its tables that only exist while the doors are open.
///
/// Evaluated per drawn cell instead of baked, because the whole point of them
/// is that they change. That is affordable precisely because there are a
/// handful: the world pass reads light once per drawn VOXEL FACE, not once per
/// pixel, so a dozen lights cost a dozen distance tests on a few thousand faces.
/// A hundred of them would not be, and the day there are a hundred is the day
/// they get a second baked field of their own.
///
/// THE POOL, AS A SURFACE SEES IT. Applied by the 3D pass ONLY, and only
/// where a lamp's glow becomes the colour of a wall, a cobble or a kit piece
/// -- never to the glow itself, so the light law's own question ("does the
/// room behind this pane glow?", world_scene.cpp) and the 2D renderer read
/// exactly the numbers they always read.
///
/// WHY. The falloff above is peak * (1 - d^2/r^2)^2 over four to five and a
/// half tiles, which is a very flat pool: four tiles out still keeps a fifth
/// of the lamp. Put the sky's ambient under that and the surface clamp over
/// it and a whole street of plaster comes out at one value, which is the
/// halo critic's first note word for word -- the wall directly above a
/// lantern's cap reading the same as the far corner, so the glow agrees with
/// nothing cast on anything. A lantern is meant to be the brightest thing on
/// its street and to POOL on the wall behind it and the cobbles under it.
///
/// WHAT. The glow is scaled by its own strength: its full value where the
/// lamp is strong, down to kPoolFloor of itself where the lamp is nearly
/// gone. That is one more power of the falloff without touching the peak or
/// the radius, so NOTHING GETS BRIGHTER and no surface blows out that did
/// not blow out before -- the far field gets darker, and that is what makes
/// the near field read as a pool. All three channels take the same factor,
/// so a lantern's warmth and a fire's are the colours they were.
inline constexpr float kPoolFloor = 0.15F;

[[nodiscard]] inline Rgb pooledGlow(const Rgb& glow) noexcept {
    const float strength = std::min(1.0F, std::max(glow.r, std::max(glow.g, glow.b)));
    if (strength <= 0.0F) {
        return Rgb{};
    }
    const float shaped = kPoolFloor + (1.0F - kPoolFloor) * strength;
    return Rgb{glow.r * shaped, glow.g * shaped, glow.b * shaped};
}

/// Same radius, peak and falloff as the baked lamps, so a hearth and a street
/// lantern of equal luminance light a room identically.
[[nodiscard]] Rgb dynamicGlowAt(const std::vector<Lamp>& lamps, std::int32_t x, std::int32_t y,
                                std::int32_t z) noexcept;

/// A dense per-cell glow field over the world, built once from the lamps.
class LampGlow {
public:
    LampGlow() = default;

    /// Accumulates every lamp into a field the size of the world. About 6 MB
    /// for the Docks, built in a few milliseconds, and then it is a lookup.
    [[nodiscard]] static LampGlow build(const sim::TileQuery& tiles,
                                        const std::vector<Lamp>& lamps);

    /// Additive light at a cell. Black where no lamp reaches.
    [[nodiscard]] Rgb at(std::int32_t x, std::int32_t y, std::int32_t z) const noexcept;

    /// How many cells any lamp reaches. Zero means the bake never loaded.
    [[nodiscard]] std::size_t litCellCount() const noexcept { return litCells_; }

private:
    std::int32_t sizeX_ = 0;
    std::int32_t sizeY_ = 0;
    std::int32_t sizeZ_ = 0;
    /// 0xAABBGGRR-ish packed glow, one per cell. Zero means unlit.
    std::vector<std::uint32_t> field_;
    std::size_t litCells_ = 0;
};

}  // namespace granadad::render
