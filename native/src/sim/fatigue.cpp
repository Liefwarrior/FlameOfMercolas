#include "granadad/sim/fatigue.hpp"

namespace granadad::sim {

namespace {

[[nodiscard]] std::int32_t clampAttribute(std::int32_t value) noexcept {
    if (value < kAttributeFloor) {
        return kAttributeFloor;
    }
    if (value > kAttributeCeiling) {
        return kAttributeCeiling;
    }
    return value;
}

}  // namespace

std::int32_t fatigueTermQ8(std::int32_t currentFine, std::int32_t maxFine) noexcept {
    if (maxFine <= 0) {
        // A degenerate pool reads FULL, deliberately: full is the neutral
        // baseline everywhere the term is applied, so a caller that never set
        // a pool up gets the shipped behaviour rather than a penalty.
        return kFatigueTermFullQ8;
    }
    std::int32_t current = currentFine;
    if (current < 0) {
        current = 0;
    }
    if (current > maxFine) {
        current = maxFine;
    }
    const std::int64_t span = kFatigueTermFullQ8 - kFatigueTermEmptyQ8;
    return static_cast<std::int32_t>(kFatigueTermEmptyQ8 +
                                     (span * current) / maxFine);
}

std::int32_t fatigueMaxPoints(const AttributeBlock& attributes) noexcept {
    // 2*VIG + MGT + AGI -- see the header on the mapping from the reference's
    // STR + WIL + AGI + END and on why WIT is deliberately out.
    return 2 * attributes.value(AttributeId::Vigor) +
           attributes.value(AttributeId::Might) +
           attributes.value(AttributeId::Agility);
}

std::int32_t fatigueRegenFinePerStep(std::int32_t vigor, std::int32_t gritLevel,
                                     bool moving) noexcept {
    const std::int32_t vig = clampAttribute(vigor);
    std::int32_t grit = gritLevel;
    if (grit < 0) {
        grit = 0;
    }
    if (grit > 40) {
        grit = 40;
    }
    const std::int32_t rate = kFatigueRegenBaseFine + vig / 8 + grit / 4;
    return moving ? rate / 2 : rate;
}

std::int32_t verticalFatigueCostFine(std::int32_t basePoints, std::int32_t agility,
                                     std::int32_t skyrunningLevel) noexcept {
    const std::int32_t agi = clampAttribute(agility);
    std::int32_t sky = skyrunningLevel;
    if (sky < 0) {
        sky = 0;
    }
    if (sky > 20) {
        sky = 20;
    }
    // Two factors, both exactly 1 at the base sheet: AGI 40 makes 256/256 and
    // skyrunning 0 makes 64/64. 64-bit through the multiplies so the ceiling
    // sheet cannot overflow however the constants are retuned.
    const std::int64_t baseFine = static_cast<std::int64_t>(basePoints) * kFatiguePointFine;
    const std::int64_t agiScaled = (baseFine * (296 - agi)) / 256;
    return static_cast<std::int32_t>((agiScaled * (64 - sky)) / 64);
}

std::int32_t meleeDamageBonus(std::int32_t might) noexcept {
    return (clampAttribute(might) - kAttributeBase) / 15;
}

std::int32_t agilitySpeedScaleQ8(std::int32_t agility) noexcept {
    return 256 + ((clampAttribute(agility) - kAttributeBase) * 4) / 5;
}

std::int32_t castWitBonusPercent(std::int32_t wit) noexcept {
    return (clampAttribute(wit) - kAttributeBase) / 5;
}

std::int64_t witScaledCooldown(std::int64_t cooldownTicks, std::int32_t wit) noexcept {
    if (cooldownTicks <= 0) {
        return 0;
    }
    const std::int64_t scaled = (cooldownTicks * (340 - clampAttribute(wit))) / 300;
    // A real cooldown never scales away entirely: recovery is a discount, not
    // an exemption.
    return scaled < 1 ? 1 : scaled;
}

void PlayerFatigue::resetFor(const AttributeBlock& attributes) noexcept {
    maxFine_ = fatigueMaxPoints(attributes) * kFatiguePointFine;
    currentFine_ = maxFine_;
    winded_ = false;
}

void PlayerFatigue::resizeFor(const AttributeBlock& attributes) noexcept {
    maxFine_ = fatigueMaxPoints(attributes) * kFatiguePointFine;
    if (currentFine_ > maxFine_) {
        currentFine_ = maxFine_;
    }
}

void PlayerFatigue::drain(std::int32_t fine) noexcept {
    if (fine <= 0) {
        return;
    }
    currentFine_ = currentFine_ > fine ? currentFine_ - fine : 0;
    if (currentFine_ == 0) {
        winded_ = true;
    }
}

void PlayerFatigue::regen(std::int32_t fine) noexcept {
    if (fine <= 0) {
        return;
    }
    currentFine_ = currentFine_ + fine > maxFine_ ? maxFine_ : currentFine_ + fine;
    if (winded_ && currentFine_ >= maxFine_ / kWindedRecoverDivisor) {
        winded_ = false;
    }
}

void PlayerFatigue::hashInto(HashSink& sink) const {
    sink.put_int(static_cast<std::uint32_t>(currentFine_));
    sink.put_int(static_cast<std::uint32_t>(maxFine_));
    sink.put_byte(winded_ ? 1U : 0U);
}

}  // namespace granadad::sim
