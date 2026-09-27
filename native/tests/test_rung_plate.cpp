// THE RUNG PLATE, PINNED -- the ward says so, and the simulation cannot tell.
//
// The claims below, each its own case (the sixth is one case per track), and
// after them the passes' own pins: the alert row's register and rails on
// pixels against a twin, the demand, the lift window both ways, the text
// plate's corner, the lift curve on a sky (the tick's shadow against the
// prose's), the head's centring, the voice row's stand-down through its
// fade (at the emitter, on pixels at 720x720, and through a real pass bark
// in the Gull whose clock runs under the plate), the voice row's pad above
// it on pixels, and the 320x180 fold.
//
//   1. THE WATCH seeds silently and reports a rise per track, never a fall.
//      A session that loads with rungs on the sheet has not just earned
//      them; a rung that skips one says so rather than inventing the one it
//      skipped; the same rise after a fall is reported again, honestly.
//   2. THE SHEET: every legend.<track>.<rung> key and legend.top resolves,
//      one row each, fifty-six glyphs and under (one line of the 4x6 font
//      inside the plate at 320x180), no dash, no numeral (a count is the
//      alert row's register, never this prose's), the priests' word absent. The
//      head comes off legend.cpp's own two tables and nothing else; a key
//      nobody authored draws an empty row, never a sentence the code chose.
//   3. THE PLATE fires on a real rung, holds its five seconds, queues a
//      second rung behind itself, rises THROUGH its seat rather than sliding
//      back, lifts over three times its rise, and sleeps -- and a skill that
//      levels on the same step is the plate's own dim tick row, told once,
//      while one that levels later in the hold queues as a toast and starts
//      only once the plate has lifted all the way; a toast already on screen
//      goes dark rather than share the corner and does not come back.
//   4. THE PRIORITY: a page and a bouncer's warning each dismiss the plate
//      on the spot, and a rung earned under either queues and surfaces the
//      moment the way is clear -- the alert row's own rule, applied whole.
//   5. OUTSIDE EVERY HASH. Two sessions from one config run the same sim
//      acts; one also draws the plate, dismisses it under a page and queues
//      another. The engine's combined hash, the tavern's own section and the
//      three books come out byte-identical at every checkpoint.
//   6. --rung=TRACK plays the real verbs and lands both of its beats (the
//      rung rose, the plate is up with that rung's own words) on all five
//      tracks; a word that names none of the five lands neither and says so.
//   7. THE GEOMETRY, at 320x180, 640x360, 960x540 and 1920x1080: the plate
//      seats under the ribbon block and the announce plate's row, above the
//      reticle's fence with a cell row of clearance wherever there is one
//      to spend (the smallest window keeps five glance units between its
//      keyline and the stat stack and splits the rest), in the terminal
//      register everywhere but 640x360, where it takes the glance scale
//      (rungPlateMetric), the tick row one unit low in its row, inside the frame
//      less a cell a side, centred, exactly two cells of gutter inside each
//      keyline (pinned as the number two, and on the prose's first ink
//      column), a tick row where the frame has the row and dropped where it
//      has not, and every authored row draws whole with no ink outside its
//      own pane.
//   8. THE ALERT ROW stands at half weight under the plate, on the plate's
//      own fade -- stepping down as it rises, whole again once it has
//      lifted -- its own clock stopped under the plate and capped after;
//      and a bouncer's WARNING, which dismisses the plate instead, is never
//      the line dimmed, on any frame, pinned step by step against a twin
//      session with no rung.
//   9. THE DRAWN WIRING: a frame with the plate up carries the alert row's
//      amber at well under the same frame's without one -- the weight is
//      pinned on pixels, not only as a function.
//  10. A PAGE'S CLOSE TAIL outranks the plate: a rung earned under a page
//      rises once the tail is down, not beneath it.
//  11. THE LATEST LEVEL SUPERSEDES: a toast still queued for a skill is
//      dropped when a newer level of it folds into the plate or queues, so
//      no stale, lower number ever plays after the card.
//  12. NOTHING COMES BACK UNTIL THE PLATE HAS LEFT: the alert row's
//      register and the announce slot follow the plate on screen through
//      its whole lift, and a line capped under the plate never swells.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/anim.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/panel.hpp"
#include "granadad/render/rung_plate.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/brawl.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/crime.hpp"
#include "granadad/sim/dialogue.hpp"
#include "granadad/sim/faction.hpp"
#include "granadad/sim/legend.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/social.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/world_hash.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

namespace {

constexpr sim::LegendTrack kTracks[] = {sim::LegendTrack::Wire, sim::LegendTrack::Roofs,
                                        sim::LegendTrack::Flame, sim::LegendTrack::Trade,
                                        sim::LegendTrack::Law};

const sim::CasebookRaws& raws() {
    static sim::CasebookRaws file = sim::CasebookRaws::load(content::contentDir());
    return file;
}

const sim::BarkTables& barks() {
    static sim::BarkTables sheet = sim::BarkTables::load(content::contentDir());
    return sheet;
}

/// A session standing on a named lead's own site -- test_pull's fixture, so
/// the plate is proved where the toast was.
[[nodiscard]] SessionConfig configAt(const char* leadId) {
    const std::int32_t at = raws().indexOf(leadId);
    REQUIRE(at >= 0);
    const sim::LeadSite site = raws().leads()[static_cast<std::size_t>(at)].site;
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = 8 * 3600;
    config.spawnX = site.x;
    config.spawnY = site.y;
    config.spawnBand = site.band;
    config.width = 960;
    config.height = 540;
    return config;
}

/// A session inside the Gull at a named hour and tile: test_tavern's own
/// door-policy fixture, for the warning.
[[nodiscard]] SessionConfig configInTheGull(int hour, std::int32_t tileX, std::int32_t tileY) {
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.timeOfDay = hour * 3600;
    config.spawnX = tileX;
    config.spawnY = tileY;
    config.spawnBand = sim::gull::kGroundBand;
    config.width = 960;
    config.height = 540;
    return config;
}

/// One step of walking ends the first-run gate (the opening page), the way a
/// real session ends it; then a few idle steps so every watch has seeded.
void wake(Session& session) {
    sim::MoveInput nudge;
    nudge.forward = 1;
    session.step(nudge);
    session.stepMany(sim::MoveInput{}, 4);
}

/// The three digests test_pull compares: the engine's combined hash, the
/// tavern's own section, and the three books.
struct Hashes {
    std::uint64_t engine = 0;
    std::uint64_t tavern = 0;
    std::uint64_t books = 0;
    [[nodiscard]] bool operator==(const Hashes& other) const noexcept {
        return engine == other.engine && tavern == other.tavern && books == other.books;
    }
};

[[nodiscard]] Hashes hashesOf(const Session& session) {
    Hashes out;
    out.engine = session.simHash();
    {
        sim::WorldHasher hasher;
        session.tavern().hash_into(hasher.section_sink(session.tavern().id()));
        out.tavern = hasher.section_hash(session.tavern().id());
    }
    {
        sim::WorldHasher hasher;
        sim::HashSink& sink = hasher.section_sink(0x424F4F4Bu);
        session.casebook().hashInto(sink);
        session.sheetBook().hashInto(sink);
        session.evictBook().hashInto(sink);
        out.books = hasher.combined_hash();
    }
    return out;
}

// --- the counters a rung is a function of, moved the way the sim moves them --

/// A cracked strongbox: eight points of THE WIRE, the first threshold exactly.
void crackABox(Session& session) {
    session.tavern().dialogue().crimes().commit(sim::Crime::Burgle, false);
}

/// Three roof-runs: nine points of THE ROOFS.
void runTheRoofs(Session& session) {
    for (int i = 0; i < 3; ++i) {
        session.tavern().dialogue().crimes().commit(sim::Crime::RoofRun, false);
    }
}

/// Standing with a named guild moved by exactly what the track needs to
/// reach a score, so the case does not depend on where the ward's opinion
/// started.
void standWith(Session& session, const char* guild, std::int32_t delta) {
    sim::DialogueDirector& talk = session.tavern().dialogue();
    REQUIRE(talk.standings().registry() != nullptr);
    const std::int32_t index = talk.standings().registry()->indexOf(guild);
    REQUIRE(index >= 0);
    talk.standings().addStanding(index, delta);
}

/// THE LAW to its first rung: the Watch's standing lifted to the threshold.
void winTheWatch(Session& session) {
    const std::int32_t score = session.legend().row(sim::LegendTrack::Law).score;
    standWith(session, "watch", sim::kLegendThresholds[0] - score);
}

/// THE FLAME to its first rung: the temple's standing, which the track halves.
void winTheTemple(Session& session) {
    const std::int32_t score = session.legend().row(sim::LegendTrack::Flame).score;
    standWith(session, "temple", 2 * (sim::kLegendThresholds[0] - score));
}

[[nodiscard]] std::string headFor(sim::LegendTrack track, std::int32_t rung) {
    return std::string(sim::legendTrackName(track)) + "  --  " +
           std::string(sim::legendTitle(track, rung));
}

[[nodiscard]] std::string rowFor(sim::LegendTrack track, std::int32_t rung) {
    return std::string(barks().line(legendRungKey(track, rung), 0));
}

[[nodiscard]] std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

/// The whole authored sheet's keys, legend.top first.
[[nodiscard]] std::vector<std::string> everyLegendKey() {
    std::vector<std::string> keys;
    keys.emplace_back(kLegendTopKey);
    for (const sim::LegendTrack track : kTracks) {
        for (std::int32_t rung = 1; rung <= sim::kLegendRungs; ++rung) {
            keys.push_back(legendRungKey(track, rung));
        }
    }
    return keys;
}

/// Changed pixels between two frames, split by whether they fall inside a
/// box grown by `slack` pixels a side.
struct Ink {
    std::size_t inside = 0;
    std::size_t outside = 0;
};

[[nodiscard]] Ink inkOf(const Framebuffer& with, const Framebuffer& without,
                        const RungPlateBox& box, int slack) {
    Ink ink;
    for (int y = 0; y < with.height(); ++y) {
        for (int x = 0; x < with.width(); ++x) {
            const std::size_t i = with.index(x, y);
            if (with.pixels()[i] == without.pixels()[i]) {
                continue;
            }
            const bool in = x >= box.x - slack && x < box.x + box.w + slack &&
                            y >= box.y - slack && y < box.y + box.h + slack;
            if (in) {
                ++ink.inside;
            } else {
                ++ink.outside;
            }
        }
    }
    return ink;
}

}  // namespace

// ===========================================================================
// 1. THE WATCH
// ===========================================================================

