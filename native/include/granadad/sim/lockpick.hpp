#pragma once

// A lock, and the thing you do to it with two pieces of wire.
//
// WHY A MINIGAME AND NOT A DIE ROLL
//
// S5 shipped burglary as one line: stand beside a strongbox, press G, and the
// coin is yours. There was no lock on it. CRACKSMANSHIP -- "locks, traps",
// WIT, TRAINED, in content/raws/skills/skills.json since S1 -- was read in
// exactly one place, to decide how much fell out of a box that had already
// opened itself. A skill that only scales a reward is a multiplier, not a
// skill.
//
// So the box is LOCKED, and what a cracksman does about it is played rather
// than resolved. Four things make it a game and not a wait:
//
//   THE PINS ARE SECRET AND FIXED. Each pin's depth is a pure hash of the
//   world seed and the lock's own id -- no draw is consumed, the same lock is
//   the same lock in both halves of a twin run, and a player who has cracked
//   this box before remembers it. Nothing is re-rolled to punish a retry.
//
//   THE PICK CAN BREAK. Strain accumulates on a wrong depth and a pick snaps
//   at kStrainPerPick. Picks are a finite thing you carry; running out ends
//   the attempt with the lock JAMMED, and a jammed lock never opens quietly
//   again.
//
//   SKILL BUYS INFORMATION AND FORGIVENESS, NOT SUCCESS. Below
//   kFeelLevel the lock answers a wrong probe with "nothing"; at and above it
//   the wire tells you which way you were wrong. Tolerance -- how far off a
//   probe may be and still set the pin -- rises with the level. A master
//   cracksman does not open a lock the moment he touches it; he opens it
//   without breaking anything, in a quarter of the tries, and hears what he is
//   doing.
//
//   EVERY PROBE MAKES NOISE. It goes straight into stealth.hpp's StealthState
//   and from there into whether the room noticed. Picking a lock in a full
//   taproom is a different act from picking one at four in the morning, and it
//   is the same rule that decides both.
//
// FORCING is the other door. A jammed or hopeless lock can be broken open with
// what you have: it always works, it is kForceNoise loud, and it damages what
// is inside. That is the consequence for failure the sprint asked for, and it
// is a choice rather than a dead end.
//
// NO FLOATS. NO CONTAINERS THAT REORDER. Everything here is a small integer and
// a fixed array, and the whole state is hashable.

#include <array>
#include <cstdint>
#include <string_view>

#include "granadad/sim/world_hash.hpp"

