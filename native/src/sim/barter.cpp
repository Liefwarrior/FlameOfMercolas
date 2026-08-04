#include "granadad/sim/barter.hpp"

#include <algorithm>

namespace granadad::sim {

std::string_view goodsName(Goods goods) noexcept {
    switch (goods) {
        case Goods::Drink:
            return "A DRINK";
        case Goods::Room:
            return "A ROOM";
    }
    return "GOODS";
}

std::string_view haggleOutcomeName(HaggleOutcome outcome) noexcept {
    switch (outcome) {
        case HaggleOutcome::Idle:
            return "idle";
        case HaggleOutcome::Open:
            return "open";
        case HaggleOutcome::Countered:
            return "countered";
        case HaggleOutcome::Struck:
            return "struck";
        case HaggleOutcome::Insulted:
            return "insulted";
        case HaggleOutcome::Refused:
            return "refused";
        case HaggleOutcome::WalkedOut:
            return "walked out";
    }
    return "?";
}

std::int32_t attitudePercent(Attitude attitude) noexcept {
    switch (attitude) {
        // A shopkeeper who wants you out of the shop prices you out of it. He
        // does not refuse to sell -- DOCKS-GAZETTEER section 5.3: information
        // is never refused, and neither is a mug of ale -- he just makes it
        // expensive enough to be a message.
        case Attitude::Hostile:
            return 45;
        case Attitude::Cold:
            return 18;
        case Attitude::Neutral:
            return 0;
        case Attitude::Warm:
            return -10;
        case Attitude::Friend:
            return -20;
        // The kitchen price. Kin do not pay the room rate.
        case Attitude::Kin:
            return -30;
    }
    return 0;
}

std::int32_t skillPercent(std::int32_t playerSkill, std::int32_t merchantSkill) noexcept {
    // The GAP, not the level: a novice buying from a novice pays the odds.
    // Divided by three so a fifteen-point edge is worth five percent, and
    // clamped so no skill gap ever turns a purchase into a gift.
    const std::int32_t gap = merchantSkill - playerSkill;
    return std::clamp(gap / 3, -25, 25);
}

std::int32_t askingPrice(const HaggleTerms& terms) noexcept {
    const std::int32_t base = std::max(1, terms.basePrice);
    const std::int32_t percent = 100 + attitudePercent(terms.attitude) +
                                 skillPercent(terms.playerSkill, terms.merchantSkill) +
                                 std::clamp(terms.guildPercent, -40, 40);
    // Rounded to NEAREST, not up. Rounding up sounds like the house's habit
    // until you do the arithmetic on a two-coin mug: every percentage above
    // par, however small, becomes a whole extra coin, so a merely-cool
    // bartender charges fifty per cent more than a friendly one. Nearest keeps
    // the small prices honest and still moves a twelve-coin bed from eight to
    // seventeen across the range, which is where the standing is legible.
    return std::max(1, (base * std::max(1, percent) + 50) / 100);
}

std::int32_t reservePrice(const HaggleTerms& terms) noexcept {
    const std::int32_t base = std::max(1, terms.basePrice);
    const std::int32_t asking = askingPrice(terms);
    // How far they can be pushed. Twenty points of ground, plus a fifth of the
    // player's streetwise -- a master haggler opens more room than a novice
    // does, which is what makes the skill worth having.
    const std::int32_t concession = std::clamp(20 + terms.playerSkill / 5, 20, 45);
    const std::int32_t pushed = (asking * (100 - concession)) / 100;
    const std::int32_t floor = std::max(1, (base * kReserveFloorPercent) / 100);
    return std::clamp(std::max(1, pushed), floor, asking);
}

void Haggle::reset() noexcept {
    active_ = false;
    asking_ = 0;
    reserve_ = 0;
    patience_ = 0;
    rounds_ = 0;
    settled_ = 0;
    lastEffort_ = 0;
    outcome_ = HaggleOutcome::Idle;
    lastDeed_ = Deed::Spoke;
}

void Haggle::open(const HaggleTerms& terms) {
    reset();
    terms_ = terms;
    asking_ = askingPrice(terms);
    reserve_ = reservePrice(terms);
    patience_ = kHagglePatience;
    active_ = true;
    outcome_ = HaggleOutcome::Open;
}

HaggleOutcome Haggle::takeAsking() {
    if (!active_) {
        return HaggleOutcome::Idle;
    }
    settled_ = asking_;
    active_ = false;
    outcome_ = HaggleOutcome::Struck;
    // Paying the asking price without argument is a courtesy, and it is
    // remembered as one. It teaches you nothing about haggling.
    lastDeed_ = rounds_ == 0 ? Deed::PaidAsking : Deed::HaggledFair;
    lastEffort_ = 0;
    return outcome_;
}

HaggleOutcome Haggle::walkAway() {
    if (!active_) {
        return HaggleOutcome::Idle;
    }
    active_ = false;
    settled_ = 0;
    outcome_ = HaggleOutcome::WalkedOut;
    lastDeed_ = Deed::WalkedOut;
    lastEffort_ = 0;
    return outcome_;
}

HaggleOutcome Haggle::offer(std::int32_t coins) {
    if (!active_) {
        return HaggleOutcome::Idle;
    }
    ++rounds_;
    lastEffort_ = 0;
    const std::int32_t named = std::max(0, coins);

    // Over the asking price is not a negotiation, it is a tip.
    if (named >= asking_) {
        settled_ = asking_;
        active_ = false;
        outcome_ = HaggleOutcome::Struck;
        lastDeed_ = Deed::PaidAsking;
        return outcome_;
    }

    // At or above the reserve they take it, and how hard you ground them
    // decides whether they smile about it.
    if (named >= reserve_) {
        settled_ = named;
        active_ = false;
        outcome_ = HaggleOutcome::Struck;
        const std::int32_t midpoint = reserve_ + (asking_ - reserve_) / 2;
        if (named >= midpoint) {
            lastDeed_ = Deed::HaggledFair;
            lastEffort_ = 1;
        } else {
            // Ground below the midpoint: a better piece of trading and a worse
            // piece of manners. Both are true and both are recorded.
            lastDeed_ = Deed::HaggledHard;
            lastEffort_ = 2;
        }
        return outcome_;
    }

    // Half the reserve is not an offer, it is a comment on their goods.
    const std::int32_t insultFloor = reserve_ / 2;
    if (named < insultFloor) {
        patience_ -= 2;
        lastDeed_ = Deed::Lowballed;
        if (patience_ <= 0) {
            active_ = false;
            outcome_ = HaggleOutcome::Refused;
            return outcome_;
        }
        outcome_ = HaggleOutcome::Insulted;
        return outcome_;
    }

    // They come halfway from where they are to where you are -- never past the
    // reserve, which they never name.
    --patience_;
    const std::int32_t target = std::max(named, reserve_);
    asking_ = std::max(reserve_, asking_ - std::max(1, (asking_ - target) / 2));
    lastDeed_ = Deed::HaggledHard;
    if (patience_ <= 0) {
        active_ = false;
        outcome_ = HaggleOutcome::Refused;
        return outcome_;
    }
    outcome_ = HaggleOutcome::Countered;
    return outcome_;
}

void Haggle::hashInto(HashSink& sink) const {
    sink.put_byte(active_ ? 1U : 0U);
    sink.put_int(static_cast<std::uint32_t>(terms_.basePrice));
    sink.put_byte(static_cast<std::uint32_t>(terms_.attitude));
    sink.put_int(static_cast<std::uint32_t>(terms_.playerSkill));
    sink.put_int(static_cast<std::uint32_t>(terms_.merchantSkill));
    sink.put_byte(static_cast<std::uint32_t>(terms_.goods));
    sink.put_int(static_cast<std::uint32_t>(asking_));
    sink.put_int(static_cast<std::uint32_t>(reserve_));
    sink.put_int(static_cast<std::uint32_t>(patience_));
    sink.put_int(static_cast<std::uint32_t>(rounds_));
    sink.put_int(static_cast<std::uint32_t>(settled_));
    sink.put_int(static_cast<std::uint32_t>(lastEffort_));
    sink.put_byte(static_cast<std::uint32_t>(outcome_));
    sink.put_byte(static_cast<std::uint32_t>(lastDeed_));
}

}  // namespace granadad::sim