TEST_CASE("the watch seeds silently, reports a rise per track and never a fall") {
    Session session(configAt("mission-backroom"));
    LegendRiseWatch watch;
    CHECK_FALSE(watch.seeded());

    // A rung already on the sheet when the watch first looks is not news:
    // the first call seeds and reports nothing.
    crackABox(session);
    REQUIRE(session.legend().row(sim::LegendTrack::Wire).rung == 1);
    CHECK(watch.diff(session.legend()).empty());
    CHECK(watch.seeded());
    CHECK(watch.diff(session.legend()).empty());

    // One rung, one rise, in the track's own numbers.
    runTheRoofs(session);
    std::vector<LegendRise> rises = watch.diff(session.legend());
    REQUIRE(rises.size() == 1);
    CHECK(rises[0].track == sim::LegendTrack::Roofs);
    CHECK(rises[0].from == 0);
    CHECK(rises[0].to == 1);
    CHECK(watch.diff(session.legend()).empty());

    // Two tracks in one look: two rises, in track order.
    winTheTemple(session);
    winTheWatch(session);
    rises = watch.diff(session.legend());
    REQUIRE(rises.size() == 2);
    CHECK(rises[0].track == sim::LegendTrack::Flame);
    CHECK(rises[0].to == 1);
    CHECK(rises[1].track == sim::LegendTrack::Law);
    CHECK(rises[1].to == 1);

    // A fall is the sheet's business and says nothing; the same rise again
    // is reported again, because the ward is calling you that again.
    standWith(session, "watch", -sim::kLegendThresholds[0]);
    REQUIRE(session.legend().row(sim::LegendTrack::Law).rung == 0);
    CHECK(watch.diff(session.legend()).empty());
    winTheWatch(session);
    rises = watch.diff(session.legend());
    REQUIRE(rises.size() == 1);
    CHECK(rises[0].track == sim::LegendTrack::Law);
    CHECK(rises[0].from == 0);
    CHECK(rises[0].to == 1);

    // A rise that skips a rung says so rather than inventing the one it
    // skipped: four boxes in one step is thirty-two points, the second rung.
    Session other(configAt("mission-backroom"));
    LegendRiseWatch fresh;
    CHECK(fresh.diff(other.legend()).empty());
    for (int i = 0; i < 4; ++i) {
        crackABox(other);
    }
    rises = fresh.diff(other.legend());
    REQUIRE(rises.size() == 1);
    CHECK(rises[0].track == sim::LegendTrack::Wire);
    CHECK(rises[0].from == 0);
    CHECK(rises[0].to == 2);
}

// ===========================================================================
// 2. THE SHEET
// ===========================================================================

TEST_CASE("every legend key resolves: one row each, fifty-six glyphs and under, no dash, no numeral, nothing a priest would say") {
    const sim::BarkTables& sheet = barks();
    REQUIRE(sheet.loaded());
    const std::vector<std::string> keys = everyLegendKey();
    CHECK(keys.size() == 1 + sim::kLegendTracks * static_cast<std::size_t>(sim::kLegendRungs));
    CHECK(legendRungKey(sim::LegendTrack::Wire, 1) == "legend.wire.1");
    CHECK(legendRungKey(sim::LegendTrack::Law, 4) == "legend.law.4");
    for (const std::string& key : keys) {
        INFO(key);
        const std::vector<std::string>* rows = sheet.rows(key);
        REQUIRE(rows != nullptr);
        // ONE row on purpose: a rung is taken once, so there is nothing to
        // rotate, and the plate reads the first row and never another.
        CHECK(rows->size() == 1);
        const std::string& row = rows->front();
        CHECK_FALSE(row.empty());
        CHECK(sheet.line(key, 0) == row);
        CHECK(sheet.line(key, 7) == row);
        // One line of the plate at the smallest window.
        CHECK(row.size() <= static_cast<std::size_t>(kRungPlateRowCells));
        // No dash of any width in anything spoken.
        CHECK(row.find('-') == std::string::npos);
        CHECK(row.find("\xE2\x80\x93") == std::string::npos);
        CHECK(row.find("\xE2\x80\x94") == std::string::npos);
        // No numeral in anything the plate says. The record critic read
        // "OVER 3 TILES." under a plate and called it a numeric fragment out
        // of register; that row is the alert row's (composed in
        // Session::tryClimb, said through say()), and the plate's prose
        // stays words.
        CHECK(row.find_first_of("0123456789") == std::string::npos);
        // The learned name for the priests' quarry stays in a priest's mouth.
        CHECK(lowered(row).find("bloodletter") == std::string::npos);
    }

    // The five words the keys are spelt with round-trip, case-folded, and a
    // sixth word is refused rather than guessed at.
    for (const sim::LegendTrack track : kTracks) {
        sim::LegendTrack back = sim::LegendTrack::Law;
        REQUIRE(legendTrackFromKey(legendTrackKey(track), back));
        CHECK(back == track);
        std::string shouted(legendTrackKey(track));
        for (char& c : shouted) {
            c = static_cast<char>(c - 'a' + 'A');
        }
        REQUIRE(legendTrackFromKey(shouted, back));
        CHECK(back == track);
    }
    sim::LegendTrack junk = sim::LegendTrack::Wire;
    CHECK_FALSE(legendTrackFromKey("bloodletter", junk));
    CHECK_FALSE(legendTrackFromKey("", junk));
    CHECK_FALSE(legendTrackFromKey("wires", junk));

    // The plate's text for every rung: the head off legend.cpp's own two
    // tables, the prose the authored row, the top row only at the top.
    for (const sim::LegendTrack track : kTracks) {
        for (std::int32_t rung = 1; rung <= sim::kLegendRungs; ++rung) {
            INFO(legendTrackKey(track), " ", rung);
            const RungPlateText text = rungPlateFor(LegendRise{track, rung - 1, rung}, sheet);
            CHECK_FALSE(text.empty());
            CHECK(text.head == headFor(track, rung));
            CHECK(text.head.find(sim::legendTrackName(track)) == 0);
            CHECK(text.head.find(sim::legendTitle(track, rung)) != std::string::npos);
            CHECK(text.prose == rowFor(track, rung));
            CHECK(text.top.empty() == (rung < sim::kLegendRungs));
            if (rung == sim::kLegendRungs) {
                CHECK(text.top == sheet.line(kLegendTopKey, 0));
            }
        }
    }

    // A key nobody authored draws an empty row, never a sentence the code
    // chose: an empty sheet still names the rung and says nothing.
    sim::BarkTables none;
    const RungPlateText bare =
        rungPlateFor(LegendRise{sim::LegendTrack::Wire, 0, 1}, none);
    CHECK(bare.head == "THE WIRE  --  LIGHT FINGERS");
    CHECK(bare.prose.empty());
    CHECK(bare.top.empty());
    CHECK_FALSE(bare.empty());
}

// ===========================================================================
// 3. THE PLATE
// ===========================================================================

TEST_CASE("a rung rising puts the plate up, holds it, queues a second behind it, rises through its seat and sleeps") {
    Session session(configAt("mission-backroom"));
    wake(session);
    REQUIRE_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateText().empty());
    CHECK(session.rungPlateQueued() == 0);
    CHECK(session.rungPlateFade() == 0.0F);
    CHECK(session.rungPlateState().fade == 0.0F);

    // Points that raise no rung show nothing.
    session.tavern().dialogue().crimes().commit(sim::Crime::Lift, false);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.rungPlateWanted());

    // The rung: the very next step says so, in the ward's own words.
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Wire, 1));
    CHECK(session.rungPlateText().prose == rowFor(sim::LegendTrack::Wire, 1));
    CHECK(session.rungPlateText().top.empty());
    CHECK(session.rungPlateFade() > 0.0F);
    CHECK(session.rungPlateQueued() == 0);
    RungPlateState state = session.rungPlateState();
    CHECK(state.head == headFor(sim::LegendTrack::Wire, 1));
    CHECK(state.prose == rowFor(sim::LegendTrack::Wire, 1));
    CHECK(state.fade > 0.0F);
    // Rising: still below its seat, closing on it.
    CHECK(state.drift < 0.0F);

    // Up all the way inside the ease, and seated.
    session.stepMany(sim::MoveInput{}, kPageEaseSteps);
    CHECK(session.rungPlateFade() == 1.0F);
    CHECK(session.rungPlateState().drift == 0.0F);

    // A second rung while the first is up waits its turn rather than
    // swapping the words mid-hold.
    runTheRoofs(session);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateQueued() == 1);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Wire, 1));
    CHECK(session.rungPlateWanted());

    // The hold spent: the plate is no longer wanted, still fading, and it
    // is past its seat and drifting on up and out -- through, never back
    // the way it came. The roofs are still waiting.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps - kPageEaseSteps);
    CHECK_FALSE(session.rungPlateWanted());
    state = session.rungPlateState();
    CHECK(state.fade > 0.0F);
    CHECK(state.fade < 1.0F);
    CHECK(state.drift > 0.0F);
    CHECK(session.rungPlateQueued() == 1);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Wire, 1));
    // THE LIFT IS THREE TIMES THE RISE: the wire's plate is still the one on
    // screen a page ease into its lift, and two page eases in -- where an
    // eight-step lift would have been long gone and the roofs' plate up.
    session.stepMany(sim::MoveInput{}, kPageEaseSteps + 1);
    CHECK(session.rungPlateState().fade > 0.0F);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Wire, 1));
    session.stepMany(sim::MoveInput{}, kPageEaseSteps);
    CHECK(session.rungPlateState().fade > 0.0F);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Wire, 1));

    // The rest of the lift, a beat of black, then the roofs' turn with
    // their own words.
    session.stepMany(sim::MoveInput{}, kRungPlateLiftSteps - 2 * kPageEaseSteps - 1 + 4);
    CHECK(session.rungPlateWanted());
    CHECK(session.rungPlateQueued() == 0);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateText().prose == rowFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateFade() > 0.0F);

    // And then sleep, the words kept so the notice ends with its own.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + kRungPlateLiftSteps + 2);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateQueued() == 0);
    CHECK(session.rungPlateFade() == 0.0F);
    CHECK(session.rungPlateState().fade == 0.0F);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
}

TEST_CASE("the top rung carries the top row under its own") {
    Session session(configAt("mission-backroom"));
    wake(session);
    // Fifteen boxes: a hundred and twenty points of THE WIRE, the top.
    for (int i = 0; i < 15; ++i) {
        crackABox(session);
    }
    REQUIRE(session.legend().row(sim::LegendTrack::Wire).rung == sim::kLegendRungs);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Wire, sim::kLegendRungs));
    CHECK(session.rungPlateText().prose == rowFor(sim::LegendTrack::Wire, sim::kLegendRungs));
    CHECK(session.rungPlateText().top == barks().line(kLegendTopKey, 0));
    const RungPlateState state = session.rungPlateState();
    CHECK(state.top == barks().line(kLegendTopKey, 0));
}

TEST_CASE("a rung and a skill level in one step: the level is the plate's own tick row, told once, and no toast follows") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    REQUIRE_FALSE(session.skillToastWanted());

    // The same step: the level and the rung. The level is the card's own
    // dim last row, the toast's own words, and the corner stays dark.
    while (!skills.use(sim::kBlockSkill)) {
    }
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateWanted());
    CHECK(session.rungPlateText().tick == "SHIELDWALL RISES TO 1");
    CHECK(session.rungPlateState().tick == "SHIELDWALL RISES TO 1");
    CHECK_FALSE(session.skillToastWanted());
    CHECK(session.skillToastLabel().empty());
    CHECK(session.pullHud().skillToastFade == 0.0F);

    // Through the hold, the lift and past it: no toast, ever. Told once.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + kRungPlateLiftSteps + 40);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateState().fade == 0.0F);
    CHECK_FALSE(session.skillToastWanted());
    CHECK(session.skillToastLabel().empty());
    CHECK(session.rungPlateText().tick == "SHIELDWALL RISES TO 1");
}

