// UI-EA (LANE HUD): THE LAW OF EARNED TEXT, PINNED.
//
// The brief is a measurable target -- half the words -- and the spec's street
// budgets bind the AT-REST frame: rest #11 is EIGHT words, and everything
// that used to be furniture (clock, purse, case row, room, guild, objective,
// the street sub-label, the build stamp) either sleeps until its own event or
// is gone outright. A diet with no scale regresses one row at a time, which
// is exactly how the HUD grew to twenty-four words in the first place; these
// cases are the scale.
//
// TWO LAYERS, DELIBERATELY. The word counts are asserted at the HudState
// level with the census's own token law (a whitespace-delimited token
// containing an alphanumeric is a word; glyphs are free), because that is
// the level the budget binds and the level a regression lands at. The WAKE
// MACHINERY -- edges arming countdowns, countdowns expiring -- is asserted
// on a real Session through the *Wanted() accessors, the exact pattern
// test_place_plate.cpp already holds the threshold plate to.

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/pull.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/player.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;

namespace {

/// The census's own measurement law: a WORD is a whitespace-delimited token
/// containing at least one alphanumeric ("5/10" is one word; "T" is a word);
/// bars, bullets and bare punctuation are glyphs and count zero.
[[nodiscard]] int wordsIn(std::string_view text) {
    int words = 0;
    bool inToken = false;
    bool tokenCounts = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n') {
            if (inToken && tokenCounts) {
                ++words;
            }
            inToken = false;
            tokenCounts = false;
            continue;
        }
        inToken = true;
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            tokenCounts = true;
        }
    }
    if (inToken && tokenCounts) {
        ++words;
    }
    return words;
}

/// Every word drawHud would put on the frame for this state, counted through
/// the same gates drawHud draws through (a row at zero fade or with an empty
/// label costs nothing). The compass is charged at its worst case -- five
/// point labels in the 180-degree window -- and the clock at one word, the
/// purse at two, exactly what they print.
[[nodiscard]] int hudWordCount(const HudState& s) {
    int words = 0;
    if (s.showCompass) {
        words += 5;  // worst-case visible points: e.g. W NW N NE E
    }
    if (s.timeOfDaySeconds >= 0 && s.clockFade > 0.0F) {
        words += 1;  // "20:02"
    }
    if (s.coin >= 0 && s.purseFade > 0.0F) {
        words += 2;  // "120 C"
    }
    const auto row = [&](std::string_view label, float fade) {
        if (!label.empty() && fade > 0.0F) {
            words += wordsIn(label);
        }
    };
    row(s.standingLabel, s.standingFade);
    row(s.heatLabel, s.heatFade);
    row(s.stashLabel, s.stashFade);
    row(s.spellLabel, s.spellFade);
    for (std::size_t i = 0; i < s.effectLabels.size(); ++i) {
        row(s.effectLabels[i], s.effectFades[i]);
    }
    row(s.stealthLabel, s.stealthFade);
    row(s.lockLabel, s.lockFade);
    row(s.blockLabel, s.blockFade);
    row(s.handsLabel, s.handsFade);
    row(s.caseLabel, s.caseFade);
    row(s.roomLabel, s.roomFade);
    row(s.guildLabel, s.guildFade);
    row(s.objectiveLabel, s.objectiveFade);
    row(s.rivalLabel, s.rivalFade);
    row(s.wheelHint, s.wheelHintFade);
    if (s.showAlert && s.alertFade > 0.0F) {
        row(s.alert, 1.0F);
    }
    row(s.placePlate, s.placePlateFade);
    row(s.casePlate, s.casePlateFade);
    // THE PULL PACK: the followed lead's line is STATE tier (up whenever a
    // lead is followed, drawn with the compass), the toast is EVENT tier.
    if (s.showCompass) {
        row(s.pullLabel, 1.0F);
    }
    row(s.skillToast, s.skillToastFade);
    if (s.quickBarFade > 0.0F) {
        words += 10;  // the ten slot digits
        if (s.quickSelected >= 0 &&
            s.quickSelected < static_cast<int>(s.quickSlots.size())) {
            words += wordsIn(s.quickSlots[static_cast<std::size_t>(s.quickSelected)]);
        }
    }
    if (!s.aimVerb.empty() && s.interactFade > 0.0F) {
        words += wordsIn(s.aimKey) + wordsIn(s.aimVerb) + wordsIn(s.aimSubject) +
                 wordsIn(s.aimNote);
    }
    return words;
}

