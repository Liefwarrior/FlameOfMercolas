#pragma once

// The Watch, and what happens when it takes you.
//
// WHAT WAS MISSING. S5 shipped a warrant: heat past sixty, a red line on the
// HUD, hysteresis so it cannot flicker, a hashed bit in the ledger -- and its
// own header said, out loud, "NOTHING ARRESTS. A warrant is issued, hashed,
// shown in red on the HUD and read by exactly one thing -- how long a bouncer
// waits before putting you out." The S5 review agreed: "a warrant's only
// readers are a bouncer's timer and a menace sum." A law with no consequence is
// a number on a sheet, and it makes every crime free.
//
// This file is the consequence. It is RULES ONLY -- no room, no actor, no
// pathfinding. Whoever owns the taproom decides who is standing where and
// whether they can see you (Tavern::tickWatch); everything about what that
// costs is here, so it can be reasoned about and mutated on its own.
//
// THE SENTENCE IS CANON'S, NOT OURS. DECISIONS.md, Eli 2026-07-14, verbatim:
// "guards should arrest criminals for 1-3 days before turning them loose unless
// they're a skyrunner then it's cut off their hand first offense and hanging on
// the second." That is the whole of sentenceFor(), including the asymmetry: an
// ordinary thief is HELD and released, and only a Skyrunner is maimed or
// condemned. ACTORS-SPEC's older "the Watch arrests, never executes" still
// stands for everyone else, and the two documents are read together.
//
// WHAT A "CONDEMNED" PLAYER IS, HONESTLY. Nothing in this build kills the
// player -- a brawl floors at kPlayerBrawlFloor and a roof cannot do better.
// So a second Skyrunner offence sets a STATUS the ward and the HUD both know
// about, and no death is simulated. See Sentence::Condemned.
//
// NO FLOATS. Every number is an integer or a permille against a draw, which is
// the same shape passes() already gives the rest of the simulation.

#include <cstdint>
#include <string_view>