TEST_CASE("the announce slot waits behind the plate: NEW LEAD is drawn at zero the instant the plate is wanted, keeps its clock, and comes back with what it had") {
    Session session(configAt("mission-backroom"));
    wake(session);
    REQUIRE_FALSE(session.casePlateWanted());

    // The lead read arms the notice and tips no rung; the box on the same
    // step tips THE WIRE. The plate is wanted from the next step, and the
    // notice is drawn at zero from that same step -- not eased out across
    // the plate's rise.
    session.examine();
    REQUIRE(session.casePlateWanted());
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    CHECK(session.casePlateWanted());
    CHECK(session.casePlateFadeNow() == 0.0F);
    CHECK(session.placePlateFadeNow() == 0.0F);
    for (int i = 0; i < kPageEaseSteps; ++i) {
        session.stepMany(sim::MoveInput{}, 1);
        CHECK(session.casePlateFadeNow() == 0.0F);
    }
    REQUIRE(session.rungPlateFade() == 1.0F);

    // THE CLOCK HELD: the notice's own hold is shorter than the plate's, and
    // it is still wanted when the plate's hold is spent.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps - kPageEaseSteps);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.casePlateWanted());

    // STILL ZERO THROUGH THE LIFT: the slot follows the plate on screen,
    // not its hold, so nothing rises across the card leaving.
    int lifting = 0;
    while (session.rungPlateState().fade > 0.0F && lifting < 2 * kRungPlateLiftSteps) {
        CHECK(session.casePlateFadeNow() == 0.0F);
        CHECK(session.casePlateWanted());
        session.stepMany(sim::MoveInput{}, 1);
        ++lifting;
    }
    REQUIRE(session.rungPlateState().fade == 0.0F);
    CHECK(lifting > kPageEaseSteps);

    // Back on its own ease once the plate has left, and then its own hold
    // runs out.
    session.stepMany(sim::MoveInput{}, kPageEaseSteps + 2);
    CHECK(session.casePlateWanted());
    CHECK(session.casePlateFadeNow() > 0.0F);
    session.stepMany(sim::MoveInput{}, 200 + kPageEaseSteps);
    CHECK_FALSE(session.casePlateWanted());
}

TEST_CASE("the latest level supersedes: a toast still queued for a skill is dropped when a newer level folds into the plate, and no stale toast follows") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    // Level 1: its toast up. Level 2 the next step: queued behind it.
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.skillToastWanted());
    REQUIRE(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    // Level 3 on the rung's own step: folded into the plate, the queued
    // level 2 gone, the toast on screen cut.
    while (!skills.use(sim::kBlockSkill)) {
    }
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    CHECK(session.rungPlateText().tick == "SHIELDWALL RISES TO 3");
    CHECK_FALSE(session.skillToastWanted());
    // Through the hold, the lift and well past: no toast, and never the
    // stale level.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + kRungPlateLiftSteps + 200);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK_FALSE(session.skillToastWanted());
    CHECK(session.skillToastLabel() != "SHIELDWALL RISES TO 2");
}

TEST_CASE("two rungs on one step with a skill: the tick rides the second, queued plate, whichever track it is") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    while (!skills.use(sim::kBlockSkill)) {
    }
    crackABox(session);
    runTheRoofs(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    REQUIRE(session.rungPlateQueued() == 1);
    // The wire's plate is up bare; the roofs' waits with the tick.
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Wire, 1));
    CHECK(session.rungPlateText().tick.empty());
    CHECK_FALSE(session.skillToastWanted());
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + kRungPlateLiftSteps + 4);
    REQUIRE(session.rungPlateWanted());
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateText().tick == "SHIELDWALL RISES TO 1");
    CHECK_FALSE(session.skillToastWanted());
}

TEST_CASE("a plate dismissed by a page caps the row's clock, and dismissed is gone: a short page never hands the card back") {
    Session session(configAt("mission-backroom"));
    wake(session);
    // A line on the row, a plate up, then a page over both at two seconds.
    session.examine();
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    session.stepMany(sim::MoveInput{}, 120);
    session.toggleCasebook();
    REQUIRE(session.casebookOpen());
    session.stepMany(sim::MoveInput{}, 3);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateFade() == 0.0F);
    // The page down after three steps: the card does not come back -- its
    // lift was cut with its hold -- and the line, capped at the dismissal,
    // is gone inside its cap and the page's own tail.
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());
    for (int i = 0; i < 2 * kRungPlateLiftSteps; ++i) {
        session.stepMany(sim::MoveInput{}, 1);
        CHECK(session.rungPlateState().fade == 0.0F);
        CHECK(session.rungPlateFade() == 0.0F);
    }
    CHECK_FALSE(session.rungPlateWanted());
    session.stepMany(sim::MoveInput{}, kRungPlateRowAfterSteps + kPageEaseSteps + 4);
    CHECK(session.alertFadeNow() == 0.0F);
}

TEST_CASE("a watchman's demand under a live plate is never capped nor stepped down: it dismisses the plate and keeps its whole hold at the glance size") {
    // Eleven at night at the Gull's counter, Cull on his stool: the watch
    // rhythm suite's own spot. A blade drawn on a patron in his sight is
    // the violence he closes on with his halt.
    Session session(configInTheGull(23, sim::gull::kBartenderX, sim::gull::kBartenderY + 1));
    wake(session);
    REQUIRE(session.tavern().playerInside());
    const sim::Actor* cull = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (actor.name() == "Watchman Cull" && actor.present()) {
            cull = &actor;
        }
    }
    REQUIRE(cull != nullptr);
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1 + kPageEaseSteps);
    REQUIRE(session.rungPlateFade() == 1.0F);
    // A patron Cull can see the player strike, from a standable tile a
    // step off him, facing him -- test_watch_rhythm's standFacingPatronFor,
    // through the session's own body.
    const sim::Actor* mark = nullptr;
    for (const sim::Actor& actor : session.tavern().actors()) {
        if (!actor.present() || sim::isFloored(actor.activity()) ||
            actor.role() != sim::ActorRole::Patron || session.tavern().isProfessional(actor)) {
            continue;
        }
        const std::int32_t sides[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (const auto& side : sides) {
            const std::int32_t px = actor.tileX() + side[0];
            const std::int32_t py = actor.tileY() + side[1];
            if (!session.tiles().standable(px, py, actor.band())) {
                continue;
            }
            session.body().placeAt(px, py, actor.band());
            session.body().setYaw(sim::bearingTo(px, py, actor.tileX(), actor.tileY()));
            session.stepMany(sim::MoveInput{}, 1);
            if (session.tavern().noticeBy(*cull).seen) {
                mark = &actor;
                break;
            }
        }
        if (mark != nullptr) {
            break;
        }
    }
    REQUIRE(mark != nullptr);
    REQUIRE(session.rungPlateWanted());
    session.tavern().setPlayerCombat(sim::Weapon::Edged, sim::Intent::Kill);
    // THE PLATE MUST STILL BE LIVE ON THE STEP THE DEMAND LANDS -- else a
    // bouncer's warning could have dismissed it first and the case would
    // prove nothing about the demand (the auditor).
    int steps = 0;
    bool liveBefore = false;
    while (session.tavern().lastDemand().empty() && steps < 120) {
        if (steps % 20 == 0) {
            session.punch();
        }
        liveBefore = session.rungPlateWanted() && session.rungPlateFade() > 0.0F;
        session.stepMany(sim::MoveInput{}, 1);
        ++steps;
    }
    REQUIRE_FALSE(session.tavern().lastDemand().empty());
    REQUIRE(liveBefore);
    // AND THE HOLD IS STILL RUNNING ON THE LANDING STEP -- it is cut the
    // step AFTER the demand is said, since stepRungPlate runs before the
    // sim's tick -- so the plate's own hold did not expire under the loop.
    // A bouncer's warning in the same second would not cut it before the
    // demand either (both come out of the same tick), so that is ruled out
    // by name: no warning is up.
    REQUIRE(session.rungPlateWanted());
    REQUIRE(session.tavern().lastWarning().empty());
    REQUIRE(session.alertSpeakerNow().rfind("Watchman Cull", 0) == 0);
    // The demand is on the row: the plate is gone the same step, the row is
    // at the glance size, and the line keeps its WHOLE hold, counted down
    // one a step from past the cap -- never cut to the two seconds a capped
    // line would have.
    const std::int32_t hold = session.alertStepsLeftNow();
    CHECK(hold > kRungPlateRowAfterSteps);
    for (int i = 0; i < 8; ++i) {
        INFO("step ", i, " after the demand");
        CHECK(session.rungPlateState().fade == 0.0F);
        CHECK_FALSE(session.alertStepsDownNow());
        CHECK(session.alertStepsLeftNow() == hold - i);
        session.stepMany(sim::MoveInput{}, 1);
    }
    CHECK_FALSE(session.rungPlateWanted());
}

TEST_CASE("a line said during the lift is capped on the card's last frame, and a page over a lifting card dismisses it for good") {
    // THE CAP: the hold ends, the card lifts, and a line lands on the row
    // mid-lift (its cap cleared by say()). It is capped the frame the card
    // leaves and finishes small.
    {
        Session session(configAt("mission-backroom"));
        wake(session);
        crackABox(session);
        session.stepMany(sim::MoveInput{}, 1 + kRungPlateHoldSteps + 6);
        REQUIRE_FALSE(session.rungPlateWanted());
        REQUIRE(session.rungPlateState().fade > 0.0F);
        session.punch();
        session.stepMany(sim::MoveInput{}, 1);
        REQUIRE(session.alertStepsLeftNow() > kRungPlateRowAfterSteps);
        int lifting = 0;
        while (session.rungPlateState().fade > 0.0F && lifting < kRungPlateLiftSteps + 4) {
            session.stepMany(sim::MoveInput{}, 1);
            ++lifting;
        }
        REQUIRE(session.rungPlateState().fade == 0.0F);
        session.stepMany(sim::MoveInput{}, 1);
        CHECK(session.alertStepsLeftNow() <= kRungPlateRowAfterSteps);
        CHECK(session.alertStepsDownNow());
        session.stepMany(sim::MoveInput{}, kRungPlateRowAfterSteps + kPageEaseSteps + 4);
        CHECK(session.alertFadeNow() == 0.0F);
    }
    // THE PAGE: a card six steps into its lift, a page over it for three
    // steps. The card never comes back.
    {
        Session session(configAt("mission-backroom"));
        wake(session);
        crackABox(session);
        session.stepMany(sim::MoveInput{}, 1 + kRungPlateHoldSteps + 6);
        REQUIRE(session.rungPlateState().fade > 0.0F);
        session.toggleCasebook();
        REQUIRE(session.casebookOpen());
        session.stepMany(sim::MoveInput{}, 3);
        session.toggleCasebook();
        REQUIRE_FALSE(session.casebookOpen());
        for (int i = 0; i < 2 * kRungPlateLiftSteps; ++i) {
            session.stepMany(sim::MoveInput{}, 1);
            CHECK(session.rungPlateState().fade == 0.0F);
        }
    }
}

TEST_CASE("the voice row's capped state outlives its hold through the ease-out, and a farewell holds under the plate") {
    StreetVoice voice;
    VoiceScene held;
    held.holdClock = true;
    VoiceScene running;
    // A farewell (an event line, the one kind a verb can push without a
    // room) under the plate: its clock does not run.
    voice.speakFarewell("Watchman Cull", "KEEP MY ROUNDS QUIET. GO ON.", "farewell.cull", 5, 10);
    REQUIRE(voice.up());
    for (int i = 0; i < kVoiceHoldMaxSteps + 1; ++i) {
        voice.step(held);
    }
    CHECK(voice.up());
    CHECK_FALSE(voice.capped());
    // Capped when the plate goes: still up for the cap, then down -- and
    // CAPPED through the fade that follows, until the row clears.
    voice.capHold(2);
    CHECK(voice.capped());
    voice.step(running);
    voice.step(running);
    CHECK_FALSE(voice.up());
    CHECK(voice.capped());
    voice.clearIfSettled(false);
    CHECK(voice.capped());
    voice.clearIfSettled(true);
    CHECK_FALSE(voice.capped());
    // A LINE WHOSE HOLD HAS RUN OUT, still fading, capped on the plate's
    // last frame: capped all the same, until the row clears (the auditor,
    // ninth pass: keyed on the hold, this un-capped the bark mid-fade).
    voice.speakFarewell("Watchman Cull", "KEEP THE LANE CLEAR BEHIND YOU.", "farewell.cull", 5,
                        900);
    for (int i = 0; i < kVoiceHoldMaxSteps + 1; ++i) {
        voice.step(running);
    }
    REQUIRE_FALSE(voice.up());
    REQUIRE_FALSE(voice.current().empty());
    CHECK_FALSE(voice.capped());
    voice.capHold(kRungPlateRowAfterSteps);
    CHECK(voice.capped());
    voice.clearIfSettled(true);
    CHECK_FALSE(voice.capped());
}

