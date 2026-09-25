// THE RUNG PLATE, PINNED -- the ward says so, and the simulation cannot tell.
//
// Seven claims, each its own case (the sixth is one case per track):
//
//   1. THE WATCH seeds silently and reports a rise per track, never a fall.
//      A session that loads with rungs on the sheet has not just earned
//      them; a rung that skips one says so rather than inventing the one it
//      skipped; the same rise after a fall is reported again, honestly.
//   2. THE SHEET: every legend.<track>.<rung> key and legend.top resolves,
//      one row each, fifty-six glyphs and under (one line of the 4x6 font
//      inside the plate at 320x180), no dash, the priests' word absent. The
//      head comes off legend.cpp's own two tables and nothing else; a key
//      nobody authored draws an empty row, never a sentence the code chose.
//   3. THE PLATE fires on a real rung, holds its five seconds, queues a
//      second rung behind itself, rises THROUGH its seat rather than sliding
//      back, and sleeps -- and the same step's skill level waits behind it,
//      never dropped, while a toast already on screen finishes.
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
//   7. THE GEOMETRY, at 320x180, 960x540 and 1920x1080: the plate seats
//      under the ribbon block and the announce plate's row, above the
//      reticle's fence, inside the frame less a cell a side, centred, and
//      every authored row draws whole with no ink outside its own pane.

#include <doctest/doctest.h>

#include <algorithm>
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

TEST_CASE("every legend key resolves: one row each, fifty-six glyphs and under, no dash, nothing a priest would say") {
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

    // A beat of black, then the roofs' turn with their own words.
    session.stepMany(sim::MoveInput{}, kPageEaseSteps + 4);
    CHECK(session.rungPlateWanted());
    CHECK(session.rungPlateQueued() == 0);
    CHECK(session.rungPlateText().head == headFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateText().prose == rowFor(sim::LegendTrack::Roofs, 1));
    CHECK(session.rungPlateFade() > 0.0F);

    // And then sleep, the words kept so the notice ends with its own.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + kPageEaseSteps + 2);
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

TEST_CASE("a rung and a skill level in one step: the plate wins, the toast waits and is never dropped") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    REQUIRE_FALSE(session.skillToastWanted());

    // The same step: the level and the rung.
    while (!skills.use(sim::kBlockSkill)) {
    }
    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateWanted());
    CHECK_FALSE(session.skillToastWanted());
    CHECK(session.skillToastLabel().empty());
    CHECK(session.pullHud().skillToastFade == 0.0F);

    // Still waiting through the whole hold.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps / 2);
    CHECK(session.rungPlateWanted());
    CHECK_FALSE(session.skillToastWanted());

    // The plate down: the toast, with the raws' own name.
    session.stepMany(sim::MoveInput{}, kRungPlateHoldSteps / 2 + kPageEaseSteps + 2);
    CHECK_FALSE(session.rungPlateWanted());
    CHECK(session.skillToastWanted());
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    CHECK(session.pullHud().skillToastFade > 0.0F);
}

TEST_CASE("a toast already on screen finishes when a rung lands under it") {
    Session session(configAt("mission-backroom"));
    wake(session);
    sim::SkillTrack& skills = session.tavern().dialogue().skills();
    REQUIRE(skills.find(sim::kBlockSkill) != nullptr);
    while (!skills.use(sim::kBlockSkill)) {
    }
    session.stepMany(sim::MoveInput{}, 1);
    REQUIRE(session.skillToastWanted());
    REQUIRE(session.skillToastLabel() == "SHIELDWALL RISES TO 1");

    crackABox(session);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.rungPlateWanted());
    CHECK(session.skillToastWanted());
    CHECK(session.skillToastLabel() == "SHIELDWALL RISES TO 1");
    session.stepMany(sim::MoveInput{}, kPageEaseSteps);
    CHECK(session.rungPlateFade() == 1.0F);
    CHECK(session.pullHud().skillToastFade > 0.0F);
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

    // The page down: the roofs' plate, the very next step, with its bell.
    session.toggleCasebook();
    REQUIRE_FALSE(session.casebookOpen());
    session.stepMany(sim::MoveInput{}, 1);
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
    plain.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + 2 * kPageEaseSteps + 4);
    plated.stepMany(sim::MoveInput{}, kRungPlateHoldSteps + 2 * kPageEaseSteps + 4);
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
}

// ===========================================================================
// 7. THE GEOMETRY
// ===========================================================================

TEST_CASE("the plate seats under the ribbon and the announce row, above the reticle's fence, inside the frame, at three sizes, and draws whole") {
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

    for (const auto& size : {std::pair{320, 180}, std::pair{960, 540}, std::pair{1920, 1080}}) {
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
            const PanelMetric metric = panelMetric(height);
            // Inside the frame less a cell of margin a side, and centred.
            CHECK(box.x >= metric.cellW());
            CHECK(box.x + box.w <= width - metric.cellW());
            CHECK(std::abs((box.x + box.w / 2) - width / 2) <= metric.cellW());
            // Wide enough for the longest row whole with its padding and
            // borders; exactly tall enough for its rows and the two rules.
            CHECK(box.w >= metric.widthOf(static_cast<int>(longestRow.size()) + 4));
            CHECK(box.h == metric.heightOf((topped ? 3 : 2) + 2));
            // UNDER THE RIBBON BLOCK AND THE ANNOUNCE PLATE'S ROW. At the HUD's
            // scale 1 the announce plate's lowest pixel is row 34 (the ribbon
            // block's 18, its lift and pad, the plate's glyphs and a pad);
            // at every larger scale the true row is lower than 34 scales, so
            // this floor is the conservative one.
            CHECK(box.y >= 34 * hudScale(height));
            // ABOVE THE RETICLE'S FENCE: the plate never sits on the thing
            // the player is looking at, nor on the words that name it.
            const CentreRect fence = hudAimRect(width, height);
            CHECK(box.y + box.h <= fence.y0);
            // Seated high: the top quarter of the spare, not the middle.
            CHECK(box.y < (height - box.h) / 2);

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

    // Every authored row at the smallest window: its own pane, whole, with
    // a cell of air off either edge -- what kRungPlateRowCells promises.
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
            const PanelMetric metric = panelMetric(180);
            CHECK(box.w >= metric.widthOf(static_cast<int>(text.prose.size()) + 4));
            CHECK(box.w >= metric.widthOf(static_cast<int>(text.head.size()) + 6));
            CHECK(box.x >= metric.cellW());
            CHECK(box.x + box.w <= 320 - metric.cellW());
        }
    }
}
