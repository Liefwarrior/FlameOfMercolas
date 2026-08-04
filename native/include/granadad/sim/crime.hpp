#pragma once

// The six criminal acts of the ward, what they are worth, and what the Watch
// remembers about them.
//
// WHY A LEDGER AND NOT SIX FLAGS. S4 shipped exactly one crime -- a hand in a
// purse -- and it had no consequence outside the room it happened in: the
// person remembered, the Gull remembered for five minutes, and the district's
// only law-keeping faction never heard about it at all. That is not a criminal
// underworld, it is a pickpocketing minigame.
//
// S5 gives the Skyrunners something to be. Six acts, one tally each, one number
// the Watch keeps, and ONE call site (DialogueDirector::noteCrime) through
// which every one of them moves all four things it should move: the tally, the
// heat, the roofs' opinion of you and -- through the mirror the ladders already
// declare -- the garrison's.
//
//   LIFT      a hand in a purse. The apprentice crime; everybody's first.
//   BURGLE    a strongbox cracked in a room you did not rent.
//   SMUGGLE   a bale carried out of a house past somebody who would mind.
//   FENCE     stolen property sold on. The only one of the six with no victim
//             standing in front of you, which is why the Watch minds it least
//             and why it is the one that actually pays.
//   EXTORT    coin taken by leaning on somebody who would rather you did not.
//   ROOFRUN   being on a roof at all, which DOCKS-GAZETTEER section 2.5 rules
//             is unseemly for every Trojian except a presented Wielder. It
//             costs almost no heat and it is worth real standing, because it is
//             the one act on the list that IS the guild's whole identity.
//
// HEAT is what the Watch has heard, not what you did: an act nobody witnessed
// raises none of it. It cools on its own, and past kWarrantAt there is paper
// out on you.
//
// NO FLOATS. NO UNORDERED CONTAINERS. Every number is a clamped integer, the
// tallies are a fixed dense array in enum order, and the whole thing is
// byte-encodable and hashed for the same reason the social ledger is: a thing
// the twin-run gate cannot see is a thing the gate does not protect.

#include <cstdint>
#include <string_view>
#include <vector>

#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the acts
// ---------------------------------------------------------------------------

/// Append-only: the ordinal is hashed and written into the ledger's encoding.
enum class Crime : std::uint8_t {
    Lift = 0,
    Burgle = 1,
    Smuggle = 2,
    Fence = 3,
    Extort = 4,
    RoofRun = 5,
};

inline constexpr std::size_t kCrimeCount = 6;

[[nodiscard]] std::string_view crimeName(Crime crime) noexcept;

/// The name a questline stage counts this act under. Data, not code: a stage in
/// content/raws/quests/*.json says `"counter": "lifts"` and the tally finds it.
[[nodiscard]] std::string_view crimeTally(Crime crime) noexcept;

/// What the Watch adds to its opinion of you when somebody SAW it.
[[nodiscard]] std::int32_t crimeHeat(Crime crime) noexcept;

/// What it is worth to the roofs. Halved onto the Watch by the mirror the
/// ladders already declare -- see FactionLedger::addStanding.
[[nodiscard]] std::int32_t crimeStanding(Crime crime) noexcept;

/// Which skill the hands get better at. Empty for the two that are not a
/// craft -- fencing is a conversation and a roof-run is the body's own skill,
/// which the body already charges for.
[[nodiscard]] std::string_view crimeSkill(Crime crime) noexcept;

// ---------------------------------------------------------------------------
// the numbers
// ---------------------------------------------------------------------------

inline constexpr std::int32_t kHeatMax = 100;
/// Heat at or above which the Watch has paper out on you.
inline constexpr std::int32_t kWarrantAt = 60;
/// And below which the paper lapses on its own.
inline constexpr std::int32_t kWarrantLapsesAt = 20;
/// Simulated seconds the ward takes to forget one point of it. Five minutes a
/// point: a night's work is not walked off before the doors shut.
inline constexpr std::int32_t kHeatCoolSeconds = 300;

/// What one piece of stolen property is worth on the open counter it will never
/// see. The fence pays a PERCENTAGE of this, which is the whole of the trade.
inline constexpr std::int32_t kLootValue = 8;
/// What a fence gives a stranger of the roofs, before rank and standing.
inline constexpr std::int32_t kFenceBaseRate = 35;
/// And the band the rate is clamped to, so no rung ever makes stolen goods
/// worth more than honest ones.
inline constexpr std::int32_t kFenceRateFloor = 25;
inline constexpr std::int32_t kFenceRateCeiling = 85;