TEST_CASE("a real pass bark's clock runs under the plate, and the voice row is stood down from the plate's rise until the row clears (the session's own seam)") {
    // Eight in the evening at the Gull's counter, a drift along the bar
    // until a stool speaks (test_street_voice's own walk): a PASS or
    // OVERHEARD line, whose clock runs under the plate. Its hold is its
    // row's own (kVoiceHoldMinSteps plus kVoiceHoldPerGlyphSteps a glyph,
    // to the max), so the case knows the step it should leave on: that
    // step, not the plate's cap (kRungPlateHoldSteps plus
    // kRungPlateRowAfterSteps, where a HELD line would read), is the pin
    // that its clock ran. The plate goes up the step after the bark.
    //
    // NOT PINNED HERE: a bark running out inside the lift's last frames
    // (the tenth-pass window). Tried: idled to that step, the room
    // replaced the bark six steps short of it -- the row yields to its
    // own rank or above whenever the room has a new line -- so the step a
    // bark leaves on is the room's to choose, not the case's. The
    // emitter case above pins what the cap does to an exhausted line.
    Session session(configInTheGull(20, sim::gull::kBartenderX, sim::gull::kBartenderY + 1));
    wake(session);
    constexpr std::int32_t kCappedAt = kRungPlateHoldSteps + kRungPlateRowAfterSteps;
    sim::MoveInput drift;
    int walked = 0;
    std::int32_t hold = 0;
    while (walked < 4 * 60 * 60) {
        drift.forward = (walked / 120) % 2 == 0 ? 1 : 0;
        drift.turn = (walked / 60) % 3 == 0 ? 1 : 0;
        session.step(drift);
        ++walked;
        if (!session.voice().up()) {
            continue;
        }
        const VoiceLine& line = session.voice().current();
        hold = std::clamp(kVoiceHoldMinSteps + static_cast<std::int32_t>(line.text.size()) *
                                                   kVoiceHoldPerGlyphSteps,
                          kVoiceHoldMinSteps, kVoiceHoldMaxSteps);
        // A bark long enough to outlive the plate's hold, and whose own
        // step tells apart from the cap's.
        if (line.channel < VoiceChannel::Farewell && hold > kRungPlateHoldSteps + 30 &&
            std::abs(hold - kCappedAt) > 6) {
            break;
        }
    }
    REQUIRE(session.voice().up());
    REQUIRE(session.voice().current().channel < VoiceChannel::Farewell);
    const std::string bark = session.voice().current().text;
    REQUIRE_FALSE(session.voiceStepsDownNow());
    crackABox(session);
    int lived = 0;
    int ranOut = -1;
    int lastFrame = -1;
    bool plateSeen = false;
    for (; lived < kVoiceHoldMaxSteps + 2 * kPageEaseSteps + 8; ++lived) {
        session.stepMany(sim::MoveInput{}, 1);
        const float fade = session.rungPlateState().fade;
        if (fade > 0.0F) {
            plateSeen = true;
        } else if (plateSeen && lastFrame < 0) {
            lastFrame = lived;
        }
        if (session.voice().current().text != bark) {
            break;
        }
        if (ranOut < 0 && !session.voice().up()) {
            ranOut = lived;
        }
        INFO("step ", lived, " of the bark under and after the plate");
        CHECK(session.voiceStepsDownNow());
    }
    const int cleared = lived;
    INFO("hold ", hold, " ran out ", ranOut, " last frame ", lastFrame, " cleared ", cleared);
    REQUIRE(plateSeen);
    REQUIRE(lastFrame >= 0);
    REQUIRE(ranOut >= 0);
    // Its own clock, run under the plate: it left within two steps of its
    // hold, after the plate, and nowhere near where the cap would have
    // put a held line.
    CHECK(std::abs(ranOut - (hold - 1)) <= 2);
    CHECK(ranOut > lastFrame);
    CHECK(std::abs(ranOut - kCappedAt) > 3);
    // And the row cleared after its fade, still small to the end.
    CHECK(cleared > ranOut);
}

TEST_CASE("the voice row stood down keeps its top rail clear of the row above it, on pixels") {
    // A bark stood down with a hands row above it at 960x540: the pad the
    // row buys above itself keeps the hands row's drop shadow off the top
    // rail (the auditor, ninth pass: without it the shadow's last row was
    // the rail's).
    HudState state;
    state.voiceName = "Watchman Cull";
    state.voiceLine = "KEEP MY ROUNDS QUIET. GO ON.";
    state.voiceFade = 1.0F;
    state.voiceInkWeight = 0.5F;
    state.voiceStepsDown = true;
    state.alertStepScale = rungPlateMetric(960, 540).scale;
    state.handsLabel = "FISTS UP";
    state.handsFade = 1.0F;
    Framebuffer fb(960, 540);
    fb.clear(Rgb{0.08F, 0.09F, 0.10F});
    drawHud(fb, state);
    const auto accent = [](const Rgb& p) {
        return p.r > 0.20F && p.g > 0.72F * p.r && p.g < 0.88F * p.r && p.b > 0.55F * p.r &&
               p.b < 0.75F * p.r;
    };
    const auto luma = [](const Rgb& p) {
        return static_cast<double>(0.30F * p.r + 0.59F * p.g + 0.11F * p.b);
    };
    int top = 540;
    int left = 960;
    int right = -1;
    for (int y = hudAimRect(960, 540).y1; y < 540; ++y) {
        for (int x = 0; x < 960; ++x) {
            if (accent(unpackRgb(fb.pixels()[fb.index(x, y)]))) {
                top = std::min(top, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
    }
    REQUIRE(right > left);
    // The plate's top rail: a unit and the rail above the glyph box. Read
    // ACROSS THE WHOLE RAIL -- from its first bone pixel to its last, the
    // plate being wider than the name -- because the hands row above is
    // centred on the frame and its shadow falls in the middle of the
    // plate, not under the name (the auditor, tenth pass: a scan of the
    // name's columns alone passed with or without the pad).
    const int vs = std::min(hudMinorScale(540), rungPlateMetric(960, 540).scale);
    const int railY = top - (vs + std::max(1, vs / 2));
    const auto railInk = [](const Rgb& p) { return std::abs(p.r - p.g) < 0.06F && p.r > 0.15F; };
    int railLeft = 960;
    int railRight = -1;
    for (int x = 0; x < 960; ++x) {
        if (railInk(unpackRgb(fb.pixels()[fb.index(x, railY)]))) {
            railLeft = std::min(railLeft, x);
            railRight = std::max(railRight, x);
        }
    }
    REQUIRE(railRight > railLeft);
    REQUIRE(railLeft < left);
    REQUIRE(railRight > right);
    int dark = 0;
    double darkest = 1.0;
    for (int x = railLeft; x <= railRight; ++x) {
        const double l = luma(unpackRgb(fb.pixels()[fb.index(x, railY)]));
        darkest = std::min(darkest, l);
        if (l < 0.15) {
            ++dark;
        }
    }
    INFO("rail row ", railY, " x ", railLeft, "..", railRight, " darkest ", darkest, " dark ", dark);
    CHECK(dark == 0);
    CHECK(darkest > 0.15);
}

TEST_CASE("the voice row drawn stood down keeps the plate's register through a fade, on pixels, where the minor scale is a tier above it") {
    // 720x720: the HUD's minor scale is 3, the plate's register 2. The
    // name is drawn in the person accent (0.90, 0.72, 0.58): blue at about
    // two thirds of red, green at four fifths -- neither bone nor amber.
    const auto accent = [](const Rgb& p) {
        return p.r > 0.20F && p.g > 0.72F * p.r && p.g < 0.88F * p.r && p.b > 0.55F * p.r &&
               p.b < 0.75F * p.r;
    };
    int heights[2] = {0, 0};
    for (int pass = 0; pass < 2; ++pass) {
        HudState state;
        state.voiceName = "Watchman Cull";
        state.voiceLine = "KEEP MY ROUNDS QUIET. GO ON.";
        state.voiceFade = 0.5F;
        state.voiceInkWeight = 1.0F;
        state.alertStepScale = rungPlateMetric(720, 720).scale;
        state.voiceStepsDown = pass == 1;
        Framebuffer fb(720, 720);
        fb.clear(Rgb{0.08F, 0.09F, 0.10F});
        drawHud(fb, state);
        int top = 720;
        int bottom = -1;
        for (int y = hudAimRect(720, 720).y1; y < 720; ++y) {
            for (int x = 0; x < 720; ++x) {
                if (accent(unpackRgb(fb.pixels()[fb.index(x, y)]))) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        }
        REQUIRE(bottom >= top);
        heights[pass] = bottom - top + 1;
    }
    CHECK(heights[0] == 6 * hudMinorScale(720));
    CHECK(heights[1] == 6 * rungPlateMetric(720, 720).scale);
    CHECK(heights[1] < heights[0]);
}

TEST_CASE("the head block sits on the card's axis for every track's head, to the half cell, on pixels") {
    // The panel accent (the gold block): red high, green about seven
    // eighths of it, blue under half.
    const auto gold = [](const Rgb& p) {
        return p.r > 0.80F && p.g > 0.80F * p.r && p.g < 0.95F * p.r && p.b < 0.55F * p.r;
    };
    for (const sim::LegendTrack track : {sim::LegendTrack::Wire, sim::LegendTrack::Roofs,
                                         sim::LegendTrack::Flame, sim::LegendTrack::Trade,
                                         sim::LegendTrack::Law}) {
        const std::string head = headFor(track, 1);
        INFO(head);
        RungPlateState state;
        state.head = head;
        state.prose = "Up and over, and the roofs had no objection.";
        state.fade = 1.0F;
        state.drift = 0.0F;
        const RungPlateBox box = rungPlateBox(960, 540, state);
        REQUIRE(box.draws);
        const PanelMetric metric = rungPlateMetric(960, 540);
        Framebuffer fb(960, 540);
        fb.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawRungPlate(fb, state);
        const int headY = box.y + metric.heightOf(1) + metric.cellH() / 2;
        int left = 960;
        int right = -1;
        for (int x = 0; x < 960; ++x) {
            if (gold(unpackRgb(fb.pixels()[fb.index(x, headY)]))) {
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        REQUIRE(right > left);
        // Twice the centres, so a half pixel is a whole number: within one.
        // THE CARD'S AXIS IS THE SLAB'S: the slab runs a unit past the box
        // on the left, and the keyline's last dash ends a unit short of the
        // box's right edge, so the axis the eye reads sits half a unit left
        // of the box's own (the director measured the gold's margins against
        // the slab, 137/137 at 960).
        const int blockTwice = left + right;
        const int axisTwice = 2 * box.x + box.w - metric.scale - 1;
        INFO("block ", left, "..", right, " axis*2 ", axisTwice);
        CHECK(std::abs(blockTwice - axisTwice) <= 1);
    }
}

TEST_CASE("at 320x180 a skill that levels on the rung's step is a toast after the lift, never a tick the card has no row for") {
    SessionConfig config = configAt("mission-backroom");
    config.width = 320;
    config.height = 180;
    Session session(config);
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    while (!skills.use(sim::kBlockSkill)) {
    }
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    CHECK(session.rungPlateText().tick.empty());
    CHECK_FALSE(session.skillToastWanted());
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + kRungPlateLiftSteps + 4);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.skillToastWanted());
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
}

TEST_CASE("a text plate's corner is drawn once: at a quarter alpha the corner pixel is the rail's pixel, and at full alpha nothing changed") {
    for (const float alpha : {0.25F, 1.0F}) {
        INFO("alpha ", alpha);
        Framebuffer fb(64, 32);
        fb.clear(Rgb{0.0F, 0.0F, 0.0F});
        drawTextPlate(fb, 8, 8, 40, 24, 1, alpha);
        const std::uint32_t corner = fb.pixels()[fb.index(8, 8)];
        const std::uint32_t top = fb.pixels()[fb.index(20, 8)];
        const std::uint32_t left = fb.pixels()[fb.index(8, 16)];
        CHECK(corner == top);
        CHECK(corner == left);
        CHECK(corner != fb.pixels()[fb.index(20, 16)]);
    }
}

TEST_CASE("through the lift the ink holds whole to the midpoint while the slab thins, and the shadows go with the slab") {
    RungPlateState state;
    state.head = "THE ROOFS  --  TENANT";
    state.prose = "Up and over, and the roofs had no objection.";
    state.tick = "SKYRUNNING RISES TO 2";
    state.drift = 0.0F;
    const auto brightest = [](const Framebuffer& fb, int x0, int y0, int x1, int y1) {
        double best = 0.0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const Rgb p = unpackRgb(fb.pixels()[fb.index(x, y)]);
                best = std::max(best, static_cast<double>(0.30F * p.r + 0.59F * p.g + 0.11F * p.b));
            }
        }
        return best;
    };
    const auto darkest = [](const Framebuffer& fb, int x0, int y0, int x1, int y1) {
        double worst = 1.0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const Rgb p = unpackRgb(fb.pixels()[fb.index(x, y)]);
                worst = std::min(worst, static_cast<double>(0.30F * p.r + 0.59F * p.g + 0.11F * p.b));
            }
        }
        return worst;
    };
    const Rgb sky{0.62F, 0.66F, 0.72F};
    const RungPlateBox box = rungPlateBox(960, 540, state);
    REQUIRE(box.draws);
    const PanelMetric metric = rungPlateMetric(960, 540);
    // The prose row's box, the tick row's (one unit low), and the air row's
    // middle for the slab.
    const int proseY0 = box.y + metric.heightOf(3);
    const int proseY1 = proseY0 + metric.cellH();
    const int tickY0 = box.y + metric.heightOf(4) + metric.scale;
    const int tickY1 = tickY0 + metric.cellH();
    const int airY = box.y + metric.heightOf(2) + metric.cellH() / 2;
    double proseAtFull = 0.0;
    double shadowAtFull = 1.0;
    double slabAtFull = 0.0;
    for (const float fade : {1.0F, 0.5F, 0.25F}) {
        INFO("fade ", fade);
        state.fade = fade;
        Framebuffer fb(960, 540);
        fb.clear(sky);
        drawRungPlate(fb, state);
        // The shadows are read INSIDE the gutters: the keyline's bars in the
        // border cells cast the frame's own shadow (the square) on every
        // row, which is not the row's.
        const int inX0 = box.x + metric.widthOf(3);
        const int inX1 = box.x + box.w - metric.widthOf(3);
        const double prose = brightest(fb, box.x, proseY0, box.x + box.w, proseY1);
        const double shadow = darkest(fb, inX0, proseY0, inX1, proseY1 + metric.scale);
        const double tickShadow = darkest(fb, inX0, tickY0, inX1, tickY1 + metric.scale);
        const Rgb slab = unpackRgb(fb.pixels()[fb.index(box.x + box.w / 2, airY)]);
        const double slabLuma = static_cast<double>(0.30F * slab.r + 0.59F * slab.g + 0.11F * slab.b);
        if (fade == 1.0F) {
            proseAtFull = prose;
            shadowAtFull = shadow;
            slabAtFull = slabLuma;
            continue;
        }
        if (fade == 0.5F) {
            // The ink whole (the brightest prose pixel is the up card's),
            // the slab a quarter of its alpha (lighter, sky through it).
            CHECK(prose == doctest::Approx(proseAtFull).epsilon(0.02));
            CHECK(slabLuma > slabAtFull + 0.1);
            // And the shadow has gone with the slab: the darkest pixel in
            // the prose rows is nowhere near the up card's shadow.
            CHECK(shadow > shadowAtFull + 0.2);
            // THE DIM TICK ROW'S ONE POWER FASTER: its darkest pixel lighter
            // than the prose row's (the cube of the fade against the
            // square; equal at full).
            CHECK(tickShadow > shadow + 0.03);
        } else {
            // A quarter in: the ink at half, still over a slab nearly gone.
            CHECK(prose < proseAtFull);
            CHECK(slabLuma > slabAtFull + 0.2);
        }
    }
}

