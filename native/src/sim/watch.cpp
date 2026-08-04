#include "granadad/sim/watch.hpp"

#include <algorithm>

namespace granadad::sim {

std::string_view watchCauseName(WatchCause cause) noexcept {
    switch (cause) {
        case WatchCause::None:
            return "none";
        case WatchCause::Warrant:
            return "warrant";
        case WatchCause::Contraband:
            return "contraband";
        case WatchCause::Both:
            return "warrant and goods";
    }
    return "?";
}

WatchCause watchCause(bool warrant, bool noticed, std::int32_t illicitUnits) noexcept {
    // Noticing an empty sack is not a cause. The two halves are independent on
    // purpose: a wanted man with nothing on him is still wanted, and a clean man
    // with a bale is still carrying it.
    const bool goods = noticed && illicitUnits > 0;
    if (warrant && goods) {
        return WatchCause::Both;
    }
    if (warrant) {
        return WatchCause::Warrant;
    }
    if (goods) {
        return WatchCause::Contraband;
    }
    return WatchCause::None;
}

std::int32_t noticePermille(std::int32_t illicitDrams, std::int32_t carrierStreetwise,
                            std::int32_t watchmanKit) noexcept {
    if (illicitDrams <= 0) {
        // Nothing to find. Stated rather than left to the arithmetic, because
        // "a clean man is never taken for a load he is not carrying" is a rule
        // and not a rounding accident.
        return 0;
    }
    const std::int32_t byLoad = std::max(0, illicitDrams) * 6;
    const std::int32_t byEye = std::max(0, watchmanKit) * 3;
    const std::int32_t byNerve = std::max(0, carrierStreetwise) * 10;
    // Twenty in a thousand for simply being there with something on you: a man
    // whose job is seized cargo does look up.
    return std::clamp(20 + byLoad + byEye - byNerve, 0, 750);
}

std::string_view sentenceName(Sentence sentence) noexcept {
    switch (sentence) {
        case Sentence::None:
            return "none";
        case Sentence::Fined:
            return "fined";
        case Sentence::Held:
            return "held";
        case Sentence::Maimed:
            return "maimed";
        case Sentence::Condemned:
            return "condemned";
    }
    return "?";
}

Sentence sentenceFor(bool skyrunner, bool warrant, std::int32_t priorArrests) noexcept {
    if (!warrant) {
        // Stopped and searched, with nothing on the roll against you. The goods
        // are seized and the ward takes a charge for its trouble; nobody spends
        // a night in a cell over a jar.
        return Sentence::Fined;
    }
    if (!skyrunner) {
        // ACTORS-SPEC section 2.7: the Watch arrests, never executes. Unchanged
        // for everybody who is not on the roofs' roll, however many times.
        return Sentence::Held;
    }
    // DECISIONS.md, Eli 2026-07-14: "unless they're a skyrunner then it's cut
    // off their hand first offense and hanging on the second."
    return priorArrests <= 0 ? Sentence::Maimed : Sentence::Condemned;
}

std::int32_t heldHours(std::uint64_t draw) noexcept {
    constexpr std::int32_t kSpan = kHeldHoursMax - kHeldHoursMin + 1;
    return kHeldHoursMin + static_cast<std::int32_t>(draw % static_cast<std::uint64_t>(kSpan));
}

std::int32_t fineFor(std::int32_t heat, std::int32_t unitsSeized) noexcept {
    // A charge for the paper and a charge a unit. Both small: the punishment is
    // the night and the seizure, and a fine big enough to end a run would make
    // the whole trade unplayable rather than risky.
    const std::int32_t byHeat = std::max(0, heat) / 4;
    const std::int32_t byGoods = std::max(0, unitsSeized) * 3;
    return byHeat + byGoods;
}

}  // namespace granadad::sim
