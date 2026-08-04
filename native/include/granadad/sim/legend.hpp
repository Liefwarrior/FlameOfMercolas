#pragma once

// WHAT THE WARD WILL REMEMBER YOU FOR: the long game, on five tracks.
//
// THE PROBLEM THIS SOLVES
//
// Nine sprints of systems each grew their own counter and none of them added
// up to anything. The player's CRACKSMANSHIP rises, their SKYRUNNING rises,
// they take rungs on five ladders, they earn coin, they pay off contracts, they
// work a nemesis feud, and the ward has no opinion about any of it beyond the
// one ladder they happen to be highest on. There is no answer to "who am I in
// this city yet", which is the question a sandbox is supposed to be about.
//
// Eli's north star, from memory and from docs/design/DECISIONS.md: SOCIAL POWER
// IS MAXED FROM THE START AND PHYSICAL POWER GROWS THROUGH EXPLOITABLE SYSTEMS.
// So this is not a level, not an XP bar and not a stat. It is the WARD'S
// opinion of what you have been doing, on five tracks that are five different
// people you could turn out to be, and it is entirely DERIVED.
//
// DERIVED, AND THAT IS THE WHOLE DESIGN
//
// Legend holds no state of its own. Every rung is a pure function of counters
// that already existed and were already hashed: the crime tallies, the skill
// track, the faction standings, the contract ledger, the casebook. So:
//
//   - there is nothing new to keep in sync, and nothing that can desync;
//   - it cannot break the twin-run gate, because it adds no state to hash;
//   - a save file that stores the counters stores the legend;
//   - and every rung is EXPLAINABLE. The panel prints what the next one wants
//     in the same units the player already sees, which is the difference
//     between a progression system and a slot machine.
//
// WHAT A RUNG IS WORTH
//
// Three of the five buy something the hands can feel, and they are wired at
// exactly one call site each -- named in the comment on each boon below so a
// reader can check rather than believe. The other two are read by the surface
// and by nothing else, which is stated plainly here rather than implied: a
// title the ward calls you is worth having on its own, and a track that lied
// about doing more would be worse than one that says what it does.
//
// NO FLOATS. Every threshold and every boon is a small integer.

#include <cstdint>
#include <string_view>
#include <vector>

namespace granadad::sim {

class Casebook;
class CrimeLedger;
class FactionLedger;
class SkillTrack;
class ContractBoard;

/// The five people you could turn out to be.
enum class LegendTrack : std::uint8_t {
    /// Lifts, cracked boxes, fenced goods. The hands.
    Wire = 0,
    /// Climbs, leaps, the roof-run tally and SKYRUNNING itself.
    Roofs = 1,
    /// The Flame's own work: the trail followed, and standing in the temple.
    Flame = 2,
    /// Contracts taken and paid, and coin earned honestly.
    Trade = 3,
    /// Standing with the Watch, and a clean sheet. The hardest to hold while
    /// holding any of the first two, which is the point of having both.
    Law = 4,
};

inline constexpr std::size_t kLegendTracks = 5;

[[nodiscard]] std::string_view legendTrackName(LegendTrack track) noexcept;

/// How many rungs a track has above nothing. Four: a name for a beginner, one
/// for somebody the ward has noticed, one for somebody it defers to, and one
/// for somebody it tells stories about.
inline constexpr std::int32_t kLegendRungs = 4;

/// The score at which each rung is reached. Shared by all five tracks, so a
/// player can read one and know all of them -- five different curves would be
/// five different things to learn for no gain.
inline constexpr std::int32_t kLegendThresholds[kLegendRungs] = {8, 25, 60, 120};

/// One track, scored and named.
struct LegendRow {
    LegendTrack track = LegendTrack::Wire;
    /// 0..kLegendRungs.
    std::int32_t rung = 0;
    /// The raw score, in this track's own mixed units.
    std::int32_t score = 0;
    /// What the ward calls you on this track right now. "NOBODY" at rung 0.
    std::string_view title;
    /// The score the next rung wants, or 0 when the track is topped out.
    std::int32_t nextAt = 0;
};

/// Everything the counters add up to. Built by `legendOf`, held by nobody.
class Legend {
public:
    [[nodiscard]] const LegendRow& row(LegendTrack track) const noexcept {
        return rows_[static_cast<std::size_t>(track)];
    }
    [[nodiscard]] const LegendRow* rows() const noexcept { return rows_; }

    /// The track the player is highest on, ties broken toward the earlier
    /// track so two runs cannot disagree. Always valid; at rung 0 across the
    /// board it is Wire with the title "NOBODY", which is an honest thing for
    /// the ward to think of a man who arrived this morning.
    [[nodiscard]] LegendTrack best() const noexcept;
    /// "CUTPURSE" -- the title of `best()`.
    [[nodiscard]] std::string_view title() const noexcept;
    /// Rungs across all five, which is the one number that says "long game".
    [[nodiscard]] std::int32_t totalRungs() const noexcept;

    // --- what a rung buys ---------------------------------------------------

    /// EXTRA PICKS IN A SET, off THE WIRE. Wired in Tavern::buyPicks, which is
    /// the only place a set is ever handed over. A cracksman the ward knows
    /// gets more wire for the same coin, because Finch would rather he came
    /// back.
    [[nodiscard]] std::int32_t picksPerSetBonus() const noexcept;

    /// EXTRA TILES OF LOOK, off THE FLAME. Wired in Session::examine, which is
    /// the only caller of Casebook::look. A Wielder the ward defers to is shown
    /// things from the doorway instead of having to walk in and stand over
    /// them.
    [[nodiscard]] std::int32_t lookRangeBonus() const noexcept;

    /// A DISCOUNT AT A COUNTER, in percent, off THE TRADE. Wired in
    /// Tavern::alePrice and Tavern::bedPrice beside guildPricePercent, which is
    /// the same shape and the same units, so the two stack the way a rung and a
    /// reputation should.
    [[nodiscard]] std::int32_t pricePercent() const noexcept;

    // --- and what the other two are for -------------------------------------
    //
    // THE ROOFS and THE LAW are read by the casebook surface and by nothing
    // else. That is deliberate and it is stated rather than hidden: SKYRUNNING
    // already buys a band of safe drop and a tile of leap carry directly
    // (player.hpp), and the Watch's memory is already the heat model's, so a
    // second modifier on either would be a second answer to a question that has
    // one. What the two tracks add is the ward's NAME for you, which is the
    // thing the game had none of.

private:
    friend Legend legendOf(const CrimeLedger&, const SkillTrack&, const FactionLedger&,
                           const ContractBoard&, const Casebook&);
    LegendRow rows_[kLegendTracks];
};

/// Adds the counters up. Pure: it reads and changes nothing.
[[nodiscard]] Legend legendOf(const CrimeLedger& crimes, const SkillTrack& skills,
                              const FactionLedger& standings, const ContractBoard& work,
                              const Casebook& notes);

/// The authored titles, four per track, in rung order. On this page rather than
/// in a raws file ON PURPOSE: they are the vocabulary the ward's own ladders
/// already use (content/raws/factions/ranks.json's Tenant/Cutpurse, the
/// Temple's Disciple), and inventing a parallel file for twenty strings would
/// put two sources of truth on the same words.
[[nodiscard]] std::string_view legendTitle(LegendTrack track, std::int32_t rung) noexcept;

}  // namespace granadad::sim
