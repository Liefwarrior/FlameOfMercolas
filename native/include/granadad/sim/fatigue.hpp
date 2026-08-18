#pragma once

// The wind in a body: one pool, and everything physical reads it.
//
// THE SOURCE is docs/design/ELDER-SCROLLS-REFERENCE.md section 1.2, whose own
// verdict line calls Morrowind's fatigue "the mechanic to copy": one bar,
// drained by sprinting, jumping, climbing, swinging and casting, regenerating
// while the body is easy, and multiplied as a 1.25x-full to 0.75x-empty term
// into everything that already rolls. ADAPTED, NOT TRANSCRIBED -- the doc's
// formulas are in Gamebryo floats over STR/WIL/AGI/END and this sim is integer
// Q8 over MGT/AGI/VIG/WIT, so every mapping is restated here in this engine's
// own units, beside the number it produced.
//
// THE ATTRIBUTE MAPPING, stated once. Morrowind's MaxFatigue is the SUM OF
// FOUR ATTRIBUTES (STR + WIL + AGI + END, ~40-50 each at start). This sim has
// four attributes and no WIL; the pool is
//
//     maxPoints = 2*VIG + MGT + AGI
//
// VIG doubled because Vigor IS this sim's endurance-and-constitution word and
// the reference's END is the stat players associate with the pool; MGT and AGI
// carry the strength/agility share unchanged; WIT is deliberately OUT, because
// the mind's share of the economy is the cast check and the cooldown recovery
// (see castWitBonusPercent / witScaledCooldown below) -- giving WIT the pool
// too would make it the only attribute paid twice. At the chargen base of 40
// across the board this is 160 points, the same order as Morrowind's own
// starting 160-200.
//
// UNITS. A POINT is the unit the reference's economy is quoted in (run drains
// 5/s, a jump costs 5). The pool ticks at kStepsPerSecond though, and 5/60 of
// a point is not an integer -- so the pool is STORED IN FINE UNITS of 1/256
// point (kFatiguePointFine), the same Q8 discipline as every other sub-unit
// quantity in this sim. Every constant below states what it is worth back in
// points per second so the mapping to the reference stays checkable.
//
// NO COLLAPSE, ON PURPOSE. Morrowind's fatigue <= 0 knocks the body helpless
// to the floor. Here an EMPTY pool gates sprint and the climb verbs and drags
// the FatigueTerm to its floor -- and nothing else. A Docks brawler passing
// out mid-street would be a new brawl outcome (classifyFight/kPlayerBrawlFloor
// semantics are pinned, and a collapse is a defeat nobody swung for), so the
// collapse mechanic is EXPLICITLY OUT OF SCOPE for v1 rather than quietly
// missing.
//
// PLAYER-SCOPED, AND WHERE THE LINE IS. Ward actors genuinely share strike()
// and the movement rules, but no ward actor carries this pool in this build:
// giving them one would rebalance every schedule walk and every brawl the
// ward's daily-life determinism already bakes into live-run hashes, which the
// standing directive says not to touch in this pass. The seam is honest -- the
// FatigueTerm defaults every non-player call site to kFatigueTermFullQ8, so
// the day ward actors earn a pool they pass their own term down the very same
// parameter.

#include <cstdint>
#include <string_view>

#include "granadad/sim/attributes.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

/// The skill a hard-worked body recovers by. content/raws/skills/skills.json's
/// own row: "grit", governing VIG, TRAINED tier -- named once here the way
/// kBlockSkill is named in brawl.hpp and kRoofSkill in social.hpp.
inline constexpr std::string_view kGritSkill = "grit";

// ---------------------------------------------------------------------------
// units and the term
// ---------------------------------------------------------------------------

/// Fine units per fatigue point. The pool's own Q8.
inline constexpr std::int32_t kFatiguePointFine = 256;

/// The FatigueTerm's two ends, Q8 against a neutral 256: 320 is the
/// reference's 1.25x at a full pool, 192 its 0.75x at an empty one.
///
/// HOW IT IS APPLIED IS THE ADAPTATION THAT MATTERS: everywhere the term
/// touches an existing roll, FULL IS THE BASELINE this engine already shipped
/// -- the 1-in-8 brawl whiff, the linkcraft cast chance -- and the term only
/// ever degrades from there as the pool empties. A term that could BUY
/// outcomes past the shipped baseline would be state flat-buying success,
/// which is the shape the S9 guardrail exists to refuse.
inline constexpr std::int32_t kFatigueTermFullQ8 = 320;
inline constexpr std::int32_t kFatigueTermEmptyQ8 = 192;