namespace granadad::sim {

// ---------------------------------------------------------------------------
// the numbers
// ---------------------------------------------------------------------------

/// Most pins a lock in this ward has. A guest's strongbox is three; the
/// district's real doors are for a later sprint and the ceiling is here for
/// them.
inline constexpr std::int32_t kMaxPins = 6;
/// How many depths a pin can sit at. Nine is enough to be a search and few
/// enough to walk with two keys.
inline constexpr std::int32_t kPinDepths = 9;

/// Wrong probes a pick survives before it snaps, before skill.
inline constexpr std::int32_t kStrainPerPick = 3;
/// And what CRACKSMANSHIP adds: one more wrong probe per this many levels.
inline constexpr std::int32_t kStrainPerCraftLevels = 12;
/// Ceiling on the strain a pick will ever take. A master still breaks wire.
inline constexpr std::int32_t kStrainCeiling = 8;

/// The level at and above which the lock tells you WHICH WAY you were wrong.
/// Below it a bad probe is just a bad probe.
inline constexpr std::int32_t kFeelLevel = 10;
/// Levels per point of tolerance -- how far off a probe may be and still set
/// the pin. Zero tolerance below the first band, which is what makes an
/// untrained hand a search of nine depths.
inline constexpr std::int32_t kTolerancePerCraftLevels = 15;
/// And the most forgiveness anybody ever gets. Two: a master can be two
/// notches out and still feel the pin drop, and no further -- a lock that
/// opened at any depth would not be a lock.
inline constexpr std::int32_t kToleranceCap = 2;

/// What a probe does to the air, on stealth.hpp's 0..100 noise scale.
inline constexpr std::int32_t kProbeNoise = 18;
/// A pick snapping is a sound a room hears.
inline constexpr std::int32_t kBreakNoise = 42;
/// And putting a boot to the lid is the loudest thing in this file.
inline constexpr std::int32_t kForceNoise = 88;

/// Picks a body starts the game carrying.
inline constexpr std::int32_t kStartingPicks = 5;
/// What a set of picks costs off the Skyrunners' own contact, per pick.
inline constexpr std::int32_t kPickPrice = 3;
/// And how many he hands over at a time.
inline constexpr std::int32_t kPicksPerSet = 3;

/// What forcing a lock costs whatever was inside, as a percentage kept. A
/// boot through a strongbox lid does not leave the christening cup unbent.
inline constexpr std::int32_t kForcedYieldPercent = 50;

// ---------------------------------------------------------------------------
// the lock
// ---------------------------------------------------------------------------

/// A lock that exists in the world: how hard, and which one it is.
///
/// `id` is what makes its pins ITS pins. Anything lockable in the ward hands
/// out a stable id and gets a stable lock; nothing is generated at open time.
struct Lock {
    std::int32_t id = 0;
    std::int32_t pins = 3;
    /// 0..3. Raises the pins' spread and the strain a wrong probe costs. A
    /// captain's strongbox is not a cellar hasp.
    std::int32_t wards = 0;
};

/// The secret depth of one pin. PURE: (worldSeed, lock id, pin index) in, a
/// depth in 0..kPinDepths-1 out, no state anywhere and no draw consumed.
[[nodiscard]] std::int32_t pinDepth(std::uint64_t worldSeed, const Lock& lock,
                                    std::int32_t pin) noexcept;

/// How far off a probe may be at this level of CRACKSMANSHIP.
[[nodiscard]] std::int32_t pickTolerance(std::int32_t craftLevel) noexcept;
/// How many wrong probes a pick survives at this level, against this lock.
[[nodiscard]] std::int32_t pickStrain(std::int32_t craftLevel, std::int32_t wards) noexcept;
/// Whether the wire talks back at this level.
[[nodiscard]] bool hasFeel(std::int32_t craftLevel) noexcept;

// ---------------------------------------------------------------------------
// the attempt
// ---------------------------------------------------------------------------

/// What one probe felt like.
enum class Feel : std::uint8_t {
    /// Nothing is open.
    Idle = 0,
    /// The pin dropped. On to the next one.
    Set = 1,
    /// Wrong, and you were low. Only ever returned at kFeelLevel and above.
    TooShallow = 2,
    /// Wrong, and you were high. Same.
    TooDeep = 3,
    /// Wrong, and you have no idea which way. What an apprentice gets.
    NoFeel = 4,
    /// Wrong, and that was the pick. One fewer in your roll, strain reset, and
    /// every pin you had set has dropped back.
    Broke = 5,
    /// Wrong, that was the pick, and it was your last one. The lock is jammed:
    /// nothing but force opens it now.
    Jammed = 6,
    /// Every pin set. It is open.
    Open = 7,
    /// You put your shoulder to it. Open, loud, and something inside is bent.
    Forced = 8,
    /// There is nothing here to pick, or it is already open.
    Nothing = 9,
};

[[nodiscard]] std::string_view feelName(Feel feel) noexcept;

/// One lock being worked, and the two pieces of wire working it.
///
/// A single attempt at a time -- a body has two hands. Whoever owns the room
/// owns one of these and hands it the lock.
class Lockpicking {
public:
    /// Starts on a lock. Any attempt already open is abandoned, which loses
    /// nothing: a half-picked lock is a lock.
    void begin(const Lock& lock, std::uint64_t worldSeed, std::int32_t craftLevel) noexcept;

    /// True while there is a lock under the wire.
    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] const Lock& lock() const noexcept { return lock_; }

    /// Which pin the wire is on, and how many are already set.
    [[nodiscard]] std::int32_t pin() const noexcept { return pin_; }
    [[nodiscard]] std::int32_t pinsSet() const noexcept { return pin_; }
    /// Where the pick is being held. Moved by the player, not by the lock.
    [[nodiscard]] std::int32_t depth() const noexcept { return depth_; }
    void moveDepth(std::int32_t delta) noexcept;

    /// Strain on the wire, and what it takes.
    [[nodiscard]] std::int32_t strain() const noexcept { return strain_; }
    [[nodiscard]] std::int32_t strainLimit() const noexcept { return strainLimit_; }

    /// Probes at the depth the pick is being held at.
    ///
    /// `picksLeft` is the roll the body is carrying -- passed in and DECREMENTED
    /// by this call when a pick snaps, because the picks belong to the player
    /// and not to the lock.
    Feel probe(std::int32_t& picksLeft) noexcept;

    /// Gives up. The lock keeps whatever pins were set -- nothing is: a lock
    /// let go of relocks itself, which is the honest version and also the one
    /// that stops a player farming a pin at a time with no risk.
    void abandon() noexcept;

    /// What the last probe felt like, for the surface that draws it.
    [[nodiscard]] Feel lastFeel() const noexcept { return last_; }
    /// True once the lock has actually come open.
    [[nodiscard]] bool opened() const noexcept { return opened_; }
    /// True when the last pick snapped with none left: only force works now.
    [[nodiscard]] bool jammed() const noexcept { return jammed_; }

    /// Probes made on this lock, all told. What a skill charges for.
    [[nodiscard]] std::int32_t probes() const noexcept { return probes_; }

    void hashInto(HashSink& sink) const;

private:
    Lock lock_;
    std::array<std::int32_t, kMaxPins> depths_{};
    bool open_ = false;
    bool opened_ = false;
    bool jammed_ = false;
    std::int32_t pin_ = 0;
    std::int32_t depth_ = 0;
    std::int32_t strain_ = 0;
    std::int32_t strainLimit_ = kStrainPerPick;
    std::int32_t tolerance_ = 0;
    std::int32_t probes_ = 0;
    bool feel_ = false;
    Feel last_ = Feel::Idle;
};

}  // namespace granadad::sim