TEST_CASE("a toast never starts ahead of a plate waiting behind a page, and the line said before it is capped") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    // A line on the row, a plate up, then a page over both at two seconds:
    // the plate is dismissed and the line's clock is capped there.
    session.examine();
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    session.stepMany(sim::MoveInput{}, 120);
    session.toggleCasebook();
    REQUIRE(session.casebookOpen());
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.rungPlateWanted());
    // A rung and a skill under the page: the plate queues, and the toast
    // must not start ahead of it when the page goes down -- it would only
    // be cut the moment the plate rose.
    runTheRoofs(session);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateQueued() == 1);
    // The skill a step LATER than the rung, so it is a toast and not the
    // queued plate's own tick.
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.skillToastWanted());
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());
    // The dismissed plate's own lift has to settle before the queued one
    // can start (the beat of black between two plates); the toast waits
    // through all of it, the tail included.
    int waited = 0;
    while (!session.rungPlateWanted() && waited < kRungPlateLiftSteps + 2 * kPageEaseSteps + 8) {
        session.stepMany(sim::MoveInput{}, 1);
        CHECK_FALSE(session.skillToastWanted());
        ++waited;
    }
    REQUIRE(session.rungPlateWanted());
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateText().tick.empty());
    // The line said before the first plate was capped at its dismissal and
    // frozen under the second plate; it is gone within its cap after the
    // second lift (the one-plate case above pins the cap on its own).
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + kRungPlateLiftSteps + 2);
    CHECK_FALSE(session.rungPlateWanted());
    session.stepMany(sim::MoveInput{}, kRungPlateRowAfterSteps + kPageEaseSteps + 4);
    CHECK(session.alertFadeNow() == 0.0F);
    // And then the toast, once the second plate has left.
    CHECK(session.skillToastWanted());
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
}

TEST_CASE("with no plate at all the queue keeps one toast per skill, the newest") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    // The first toast's hold and ease spent: the next up is level 3, and
    // level 2 never shows.
    session.stepMany(sim::MoveInput{}, 180 + kPageEaseSteps + 4);
    CHECK(session.skillToastWanted());
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 3");
}

TEST_CASE("a skill that levels later in the hold queues as a toast and starts only once the plate has lifted all the way") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    CHECK(session.rungPlateText().tick.empty());
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps / 2);

    // Mid-hold: a different action levels a skill. Not the plate's row --
    // queued, and never started while any of the plate is on screen.
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateWanted());
    CHECK(session.rungPlateText().tick.empty());
    CHECK_FALSE(session.skillToastWanted());
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps / 2);
    CHECK_FALSE(session.rungPlateWanted());
    int steps = 0;
    while (session.rungPlateState().fade > 0.0F && steps < 2 * kRungPlateLiftSteps) {
        CHECK_FALSE(session.skillToastWanted());
        session.stepMany(sim::MoveInput{}, 1);
        ++steps;
    }
    REQUIRE(session.rungPlateState().fade == 0.0F);

    // Gone all the way: the toast, with the raws' own name.
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.skillToastWanted());
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    CHECK(session.pullHud().skillToastFade > 0.0F);
}

TEST_CASE("a toast already on screen goes dark the instant a rung lands under it, and does not come back") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.skillToastWanted());
    REQUIRE(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    REQUIRE(session.pullHud().skillToastFade > 0.0F);

    // The rung lands on the toast already up: the same corner, and -- on
    // this track's own drive -- sometimes the same event.
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateWanted());
    CHECK_FALSE(session.skillToastWanted());
    CHECK(session.pullHud().skillToastFade == 0.0F);

    // Through the whole hold and past it: dismissed, not paused, so it never
    // flashes back once the plate clears the corner.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + kRungPlateLiftSteps + 2);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK_FALSE(session.skillToastWanted());
    CHECK(session.pullHud().skillToastFade == 0.0F);
}

// ===========================================================================
// 4. THE PRIORITY
// ===========================================================================

TEST_CASE("a page dismisses the plate; a rung under a page queues, is never dropped, and shows when the page goes down") {
    Session session(configAt("mission-backroom"));
    wake(session);
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    session.stepMany(sim::MoveInput{}, kPageEaseSteps);
    REQUIRE(session.rungPlateFade() == 1.0F);

    // The page key: the plate that was up is done, its hold cut, and the
    // ease-out is painted at zero under the page. Dismissed, not held.
    session.toggleCasebook();
    REQUIRE(session.casebookOpen());
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateState().fade == 0.0F);
    CHECK(session.rungPlateQueued() == 0);

    // A rung under the page waits, and keeps waiting.
    runTheRoofs(session);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateQueued() == 1);
    CHECK_FALSE(session.rungPlateWanted());
    session.stepMany(sim::MoveInput{}, 40);
    CHECK(session.rungPlateQueued() == 1);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateState().fade == 0.0F);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Wire, 1));

    // The page down: the roofs' plate waits for the page's own close tail
    // -- it would spend its whole rise unseen beneath it otherwise -- and
    // comes up, with its bell, once the tail is gone.
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateQueued() == 1);
    session.stepMany(sim::MoveInput{}, kPageEaseSteps + 2);
    CHECK(session.rungPlateWanted());
    CHECK(session.rungPlateQueued() == 0);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateText().prose == rowFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateState().fade > 0.0F);
}

