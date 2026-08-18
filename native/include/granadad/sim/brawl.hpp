#pragma once

// The line between a fight the world resolves and a fight that gets its own
// screen.
//
// Eli's ruling: "real-time brawls, dedicated screen for real fights. Fists,
// shoves and bouncer ejections resolve IN-WORLD with no transition. Lethal
// armed combat enters the dedicated first-person combat screen. You will need a
// clear rule for which is which -- make it explicit and testable."
//
// THE RULE
//
// A fight is a BRAWL, and stays in the world, exactly while ALL THREE hold:
//
//   B1  NOTHING EDGED IS OUT.        Every hand in it holds fists, something
//                                    improvised off a table, or a carried
//                                    cudgel. A knife, a boat-hook or a cutlass
//                                    is not a brawl weapon, and drawing one is
//                                    the loudest thing that can happen in a
//                                    taproom.
//   B2  NOBODY MEANS TO KILL.        Intent is state, not a guess: an actor who
//                                    escalates to Kill has decided, and the
//                                    fight changes character the same instant.
//   B3  NOBODY IS BEING FINISHED.    A fighter at or under a quarter health is
//                                    BLOODIED. Beating a bloodied man while
//                                    meaning him Harm is not a bar fight any
//                                    more, whatever is in your hands.
//
// The moment any one of them stops holding, the fight is LETHAL and belongs to
// the dedicated first-person combat screen (docs/design/COMBAT-SCREEN-SPEC.md).
//
// B3 is the clause that makes this a rule rather than an inventory check. Two
// men swinging stools at each other is a brawl however long it goes on, as long
// as neither has decided to do more than win: a Subdue beating that puts
// someone on the floor is exactly what a bouncer does for a living. What tips
// it is intent meeting damage.
//
// WHY IT LIVES IN SIM. The classification decides whether the client transitions
// screens, so it cannot be a renderer's opinion; and it is pure integer logic
// over state the world already owns, so it costs nothing to keep here where the
// twin-run gate can see it.

#include <cstdint>
#include <span>
#include <string_view>

#include "granadad/sim/fatigue.hpp"