/// The rest street, exactly as a settled session assembles it: compass and
/// bars up, the LOOK verb on the reticle, every reference row asleep. This is
/// the state the wake tests below prove a real Session settles into.
[[nodiscard]] HudState restStreet() {
    HudState rest;
    rest.health = 100;
    rest.fatigue = 160;
    rest.fatigueMax = 160;
    rest.yawBam = sim::kFacingNorth;
    rest.timeOfDaySeconds = 20 * 3600;
    rest.clockFade = 0.0F;  // asleep: no tick, no charge, no priced page
    rest.coin = 120;
    rest.purseFade = 0.0F;  // asleep: no delta
    rest.caseLabel = "CASE 0/1 > MISSION OF THE FLAME";
    rest.caseFade = 0.0F;   // asleep: no beat moved, no book closed
    rest.aimKey = "E";
    rest.aimVerb = "LOOK";
    return rest;
}

/// hud.cpp's own kGlyphH, restated: the 4x6 font is six rows tall, and the
/// ribbon strip is one glyph row plus a scale unit of air plus its shadow.
constexpr int kGlyphHeightForTests = 6;

SessionConfig quietDocks(int hour = 20) {
    SessionConfig config;
    config.contentDir = granadad::content::contentDir();
    config.world = sim::docks::kWorldName;
    config.timeOfDay = hour * 3600;
    config.spawnYaw = sim::kFacingNorth;
    config.spawnYawGiven = true;
    config.width = 320;
    config.height = 180;
    return config;
}

}  // namespace

TEST_CASE("the rest street holds the spec's eight-word budget") {
    // UI-EA-SPEC 1.2 #11: rest = 8. Compass (worst case five letters) plus
    // the reticle's own "E - LOOK" is seven; everything else is asleep.
    const HudState rest = restStreet();
    const int words = hudWordCount(rest);
    INFO("rest street words: ", words);
    CHECK(words <= 8);

    // And the rest frame DRAWS that way: the clock, the purse and the case
    // row put zero ink on the frame at zero fade, so the counted budget and
    // the rendered frame are one fact.
    Framebuffer bare(640, 360);
    bare.clear(Rgb{0.10F, 0.12F, 0.14F});
    Framebuffer awake(640, 360);
    awake.clear(Rgb{0.10F, 0.12F, 0.14F});
    HudState lit = rest;
    lit.clockFade = 1.0F;
    lit.purseFade = 1.0F;
    lit.caseFade = 1.0F;
    drawHud(awake, lit);
    Framebuffer asleep(640, 360);
    asleep.clear(Rgb{0.10F, 0.12F, 0.14F});
    drawHud(asleep, rest);
    std::size_t awakeInk = 0;
    std::size_t asleepInk = 0;
    for (std::size_t i = 0; i < bare.pixels().size(); ++i) {
        awakeInk += bare.pixels()[i] != awake.pixels()[i] ? 1U : 0U;
        asleepInk += bare.pixels()[i] != asleep.pixels()[i] ? 1U : 0U;
    }
    INFO("ink awake ", awakeInk, ", asleep ", asleepInk);
    CHECK(awakeInk > asleepInk);  // the three woken rows really draw...
    CHECK(asleepInk > 0);         // ...and the rest frame still has its compass and bars
}

TEST_CASE("the followed lead is the one line the rest budget grew by, and it is never more than seven words") {
    // UI-EA-SPEC 1.2 #11, amended by the PULL PACK: rest = 8, plus the
    // followed lead's line -- bearing and paces (2), the place in the sign's
    // own words (the article kept: THE NETTERS' COMPOUND, 3), BAND n
    // off-plane (2) -- while a lead is followed. The longest line any
    // authored lead can print is pinned here across all three case files
    // from one fixed spot, so a renamed place cannot quietly grow the street.
    const sim::CasebookRaws bloodletter = sim::CasebookRaws::load(granadad::content::contentDir());
    const sim::CasebookRaws courier =
        sim::CasebookRaws::loadFile(sim::missionSheetRawsPath(granadad::content::contentDir()));
    const sim::CasebookRaws eviction =
        sim::CasebookRaws::loadFile(sim::evictionRawsPath(granadad::content::contentDir()));
    int widest = 0;
    std::string widestLine;
    for (const sim::CasebookRaws* file : {&bloodletter, &courier, &eviction}) {
        for (const sim::Lead& lead : file->leads()) {
            // From the Mission's back room, on the surface band, so every
            // off-band lead prints its BAND word too.
            const std::string line =
                pullLine(pullBearing(126, 110, 19, lead.site, false), lead.place);
            INFO("line: ", line);
            CHECK(wordsIn(line) <= 7);
            if (static_cast<int>(line.size()) > widest) {
                widest = static_cast<int>(line.size());
                widestLine = line;
            }
        }
    }
    HudState rest = restStreet();
    rest.pullLabel = widestLine;
    const int words = hudWordCount(rest);
    INFO("rest street with the followed lead: ", words, " words (", widestLine, ")");
    CHECK(words <= 8 + 7);
    // And with no lead followed the old budget is untouched.
    CHECK(hudWordCount(restStreet()) <= 8);
}

