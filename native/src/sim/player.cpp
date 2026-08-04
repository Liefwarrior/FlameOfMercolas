#include "granadad/sim/player.hpp"

#include "granadad/sim/rng.hpp"

namespace granadad::sim {

namespace {

/// Clamps a signed displacement to at most kMaxStepQ8, keeping its sign.
[[nodiscard]] std::int32_t cappedStep(std::int32_t remaining) noexcept {
    if (remaining > kMaxStepQ8) {
        return kMaxStepQ8;
    }
    if (remaining < -kMaxStepQ8) {
        return -kMaxStepQ8;
    }
    return remaining;
}

}  // namespace

PlayerBody::PlayerBody(const TileQuery& tiles, std::int32_t tileX, std::int32_t tileY,
                       std::int32_t band, Angle yaw) noexcept
    : tiles_(&tiles),
      x_(q8_tile_centre(tileX)),
      y_(q8_tile_centre(tileY)),
      band_(band),
      feetZ_(q8_of_tile(band)),
      yaw_(yaw & (kTurnFull - 1)) {
    spawnedLegally_ = tiles.standable(tileX, tileY, band) && bodyFits(x_, y_, band);
}

void PlayerBody::setPitch(Angle pitch) noexcept {
    pitch_ = pitch > kMaxPitch ? kMaxPitch : (pitch < -kMaxPitch ? -kMaxPitch : pitch);
}

bool PlayerBody::bodyFits(std::int32_t cx, std::int32_t cy, std::int32_t band) const noexcept {
    const std::int32_t ctx = q8_tile(cx);
    const std::int32_t cty = q8_tile(cy);
    if (!tiles_->standable(ctx, cty, band)) {
        return false;
    }
    // The square's footprint. kBodyRadius < 128, so this spans at most two
    // tiles on each axis, but the loop does not depend on that.
    const std::int32_t x0 = q8_tile(cx - kBodyRadius);
    const std::int32_t x1 = q8_tile(cx + kBodyRadius);
    const std::int32_t y0 = q8_tile(cy - kBodyRadius);
    const std::int32_t y1 = q8_tile(cy + kBodyRadius);
    for (std::int32_t ty = y0; ty <= y1; ++ty) {
        for (std::int32_t tx = x0; tx <= x1; ++tx) {
            if (!tiles_->solid(tx, ty, band)) {
                continue;
            }
            // A filled cell at the body's own level is a wall — UNLESS its top
            // face is a surface exactly one level up, in which case it is the
            // kerb the body just stepped off and the shoulder is allowed to
            // overhang it. Without this, stepping down off any raised walkway
            // is impossible: the tile you came from is solid rock at your new
            // level, and it is still inside your shoulder.
            if (!tiles_->walkable(tx, ty, band + 1)) {
                return false;
            }
        }
    }
    return true;
}

void PlayerBody::moveAxis(std::int32_t deltaX, std::int32_t deltaY) noexcept {
    std::int32_t remainingX = deltaX;
    std::int32_t remainingY = deltaY;
    while (remainingX != 0 || remainingY != 0) {
        const std::int32_t stepX = cappedStep(remainingX);
        const std::int32_t stepY = cappedStep(remainingY);
        const std::int32_t nx = wrap_add(x_, stepX);
        const std::int32_t ny = wrap_add(y_, stepY);

        std::int32_t nextBand = band_;
        const std::int32_t ntx = q8_tile(nx);
        const std::int32_t nty = q8_tile(ny);
        if (ntx != tileX() || nty != tileY()) {
            nextBand = tiles_->stepBand(tileX(), tileY(), band_, ntx, nty);
            if (nextBand == TileQuery::kNoBand) {
                return;
            }
        }
        if (!bodyFits(nx, ny, nextBand)) {
            return;
        }
        x_ = nx;
        y_ = ny;
        band_ = nextBand;
        remainingX -= stepX;
        remainingY -= stepY;
    }
}

void PlayerBody::step(const MoveInput& input) noexcept {
    ++steps_;

    // --- look ---------------------------------------------------------------
    yaw_ = wrap_add(yaw_, wrap_mul(input.turn, kTurnRate));
    yaw_ = wrap_add(yaw_, input.yawDelta);
    yaw_ &= (kTurnFull - 1);
    setPitch(wrap_add(pitch_, input.pitchDelta));

    // --- walk ---------------------------------------------------------------
    const std::int32_t speed = input.run ? kRunSpeed : kWalkSpeed;
    if (input.forward != 0 || input.strafe != 0) {
        // Direction in Q16, intent in {-1,0,1}, speed in Q8-per-step. The
        // product is Q16*Q8 and comes back to Q8 with one shift, in 64 bits so
        // nothing can overflow on the way.
        const std::int64_t dirX =
            static_cast<std::int64_t>(forward_x_q16(yaw_)) * input.forward +
            static_cast<std::int64_t>(right_x_q16(yaw_)) * input.strafe;
        const std::int64_t dirY =
            static_cast<std::int64_t>(forward_y_q16(yaw_)) * input.forward +
            static_cast<std::int64_t>(right_y_q16(yaw_)) * input.strafe;

        // Diagonal input would otherwise be sqrt(2) faster than straight. The
        // Q16 vector is already unit length per axis, so normalising means
        // scaling by 1/sqrt(2) when both axes are engaged -- 46341/65536, the
        // same constant the sine table already carries at its midpoint.
        std::int64_t scale = kTrigOne;
        if (input.forward != 0 && input.strafe != 0) {
            scale = 46341;
        }

        const std::int32_t moveX = static_cast<std::int32_t>((dirX * speed * scale) >> 32);
        const std::int32_t moveY = static_cast<std::int32_t>((dirY * speed * scale) >> 32);

        // One axis at a time: a body sliding along a warehouse front keeps its
        // tangential speed instead of stopping dead on the corner.
        moveAxis(moveX, 0);
        moveAxis(0, moveY);
    }

    // --- settle onto the band's surface -------------------------------------
    const std::int32_t targetZ = q8_of_tile(band_);
    if (feetZ_ < targetZ) {
        feetZ_ = feetZ_ + kEyeEaseRate > targetZ ? targetZ : feetZ_ + kEyeEaseRate;
    } else if (feetZ_ > targetZ) {
        feetZ_ = feetZ_ - kEyeEaseRate < targetZ ? targetZ : feetZ_ - kEyeEaseRate;
    }
}

void PlayerBody::push(std::int32_t dxQ8, std::int32_t dyQ8) noexcept {
    // Same order as a walking step: one axis, then the other, so a body shoved
    // along a wall slides down it instead of stopping dead on the first
    // corner. moveAxis already caps and substeps, so an impulse of any size is
    // safe against tunnelling.
    moveAxis(dxQ8, 0);
    moveAxis(0, dyQ8);
    const std::int32_t targetZ = q8_of_tile(band_);
    if (feetZ_ < targetZ) {
        feetZ_ = feetZ_ + kEyeEaseRate > targetZ ? targetZ : feetZ_ + kEyeEaseRate;
    } else if (feetZ_ > targetZ) {
        feetZ_ = feetZ_ - kEyeEaseRate < targetZ ? targetZ : feetZ_ - kEyeEaseRate;
    }
}

// VERIFICATION GAP (S2): the body's digest is NOT part of the world hash.
// PlayerBody is not a SimulationSystem, so nothing folds it into a WorldHasher
// section; the twin-run gate compares this number only where a workload asks
// for it by hand. Two runs that diverged in the player's position alone, with
// every registered system agreeing, would pass the gate. Closing it means
// either registering the body as a system or hashing it from whoever owns it.
std::uint64_t PlayerBody::digest() const noexcept {
    std::uint64_t h =
        mix64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(x_)) * 0x9E3779B97F4A7C15ull);
    h = mix64(h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(y_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(band_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(feetZ_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(yaw_)));
    h = mix64(h + static_cast<std::uint64_t>(static_cast<std::uint32_t>(pitch_)));
    return mix64(h + static_cast<std::uint64_t>(steps_));
}

}  // namespace granadad::sim
