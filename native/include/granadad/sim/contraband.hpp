#pragma once

// The five goods the ward will not put on a counter, and what a body can carry
// of them.
//
// WHY THIS EXISTS. S5 gave the roofs six criminal acts and one thing to carry:
// a bale, which was a bool. Its own comment said so -- "a bale is a BOOLEAN,
// not an item. There is no inventory in this build, so what is being carried
// has no weight, no contents, no owner". Every consequence downstream of it was
// real and the object was a flag with a name, which meant there was nothing for
// a market to price, nothing for a contract to want and nothing for a watchman
// to find. S6 needs all three.
//
// FIVE KINDS AND NO SIXTH, and they are the five the sprint was asked for:
//
//   SCALP      vermin taken for the ward's own bounty. THE ONLY LEGAL ONE, and
//              even it is not simply legal: DECISIONS.md's tenure ruling says
//              the Church "sanctions the redemption of a scalp", so a bounty is
//              redeemed under a priest's mark and not otherwise. The Java build
//              reached the same place from the other end -- TradeGoods files
//              rat, gull and cat scalps as MATERIALS and CullVerb's own header
//              says human scalps defer past S12 -- so what is takeable here is
//              vermin, and the sanction is what a priest is FOR.
//   DUST       the powder that comes off a boat and is never on the manifest.
//              The ward's own word, and an S6 coinage: canon names no narcotic,
//              and inventing a whole apothecary would be inventing setting.
//   MOONSHINE  spirit that never passed the Weighhouse. DOCKS-GAZETTEER section
//              1 is explicit that "tariffs are the visible hand of the off-map
//              city -- the posted rates at the customs house explain the whole
//              smuggling economy", and untaxed drink is the oldest answer to a
//              posted rate there is.
//   FLOWER     the ward's smoke. The owner's own word for it.
//   ARTIFACT   a piece with somebody's name on it. Not the anonymous "pieces"
//              the crime ledger already counts -- those are what a fence buys
//              by the handful and asks nothing about. This is the cup, the
//              chart, the signet, the thing a contract can NAME, which is what
//              makes recovering one a job rather than a transaction.
//
// WHAT A KIND IS, MECHANICALLY. Five numbers and two flags, and every one of
// them has a consumer:
//
//   value      what it is worth to somebody who wants it. Prices a contract.
//   heat       what the Watch adds per unit when it finds it on you. Zero for
//              the legal one.
//   weight     drams a unit. THIS IS THE RISK DIAL: a watchman notices a load,
//              not a count, so eight jars of spirit are conspicuous and eight
//              twists of dust are not.
//   skill      the craft handling it trains, out of the owner's own vocabulary.
//   legal      whether carrying it is an offence at all.
//   sanction   whether redeeming it wants the Flame's mark.
//
// NO FLOATS. NO UNORDERED CONTAINERS. The stash is a dense array in enum order,
// byte-encodable and hashed, for exactly the reason the crime ledger is: a
// thing the twin-run gate cannot see is a thing the gate does not protect.

#include <cstdint>
#include <string_view>
#include <vector>

#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the goods
// ---------------------------------------------------------------------------

/// Append-only: the ordinal is hashed and written into the stash's encoding.
enum class Contraband : std::uint8_t {
    Scalp = 0,
    Dust = 1,
    Moonshine = 2,
    Flower = 3,
    Artifact = 4,
};

inline constexpr std::size_t kContrabandCount = 5;

/// The raws-facing snake_case name. THE one content-string spelling, the way
/// TradeGoods::kindForSymbol is in the Java: content/raws/contracts/*.json says
/// `"good": "moonshine"` and this is what resolves it.
[[nodiscard]] std::string_view contrabandSymbol(Contraband good) noexcept;
/// What the HUD calls it. ASCII, upper case, short enough for a corner.
[[nodiscard]] std::string_view contrabandLabel(Contraband good) noexcept;
/// The kind a raws symbol names, or false when nothing does.
[[nodiscard]] bool contrabandFromSymbol(std::string_view symbol, Contraband& out) noexcept;

/// What one unit is worth to somebody who wants it, in the smallest coin there
/// is. A contract's pay is built off this and the patron's own terms.
[[nodiscard]] std::int32_t contrabandValue(Contraband good) noexcept;
/// What the Watch adds to its opinion of you, per unit, when it searches you
/// and finds this. Zero for the one the ward pays a bounty on.
[[nodiscard]] std::int32_t contrabandHeat(Contraband good) noexcept;
/// Drams a unit. What a watchman actually notices.
[[nodiscard]] std::int32_t contrabandWeight(Contraband good) noexcept;
/// The skill handling it trains, out of content/raws/skills/skills.json.
[[nodiscard]] std::string_view contrabandSkill(Contraband good) noexcept;
/// Whether carrying it is an offence at all.
[[nodiscard]] bool contrabandLegal(Contraband good) noexcept;
/// Whether redeeming it wants a priest's mark first. See the note on SCALP.
[[nodiscard]] bool contrabandNeedsSanction(Contraband good) noexcept;

// ---------------------------------------------------------------------------
// the numbers
// ---------------------------------------------------------------------------

/// Drams a body will carry before it stops taking things. A moonshine jar is
/// twenty-four of them, so this is ten jars, or eighty twists of dust, or any
/// mix in between -- which is the whole point of quoting a load in weight.
inline constexpr std::int32_t kStashDrams = 240;
/// And a plain count ceiling, so nothing can be farmed into a four-figure pile
/// by being light.
inline constexpr std::int32_t kStashUnits = 48;

// ---------------------------------------------------------------------------
// what is in the sack
// ---------------------------------------------------------------------------

/// Everything the player is carrying that somebody would rather they were not.
class Stash {
public:
    [[nodiscard]] std::int32_t count(Contraband good) const noexcept;
    /// Every unit of every kind.
    [[nodiscard]] std::int32_t units() const noexcept;
    /// Drams. What the load actually weighs.
    [[nodiscard]] std::int32_t weight() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return units() == 0; }

    /// Units of the kinds carrying one of them is an offence.
    [[nodiscard]] std::int32_t illicitUnits() const noexcept;
    /// Drams of the same. This is what a watchman's eye is on.
    [[nodiscard]] std::int32_t illicitWeight() const noexcept;
    /// What the Watch would add to its opinion of you if it searched you now.
    /// Clamped to kHeatMax's own ceiling by the ledger that applies it.
    [[nodiscard]] std::int32_t heatIfSearched() const noexcept;

    /// Puts units in. Returns how many actually fitted -- a sack has a size,
    /// and a caller that ignores the answer has minted goods out of nothing.
    std::int32_t add(Contraband good, std::int32_t units);
    /// Takes units out. Returns how many were actually there.
    std::int32_t take(Contraband good, std::int32_t units);
    /// The Watch's own verb: everything illicit goes into the impound and the
    /// legal remainder stays. Returns the units taken.
    std::int32_t seizeIllicit();
    void clear() noexcept;

    /// Drams still free.
    [[nodiscard]] std::int32_t roomLeft() const noexcept;

    [[nodiscard]] std::vector<std::uint8_t> encode() const;
    [[nodiscard]] static bool decode(const std::vector<std::uint8_t>& bytes, Stash& out);
    void hashInto(HashSink& sink) const;

private:
    /// Dense, in enum order, so iteration order is the vocabulary's.
    std::int32_t counts_[kContrabandCount] = {};
};

}  // namespace granadad::sim
