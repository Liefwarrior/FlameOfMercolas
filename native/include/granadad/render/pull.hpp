#pragma once

// THE PULL PACK -- the street finally says where next.
//
// ---------------------------------------------------------------------------
// WHAT THIS IS
// ---------------------------------------------------------------------------
// Oblivion's single most load-bearing quest device is not the journal, it is
// the compass: ONE active quest the player chose, its target named on the
// ribbon with a paces count that runs down as you walk ("Weye 165"), and a
// banner plus a chime every time the book changes so a player who never opens
// the journal still feels pulled. Granadad's casebook already computes every
// number that needs -- the bearing, the paces, the authored next lead -- and
// until this pass printed all of it inside the book and none of it on the
// street. The owner's own stall at the Weighhouse was fixed inside the book;
// this is the street half.
//
// Four things live here, all of them RENDER STATE:
//
//   1. THE FOLLOWED LEAD. FollowedLead is the one lead the player chose to
//      FOLLOW (the casebook page's F / X verb), defaulting to the authored
//      next lead of the fronted book so a returning player is still told
//      where they were going. It is NEVER HASHED and NEVER reaches sim: a
//      selector inside sim::Casebook would move the world hash for a UI act,
//      which is the one thing the twin-run gate exists to forbid.
//      test_pull.cpp proves the tavern and world hashes are byte-identical
//      with and without a followed lead.
//   2. THE RIBBON LINE. "NE 40  THE WEIGHHOUSE" under the compass, from the
//      SAME bearing/paces arithmetic the casebook page prints -- one function
//      (pullBearing), two surfaces, so the book and the street can never
//      disagree about where a lead is. Double math is legal here: this is the
//      render side of the sim boundary (controls.hpp's own rule) and nothing
//      it computes is ever fed back.
//   3. THE SKILL-UP TOAST. sim::SkillTrack::use() has returned `levelled`
//      since S17 and every caller threw it away. SkillRiseWatch diffs the
//      track's levels once a step on the render side, so "SKYRUNNING RISES
//      TO 12" fires in situ -- mid-climb, mid-fight -- with no sim change, no
//      hash change and no draw.
//   4. THE BOOK NEWS. BookNewsWatch diffs the three books, the journal and
//      the radiant board once a step, so EVERY change to what the player is
//      working on lands on the one announcement plate with one cue -- the two
//      scripted hears the courier and eviction cases made silently, a
//      questline stage, an errand taken.
//
// ---------------------------------------------------------------------------
// THE MARKER DOCTRINE (owner ruling D8)
// ---------------------------------------------------------------------------
// Ticks on the ribbon for DISCOVERED NAMED PLACES, and one text line for the
// ONE lead the player chose to follow. Never a person, never a clue, never
// through a wall, no marks on the map plan by default. So the line names the
// lead's PLACE (the sign's own words), not its `who` and not its `what`; the
// ticks are bearings on a strip of sky, not arrows in the world; and the
// player picks via FOLLOW. It is a bearing, not a route: the Docks is still
// one lap on foot and the signs still have to be read.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/casebook.hpp"
#include "granadad/sim/questline.hpp"
#include "granadad/sim/radiant_quest.hpp"
#include "granadad/sim/social.hpp"

namespace granadad::render {

/// Which of Session's three fixed books a lead index belongs to, in the
/// fixed member order Session declares them (casebook_, sheetBook_,
/// evictBook_). An index into BookSet, never a hashed value.
enum class CaseBookId : std::uint8_t {
    Bloodletter = 0,
    Courier = 1,
    Eviction = 2,
};
inline constexpr int kCaseBookCount = 3;

/// The three books, read-only, as the pull sees them. Session fills it from
/// its own members every time it asks a question here; nothing is kept.
struct BookSet {
    std::array<const sim::Casebook*, 3> books{};
    std::array<const sim::CasebookRaws*, 3> raws{};