/// Coin in a guest's strongbox above the Gull's stair, before the cracksman's
/// own hands are counted.
inline constexpr std::int32_t kStrongboxCoin = 14;

/// What a bale of contraband is worth to the person waiting for it.
inline constexpr std::int32_t kBalePay = 22;

// ---------------------------------------------------------------------------
// the ledger
// ---------------------------------------------------------------------------

/// What the player has done, what they are carrying, and what the Watch knows.
class CrimeLedger {
public:
    // --- the tallies --------------------------------------------------------

    [[nodiscard]] std::int32_t tally(Crime crime) const noexcept;
    [[nodiscard]] std::int32_t crimesCommitted() const noexcept { return committed_; }

    /// Records one act. `witnessed` is the ONLY thing that raises heat: the
    /// Watch keeps a record of what it has heard, not of what happened.
    void commit(Crime crime, bool witnessed);

    // --- stolen property ----------------------------------------------------
    //
    // PIECES, not coin. A lifted purse pays out immediately because coin is
    // coin; a cracked box and a leaned-on trader hand you things, and a thing
    // has to be sold to somebody before it is money. That is what a fence is
    // FOR, and without it the Skyrunners' second rung buys nothing.

    [[nodiscard]] std::int32_t loot() const noexcept { return loot_; }
    void takeLoot(std::int32_t pieces);
    /// Sells up to `pieces` at `ratePercent` of kLootValue each. Returns the
    /// coin and removes what it sold.
    std::int32_t sellLoot(std::int32_t pieces, std::int32_t ratePercent);

    // --- contraband ---------------------------------------------------------

    [[nodiscard]] bool carryingBale() const noexcept { return bale_; }
    void takeBale() noexcept { bale_ = true; }
    void dropBale() noexcept { bale_ = false; }
    [[nodiscard]] std::int32_t balesRun() const noexcept { return balesRun_; }
    /// A bale landed where it was going. Returns the pay.
    std::int32_t deliverBale();

    // --- what the Watch knows -----------------------------------------------

    [[nodiscard]] std::int32_t heat() const noexcept { return heat_; }
    void addHeat(std::int32_t delta);
    [[nodiscard]] bool warrant() const noexcept { return warrant_; }

    /// One simulated second of the ward forgetting. Idempotent per second: the
    /// caller passes the tick so a clock that jumps -- sleeping a night in a
    /// rented room -- cools the right amount rather than one point.
    void cool(std::int64_t tick);

    /// Tears the paper up. THE WATCH'S OWN `warrant` TOKEN IS WHAT BUYS THIS,
    /// and the caller checks it: a sergeant can lose his own file, and nobody
    /// else can. Leaves the heat where it is, because the ward still remembers
    /// even when the roll does not.
    void quashWarrant() noexcept;

    /// Goes to ground. THE SKYRUNNERS' `lair` TOKEN IS WHAT BUYS THIS: a
    /// brotherhood with a roost has somewhere to be for a week, and heat and
    /// paper both go with it.
    void lieLow() noexcept;
    [[nodiscard]] std::int32_t timesLaidLow() const noexcept { return laidLow_; }

    // --- persistence --------------------------------------------------------

    [[nodiscard]] std::vector<std::uint8_t> encode() const;
    [[nodiscard]] static bool decode(const std::vector<std::uint8_t>& bytes, CrimeLedger& out);

    void hashInto(HashSink& sink) const;

private:
    /// Dense, in enum order, so iteration order is the vocabulary's.
    std::int32_t tallies_[kCrimeCount] = {};
    std::int32_t committed_ = 0;
    std::int32_t loot_ = 0;
    std::int32_t heat_ = 0;
    std::int32_t laidLow_ = 0;
    std::int32_t balesRun_ = 0;
    /// The tick heat was last charged against, so a clock jump cools once for
    /// every five minutes it jumped and not once for the jump.
    std::int64_t cooledAtTick_ = 0;
    bool bale_ = false;
    bool warrant_ = false;
};

/// What a fence pays this player, as a percentage of kLootValue a piece.
///
/// THREE TERMS AND NO ROLL, the same shape guildPricePercent already uses: the
/// rung you are on, what the roofs think of you, and a floor and a ceiling so
/// the trade is never a gift in either direction.
[[nodiscard]] std::int32_t fenceRatePercent(std::int32_t rank, std::int32_t standing) noexcept;

}  // namespace granadad::sim
