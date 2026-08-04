#include "granadad/sim/stealth.hpp"

#include <algorithm>

#include "granadad/sim/fixed.hpp"
#include "granadad/sim/tile_query.hpp"

namespace granadad::sim {

namespace {

/// Sixteenths of a tile. Radius is quoted in these so the renderer's 3.5 and
/// 5.5 tile bounds land on exact integers instead of being rounded twice.
constexpr std::int32_t kRadiusUnit = 16;
constexpr std::int32_t kRadiusFloorQ4 = 56;  // 3.5 tiles
constexpr std::int32_t kRadiusCeilQ4 = 88;   // 5.5 tiles

constexpr std::int32_t clampTo(std::int32_t value, std::int32_t low,
                               std::int32_t high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

/// Saturating union, on the 0..kLightMax scale: a + b - a*b/max. Two lanterns
/// over one table never make a tile twice as bright as a tile can be.
constexpr std::int32_t unite(std::int32_t a, std::int32_t b) noexcept {
    return clampTo(a + b - (a * b) / kLightMax, 0, kLightMax);
}

}  // namespace

// ---------------------------------------------------------------------------
// light
// ---------------------------------------------------------------------------

std::int32_t lightRadiusQ4(std::int32_t luminance) noexcept {
    if (luminance <= 0) {
        return 0;
    }
    // 4 + (lum - 8)/12 tiles, in sixteenths, then the renderer's own clamp.
    const std::int32_t raw = 4 * kRadiusUnit + ((luminance - 8) * kRadiusUnit) / 12;
    return clampTo(raw, kRadiusFloorQ4, kRadiusCeilQ4);
}

std::int32_t lightPeak(std::int32_t luminance) noexcept {
    if (luminance <= 0) {
        return 0;
    }
    // 0.55 + 0.45 * lum/26, on the 0..100 scale.
    const std::int32_t raw = 55 + (45 * luminance) / 26;
    return clampTo(raw, 0, kLightMax);
}

std::int32_t glowFrom(const SimLight& light, std::int32_t x, std::int32_t y,
                      std::int32_t band) noexcept {
    if (light.luminance <= 0 || light.band != band) {
        return 0;
    }
    const std::int32_t radiusQ4 = lightRadiusQ4(light.luminance);
    if (radiusQ4 <= 0) {
        return 0;
    }
    const std::int32_t dx = (x - light.x) * kRadiusUnit;
    const std::int32_t dy = (y - light.y) * kRadiusUnit;
    const std::int64_t d2 = static_cast<std::int64_t>(dx) * dx + static_cast<std::int64_t>(dy) * dy;
    const std::int64_t r2 = static_cast<std::int64_t>(radiusQ4) * radiusQ4;
    if (d2 >= r2) {
        return 0;
    }
    // (1 - (d/R)^2)^2, in hundredths, then scaled by the peak.
    const std::int32_t t = static_cast<std::int32_t>(100 - (d2 * 100) / r2);
    const std::int32_t falloff = (t * t) / 100;
    return clampTo((lightPeak(light.luminance) * falloff) / 100, 0, kLightMax);
}

std::int32_t glowAt(const std::vector<SimLight>& lights, std::int32_t x, std::int32_t y,
                    std::int32_t band) noexcept {
    std::int32_t total = 0;
    for (const SimLight& light : lights) {
        total = unite(total, glowFrom(light, x, y, band));
        if (total >= kLightMax) {
            break;
        }
    }
    return total;
}

std::int32_t ambientLight(std::int32_t secondOfDay) noexcept {
    // A triangle over the day: black at midnight, brightest at noon. The peak
    // is deliberately low -- committed dark, per the visual target -- so that
    // even standing on the Tarwalk at noon the lamps still matter.
    constexpr std::int32_t kNoonLight = 55;
    constexpr std::int32_t kSecondsPerDay = 86400;
    constexpr std::int32_t kHalfDay = kSecondsPerDay / 2;
    std::int32_t second = secondOfDay % kSecondsPerDay;
    if (second < 0) {
        second += kSecondsPerDay;
    }
    const std::int32_t fromMidnight = second < kHalfDay ? second : kSecondsPerDay - second;
    return clampTo((fromMidnight * kNoonLight) / kHalfDay, 0, kLightMax);
}

std::int32_t illuminationAt(const std::vector<SimLight>& lights, std::int32_t x, std::int32_t y,
                            std::int32_t band, std::int32_t secondOfDay, bool indoors) noexcept {
    std::int32_t sky = ambientLight(secondOfDay);
    if (indoors) {
        sky = (sky * kIndoorSkyPercent) / 100;
    }
    return unite(sky, glowAt(lights, x, y, band));
}

// ---------------------------------------------------------------------------
// sound and stance
// ---------------------------------------------------------------------------

std::string_view stanceName(Stance stance) noexcept {
    switch (stance) {
        case Stance::Upright:
            return "upright";
        case Stance::Crouched:
            return "crouched";
    }
    return "upright";
}

void StealthState::setMotion(bool moving, bool running) noexcept {
    moving_ = moving;
    running_ = running && moving;
}

void StealthState::makeNoise(std::int32_t amount) noexcept {
    const std::int32_t clamped = clampTo(amount, 0, kNoiseMax);
    if (clamped <= 0) {
        return;
    }
    // The loudest thing wins. Nothing ever stacks: two quiet acts in a row are
    // not a shout, and a shout is not made louder by a footstep.
    if (clamped >= actNoise()) {
        act_ = clamped;
        actSteps_ = kNoiseFadeSteps;
    }
}

void StealthState::step() noexcept {
    if (actSteps_ > 0) {
        --actSteps_;
        if (actSteps_ == 0) {
            act_ = 0;
        }
    }
}

std::int32_t StealthState::actNoise() const noexcept {
    if (act_ <= 0 || actSteps_ <= 0) {
        return 0;
    }
    // Linear fade over kNoiseFadeSteps.
    return (act_ * actSteps_) / kNoiseFadeSteps;
}

std::int32_t StealthState::noise() const noexcept {
    std::int32_t moving = 0;
    if (moving_) {
        if (stance_ == Stance::Crouched) {
            moving = kNoiseCrouchWalking;
        } else {
            moving = running_ ? kNoiseRunning : kNoiseWalking;
        }
    }
    return std::max(moving, actNoise());
}

void StealthState::hashInto(HashSink& sink) const {
    sink.put_byte(static_cast<std::uint32_t>(stance_));
    sink.put_byte(moving_ ? 1U : 0U);
    sink.put_byte(running_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(act_));
    sink.put_int(static_cast<std::uint32_t>(actSteps_));
}

// ---------------------------------------------------------------------------
// the notice rule
// ---------------------------------------------------------------------------

Angle bearingTo(std::int32_t fromX, std::int32_t fromY, std::int32_t toX,
                std::int32_t toY) noexcept {
    const std::int32_t dx = toX - fromX;
    const std::int32_t dy = toY - fromY;
    if (dx == 0 && dy == 0) {
        return 0;
    }
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    // The eighth of a turn the delta points into. North is -Y and yaw rises
    // clockwise -- see angle.hpp's compass note, which this obeys and does not
    // restate.
    if (ax * 2 <= ay) {
        return dy < 0 ? kFacingNorth : kFacingSouth;
    }
    if (ay * 2 <= ax) {
        return dx > 0 ? kFacingEast : kFacingWest;
    }
    if (dx > 0) {
        return dy < 0 ? kTurnFull / 8 : 3 * kTurnFull / 8;
    }
    return dy < 0 ? 7 * kTurnFull / 8 : 5 * kTurnFull / 8;
}

bool withinArc(Angle facing, Angle bearing, Angle arc) noexcept {
    std::int32_t delta = (bearing - facing) & (kTurnFull - 1);
    if (delta > kTurnHalf) {
        delta -= kTurnFull;
    }
    const std::int32_t magnitude = delta < 0 ? -delta : delta;
    return magnitude <= arc;
}

Notice noticeOf(const NoticeInput& input) noexcept {
    Notice out;
    if (input.oblivious) {
        // A man on the floor and a rat on the skirting see nothing that matters
        // to anybody. Reported as a full cover rather than a special case, so
        // the two halves still read.
        out.cover = kCoverBase;
        out.seen = false;
        return out;
    }

    const std::int32_t tiles = input.distanceQ8 / kSubOne;
    std::int32_t read = kNoticeBase - tiles * kNoticePerTile;
    read += (clampTo(input.light, 0, kLightMax) * kNoticeLightWeight) / kLightMax;
    if (withinArc(input.observerFacing, input.bearingToBody, kNoticeArc)) {
        read += kNoticeFacing;
    }
    if (input.alert) {
        read += kNoticeAlert;
    }
    if (!input.lineOfSight) {
        read -= kNoticeBlindPenalty;
    }

    std::int32_t cover = kCoverBase;
    if (input.stance == Stance::Crouched) {
        cover += kCoverCrouch;
    }
    cover += std::min(kCoverSneakCap, std::max(0, input.sneakLevel) * kCoverPerSneakLevel);
    cover += (clampTo(input.roomNoise, 0, kNoiseMax) * kCoverRoomNoiseWeight) / kNoiseMax;

    const std::int32_t noise =
        (clampTo(input.noise, 0, kNoiseMax) * kNoiseHalvedPercent) / 100;

    out.read = read;
    out.cover = cover;
    out.noise = noise;
    out.seen = read + noise > cover;
    return out;
}

}  // namespace granadad::sim