    [[nodiscard]] const sim::Casebook* book(CaseBookId id) const noexcept {
        return books[static_cast<std::size_t>(id)];
    }
    [[nodiscard]] const sim::CasebookRaws* raw(CaseBookId id) const noexcept {
        return raws[static_cast<std::size_t>(id)];
    }
    /// A book the player has actually been handed: bound to raws and holding
    /// at least one lead. The eviction book before the writ, and the courier
    /// book before the courier, are not.
    [[nodiscard]] bool begun(CaseBookId id) const noexcept;
    /// Begun and not closed -- the courier chassis's own "live" rule.
    [[nodiscard]] bool live(CaseBookId id) const noexcept;
};

/// THE ONE LEAD THE PLAYER CHOSE TO FOLLOW. lead < 0 means nothing chosen:
/// the pull falls back to the authored next lead (resolvePull). Render state,
/// never hashed, never encoded -- see the file header.
struct FollowedLead {
    CaseBookId book = CaseBookId::Bloodletter;
    std::int32_t lead = -1;
    [[nodiscard]] bool chosen() const noexcept { return lead >= 0; }
    void clear() noexcept { lead = -1; }
};

/// What the pull resolves to on a given frame.
struct PullTarget {
    bool set = false;
    CaseBookId book = CaseBookId::Bloodletter;
    std::int32_t lead = -1;
    /// True when this is the lead the player picked; false when it is the
    /// authored default standing in for a choice nobody made.
    bool chosen = false;
};

/// The book the auto rule fronts: the eviction while it lives, then the
/// courier's errand, then the Bloodletter -- Session::activeCaseRaws()'s own
/// precedence, stated once here so the pull and the page agree.
[[nodiscard]] CaseBookId autoFrontedBook(const BookSet& books) noexcept;

/// THE DEFAULT LEAD: the NEWEST-HEARD Open lead of a book, or -1. Not
/// Casebook::nextOpen() -- that is the FIRST Open lead in authored order,
/// which after TAKE HIM UP is the snug stool beside your feet rather than
/// the back room the book just heard. What a player was last told about is
/// where they were going. Ties (several leads opened by one look, one
/// dateline) fall to authored order, so the fresh book still opens on the
/// flagstones.
[[nodiscard]] std::int32_t newestOpenLead(const sim::Casebook& book) noexcept;

/// Resolves the followed lead against the live books.
///
///   * The chosen lead, while it is still Open in a LIVE book. A lead that
///     has been stood over (Followed or Cold) is no longer a place to go, and
///     neither is any lead of a case that has closed, so the choice lapses
///     and the default takes over -- the player never has to let go of a lead
///     they just walked to.
///   * Otherwise the fronted book's newest-heard Open lead (newestOpenLead),
///     while that book lives.
///   * Otherwise the newest-heard Open lead of any live book, in auto
///     precedence.
///   * Otherwise nothing: the ribbon line is empty and the street is quiet.
[[nodiscard]] PullTarget resolvePull(const FollowedLead& followed, const BookSet& books,
                                     CaseBookId fronted) noexcept;

/// How a lead on another plane is worded. The BOOK prints the band's number
/// ("BAND 18"): a reader with the page up can see the two planes side by
/// side. The STREET cannot see its own band, so the ribbon says BELOW or
/// ABOVE -- the fact a walking body can act on.
enum class BandWord : std::uint8_t { Number = 0, Relative = 1 };

/// "NE 40", plus "  BAND 18" (or "  BELOW" / "  ABOVE") when the site is on
/// another plane -- THE ONE bearing/paces arithmetic. The casebook page's
/// commit verb and the ribbon line both print exactly this string, from
/// this function, so a player reading NE 40 in the book finds NE 40 on the
/// street. Euclidean paces, rounded, the way the page has always counted
/// them. `here` is the caller's own answer to "could the body LOOK at it
/// from where it stands" (the legend's reach bonus included), and prints as
/// HERE. The compass POINT stays eight-point: it is a word.
[[nodiscard]] std::string pullBearing(std::int32_t px, std::int32_t py, std::int32_t band,
                                      const sim::LeadSite& site, bool here,
                                      BandWord bandWord = BandWord::Number);

/// THE TRUE BEARING for a ribbon tick, BAM 0..65535 (0 north, 16384 east),
/// off a real atan2 -- render-side double math, legal on this side of the
/// sim boundary and never fed back. NOT sim::bearingTo: that is the stealth
/// pass's eight-point quantiser, and a notch snapped to a compass letter
/// sits under the letter rather than on the bearing, so ten discovered
/// places collapse onto two notches. Same tile answers 0.
[[nodiscard]] std::int32_t pullTickBam(std::int32_t px, std::int32_t py, std::int32_t tx,
                                       std::int32_t ty) noexcept;

/// "NE 40  THE WEIGHHOUSE" -- the bearing, two cells of air, the place in the
/// sign's own words. Never the witness, never the clue (doctrine D8).
[[nodiscard]] std::string pullLine(std::string_view bearing, std::string_view place);

// ---------------------------------------------------------------------------
// the skill-up toast
// ---------------------------------------------------------------------------

struct SkillRise {
    /// The raws' own displayName, shouted: "SKYRUNNING", "OPEN HAND".
    std::string label;
    std::int32_t level = 0;
};

/// Diffs a SkillTrack's levels between calls. The first call SEEDS and
/// reports nothing -- a fresh session's chargen levels are not a rise. A
/// track that changes size between calls (never, in this build; the raws are
/// read once) re-seeds rather than misreporting.
class SkillRiseWatch {
public:
    [[nodiscard]] std::vector<SkillRise> diff(const sim::SkillTrack& track);
    [[nodiscard]] bool seeded() const noexcept { return seeded_; }

private:
    std::vector<std::int32_t> levels_;
    bool seeded_ = false;
};

/// "SKYRUNNING RISES TO 12". One grammar, one place.
[[nodiscard]] std::string skillToastFor(const SkillRise& rise);

// ---------------------------------------------------------------------------
// the book news
// ---------------------------------------------------------------------------

/// What the watcher last saw. Seeded on the first call, like the skill watch.
struct BookNewsWatch {
    std::array<std::int32_t, 3> known{};
    std::array<bool, 3> closed{};
    std::size_t journalLines = 0;
    std::int32_t questsDone = 0;
    std::int32_t radiantTaken = 0;
    /// One stage count per authored questline, in QuestBook::lines() order.
    std::vector<std::int32_t> stages;
    bool seeded = false;
};

/// What the books produced since the last call, worded for the announcement
/// plate ("<NEWS>", the key is the plate's own business), or empty. When
/// several things move in one step the lead count wins over a stage and a
/// stage over an errand: one plate, one piece of news, the rarer one.
///
///   leads heard      "1 NEW LEAD" / "3 NEW LEADS"
///   stage advanced   "THE DISCIPLE'S OATH MOVES ON"
///   line finished    "THE DISCIPLE'S OATH IS DONE"
///   errand taken     "AN ERRAND TAKEN"
///
/// A close is NOT worded here: the CaseClosed sting and the case row's own
/// close line already carry it (Session::step's closedBookCount edge).
/// `plateArmedThisStep` says a site already announced its own book change
/// this step in its own words (examine()'s "3 NEW LEADS", the courier's "A
/// MISSION SHEET"), so the watcher fills the silent gaps and never says the
/// same change twice; the watch still advances.
[[nodiscard]] std::string diffBookNews(BookNewsWatch& watch, const BookSet& books,
                                       const sim::QuestBook& quests,
                                       const sim::QuestJournal& journal,
                                       const sim::RadiantBoard& radiant,
                                       bool plateArmedThisStep);

/// True on the step a book went from open to closed -- the edge Session uses
/// to let a page-fronted book fall back to the auto rule. Reads the watch
/// WITHOUT advancing it; call before diffBookNews on the same step.
[[nodiscard]] bool bookClosedThisStep(const BookNewsWatch& watch, const BookSet& books,
                                      CaseBookId id) noexcept;

}  // namespace granadad::render