TEST_CASE("the followed-lead strip is pinned at 320x180 and 1920x1080: whole, under the ribbon, clear of the corner and the play space") {
    // The one line the diet put back has to fit where the old sub-label
    // lived at the two sizes the game runs at the ends of, with the corner
    // FULL (clock, purse, standing, heat) -- the two are in one band and a
    // centred line that ran under a right-anchored corner would be the lock
    // row through the guild row again.
    for (const auto& size : {std::pair{320, 180}, std::pair{1920, 1080}}) {
        HudState corner = restStreet();
        corner.clockFade = 1.0F;
        corner.purseFade = 1.0F;
        corner.standingLabel = "WELL SPOKEN OF";
        corner.heatLabel = "HEAT 12  WANTED";
        Framebuffer base(size.first, size.second);
        base.clear(Rgb{0.10F, 0.12F, 0.14F});
        Framebuffer withCorner(size.first, size.second);
        withCorner.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawHud(withCorner, corner);

        HudState lined = corner;
        lined.pullLabel = "SW 120  THE DROWNED-NAME WALL  BAND 18";
        Framebuffer withLine(size.first, size.second);
        withLine.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawHud(withLine, lined);

        HudState lineOnly = restStreet();
        lineOnly.showHealth = false;
        lineOnly.aimVerb = {};
        lineOnly.pullLabel = lined.pullLabel;
        HudState nothing = lineOnly;
        nothing.pullLabel = {};
        Framebuffer onlyLine(size.first, size.second);
        onlyLine.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawHud(onlyLine, lineOnly);
        Framebuffer onlyCompass(size.first, size.second);
        onlyCompass.clear(Rgb{0.10F, 0.12F, 0.14F});
        drawHud(onlyCompass, nothing);

        const CentreRect centre = hudCentreRect(size.first, size.second);
        const int scale = hudScale(size.second);
        const int stripBottom = 3 * scale + (kGlyphHeightForTests + 1) * scale + scale;
        std::size_t lineInk = 0;
        int minX = size.first;
        int maxX = -1;
        for (int y = 0; y < size.second; ++y) {
            for (int x = 0; x < size.first; ++x) {
                const std::size_t i = onlyLine.index(x, y);
                const bool lineDrew = onlyLine.pixels()[i] != onlyCompass.pixels()[i];
                const bool cornerDrew = withCorner.pixels()[i] != base.pixels()[i];
                if (!lineDrew) {
                    continue;
                }
                ++lineInk;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                INFO("line ink at ", x, ",", y, " (", size.first, "x", size.second, ")");
                // Under the ribbon, above the play space...
                CHECK(y >= stripBottom);
                CHECK(y < centre.y0);
                // ...and never on a pixel the full corner painted.
                CHECK_FALSE(cornerDrew);
                // And the frame with both carries the line's own pixel there.
                CHECK(withLine.pixels()[i] == onlyLine.pixels()[i]);
            }
        }
        CHECK(lineInk > 0);
        // Whole: both ends inside the frame with a margin of air.
        CHECK(minX >= 6 * scale);
        CHECK(maxX < size.first - 6 * scale);
    }
}

TEST_CASE("the crosshair-prompt surface holds its fourteen-word budget") {
    // UI-EA-SPEC 1.2 #13: actor plate + rank + verb + compass = 14. The
    // standing row is the "rank 3" the spec keeps up; case, clock, street
    // and stamp are the cut.
    HudState aimed = restStreet();
    aimed.aimSubject = "GERTA SALTCOTTE";
    aimed.aimNote = "BARTENDER";
    aimed.aimVerb = "TALK";
    aimed.standingLabel = "WELL SPOKEN OF";
    const int words = hudWordCount(aimed);
    INFO("crosshair surface words: ", words);
    CHECK(words <= 14);
}

TEST_CASE("a fresh session settles into the rest state: every reference row asleep") {
    Session session(quietDocks());
    // Settle well past any boot bookkeeping. Nobody presses anything, so the
    // first-run gate holds the courier and nothing else is scheduled.
    session.stepMany(sim::MoveInput{}, 60);
    CHECK_FALSE(session.clockWanted());
    CHECK_FALSE(session.purseWanted());
    CHECK_FALSE(session.caseRowWanted());
    CHECK_FALSE(session.roomRowWanted());
    CHECK_FALSE(session.guildRowWanted());
    CHECK_FALSE(session.objectiveRowWanted());
    CHECK_FALSE(session.wheelHintWanted());
    CHECK_FALSE(session.placePlateWanted());
    CHECK_FALSE(session.casePlateWanted());
    CHECK(session.wheelHintLabel().empty());
}

