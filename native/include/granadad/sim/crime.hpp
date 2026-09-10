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

#include "granadad/sim/contraband.hpp"
#include "granadad/sim/watch.hpp"
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
///
/// VERIFICATION GAP (S5): NOTHING ARRESTS. A warrant is issued, hashed, shown
/// in red on the HUD and read by exactly one thing -- how long a bouncer waits
/// before putting you out. DECISIONS.md's Skyrunner escalation ruling describes
/// a Watch that maims on the first offence and hangs on the second, and there
/// is no such Watch: no patrol looks for you, no cell holds you and no gibbet
/// exists. The state is real and its consequence is one line of grace.
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
    //
    // S6 GIVES THE SACK CONTENTS. S5's own note here read "a bale is a BOOLEAN,
    // not an item... what is being carried has no weight, no contents, no
    // owner", and the S5 review carried it forward as an open gap. A bale has a
    // KIND and a COUNT now, both decided by the boat, and what comes out of it
    // is the same five goods a contract can ask for and a watchman can find.

    [[nodiscard]] bool carryingBale() const noexcept { return bale_; }
    [[nodiscard]] Contraband baleGood() const noexcept { return baleGood_; }
    [[nodiscard]] std::int32_t baleUnits() const noexcept { return bale_ ? baleUnits_ : 0; }
    void takeBale(Contraband good, std::int32_t units) noexcept;
    void dropBale() noexcept { bale_ = false; }
    [[nodiscard]] std::int32_t balesRun() const noexcept { return balesRun_; }

    /// A bale landed where it was going. Returns the pay.
    ///
    /// `ownBuyer` is whether the boat's OWN buyer is the one taking it off you.
    /// When they are -- which is what happens when nobody has hired you, and is
    /// exactly the S5 behaviour -- the goods go with them and the flat runner's
    /// fee is what you get. When somebody else hired you, the sack comes off
    /// your shoulder into your own stash and there is no fee, because the
    /// contract is what pays and being paid twice for one bale would make the
    /// snug a coin faucet with extra steps.
    std::int32_t deliverBale(bool ownBuyer);

    /// What the player is carrying that somebody would rather they were not.
    [[nodiscard]] Stash& stash() noexcept { return stash_; }
    [[nodiscard]] const Stash& stash() const noexcept { return stash_; }

    // --- what the ward has done back ----------------------------------------

    /// What an arrest cost.
    struct ArrestOutcome {
        Sentence sentence = Sentence::None;
        /// Units of contraband into the impound.
        std::int32_t unitsSeized = 0;
        /// Coin the ward took, never more than the purse it was offered.
        std::int32_t fine = 0;
        /// Hours in a cell, or 0.
        std::int32_t heldHours = 0;
    };

    /// THE WATCH TAKES YOU. The one call site an arrest goes through, for
    /// exactly the reason noteCrime is the one an act goes through: a seizure
    /// that emptied the sack and left the paper standing, or a sentence that
    /// was passed and never recorded, is the bug this shape exists to prevent.
    ///
    /// `purse` is what the player has on them, so the fine can be capped at it;
    /// `draw` is the roll behind the length of a sentence. Everything else is
    /// the ledger's own state.
    ArrestOutcome arrest(bool skyrunner, std::int32_t purse, std::uint64_t draw);

    /// ACTION-COMBAT BUILD. A WITNESSED killing was done. Raises kMurderHeat
    /// (watch.hpp) -- exactly the warrant threshold, so one witnessed murder is
    /// instant paper -- and marks the player a murderer, which makes the next
    /// arrest a Condemned one whatever the ordinary sentence ladder would say.
    /// The caller gates on the three-clause witness rule; an unwitnessed kill
    /// calls nothing, because heat is what the Watch heard. See
    /// COMBAT-ACTION-SPEC.md section 4.4.
    void markMurderer() noexcept;
    /// Whether a witnessed murder stands on the record. Read by arrest() to
    /// route to Sentence::Condemned. Permanent within a run (the court that
    /// would clear or execute it is the justice build, section 10).
    [[nodiscard]] bool murderer() const noexcept { return murderer_; }

    [[nodiscard]] std::int32_t arrests() const noexcept { return arrests_; }
    [[nodiscard]] Sentence lastSentence() const noexcept { return lastSentence_; }
    /// A hand the ward has taken. Permanent.
    [[nodiscard]] bool maimed() const noexcept { return maimed_; }
    /// Sentenced to the rope. See Sentence::Condemned on what this build does
    /// and does not simulate.
    [[nodiscard]] bool condemned() const noexcept { return condemned_; }
    /// What the hands still manage, as a percentage of what two of them take.
    [[nodiscard]] std::int32_t takePercent() const noexcept {
        return maimed_ ? kMaimedTakePercent : 100;
    }

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

    // --- S6 -----------------------------------------------------------------
    Stash stash_;
    /// What the boat sent, and how much of it.
    Contraband baleGood_ = Contraband::Moonshine;
    std::int32_t baleUnits_ = 0;
    std::int32_t arrests_ = 0;
    Sentence lastSentence_ = Sentence::None;
    bool maimed_ = false;
    bool condemned_ = false;
    /// ACTION-COMBAT BUILD: a witnessed killing stands on the record, so the
    /// next arrest condemns. See markMurderer / arrest.
    bool murderer_ = false;
};

/// What a fence pays this player, as a percentage of kLootValue a piece.
///
/// THREE TERMS AND NO ROLL, the same shape guildPricePercent already uses: the
/// rung you are on, what the roofs think of you, and a floor and a ceiling so
/// the trade is never a gift in either direction.
[[nodiscard]] std::int32_t fenceRatePercent(std::int32_t rank, std::int32_t standing) noexcept;

}  // namespace granadad::sim