TEST_CASE("a bouncer's warning dismisses the plate; a rung under it queues and surfaces once the house lets it go") {
    // Seven in the evening at the Gull's counter beside Tarn Wrenhale:
    // test_tavern's own door-policy fixture, both bouncers on.
    Session session(configInTheGull(19, sim::gull::kBartenderX, sim::gull::kBarY - 1));
    wake(session);
    REQUIRE(session.tavern().playerInside());
    REQUIRE(session.tavern().playerStanding() == sim::Standing::Welcome);
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.rungPlateWanted());
    session.stepMany(sim::MoveInput{}, kPageEaseSteps);
    REQUIRE(session.rungPlateFade() == 1.0F);

    // The offence, reported the way a swing reports it. A bouncer has to
    // cross the room to say it, and the plate stands until he has: only a
    // WARNING on the row outranks it, not the walk over.
    session.tavern().reportOffence(sim::Offence::Brawled);
    REQUIRE(session.tavern().playerStanding() == sim::Standing::BeingWarned);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateWanted());
    int steps = 0;
    while (session.tavern().playerStanding() != sim::Standing::Warned && steps < 200) {
        session.stepMany(sim::MoveInput{}, 1);
        ++steps;
    }
    REQUIRE(session.tavern().playerStanding() == sim::Standing::Warned);
    REQUIRE_FALSE(session.tavern().lastWarning().empty());
    session.stepMany(sim::MoveInput{}, 1);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateState().fade == 0.0F);
    CHECK(session.rungPlateQueued() == 0);

    // A rung under the warning waits.
    runTheRoofs(session);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateQueued() == 1);
    CHECK_FALSE(session.rungPlateWanted());
    session.stepMany(sim::MoveInput{}, 30);
    CHECK(session.rungPlateQueued() == 1);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.rungPlateState().fade == 0.0F);

    // Took the warning: out of the door, and the house lets it go
    // (Tavern::tickBouncers' own rule for a warned man who leaves). Then
    // the plate, off the queue, on the free screen.
    session.body().placeAt(sim::gull::kDoorX0, sim::gull::kStreetY, sim::gull::kGroundBand);
    steps = 0;
    while (session.tavern().playerStanding() != sim::Standing::Welcome && steps < 60) {
        session.stepMany(sim::MoveInput{}, 1);
        ++steps;
    }
    REQUIRE(session.tavern().playerStanding() == sim::Standing::Welcome);
    session.stepMany(sim::MoveInput{}, 2);
    CHECK(session.rungPlateWanted());
    CHECK(session.rungPlateQueued() == 0);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateState().fade > 0.0F);
}

TEST_CASE("the alert row stands at half weight under the plate, on the plate's own fade, and is whole again once it lifts") {
    // The line itself: whole with no plate, half under one at full strength,
    // the straight line between, clamped past both ends.
    CHECK(alertWeightUnderRungPlate(0.0F) == 1.0F);
    CHECK(alertWeightUnderRungPlate(1.0F) == kRungPlateAlertWeight);
    CHECK(alertWeightUnderRungPlate(0.5F) ==
          doctest::Approx(1.0F - 0.5F * (1.0F - kRungPlateAlertWeight)));
    CHECK(alertWeightUnderRungPlate(-1.0F) == 1.0F);
    CHECK(alertWeightUnderRungPlate(2.0F) == kRungPlateAlertWeight);
    // Half is half: pinned as the number, not the symbol.
    CHECK(kRungPlateAlertWeight == 0.5F);

    // On the frame: a lead read puts its line on the alert row (test_pull's
    // own verb), whole, before any rung.
    Session session(configAt("mission-backroom"));
    wake(session);
    session.examine();
    session.stepMany(sim::MoveInput{}, kPageEaseSteps + 2);
    REQUIRE_FALSE(session.rungPlateWanted());
    REQUIRE(session.alertFadeNow() == 1.0F);
    CHECK_FALSE(session.alertStepsDownNow());
    CHECK(session.alertInkWeightNow() == 1.0F);

    // The rung: the row steps down as the plate rises, never up, and stands
    // at half under it at full strength.
    crackABox(session);
    float last = session.alertFadeNow();
    for (int i = 0; i < kPageEaseSteps + 1; ++i) {
        session.stepMany(sim::MoveInput{}, 1);
        const float now = session.alertFadeNow();
        CHECK(now <= last);
        last = now;
    }
    REQUIRE(session.rungPlateWanted());
    REQUIRE(session.rungPlateFade() == 1.0F);
    CHECK(session.alertFadeNow() == doctest::Approx(kRungPlateAlertWeight));
    CHECK(session.alertInkWeightNow() == doctest::Approx(kRungPlateAlertWeight));
    CHECK(session.alertStepsDownNow());

    // THE ROW'S CLOCK STOPS UNDER THE PLATE: its four seconds would have run
    // out long before the five-second hold does, and they do not -- the row
    // is still up, at half, with the hold nearly spent.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps - kPageEaseSteps - 2);
    REQUIRE(session.rungPlateWanted());
    CHECK(session.alertFadeNow() == doctest::Approx(kRungPlateAlertWeight));

    // The plate lifts: the row recovers on the plate's own ease, never
    // stepping down, and stands whole once the plate is gone.
    last = session.alertFadeNow();
    int steps = 0;
    while (session.rungPlateState().fade > 0.0F && steps < 2 * kRungPlateLiftSteps) {
        // Still in the small register for the whole lift, never swelling
        // under the card as it leaves.
        CHECK(session.alertStepsDownNow());
        session.stepMany(sim::MoveInput{}, 1);
        const float now = session.alertFadeNow();
        CHECK(now >= last);
        last = now;
        ++steps;
    }
    REQUIRE(session.rungPlateState().fade == 0.0F);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.alertFadeNow() == 1.0F);
    // The line that was capped under the plate finishes in the small
    // register: it never swells.
    CHECK(session.alertStepsDownNow());

    // AND CAPPED: what it has left after the lift is two seconds at most,
    // then its own ease down.
    session.stepMany(sim::MoveInput{}, kRungPlateRowAfterSteps + kPageEaseSteps + 4);
    CHECK(session.alertFadeNow() == 0.0F);
}

TEST_CASE("a bouncer's warning is never the line dimmed: on every frame it is on the row the plate's drawn fade is zero and the row is never dimmer than a twin's with no rung") {
    Session plain(configInTheGull(19, sim::gull::kBartenderX, sim::gull::kBarY - 1));
    Session rung(configInTheGull(19, sim::gull::kBartenderX, sim::gull::kBarY - 1));
    wake(plain);
    wake(rung);
    REQUIRE(rung.tavern().playerInside());
    REQUIRE(rung.tavern().playerStanding() == sim::Standing::Welcome);
    crackABox(rung);
    plain.stepMany(sim::MoveInput{}, 1);
    rung.stepMany(sim::MoveInput{}, 1);
    REQUIRE(rung.rungPlateWanted());
    plain.stepMany(sim::MoveInput{}, kPageEaseSteps);
    rung.stepMany(sim::MoveInput{}, kPageEaseSteps);
    REQUIRE(rung.rungPlateFade() == 1.0F);

    // The offence in both, then ONE STEP AT A TIME. On every frame the
    // warning is on the row -- the first included, the bouncer's walk over
    // excluded -- the plate's drawn fade is zero and the row's fade is never
    // under the twin's; once both rows have eased in, exactly the twin's.
    plain.tavern().reportOffence(sim::Offence::Brawled);
    rung.tavern().reportOffence(sim::Offence::Brawled);
    int warnedFrames = 0;
    for (int step = 0; step < 240; ++step) {
        plain.stepMany(sim::MoveInput{}, 1);
        rung.stepMany(sim::MoveInput{}, 1);
        const bool warned = !rung.tavern().lastWarning().empty() &&
                            rung.tavern().playerStanding() != sim::Standing::Welcome;
        if (!warned) {
            continue;
        }
        ++warnedFrames;
        INFO("step ", step, ", warned frame ", warnedFrames);
        CHECK(rung.rungPlateState().fade == 0.0F);
        CHECK(rung.alertFadeNow() >= plain.alertFadeNow());
        if (warnedFrames > kPageEaseSteps + 1) {
            CHECK(rung.alertFadeNow() == plain.alertFadeNow());
            CHECK(rung.alertFadeNow() == 1.0F);
        }
    }
    CHECK(warnedFrames > 2 * kPageEaseSteps);
    REQUIRE(rung.tavern().playerStanding() == sim::Standing::Warned);
    CHECK_FALSE(rung.rungPlateWanted());
}

TEST_CASE("the drawn wiring: under a plate the alert row's own glyphs are half as bright and one register down, on pixels, against a twin without one") {
    // Two sessions on one world, the same swing at nobody in reach (NOBODY
    // IN REACH. on the row: sixteen glyphs, which fits the glance scale at
    // every size the game runs at, and a swing scores no Legend track), and
    // a rung in one. The frames differ only where the rows differ: the plate
    // above the aim prompt's fence, the alert row below it, and the voice
    // row moved by the alert's own slot. THE ALERT'S OWN GLYPHS BY THEIR
    // HUE: the alert ink is (0.90, 0.62, 0.30) at any alpha over its black
    // field, so green runs at about seven tenths of red and blue at a third
    // -- where the voice row's name (0.90, 0.72, 0.58) carries blue at two
    // thirds, its dim text is grey, every field is black, and the boards
    // are the same in both frames and never counted.
    // At 960x540, where the plate's register is the minor scale, and at
    // 1440x1080, where it is a step under it (rungPlateMetric) and the row
    // must follow the plate, not the HUD.
    for (const auto& size : {std::pair{960, 540}, std::pair{1440, 1080}}) {
    const int width = size.first;
    const int height = size.second;
    INFO(width, "x", height);
    SessionConfig config = configAt("mission-backroom");
    config.width = width;
    config.height = height;
    Session plain(config);
    Session plated(config);
    wake(plain);
    wake(plated);
    plain.punch();
    plated.punch();
    crackABox(plated);
    plain.stepMany(sim::MoveInput{}, kPageEaseSteps + 2);
    plated.stepMany(sim::MoveInput{}, kPageEaseSteps + 2);
    REQUIRE_FALSE(plain.rungPlateWanted());
    REQUIRE(plated.rungPlateWanted());
    REQUIRE(plated.rungPlateFade() == 1.0F);
    REQUIRE(plain.alertFadeNow() == 1.0F);
    Framebuffer a(width, height);
    Framebuffer b(width, height);
    (void)plain.drawFrame(a);
    (void)plated.drawFrame(b);
    const CentreRect fence = hudAimRect(width, height);
    const auto amber = [](const Rgb& p) { return static_cast<double>(std::max(0.0F, p.r - p.b)); };
    const auto alertInk = [](const Rgb& p) {
        return p.r > 0.25F && p.g > 0.60F * p.r && p.g < 0.78F * p.r && p.b < 0.45F * p.r;
    };
    // Three readings of the pixels the twins disagree on. THE PEAK: the
    // row's brightest amber, which halving the alpha halves whatever the
    // register. THE HEIGHT: how many pixel rows the glyphs stand, which
    // stepping the register down shortens whatever the alpha. THE COUNT.
    // THE RAILS: bone, r ~ g ~ b, and under the plate never as bright as the
    // words they frame -- read inside the row's own box (the glyphs plus
    // the pad the rails sit in), because the rest of the band re-packs
    // around a row one register down and its own bone text moves too.
    const auto rail = [](const Rgb& p) {
        return p.r > 0.08F && std::abs(p.r - p.g) < 0.06F && std::abs(p.g - p.b) < 0.10F &&
               p.r >= p.b;
    };
    const auto luma = [](const Rgb& p) {
        return static_cast<double>(0.30F * p.r + 0.59F * p.g + 0.11F * p.b);
    };
    double aPeak = 0.0;
    double bPeak = 0.0;
    double bRail = 0.0;
    double bWord = 0.0;
    int aTop = height;
    int aBottom = -1;
    int bTop = height;
    int bBottom = -1;
    int bLeft = width;
    int bRight = -1;
    std::size_t differing = 0;
    std::size_t aInk = 0;
    std::size_t bInk = 0;
    for (int y = fence.y1; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = a.index(x, y);
            if (a.pixels()[i] == b.pixels()[i]) {
                continue;
            }
            ++differing;
            const Rgb pa = unpackRgb(a.pixels()[i]);
            const Rgb pb = unpackRgb(b.pixels()[i]);
            if (alertInk(pb)) {
                bWord = std::max(bWord, luma(pb));
                bLeft = std::min(bLeft, x);
                bRight = std::max(bRight, x);
            }
            if (alertInk(pa)) {
                ++aInk;
                aTop = std::min(aTop, y);
                aBottom = std::max(aBottom, y);
                aPeak = std::max(aPeak, amber(pa));
            }
            if (alertInk(pb)) {
                ++bInk;
                bTop = std::min(bTop, y);
                bBottom = std::max(bBottom, y);
                bPeak = std::max(bPeak, amber(pb));
            }
        }
    }
    INFO("differing ", differing, " aInk ", aInk, " bInk ", bInk, " a rows ", aTop, "..", aBottom,
         " b rows ", bTop, "..", bBottom, " aPeak ", aPeak, " bPeak ", bPeak);
    REQUIRE(differing > 0);
    REQUIRE(aInk > 0);
    REQUIRE(bInk > 0);
    // Half weight: the brightest amber under the plate is well under the
    // plain frame's.
    CHECK(bPeak < 0.7 * aPeak);
    // One register down: the glyphs stand exactly six rows of the glance
    // scale without the plate and six of the PLATE'S scale under it (the 4x6
    // font's six rows) -- the minor scale at 960x540, a step under it at
    // 1440x1080 -- and there are fewer of their pixels.
    CHECK(aBottom - aTop + 1 == 6 * hudScale(height));
    CHECK(bBottom - bTop + 1 == 6 * rungPlateMetric(width, height).scale);
    CHECK(bInk < aInk);
    // And the rails under the words: bone at a quarter is dimmer than amber
    // at half. The row's plate is a unit of the row's scale plus the rail
    // (padY) outside the glyphs above and below and two units (padX) at the
    // sides, with the rails drawn inside it -- read that box and a unit of
    // air, no more, because the band's next row starts a pad past it.
    int railX = -1;
    int railY = -1;
    {
        const int s = rungPlateMetric(width, height).scale;
        for (int y = std::max(0, bTop - 2 * s); y <= std::min(height - 1, bBottom + 2 * s); ++y) {
            for (int x = std::max(0, bLeft - 2 * s); x <= std::min(width - 1, bRight + 2 * s); ++x) {
                const Rgb pb = unpackRgb(b.pixels()[b.index(x, y)]);
                if (rail(pb) && luma(pb) > bRail) {
                    bRail = luma(pb);
                    railX = x;
                    railY = y;
                }
            }
        }
    }
    INFO("bRail ", bRail, " at ", railX, ",", railY, " bWord ", bWord);
    CHECK(bWord > 0.0);
    CHECK(bRail > 0.0);
    CHECK(bRail < bWord);
    }
}