/// The term for a pool at `currentFine` of `maxFine`, Q8, linear between the
/// two ends. A degenerate max reads as full rather than dividing by zero.
[[nodiscard]] std::int32_t fatigueTermQ8(std::int32_t currentFine,
                                         std::int32_t maxFine) noexcept;

// ---------------------------------------------------------------------------
// what things cost, and what comes back
// ---------------------------------------------------------------------------

/// Sprint, per movement step, fine units. 21/256 of a point at 60 steps a
/// second is 4.92 points a second -- the reference's own "run: 5/s" with the
/// encumbrance term dropped, because this build has no encumbrance yet.
inline constexpr std::int32_t kSprintDrainFinePerStep = 21;

/// The verbs, in points. The reference prices a jump at 5; a mantle is a
/// whole 2.7 m two-handed climb (see kVaultReachMm's own note on why nothing
/// here can be vaulted) so it costs two jumps, and a leap sits between them.
/// A punch is the reference's light-weapon attack cost rounded up; a cast is
/// the body's share of working a link.
inline constexpr std::int32_t kJumpFatiguePoints = 5;
inline constexpr std::int32_t kMantleFatiguePoints = 10;
inline constexpr std::int32_t kLeapFatiguePoints = 8;
inline constexpr std::int32_t kPunchFatiguePoints = 4;
inline constexpr std::int32_t kCastFatiguePoints = 5;

/// Regen base, fine units per movement step. 8/256 of a point at 60 steps a
/// second is 1.88 points a second before Vigor says anything -- the
/// reference's 2.5 + 0.02*END/s restated so that VIG carries a visible share:
/// regen = kFatigueRegenBaseFine + VIG/8 + grit/4 per step, halved while the
/// legs are working. At the base-40 sheet that is 13 fine (3.0 pts/s) easy
/// and 6 fine moving; a VIG-100, grit-20 body makes 25 fine (5.9 pts/s).
inline constexpr std::int32_t kFatigueRegenBaseFine = 8;

/// An empty pool stays winded until it has recovered to max/8. Hysteresis,
/// not flavour: without it a pool at zero regains one step of regen and the
/// sprint gate flickers open and shut sixty times a second.
inline constexpr std::int32_t kWindedRecoverDivisor = 8;

/// Max fatigue in POINTS for a sheet: 2*VIG + MGT + AGI. See the header for
/// why VIG is doubled and WIT is out. Base-40 across the board: 160.
[[nodiscard]] std::int32_t fatigueMaxPoints(const AttributeBlock& attributes) noexcept;

/// What the base-40 sheet's pool comes to, stated as a constant so a
/// default-constructed PlayerFatigue can start there without owning an
/// AttributeBlock. test_fatigue.cpp asserts this equals
/// fatigueMaxPoints(AttributeBlock{}) so the two cannot drift.
inline constexpr std::int32_t kFatigueBasePoints = 4 * kAttributeBase;

/// Regen per movement step, fine units. `moving` halves it -- the reference
/// regenerates only "while not draining"; a body that never regained wind
/// while walking would spend the whole game empty, so walking is half rate
/// rather than none, and the SPRINTING step itself is the one that pays the
/// drain instead.
[[nodiscard]] std::int32_t fatigueRegenFinePerStep(std::int32_t vigor,
                                                   std::int32_t gritLevel,
                                                   bool moving) noexcept;

/// What a roof verb actually costs this body, fine units. AGILITY is the
/// attribute reader (a spry body spends less hauling itself over masonry) and
/// SKYRUNNING is the skill-visible growth: level 20 has taken the cost down
/// by half, a nudge a player feels across the run without the verbs ever
/// being free. Both factors are exactly 1 at the base-40 / level-0 sheet.
[[nodiscard]] std::int32_t verticalFatigueCostFine(std::int32_t basePoints,
                                                   std::int32_t agility,
                                                   std::int32_t skyrunningLevel) noexcept;

// ---------------------------------------------------------------------------
// the attribute readers that ride beside the pool
// ---------------------------------------------------------------------------
//
// Task #84 left the chargen attribute pool "deliberately uncrossed" because no
// mechanic weighed an AttributeId. These four functions are the crossing: one
// reader per attribute, each exactly neutral at the base-40 sheet so a
// character who spent nothing plays the build that shipped yesterday.

