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

#include <cstdint>
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
};

/// The sky at a time of day, in seconds since midnight.
[[nodiscard]] SkyState skyAt(int timeOfDaySeconds);

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
