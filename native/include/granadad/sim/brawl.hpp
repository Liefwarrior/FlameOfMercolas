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
[[nodiscard]] Blow strike(Weapon weapon, Fighter& target, std::uint64_t roll) noexcept;

}  // namespace granadad::sim
