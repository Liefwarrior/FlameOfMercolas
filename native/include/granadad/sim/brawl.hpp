#pragma once

// The line between a bar fight and a killing, both fought IN THE WORLD.
//
// Eli's action-combat ruling (2026-09-02, DECISIONS.md:63) scrapped the paused
// combat screen: combat is real-time, first-person, in the tick stream, with no
// transition. This classifier SURVIVES as the LEGALITY layer -- the brawl line
// (DECISIONS.md:37) still rules what a fight IS -- but the answer no longer
// picks a venue. Both a brawl and a lethal fight resolve right here; the class
// picks the RULES: fists-and-Subdue is a bar fight with all its floors, and
// steel out / Kill meant / a bloodied man beaten is LETHAL, where blows can
// kill, the Watch has a cause, and the player's own death routes through the
// respawn/nemesis ruling. See COMBAT-ACTION-SPEC.md.
//
// THE RULE
//
// A fight is a BRAWL, and stays under bar-fight rules, exactly while ALL THREE hold:
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
// The moment any one of them stops holding, the fight is LETHAL: it keeps
// resolving in the world, but under lethal RULES (blows kill, floors lifted,
// the Watch minds it) -- see COMBAT-ACTION-SPEC.md section 4.
//
// B3 is the clause that makes this a rule rather than an inventory check. Two
// men swinging stools at each other is a brawl however long it goes on, as long
// as neither has decided to do more than win: a Subdue beating that puts
// someone on the floor is exactly what a bouncer does for a living. What tips
// it is intent meeting damage.
//
// WHY IT LIVES IN SIM. The classification decides what a landed blow is allowed
// to do -- whether it can kill, whether the Watch has a cause, which floor the
// player takes -- so it cannot be a renderer's opinion; and it is pure integer
// logic over state the world already owns, so it costs nothing to keep here
// where the twin-run gate can see it.

#include <cstdint>
#include <span>
#include <string>
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
    /// THE EVICTOR: a headed cudgel made for putting a man OUT rather than
    /// open -- the eviction case's reward. A cudgel in every way the line
    /// below cares about, and the one weapon whose landed blows read a head
    /// band off the roll -- see strike() and kEvictorHeadBand256.
    Evictor = 3,
    /// Made to open a man: knife, boat-hook, cutlass, marlinspike.
    ///
    /// RENUMBERED 3 -> 4 when the Evictor took the seat below the line. The
    /// value is a hashed byte (actor.cpp's hash_into, tavern.cpp's
    /// playerWeapon_), so this would move the world hash of any run with an
    /// Edged hand in it -- which no gate workload has: bouncers carry Blunt,
    /// the player boots with Fists, and a nemesis draws Edged only after
    /// three player victories. The population baseline never registers the
    /// Tavern at all. Stated here so the renumber is a decision on the
    /// record, and verified by the twin gate rather than trusted.
    Edged = 4,
};

/// The first weapon that takes a fight out of the world.
inline constexpr Weapon kFirstLethalWeapon = Weapon::Edged;

/// The id an authored case grants THE EVICTOR by -- the entire contract
/// between a casebook reward beat and this enum, so neither side includes
/// the other's headers. See Tavern::grantPlayerWeapon.
inline constexpr std::string_view kEvictorWeaponId = "the_evictor";

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
    /// Bar-fight rules: nobody dies, the player floors at kPlayerBrawlFloor,
    /// the house handles it. Resolved in the world.
    Brawl = 0,
    /// Lethal rules: blows can kill, floors are lifted, and the Watch has a
    /// cause. Also resolved in the world -- there is no combat screen; the
    /// class picks the rules, not a venue. See COMBAT-ACTION-SPEC.md section 4.
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

/// Whether a fight of this class runs under BAR-FIGHT rules (nobody dies, the
/// player floors, the house handles it) rather than LETHAL rules. RETIRED AS A
/// VENUE by the action-combat build: every fight now resolves in the world, so
/// this no longer means "resolved here vs. on a screen" -- it selects the rule
/// set. One place, so no caller re-derives the polarity. Retained because live
/// callers still read it; its meaning, not its shape, is what changed.
[[nodiscard]] constexpr bool resolvesInWorld(FightClass fight) noexcept {
    return fight == FightClass::Brawl;
}

// ---------------------------------------------------------------------------
// what a blow does
// ---------------------------------------------------------------------------

/// Damage a weapon does before any roll, in hit points.
[[nodiscard]] std::int32_t baseDamage(Weapon weapon) noexcept;

/// The variance ceiling strike() carves off the roll -- (roll>>3)%3, so a
/// landed blow is worth base .. base+this before the MGT bonus. Named for
/// weaponSheetLine below; strike()'s own %3 stays the ruling copy.
inline constexpr std::int32_t kStrikeVarianceMax = 2;

