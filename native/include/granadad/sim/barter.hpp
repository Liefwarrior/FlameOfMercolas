#pragma once

// Haggling, as a conversation.
//
// The Ardenfall reference lands here: a price is not a number attached to an
// item, it is an ARGUMENT between two people who each know what the thing is
// worth and disagree about who should eat the difference. So a haggle has
// rounds, the merchant concedes ground grudgingly, and they run out of patience
// if you keep grinding.
//
// Two things move the numbers, and only two:
//
//   RELATIONSHIP  what they think of you, straight off the SocialLedger. A
//                 hostile shopkeeper opens forty-five percent over the odds; a
//                 friend opens under them. This is the whole reason the social
//                 layer exists in the same sprint.
//   SKILL         STREETWISE, on both sides. The gap between your streetwise
//                 and theirs moves the opening price and how far they can be
//                 pushed. A master haggler grinds a novice; a novice does not
//                 grind Master Venn.
//
// This is NOT the investigation. DOCKS-GAZETTEER section 5.3 is binding on
// that: "the investigation is never persuasion... no dialogue-skill checks
// exist." No topic in a conversation is ever gated by a roll. Trade is the one
// place a skill decides an outcome, because trade is the one place both parties
// are openly trying to get the better of each other.
//
// NO FLOATS and NO RANDOM DRAWS. Every number below is integer arithmetic on
// the inputs, so the same offer at the same standing with the same skill
// settles at the same price on every machine and in both runs of the twin-run
// gate.

#include <cstdint>
#include <string_view>

#include "granadad/sim/barks.hpp"
#include "granadad/sim/social.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

/// What is on the counter. The Gull sells two things; later houses sell more.
enum class Goods : std::uint8_t {
    Drink = 0,
    Room = 1,
};

[[nodiscard]] std::string_view goodsName(Goods goods) noexcept;

/// Everything the price depends on. Assembled by the caller from the ledger and
/// the skill tracks, so the arithmetic below has no idea where they came from
/// and can be tested on its own.
struct HaggleTerms {
    /// What the thing is actually worth. kDrinkPrice, kRoomPrice.
    std::int32_t basePrice = 1;
    Attitude attitude = Attitude::Neutral;
    std::int32_t playerSkill = 0;
    std::int32_t merchantSkill = 0;
    Goods goods = Goods::Drink;
    /// S4: what the GUILD behind the counter is worth to this buyer, in whole
    /// percent. Negative is the member's rate; positive is a strong faction
    /// pricing a stranger up. Assembled by the caller out of the faction ledger
    /// -- see guildPricePercent -- so this file still has no idea what a guild
    /// is and can still be tested with nothing loaded.
    std::int32_t guildPercent = 0;
};

/// How far the standing alone moves the price, in whole percent.
[[nodiscard]] std::int32_t attitudePercent(Attitude attitude) noexcept;
/// How far the streetwise gap moves it, in whole percent. Clamped, so no amount
/// of skill turns a purchase into a gift.
[[nodiscard]] std::int32_t skillPercent(std::int32_t playerSkill,
                                        std::int32_t merchantSkill) noexcept;
/// What they open at.
[[nodiscard]] std::int32_t askingPrice(const HaggleTerms& terms) noexcept;
/// The least they will take, ever, in this conversation. Never below a floor
/// share of what the goods are worth: a shopkeeper does not sell at a loss
/// because you were charming.
[[nodiscard]] std::int32_t reservePrice(const HaggleTerms& terms) noexcept;

/// How many rounds of being ground down a merchant will sit through.
inline constexpr std::int32_t kHagglePatience = 3;
/// The share of what the goods are worth that reserve can never go below.
inline constexpr std::int32_t kReserveFloorPercent = 35;

/// What one move in the argument produced.
enum class HaggleOutcome : std::uint8_t {
    /// Nothing open to answer.
    Idle = 0,
    /// They have named a price and are waiting.
    Open = 1,
    /// They came down, and are waiting again.
    Countered = 2,
    /// Done. settledPrice() is what you pay.
    Struck = 3,
    /// That offer was an insult. Costs double patience.
    Insulted = 4,
    /// Out of patience. No sale, and they remember.
    Refused = 5,
    /// You ended it.
    WalkedOut = 6,
};

[[nodiscard]] std::string_view haggleOutcomeName(HaggleOutcome outcome) noexcept;

/// One haggle, from opening price to handshake or walk-out.
class Haggle {
public:
    /// Starts an argument. Any argument already running is abandoned.
    void open(const HaggleTerms& terms);
    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const HaggleTerms& terms() const noexcept { return terms_; }
    /// What they are asking RIGHT NOW. Moves down as they concede.
    [[nodiscard]] std::int32_t asking() const noexcept { return asking_; }
    /// The least they would take. Never shown to the player.
    [[nodiscard]] std::int32_t reserve() const noexcept { return reserve_; }
    [[nodiscard]] std::int32_t patience() const noexcept { return patience_; }
    [[nodiscard]] std::int32_t rounds() const noexcept { return rounds_; }
    [[nodiscard]] HaggleOutcome outcome() const noexcept { return outcome_; }
    /// What was agreed, or 0.
    [[nodiscard]] std::int32_t settledPrice() const noexcept { return settled_; }
    /// What the merchant will remember of the last move, and how hard the
    /// player worked for it. Read once and applied by the caller.
    [[nodiscard]] Deed lastDeed() const noexcept { return lastDeed_; }
    /// Streetwise effort the last move earned the player, 0 when none.
    [[nodiscard]] std::int32_t lastSkillEffort() const noexcept { return lastEffort_; }

    /// The player names a number.
    HaggleOutcome offer(std::int32_t coins);
    /// The player takes the price on the table without argument.
    HaggleOutcome takeAsking();
    /// The player leaves.
    HaggleOutcome walkAway();

    void hashInto(HashSink& sink) const;

private:
    HaggleTerms terms_;
    bool active_ = false;
    std::int32_t asking_ = 0;
    std::int32_t reserve_ = 0;
    std::int32_t patience_ = 0;
    std::int32_t rounds_ = 0;
    std::int32_t settled_ = 0;
    std::int32_t lastEffort_ = 0;
    HaggleOutcome outcome_ = HaggleOutcome::Idle;
    Deed lastDeed_ = Deed::Spoke;
};

}  // namespace granadad::sim