// ===========================================================================
// 5. OUTSIDE EVERY HASH
// ===========================================================================

TEST_CASE("the plate moves no hash: engine, tavern and books byte-identical with it up, drawn, dismissed and queued") {
    Session plain(configAt("mission-backroom"));
    Session plated(configAt("mission-backroom"));
    const auto same = [&](const char* where) {
        INFO("at ", where);
        CHECK(hashesOf(plain) == hashesOf(plated));
    };
    same("construction");
    wake(plain);
    wake(plated);
    same("awake");

    // THE SAME SIM ACT IN BOTH: the rung. Wanted in both -- the showing is
    // render state, the rung is not. DRAWN in one.
    crackABox(plain);
    crackABox(plated);
    plain.stepMany(sim::MoveInput{}, 1);
    plated.stepMany(sim::MoveInput{}, 1);
    CHECK(plain.rungPlateWanted());
    CHECK(plated.rungPlateWanted());
    same("after the rung");
    Framebuffer frame(960, 540);
    (void)plated.drawFrame(frame);
    plain.stepMany(sim::MoveInput{}, kPageEaseSteps);
    plated.stepMany(sim::MoveInput{}, kPageEaseSteps);
    (void)plated.drawFrame(frame);
    same("after the draw");

    // Dismissed under a page in one, a second rung queued in both (behind
    // the page in one, behind the plate still up in the other), drawn with
    // the page up and again with it down.
    plated.toggleCasebook();
    runTheRoofs(plain);
    runTheRoofs(plated);
    plain.stepMany(sim::MoveInput{}, 4);
    plated.stepMany(sim::MoveInput{}, 4);
    CHECK(plain.rungPlateQueued() == 1);
    CHECK(plated.rungPlateQueued() == 1);
    CHECK(plain.rungPlateWanted());
    CHECK_FALSE(plated.rungPlateWanted());
    (void)plated.drawFrame(frame);
    plated.toggleCasebook();
    same("after the page");

    // Through the hold in both, the roofs up in both, drawn in one.
    plain.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + 2 * kRungPlateLiftSteps + 4);
    plated.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + 2 * kRungPlateLiftSteps + 4);
    CHECK(plain.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
    CHECK(plated.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
    Framebuffer again(960, 540);
    (void)plated.drawFrame(again);
    same("after the hold");
    CHECK(plain.legend().row(sim::LegendTrack::Wire).rung ==
          plated.legend().row(sim::LegendTrack::Wire).rung);
    CHECK(plain.legend().row(sim::LegendTrack::Roofs).rung ==
          plated.legend().row(sim::LegendTrack::Roofs).rung);
}

// ===========================================================================
// 6. THE DRIVE
// ===========================================================================

namespace {

/// test_scripted_lines' own harness, with main()'s rule for the clock: a
/// line sets its own hour when nobody named one, and a line with no hour of
/// its own (the flame's) keeps the session's default.
[[nodiscard]] SmokeRunResult play(SmokeRunConfig run) {
    run.session.contentDir = content::contentDir();
    if (!run.session.timeOfDayGiven) {
        const int hour = scriptedStartHour(run);
        if (hour >= 0) {
            run.session.timeOfDay = hour * 3600;
        }
    }
    run.steps = 0;
    run.stamp = false;
    return runSmoke(run);
}

/// Both beats on one track: the rung rose off the real verbs, and the plate
/// is up, fully risen, reading that rung's own head and row.
void checkRung(const char* word, sim::LegendTrack track) {
    SmokeRunConfig run;
    run.rung = word;
    const SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.ok);
    CHECK_FALSE(played.scriptFellShort());
    const RungLineResult& rung = played.rungResult;
    CHECK(rung.found);
    CHECK(rung.track == track);
    CHECK(rung.rose);
    CHECK(rung.to > rung.from);
    CHECK(rung.to >= 1);
    CHECK(rung.plateUp);
    CHECK(rung.head == headFor(track, rung.to));
    CHECK(rung.row == rowFor(track, rung.to));
    CHECK_FALSE(rung.row.empty());
    CHECK(played.summary.find(std::string("rung=") + word + " ") != std::string::npos);
    CHECK(played.summary.find("row=\"" + rung.row + "\"") != std::string::npos);
    CHECK(played.summary.find("plate=up") != std::string::npos);
    CHECK(played.summary.find("found=yes") != std::string::npos);
    // WHAT THE PROMPT NAMED is on the record: the interact target from
    // where the body stands, never empty on a drive that landed.
    CHECK_FALSE(rung.aim.empty());
    CHECK(played.summary.find("aim=\"" + rung.aim + "\"") != std::string::npos);
    CHECK(played.summary.find("rose=yes") != std::string::npos);
}

/// The trade or the law at one in the afternoon: no Cull in the Gull, no
/// rung, and the summary says which.
void checkRungNeedsCull(const char* word, sim::LegendTrack track) {
    SmokeRunConfig run;
    run.rung = word;
    run.session.timeOfDay = 13 * 3600;
    run.session.timeOfDayGiven = true;
    const SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK(played.rungResult.found);
    CHECK(played.rungResult.track == track);
    CHECK_FALSE(played.rungResult.rose);
    CHECK_FALSE(played.rungResult.reason.empty());
    CHECK(played.summary.find("rose=no") != std::string::npos);
    CHECK(played.summary.find("reason=\"") != std::string::npos);
}

}  // namespace

TEST_CASE("--rung=wire: one cracked strongbox at two in the morning, and the plate reads LIGHT FINGERS") {
    checkRung("wire", sim::LegendTrack::Wire);
}

TEST_CASE("--rung=roofs: the lead and the alley leapt until the roofs will have it, and the plate reads TENANT") {
    checkRung("roofs", sim::LegendTrack::Roofs);
}

TEST_CASE("--rung=flame: the Mission's two leads read, and the plate reads DISCIPLE") {
    checkRung("flame", sim::LegendTrack::Flame);
}

TEST_CASE("--rung=trade: the ward's bounty paid across Cull's table, and the plate reads STALLKEEP") {
    checkRung("trade", sim::LegendTrack::Trade);
}

TEST_CASE("--rung=law: four drinks stood to Watchman Cull, and the plate reads KNOWN TO THE WATCH") {
    checkRung("law", sim::LegendTrack::Law);
}

TEST_CASE("--rung=trade and --rung=law at one in the afternoon: no Cull, no rung, and the summary gives the reason") {
    checkRungNeedsCull("trade", sim::LegendTrack::Trade);
    checkRungNeedsCull("law", sim::LegendTrack::Law);
}

TEST_CASE("--rung=bloodletter names no track: neither beat lands, and the summary says so") {
    SmokeRunConfig run;
    run.rung = "bloodletter";
    const SmokeRunResult played = play(run);
    INFO(played.summary);
    CHECK_FALSE(played.rungResult.found);
    CHECK_FALSE(played.rungResult.rose);
    CHECK_FALSE(played.rungResult.plateUp);
    CHECK(played.rungResult.row.empty());
    CHECK(played.scriptFellShort());
    CHECK(played.summary.find("found=no") != std::string::npos);
    CHECK(played.summary.find("plate=down") != std::string::npos);
    CHECK(played.summary.find("rose=no") != std::string::npos);
}

TEST_CASE("a squarer window steps the plate's register down until the frame holds sixty-four cells, and the longest row draws whole") {
    const sim::BarkTables& sheet = barks();
    REQUIRE(sheet.loaded());
    std::string longestRow;
    for (const std::string& key : everyLegendKey()) {
        const std::string row(sheet.line(key, 0));
        if (row.size() > longestRow.size()) {
            longestRow = row;
        }
    }
    for (const auto& size : {std::pair{1440, 1080}, std::pair{720, 720}}) {
        const int width = size.first;
        const int height = size.second;
        INFO(width, "x", height);
        const PanelMetric metric = rungPlateMetric(width, height);
        // Stepped down from the height's own register, and no further than
        // the first scale that holds the sheet's frame.
        CHECK(metric.scale < panelMetric(height).scale);
        CHECK(metric.cellsIn(width) >= 64);
        CHECK(PanelMetric{metric.scale + 1}.cellsIn(width) < 64);
        RungPlateState state;
        state.head = "THE FLAME  --  THE ONE WHO WENT DOWN THERE";
        state.prose = longestRow;
        state.fade = 1.0F;
        const RungPlateBox box = rungPlateBox(width, height, state);
        REQUIRE(box.draws);
        // The card at its full width: nothing clipped, a cell of margin.
        const int widest = std::max(static_cast<int>(state.head.size()) + 2,
                                    static_cast<int>(longestRow.size()));
        CHECK(box.w == metric.widthOf(widest + 2 + 2 * kRungPlateGutterCells));
        CHECK(box.x >= metric.cellW());
        CHECK(box.x + box.w <= width - metric.cellW());
    }
}

// ===========================================================================
// 7. THE GEOMETRY
// ===========================================================================