/// The weapon as a sheet prints it -- the reference sheet's own
/// "cane 1-6 Impact" idiom (UI-REFERENCE-TERMINAL.md): the name, the
/// unrolled span, the class. Muscle is the MIGHT row's business, not this
/// line's, so the span is the weapon's own.
[[nodiscard]] std::string weaponSheetLine(Weapon weapon);

/// THE HEAD IS ON THE ROLL. COMBAT-SPEC section 4.1's location table gives
/// the head 24 of 256, and section 9's C6 rules that v1 location is a pure
/// weighted draw, never an aim. The Evictor honours both: every landed blow
/// it throws reads an r8 band off bits 8-15 of the SAME roll the whiff and
/// the variance were already carved from -- the fatigue build's same-roll
/// discipline, third carving; the whiff owns bits 0-2 and the tired band
/// bits 32-37 -- and inside the band the blow found the head and the man
/// goes down whatever the arithmetic left him. That is the temple-KO canon
/// the spec's own DAZE rider cites (novel L1165, L172), turned up to the
/// knockout this weapon was forged for. NO NEW DRAW EXISTS ANYWHERE IN THIS.
inline constexpr int kEvictorHeadRollShift = 8;
inline constexpr std::uint64_t kEvictorHeadBand256 = 24;

/// Q8 impulse a shove imparts along the shover's facing. A shove is the
/// bouncer's actual job: it does no damage and it moves people.
inline constexpr std::int32_t kShoveImpulse = 96;

/// Reach of an in-world blow or shove, Q8. A tile and a quarter: far enough to
/// hit somebody standing on the next tile, and not far enough to reach across a
/// bar counter or past the man in front of you.
inline constexpr std::int32_t kMeleeReach = 320;

/// HALF THE WIDTH OF THE BEAM the crosshair casts, Q8. VETO 1's one tuning
/// knob, and it is a DISTANCE, not an angle: a body is "on the line" when its
/// perpendicular offset from the look-ray is within this, so the beam is the
/// same half-cell wide point-blank and at reach. That is what "you were looking
/// right at them" means. See Tavern::sightlineTarget and COMBAT-ACTION-SPEC.md
/// section 2.1 -- the draft's 120-degree cone is dead; this replaces it.
inline constexpr std::int32_t kBodyHalfWidth = 128;

/// The Q8 damage scale strike() reads for the two swing tiers -- a swing is 1x
/// (256, the bit-identical baseline) and a HARD swing is 2x (512), doubling the
/// rolled weapon damage before the MGT bonus. A PARAMETER, never a draw: the
/// charge tier is measured in integer hold-steps by the caller and passed in,
/// so the same-roll discipline is untouched. See strike() and section 2.2.
inline constexpr std::int32_t kSwingChargeQ8 = 256;
inline constexpr std::int32_t kHardSwingChargeQ8 = 512;

/// One resolved blow.
struct Blow {
    bool landed = false;
    std::int32_t damage = 0;
    /// True when the blow put the target under the bloodied line.
    bool bloodied = false;
    /// True when the blow took the target to zero.
    bool downed = false;
    /// True when an Evictor blow found the head band (kEvictorHeadBand256):
    /// downed outright, whatever the damage arithmetic left. No other weapon
    /// ever sets it, so every existing caller reads exactly what it always
    /// read.
    bool crowned = false;
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
/// THE EVICTOR reads one more band off the same roll -- see
/// kEvictorHeadBand256's own header -- and no other weapon does, so for
/// every weapon that shipped before it this function is bit-identical.
///
/// ACTION-COMBAT BUILD, and the THREE defaults ARE the old function.
/// `chargeQ8` is the swing tier (§2.2): kSwingChargeQ8 (256) is a tap and
/// kHardSwingChargeQ8 (512) a held hard swing, applied as
/// ((base+variance)*chargeQ8)>>8 BEFORE the damageBonus and the max(1,..)
/// floor -- so a hard swing doubles the rolled weapon damage and a bare hand
/// still bruises. It is a PARAMETER and not a draw: the whiff bands and the
/// crown are carved off the same roll at both tiers (a hard swing is not a
/// truer swing), and at kSwingChargeQ8 the function is bit-identical to what
/// it replaced (test_fatigue.cpp sweeps that equivalence).
///
/// Ward brawlers and NPC swings call this with the defaults: they carry no
/// pool and no charge in this build (see fatigue.hpp's header on where the
/// player-scoped line is drawn).
[[nodiscard]] Blow strike(Weapon weapon, Fighter& target, std::uint64_t roll,
                          std::int32_t damageBonus = 0,
                          std::int32_t fatigueTermQ8 = kFatigueTermFullQ8,
                          std::int32_t chargeQ8 = kSwingChargeQ8) noexcept;

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