namespace granadad::sim {

/// What a fighter has in their hands.
///
/// Order matters: everything below kFirstLethalWeapon keeps a fight in the
/// world, and everything from it up does not. Adding a weapon means deciding
/// which side of that line it falls on, which is the point of ordering it.
enum class Weapon : std::uint8_t {
    /// Fists, boots, elbows, a shoulder in the chest.
    Fists = 0,
    /// Whatever was on the table: a stool, a tankard, a bottle still corked.
    Improvised = 1,
    /// A cudgel carried on purpose. A bouncer's tool, and still not a blade.
    Blunt = 2,
    /// Made to open a man: knife, boat-hook, cutlass, marlinspike.
    Edged = 3,
};

/// The first weapon that takes a fight out of the world.
inline constexpr Weapon kFirstLethalWeapon = Weapon::Edged;

[[nodiscard]] std::string_view weaponName(Weapon weapon) noexcept;

/// How far a fighter means to take it.
enum class Intent : std::uint8_t {
    /// Win, and stop. A bouncer's whole professional life.
    Subdue = 0,
    /// Hurt him, and not much care how badly.
    Harm = 1,
    /// Decided.
    Kill = 2,
};

[[nodiscard]] std::string_view intentName(Intent intent) noexcept;

enum class FightClass : std::uint8_t {
    /// Resolved here, in the world, with no transition.
    Brawl = 0,
    /// The dedicated first-person combat screen's business.
    Lethal = 1,
};

[[nodiscard]] std::string_view fightClassName(FightClass fight) noexcept;

/// One participant, as the rule sees them.
struct Fighter {
    std::int32_t actorId = 0;
    Weapon weapon = Weapon::Fists;
    Intent intent = Intent::Subdue;
    std::int32_t hp = 1;
    std::int32_t hpMax = 1;
};

/// A fighter is bloodied at or under a QUARTER of their health. Expressed as a
/// ratio rather than a fraction so the test is exact integer arithmetic at any
/// hpMax: bloodied when hp * kBloodiedDenominator <= hpMax * kBloodiedNumerator.
inline constexpr std::int32_t kBloodiedNumerator = 1;
inline constexpr std::int32_t kBloodiedDenominator = 4;

[[nodiscard]] constexpr bool isBloodied(std::int32_t hp, std::int32_t hpMax) noexcept {
    return hp * kBloodiedDenominator <= hpMax * kBloodiedNumerator;
}
[[nodiscard]] constexpr bool isBloodied(const Fighter& fighter) noexcept {
    return isBloodied(fighter.hp, fighter.hpMax);
}

/// Down and out of the fight, but not dead: a brawl's natural end.
[[nodiscard]] constexpr bool isDowned(std::int32_t hp) noexcept { return hp <= 0; }

/// THE RULE, applied to everyone in the fight at once. See the header.
///
/// An empty or single-fighter list is a Brawl: there is nothing to escalate.
[[nodiscard]] FightClass classifyFight(std::span<const Fighter> fighters) noexcept;

/// Whether a fight of this class is resolved by the world rather than by the
/// combat screen. One place, so no caller re-derives the polarity.
[[nodiscard]] constexpr bool resolvesInWorld(FightClass fight) noexcept {
    return fight == FightClass::Brawl;
}

// ---------------------------------------------------------------------------
// what a blow does
// ---------------------------------------------------------------------------

/// Damage a weapon does before any roll, in hit points.
[[nodiscard]] std::int32_t baseDamage(Weapon weapon) noexcept;

/// Q8 impulse a shove imparts along the shover's facing. A shove is the
/// bouncer's actual job: it does no damage and it moves people.
inline constexpr std::int32_t kShoveImpulse = 96;

/// Reach of an in-world blow or shove, Q8. A tile and a quarter: far enough to
/// hit somebody standing on the next tile, and not far enough to reach across a
/// bar counter or past the man in front of you.
inline constexpr std::int32_t kMeleeReach = 320;

/// One resolved blow.
struct Blow {
    bool landed = false;
    std::int32_t damage = 0;
    /// True when the blow put the target under the bloodied line.
    bool bloodied = false;
    /// True when the blow took the target to zero.
    bool downed = false;
};

/// Resolves one blow against `target`, mutating its hp. `roll` is a raw draw
/// from the system's own RNG; only its low bits are used, and it is taken as an
/// argument rather than drawn here so the caller owns the draw ORDER, which is
/// the thing the twin-run gate is actually watching.
///
/// FATIGUE BUILD, and the two defaults ARE the old function. `damageBonus` is
/// MGT's runtime reader ((MGT-40)/15 -- see fatigue.hpp) and lands only on a
/// blow that connected, floored so a weak arm still bruises.
/// `fatigueTermQ8` reshapes the whiff: the shipped 1-in-8 miss (low three
/// bits of the roll all zero) is UNTOUCHED and is the whole of the miss at a
/// full pool, and an emptying pool adds a SECOND miss band read off the
/// roll's own high bits -- (roll>>32)&63 against (full-term)/8, so 0 wide at
/// full and 16/64 wide at empty, roughly 34% total whiff for an exhausted
/// swing. SAME-ROLL DISCIPLINE: no new draw exists anywhere in this -- both
/// bands and the variance are carved out of the one roll the caller already
/// owned, and at kFatigueTermFullQ8 the function is bit-identical to what it
/// replaced (test_fatigue.cpp sweeps that equivalence).
///
/// Ward brawlers call this with the defaults: they carry no pool in this
/// build (see fatigue.hpp's header on where the player-scoped line is drawn).
[[nodiscard]] Blow strike(Weapon weapon, Fighter& target, std::uint64_t roll,
                          std::int32_t damageBonus = 0,
                          std::int32_t fatigueTermQ8 = kFatigueTermFullQ8) noexcept;

// ---------------------------------------------------------------------------
// the guard
// ---------------------------------------------------------------------------

/// The skill a held guard is worked with. content/raws/skills/skills.json's own
/// row: "covers": "block, shield use", governing VIG, NEGLECTED tier. Named
/// once here the way kCraftingSkill is named once in spellforge.hpp.
inline constexpr std::string_view kBlockSkill = "shieldwall";

/// What an untrained guard still lets through of a landed blow, percent, and
/// the floor a trained one can push that down to. Every SHIELDWALL level takes
/// one percent off, so the whole run from raw to floor is forty levels of use.
inline constexpr std::int32_t kBlockKeptPercentAtZero = 60;
inline constexpr std::int32_t kBlockKeptPercentFloor = 20;

/// What a guarded body still takes of a landed blow.
///
/// SKILL-SCALED, NEVER TO ZERO. A raw guard already helps -- forty percent of
/// a blow turned by nothing but raised arms -- and SHIELDWALL buys the rest a
/// level at a time, down to kBlockKeptPercentFloor and no further; the
/// max(1, ...) means a landed blow always costs at least one hit point through
/// any guard at any level. A block that could null damage outright would be a
/// wall of skill that turns the fight OFF, which is the flat-outcome shape the
/// standing Morrowind steer exists to refuse: skill buys forgiveness, not
/// immunity.
///
/// PURE INTEGERS, PURE FUNCTION. No draw and no state: the caller owns both,
/// exactly as strike()'s own contract says, so a held guard never moves the
/// draw stream -- the same roll lands, and the guard only argues about what
/// it is worth.
[[nodiscard]] std::int32_t blockedDamage(std::int32_t damage,
                                         std::int32_t shieldwallLevel) noexcept;

}  // namespace granadad::sim