namespace granadad::sim {

// ---------------------------------------------------------------------------
// why a watchman is looking at you
// ---------------------------------------------------------------------------

enum class WatchCause : std::uint8_t {
    /// Nothing. Have a good evening.
    None = 0,
    /// There is paper out on you and he has seen your face.
    Warrant = 1,
    /// He has seen what you are carrying.
    Contraband = 2,
    /// Both, which is the difference between a fine and a cell.
    Both = 3,
};

[[nodiscard]] std::string_view watchCauseName(WatchCause cause) noexcept;

/// What a watchman who has just looked at this player has cause to do.
///
/// `noticed` is the caller's answer to "did he see the load", because whether
/// he could see you at all is a question about a room and this file does not
/// know there is one.
[[nodiscard]] WatchCause watchCause(bool warrant, bool noticed,
                                    std::int32_t illicitUnits) noexcept;

// ---------------------------------------------------------------------------
// noticing
// ---------------------------------------------------------------------------

/// Simulated seconds between one look and the next. A watchman off duty with a
/// drink in his hand is not frisking the room; he glances up.
inline constexpr std::int32_t kWatchLookSeconds = 5;

/// How far a watchman's interest carries, in tiles. The same eight the ward's
/// own witness rule uses, because it is the same question -- who could see that
/// -- asked by somebody with a reason to care.
inline constexpr std::int32_t kWatchSightTiles = 8;

/// The chance in a thousand that this look finds what you are carrying.
///
/// THREE TERMS AND A CLAMP, the shape every check in this build uses: the LOAD,
/// which is quoted in drams and not in units on purpose -- eight jars of spirit
/// are conspicuous and eight twists of dust are not; the watchman's own
/// KIT-KEEPING, which is literally the skill of knowing what is in a crate; and
/// the carrier's STREETWISE, which is the only thing a player can raise to
/// answer it. Never certain in either direction: a clean man is never taken for
/// a load he is not carrying, and a loaded one is never safe.
[[nodiscard]] std::int32_t noticePermille(std::int32_t illicitDrams,
                                          std::int32_t carrierStreetwise,
                                          std::int32_t watchmanKit) noexcept;

/// The chance in a thousand that a look CONNECTS THE FACE to the paper.
///
/// A warrant is not a beacon. Watchman Cull is off shift with a drink in his
/// hand; he has not got the file in his pocket and he is not checking faces at
/// the door. What he does is glance up, and one glance in twenty-two puts a
/// name to somebody he has been half-watching all evening -- which is exactly
/// what his own authored line says he is doing ("I have been looking at you for
/// a while now").
///
/// It matters mechanically as well as in fiction, and the number was TUNED
/// rather than guessed: with paper alone as instant cause, a wanted man cannot
/// cross the taproom the only door is on, and "wanted" stops meaning "do not be
/// seen" and starts meaning "the game is over" -- the S6 build's own scripted
/// Skyrunner line was arrested on its way back from the last delivery and lost
/// two of nine beats to it. At this rate a man who walks through and out is
/// usually through, and about two minutes in front of him is a coin flip.
///
/// Note what this does NOT soften: a LOAD is noticed on its own terms and far
/// more readily (noticePermille), because a full sack is a thing you can see
/// from across a room and a face is not.
inline constexpr std::int32_t kRecognisePermille = 45;

/// And what a CONDEMNED face is worth, which is a different question. A warrant
/// is paper in a drawer; a sentence of the rope was passed in public and every
/// watchman in the ward was told the face it was passed on. So a condemned man
/// is recognised whether or not there is a warrant out, at better than two and
/// a half times the rate of one -- and being taken again costs him the sack,
/// the fine, the jobs those goods were for and the night, every time.
///
/// S7 FIXED AN EXPLOIT WITH THIS NUMBER. Tavern::tickWatch used to return
/// early on condemned(): no notice, no recognition, no arrest, forever. Two
/// Skyrunner arrests bought permanent immunity from the law, so the ward's
/// harshest sentence was mechanically its safest state. It is not a status bit
/// the Watch ignores; it is the reason the Watch does not have to work for it.
inline constexpr std::int32_t kCondemnedRecognisePermille = 120;

/// Seconds a watchman will chase before he gives it up and goes back to his
/// drink. Long enough to be caught standing still, short enough that the door
/// is a real answer -- which is the whole counterplay and what the roofs are
/// for.
inline constexpr std::int32_t kWatchClosingSeconds = 12;

// ---------------------------------------------------------------------------
// the sentence
// ---------------------------------------------------------------------------

enum class Sentence : std::uint8_t {
    /// Not taken.
    None = 0,
    /// Goods to the impound, coin to the ward, and go home. What a search with
    /// no warrant behind it comes to.
    Fined = 1,
    /// A cell, one to three days of it, and out. The ordinary answer, and the
    /// only one an ordinary thief ever gets.
    Held = 2,
    /// A Skyrunner's first offence. Canon is the hand; nothing in this build
    /// models a limb, so it is a lasting penalty on the hands' own trade and a
    /// status the ward remembers. See maimedTakePercent.
    Maimed = 3,
    /// A Skyrunner's second. Canon is the rope.
    ///
    /// NOTHING HERE KILLS THE PLAYER, and this is said plainly rather than
    /// implied: no death is simulated, the body is not removed, and play
    /// continues. What the sentence does is make the state permanent and
    /// visible -- the ward has condemned you, everyone knows it, and no favour
    /// clears it. The day this build has a combat screen and a death, this is
    /// the hook it hangs on.
    ///
    /// WHAT IT DOES TO THE LAW, said out loud because S6's version of this
    /// comment did not and the omission WAS the bug. Condemnation does not
    /// switch the Watch off. A condemned man is recognised on sight at
    /// kCondemnedRecognisePermille -- warrant or no warrant -- and every
    /// subsequent arrest still empties the sack, takes the fine, kills the
    /// jobs those goods were for and jumps the clock. The sentence is the
    /// worst state in the game to be in, not the safest.
    Condemned = 4,
};

[[nodiscard]] std::string_view sentenceName(Sentence sentence) noexcept;

/// What the ward passes down. `priorArrests` counts the times this player has
/// been taken BEFORE this one.
[[nodiscard]] Sentence sentenceFor(bool skyrunner, bool warrant,
                                   std::int32_t priorArrests) noexcept;

/// Hours in a cell. One to three days, drawn -- canon says a range and a range
/// wants a roll.
inline constexpr std::int32_t kHeldHoursMin = 24;
inline constexpr std::int32_t kHeldHoursMax = 72;
[[nodiscard]] std::int32_t heldHours(std::uint64_t draw) noexcept;

/// Coin the ward takes: a charge for the paper, and a charge a unit for what
/// was in the sack. Capped at what the player actually has by the caller --
/// nobody is put in debt by a fine in this build.
[[nodiscard]] std::int32_t fineFor(std::int32_t heat, std::int32_t unitsSeized) noexcept;

/// What a maimed hand still manages, as a percentage of what two hands take.
/// Permanent, and it is the only lasting statistical penalty in the game.
inline constexpr std::int32_t kMaimedTakePercent = 50;

/// VERIFICATION GAP (S6): THERE IS NO CELL. A sentence jumps the clock, empties
/// the sack, takes the fine and puts the body back on the Tarwalk -- and
/// nothing between those facts is simulated. No patrol looks for you outside
/// the Gilded Gull, no impound holds the goods as objects, and the gibbet
/// DECISIONS.md names for a second Skyrunner offence is a status bit. The
/// consequence is real and the PLACE it happens in is not built.
///
/// The heat left after a night in a cell. NOT ZERO: the ward has not forgotten
/// what you did, it has been paid for it. Below kWarrantLapsesAt so the paper
/// goes with the sentence -- you cannot be wanted for the thing you have just
/// served for.
inline constexpr std::int32_t kHeatAfterSentence = 12;

}  // namespace granadad::sim