TEST_CASE("the plate seats under the ribbon and the announce row, above the reticle's fence, inside the frame, at four sizes, and draws whole") {
    const sim::BarkTables& sheet = barks();
    REQUIRE(sheet.loaded());
    // The longest of everything authored, and the widest head legend.cpp has.
    std::string longestRow;
    for (const std::string& key : everyLegendKey()) {
        const std::string row(sheet.line(key, 0));
        if (row.size() > longestRow.size()) {
            longestRow = row;
        }
    }
    std::string widestHead;
    for (const sim::LegendTrack track : kTracks) {
        for (std::int32_t rung = 1; rung <= sim::kLegendRungs; ++rung) {
            const std::string head = headFor(track, rung);
            if (head.size() > widestHead.size()) {
                widestHead = head;
            }
        }
    }
    REQUIRE_FALSE(longestRow.empty());
    REQUIRE_FALSE(widestHead.empty());
    const std::string topRow(sheet.line(kLegendTopKey, 0));

    for (const auto& size : {std::pair{320, 180}, std::pair{640, 360}, std::pair{960, 540},
                             std::pair{1920, 1080}}) {
        const int width = size.first;
        const int height = size.second;
        for (const bool topped : {false, true}) {
            INFO(width, "x", height, topped ? " topped" : "");
            RungPlateState state;
            state.head = widestHead;
            state.prose = longestRow;
            state.top = topped ? std::string_view{topRow} : std::string_view{};
            state.fade = 1.0F;
            state.drift = 0.0F;
            const RungPlateBox box = rungPlateBox(width, height, state);
            REQUIRE(box.draws);
            // THE REGISTER: the terminal register everywhere but 640x360,
            // where the minor scale has bottomed out at one under a HUD at
            // two and the plate takes the glance scale.
            const PanelMetric metric = rungPlateMetric(width, height);
            CHECK(metric.scale == (height == 360 ? hudScale(height) : panelMetric(height).scale));
            CHECK(metric.scale >= panelMetric(height).scale);
            // Inside the frame less a cell of margin a side, and centred.
            CHECK(box.x >= metric.cellW());
            CHECK(box.x + box.w <= width - metric.cellW());
            CHECK(std::abs((box.x + box.w / 2) - width / 2) <= metric.cellW());
            // EXACTLY the widest row plus two cells of gutter a side and the
            // two border cells -- the head wears a cell of fill either side
            // of its words, so it counts two wider than it reads; exactly
            // tall enough for its rows and the two rules.
            const int widest = std::max(
                static_cast<int>(widestHead.size()) + 2,
                std::max(static_cast<int>(longestRow.size()),
                         static_cast<int>(state.top.size())));
            CHECK(box.w == metric.widthOf(widest + 2 + 2 * kRungPlateGutterCells));
            // ONE ROW TALLER than before the leading pass: head, a blank row
            // of air, the prose, and the top row when the track is capped.
            CHECK(box.h == metric.heightOf((topped ? 4 : 3) + 2));
            // UNDER THE RIBBON BLOCK AND THE ANNOUNCE PLATE'S ROW. At the
            // HUD's scale 1 the announce plate's true lowest pixel is row 31;
            // at every larger scale the true row is lower than 31 scales, so
            // this floor is the conservative one. The topped case at 320x180
            // is the one frame tight enough that the seat's own collision
            // clearance gets clamped back down to exactly this floor rather
            // than a full row past it -- the reticle's fence wins that trade,
            // never the row above.
            CHECK(box.y >= 31 * hudScale(height));
            // ABOVE THE RETICLE'S FENCE: the plate never sits on the thing
            // the player is looking at, nor on the words that name it. WITH
            // A CELL ROW OF CLEARANCE wherever the frame has one to spend
            // between the floor and the fence; the smallest window shares
            // what it has (seven pixels) five above the keyline, two below.
            const CentreRect fence = hudAimRect(width, height);
            CHECK(box.y + box.h <= fence.y0);
            const int floorY = 31 * hudScale(height);
            if (fence.y0 - box.h - floorY >= 2 * metric.cellH()) {
                CHECK(box.y + box.h + metric.cellH() <= fence.y0);
            } else {
                // No row to spend: at least five glance units between the
                // keyline and the stat stack above, the rest split. At
                // 320x180 that is seven pixels shared five above and two
                // below (36), or none at all for the topped card (31).
                const int room = std::max(0, fence.y0 - box.h - floorY);
                CHECK(box.y == floorY + std::max(room / 2, std::min(room, 5 * hudScale(height))));
                if (width == 320) {
                    CHECK(box.y == (topped ? 31 : 36));
                }
            }
            // THE SLAB'S OWN EDGES, on pixels: a unit left of the box and
            // flush with its right edge (the last cell's own last column),
            // so the corner diamonds -- which fill the first four of their
            // cell's five columns -- keep a unit of slab either side.
            {
                Framebuffer bare(width, height);
                bare.clear(Rgb{0.10F, 0.12F, 0.14F});
                Framebuffer slab(width, height);
                slab.clear(Rgb{0.10F, 0.12F, 0.14F});
                drawRungPlate(slab, state);
                const int airY = box.y + metric.heightOf(2) + metric.cellH() / 2;
                int leftEdge = width;
                int rightEdge = -1;
                for (int x = 0; x < width; ++x) {
                    const std::size_t i = slab.index(x, airY);
                    if (slab.pixels()[i] != bare.pixels()[i]) {
                        leftEdge = std::min(leftEdge, x);
                        rightEdge = std::max(rightEdge, x);
                    }
                }
                CHECK(leftEdge == box.x - metric.scale);
                CHECK(rightEdge == box.x + box.w - 1);
            }
            // Seated high: the top quarter of the spare, not the middle.
            CHECK(box.y < (height - box.h) / 2);
            // THE TICK ROW: one row more where the frame has a row to spend
            // on it, and dropped whole where it has not -- 320x180 always,
            // and 1920x1080 once the top row has taken the last one.
            RungPlateState ticked = state;
            ticked.tick = "SKYRUNNING RISES TO 2";
            const RungPlateBox tickBox = rungPlateBox(width, height, ticked);
            REQUIRE(tickBox.draws);
            const bool tickFits = (width == 960) || ((width == 1920 || width == 640) && !topped);
            CHECK(tickBox.h == box.h + (tickFits ? metric.cellH() : 0));
            CHECK(tickBox.y + tickBox.h <= fence.y0);
            CHECK(tickBox.y >= floorY);
            if (tickFits) {
                // THE TICK'S LEADING: its caps start one unit into its own
                // row -- two of air under the prose (or the top row), two
                // between its shadow and the rule --
                // pinned on the pixels against the same card with the tick
                // blanked, which lays out the same cells and draws no tick.
                RungPlateState blankTick = ticked;
                const std::string blanks(ticked.tick.size(), ' ');
                blankTick.tick = blanks;
                Framebuffer withTick(width, height);
                withTick.clear(Rgb{0.10F, 0.12F, 0.14F});
                drawRungPlate(withTick, ticked);
                Framebuffer noTick(width, height);
                noTick.clear(Rgb{0.10F, 0.12F, 0.14F});
                drawRungPlate(noTick, blankTick);
                const int tickRow = topped ? 4 : 3;
                const int rowY0 = tickBox.y + metric.heightOf(1 + tickRow);
                int firstTickInk = height;
                for (int y = rowY0 - metric.cellH(); y < rowY0 + 2 * metric.cellH(); ++y) {
                    for (int x = 0; x < width; ++x) {
                        const std::size_t i = withTick.index(x, y);
                        if (withTick.pixels()[i] != noTick.pixels()[i]) {
                            firstTickInk = std::min(firstTickInk, y);
                        }
                    }
                }
                CHECK(firstTickInk == rowY0 + metric.scale);
            }

            // And drawn: ink inside its own pane (a cell of slack for the
            // junction motifs), nothing beyond it, ink at all.
            Framebuffer without(width, height);
            without.clear(Rgb{0.10F, 0.12F, 0.14F});
            Framebuffer with(width, height);
            with.clear(Rgb{0.10F, 0.12F, 0.14F});
            drawRungPlate(with, state);
            const Ink ink = inkOf(with, without, box, metric.cellW());
            CHECK(ink.inside > 0);
            CHECK(ink.outside == 0);

            // THE GUTTER ON PIXELS: the prose's first ink column sits exactly
            // two cells inside the left keyline (the border cell, then the
            // two of gutter), and no ink enters the right gutter. The same
            // plate with the prose blanked lays out the same cells and draws
            // no prose, so the difference is the prose's own ink.
            const std::string blanks(longestRow.size(), ' ');
            RungPlateState mute = state;
            mute.prose = blanks;
            Framebuffer noProse(width, height);
            noProse.clear(Rgb{0.10F, 0.12F, 0.14F});
            drawRungPlate(noProse, mute);
            const int proseY0 = box.y + metric.heightOf(3);
            const int proseY1 = proseY0 + metric.cellH();
            int firstInk = width;
            int lastInk = -1;
            for (int y = proseY0; y < proseY1; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t i = with.index(x, y);
                    if (with.pixels()[i] != noProse.pixels()[i]) {
                        firstInk = std::min(firstInk, x);
                        lastInk = std::max(lastInk, x);
                    }
                }
            }
            REQUIRE(lastInk >= 0);
            // Inside the first cell after the gutter, never in the gutter:
            // the font's own glyphs start on their first or second column.
            CHECK(firstInk >= box.x + metric.widthOf(1 + kRungPlateGutterCells));
            CHECK(firstInk < box.x + metric.widthOf(2 + kRungPlateGutterCells));
            CHECK(lastInk <= box.x + box.w - metric.widthOf(1 + kRungPlateGutterCells) - 1);
        }
    }

    // Empty text and a zero fade draw nothing at all, and lay out nothing.
    {
        Framebuffer bare(320, 180);
        bare.clear(Rgb{0.10F, 0.12F, 0.14F});
        Framebuffer with(320, 180);
        with.clear(Rgb{0.10F, 0.12F, 0.14F});
        RungPlateState none;
        drawRungPlate(with, none);
        CHECK(with.pixels() == bare.pixels());
        CHECK_FALSE(rungPlateBox(320, 180, none).draws);
        RungPlateState dark;
        dark.head = widestHead;
        dark.prose = longestRow;
        dark.fade = 0.0F;
        drawRungPlate(with, dark);
        CHECK(with.pixels() == bare.pixels());
        CHECK(rungPlateBox(320, 180, dark).draws);
    }

    // Every authored row at the smallest window: its own pane, whole, two
    // cells of gutter inside each keyline, a cell of air off either edge --
    // what kRungPlateRowCells promises.
    for (const sim::LegendTrack track : kTracks) {
        for (std::int32_t rung = 1; rung <= sim::kLegendRungs; ++rung) {
            INFO(legendTrackKey(track), " ", rung);
            const RungPlateText text = rungPlateFor(LegendRise{track, rung - 1, rung}, sheet);
            RungPlateState state;
            state.head = text.head;
            state.prose = text.prose;
            state.top = text.top;
            state.fade = 1.0F;
            const RungPlateBox box = rungPlateBox(320, 180, state);
            REQUIRE(box.draws);
            const PanelMetric metric = rungPlateMetric(320, 180);
            const int widest = std::max(
                static_cast<int>(text.head.size()) + 2,
                std::max(static_cast<int>(text.prose.size()), static_cast<int>(text.top.size())));
            CHECK(box.w == metric.widthOf(widest + 2 + 2 * kRungPlateGutterCells));
            CHECK(box.w >= metric.widthOf(static_cast<int>(text.prose.size()) + 2 +
                                          2 * kRungPlateGutterCells));
            CHECK(box.w >= metric.widthOf(static_cast<int>(text.head.size()) + 4 +
                                          2 * kRungPlateGutterCells));
            CHECK(box.x >= metric.cellW());
            CHECK(box.x + box.w <= 320 - metric.cellW());
        }
    }
}