TEST_CASE("a time charge wakes the clock, and the clock goes back to sleep") {
    Session session(quietDocks());
    session.stepMany(sim::MoveInput{}, 5);
    REQUIRE_FALSE(session.clockWanted());

    // A charge: the wait machinery jumps the hour, the same skipSeconds every
    // travel and every wait pick spends.
    session.skipToHour(21);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.clockWanted());

    // ~2.5 seconds later it is furniture no more.
    session.stepMany(sim::MoveInput{}, 160);
    CHECK_FALSE(session.clockWanted());
}

TEST_CASE("closing the casebook wakes the case row briefly -- the recap, then sleep") {
    Session session(quietDocks());
    session.stepMany(sim::MoveInput{}, 5);
    REQUIRE_FALSE(session.caseRowWanted());

    session.toggleCasebook();
    session.stepMany(sim::MoveInput{}, 2);
    session.toggleCasebook();
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.caseRowWanted());

    session.stepMany(sim::MoveInput{}, 160);
    CHECK_FALSE(session.caseRowWanted());
}

TEST_CASE("the Q-hold toast rides the first two quick-bar risings and then retires") {
    Session session(quietDocks());
    session.stepMany(sim::MoveInput{}, 5);
    REQUIRE_FALSE(session.wheelHintWanted());

    // First rising: a number press brings the bar up, and the toast teaches
    // the hold in the keyboard's own words.
    session.selectQuickSlot(0);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.wheelHintWanted());
    CHECK(session.wheelHintLabel() == "Q HOLD - WHEEL");

    // The pair go down together.
    session.stepMany(sim::MoveInput{}, 130);
    REQUIRE_FALSE(session.quickBarWanted());
    CHECK_FALSE(session.wheelHintWanted());

    // Second rising: taught once more.
    session.selectQuickSlot(1);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.wheelHintWanted());
    session.stepMany(sim::MoveInput{}, 130);
    REQUIRE_FALSE(session.wheelHintWanted());

    // Third rising: the bar comes up, the toast does not. Two showings is
    // teaching; three is nagging.
    session.selectQuickSlot(2);
    session.stepMany(sim::MoveInput{}, 1);
    CHECK(session.quickBarWanted());
    CHECK_FALSE(session.wheelHintWanted());
}

TEST_CASE("the tutor band holds, yields, and spends its countdown once a step") {
    // The cross-lane contract (c): this lane lands the countdown/toggle
    // helper, PAGES instantiates one per band, FLOW raises it on wake
    // events. These are the semantics the other two lanes build against.
    TutorBand band;
    band.anim.snapTo(false);
    CHECK_FALSE(band.wanted());
    CHECK(band.value() == 0.0F);

    // An event raises it in full for the tutor hold.
    band.raise();
    CHECK(band.wanted());
    band.sync(false);
    for (int i = 0; i < kTutorHoldSteps; ++i) {
        band.sync(false);  // re-asserted many times a step, like every toggle
        band.advance();
    }
    CHECK_FALSE(band.wanted());

    // A raise extends a live hold and never shortens one.
    band.raise(100);
    band.raise(10);
    CHECK(band.showSteps == 100);
    band.raise(120);
    CHECK(band.showSteps == 120);

    // Suppression puts the band down without spending the countdown -- a page
    // over the top does not eat the hold.
    band.sync(true);
    CHECK_FALSE(band.anim.target());
    band.sync(false);
    CHECK(band.anim.target());

    // cancel() is the page closing: down now, nothing left to show.
    band.cancel();
    CHECK_FALSE(band.wanted());
}

TEST_CASE("the lead-opened notice keeps the case row down -- one piece of news, once") {
    // UI-EA-SPEC 1.2 #16: "case row asleep -- the notice IS the case news."
    // Reading Crell's ledger opens three leads: the plate fires AND the case
    // row's own text changes on the same step, and the row must lose.
    const sim::CasebookRaws raws = sim::CasebookRaws::load(granadad::content::contentDir());
    const std::int32_t ledger = raws.indexOf("weighhouse-ledger");
    if (ledger < 0) {
        return;  // no authored case in this content dir; nothing to announce
    }
    const sim::Lead& lead = raws.leads()[static_cast<std::size_t>(ledger)];
    SessionConfig config = quietDocks(8);
    config.spawnX = lead.site.x;
    config.spawnY = lead.site.y;
    config.spawnBand = lead.site.band;
    Session session(config);
    session.stepMany(sim::MoveInput{}, 2);
    REQUIRE(session.casebook().hear(ledger));
    session.examine();
    CHECK(session.casePlateWanted());
    CHECK_FALSE(session.caseRowWanted());
}