/// MGT: hit points a landed melee blow gains, integer and small. (MGT-40)/15:
/// 0 at base, +4 at the ceiling -- beside fists' base 3 that is a heavy hand,
/// not a weapon class.
[[nodiscard]] std::int32_t meleeDamageBonus(std::int32_t might) noexcept;

/// AGI: the legs' own multiplier, Q8, applied to every gait. 256 at base;
/// +4/5 per point above 40, so the ceiling is 304/256 -- about 19% over, a
/// stride a player can see beside a ward actor without the district's
/// distances stopping mattering.
[[nodiscard]] std::int32_t agilitySpeedScaleQ8(std::int32_t agility) noexcept;

/// WIT: percentage points onto the cast check. (WIT-40)/5: 0 at base, +12 at
/// the ceiling -- half a difficulty-6 crafting's whole penalty, never
/// certainty (the cast ceiling still clamps).
[[nodiscard]] std::int32_t castWitBonusPercent(std::int32_t wit) noexcept;

/// WIT: the magicka-analog, spent on RECOVERY rather than on a second pool --
/// a quick mind reopens the link sooner. ticks * (340-WIT)/300: exactly 1x at
/// base 40, 0.8x at the ceiling, and a real cooldown never scales to zero.
[[nodiscard]] std::int64_t witScaledCooldown(std::int64_t cooldownTicks,
                                             std::int32_t wit) noexcept;

// ---------------------------------------------------------------------------
// the pool
// ---------------------------------------------------------------------------

/// The player's wind. SIM STATE, hashed by Tavern::hashInto beside the hit
/// points it stands next to on the HUD; every field integer; no draw anywhere
/// in it -- draining and regenerating are arithmetic, and the term it feeds
/// out only ever reshapes rolls the caller already owns.
class PlayerFatigue {
public:
    /// Sizes the pool from a sheet and FILLS IT. The one caller is the same
    /// boot seam setPlayerHealth is -- a body arrives at the Docks rested.
    void resetFor(const AttributeBlock& attributes) noexcept;

    /// HELD-EFFECTS BUILD. Re-sizes the pool for a sheet WITHOUT refilling:
    /// a held tuning that raises MGT or AGI mid-run grows the ceiling and
    /// grants not one fine unit of free wind (a recastable refill would be
    /// state flat-buying recovery, the S9 guardrail's exact shape), and one
    /// lapsing clamps what is left down to the smaller pool. `winded` is
    /// deliberately untouched -- only regen crossing the recovery line
    /// clears it, exactly as before this method existed.
    void resizeFor(const AttributeBlock& attributes) noexcept;

    [[nodiscard]] std::int32_t currentFine() const noexcept { return currentFine_; }
    [[nodiscard]] std::int32_t maxFine() const noexcept { return maxFine_; }
    /// The bar's own numbers: points, for "fatigue of max" the way hp is drawn.
    [[nodiscard]] std::int32_t currentPoints() const noexcept {
        return currentFine_ / kFatiguePointFine;
    }
    [[nodiscard]] std::int32_t maxPoints() const noexcept {
        return maxFine_ / kFatiguePointFine;
    }
    [[nodiscard]] std::int32_t termQ8() const noexcept {
        return fatigueTermQ8(currentFine_, maxFine_);
    }
    /// True from the drain that emptied the pool until it has recovered to
    /// max/kWindedRecoverDivisor. What the sprint and climb gates read.
    [[nodiscard]] bool winded() const noexcept { return winded_; }

    /// Spends `fine`, floored at zero -- NEVER below: empty is a state, not a
    /// debt, and there is no collapse (see the header). Hitting the floor
    /// sets winded.
    void drain(std::int32_t fine) noexcept;

    /// Recovers `fine`, capped at max. Crossing the recovery line clears
    /// winded.
    void regen(std::int32_t fine) noexcept;

    void hashInto(HashSink& sink) const;

private:
    /// Born full at the base-40 sheet's size, so a Tavern nobody hands a
    /// chargen sheet to -- every pre-existing test, every scripted capture --
    /// plays a rested body rather than a winded one.
    std::int32_t currentFine_ = kFatigueBasePoints * kFatiguePointFine;
    std::int32_t maxFine_ = kFatigueBasePoints * kFatiguePointFine;
    bool winded_ = false;
};

}  // namespace granadad::sim
