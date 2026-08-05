// EVERY WORD THE PLAYER CAN READ, and the two rules that hold for all of them.
//
//   DRAWABLE  the 4x6 font carries exactly the characters the authored barks
//             use (hud.cpp). A character it has no glyph for still advances
//             the cursor and draws NOTHING, so the line comes out with a hole
//             punched in it and no build goes red. That is how seven bracketed
//             receipts in dialogue.cpp and the spell workbench's OVER_TIME
//             shipped, and it is why isDrawableGlyph is public.
//
//   NOBODY'S  no string a player reads may be an identifier. The failure this
//   IDENT     guards is not a typo, it is a WIRING mistake -- a diagnostic
//             enum-name function reached for where a sentence belonged, which
//             happened three separate times in three separate systems:
//             serviceResultName into a landlord's mouth, roofMoveName onto the
//             message line, forgeErrorName bracketed after the priest's own
//             refusal. Each one was correct code and unreadable English.
//
// The surfaces below are the ones a player actually meets. Where a string is
// composed rather than authored, the composition is driven over its whole
// reachable range rather than sampled -- "3 PIECE" and "1 PICKS" were both
// live, and both only at values a one-shot case would not have picked.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/sim/barks.hpp"
#include "granadad/sim/barter.hpp"
#include "granadad/sim/casebook.hpp"
#include "granadad/sim/contraband.hpp"
#include "granadad/sim/legend.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/questline.hpp"
#include "granadad/sim/spellforge.hpp"
#include "granadad/sim/tavern.hpp"

using namespace granadad::sim;
namespace content = granadad::content;
namespace render = granadad::render;

// Six cases. Four drive composed strings over their whole reachable range; the
// fifth walks the twenty-five legend titles; the sixth walks every authored
// row in every raws file this game loads, the owner's canon included.

namespace {

/// Every character of it has a glyph. Reports WHICH character and in WHAT
/// string, because "a string somewhere is undrawable" is not a finding.
void mustDraw(std::string_view what, std::string_view text) {
    for (const char c : text) {
        INFO("in ", what, ": ", text);
        INFO("undrawable character: '", c, "' (", static_cast<int>(c), ")");
        CHECK(render::isDrawableGlyph(c));
    }
}

/// And it is not a machine's name for something. The three offenders all had
/// the same shape: lower-case words with no sentence around them, or an
/// identifier with an underscore in it.
void mustNotBeAnIdentifier(std::string_view what, std::string_view text) {
    INFO("in ", what, ": ", text);
    CHECK(text.find('_') == std::string_view::npos);
    CHECK(text != "?");
}

}  // namespace

TEST_CASE("the spell workbench prints words, not the raws' own keys") {
    // WHAT THIS PANEL USED TO SAY. fieldValue returned effectKindKey,
    // effectModeKey and targetShapeKey -- spells.json's spelling, which is data
    // -- so composing a crafting showed "MOVES: TEMPERATURE", "SHAPE:
    // OVER_TIME", "ACROSS: RANGED" and, for the duration, the engine's tick
    // count with a T welded on: "HOW LONG: 600T".
    ForgeBench bench;
    bench.active = true;
    for (int axis = 0; axis < 3; ++axis) {
        for (int mode = 0; mode < 3; ++mode) {
            for (int target = 0; target < 3; ++target) {
                bench.axis = static_cast<EffectKind>(axis);
                bench.mode = static_cast<EffectMode>(mode);
                bench.target = static_cast<TargetShape>(target);
                for (std::int32_t ticks = 0; ticks <= kBenchDurationLimit;
                     ticks += kBenchDurationStep) {
                    bench.durationTicks = ticks;
                    for (std::int32_t i = 0; i < kForgeFieldCount; ++i) {
                        const std::string label = bench.fieldLabel(i);
                        const std::string value = bench.fieldValue(i);
                        const std::string row = label + ": " + value;
                        mustDraw("forge field label", label);
                        mustDraw("forge field value", value);
                        mustNotBeAnIdentifier("forge field value", value);
                        // AND IT FITS THE COLUMN IT IS DRAWN IN.
                        // dialogue_view.cpp sizes the forge column at
                        // (width - 2*margin) / 3 / advance, which is twenty
                        // glyphs at 320x180, 640x360, 960x540 and 1280x720
                        // alike -- and TRUNCATES past it. A duration spelled
                        // out in full would have been cut mid-word.
                        INFO("row is ", row.size(), " glyphs: ", row);
                        CHECK(row.size() <= 20U);
                    }
                }
            }
        }
    }
}

TEST_CASE("a duration is told in the clock the player keeps") {
    CHECK(durationWords(0) == "AT ONCE");
    CHECK(durationWords(10) == "10 SECONDS");
    CHECK(durationWords(50) == "50 SECONDS");
    CHECK(durationWords(60) == "1 MINUTE");
    CHECK(durationWords(120) == "2 MINUTES");
    CHECK(durationWords(70) == "1 MIN 10 SEC");
    // The longest the bench can reach, and the longest string this can make.
    CHECK(durationWords(kBenchDurationLimit) == "15 MINUTES");
    CHECK(durationWords(890) == "14 MIN 50 SEC");
    CHECK(std::string("LASTS: " + durationWords(890)).size() == 20U);
    // One tick is one second of simulated time (engine.hpp), and the bench used
    // to print the count with a T welded onto it. Not the letter T -- MINUTE
    // and AT ONCE both carry one -- but the whole old format, at every value
    // the bench can reach.
    for (std::int32_t s = 0; s <= kBenchDurationLimit; s += kBenchDurationStep) {
        const std::string words = durationWords(s);
        INFO("at ", s, " seconds: ", words);
        CHECK(words != std::to_string(s) + "T");
        CHECK(words != std::to_string(s));
        mustDraw("durationWords", words);
    }
}

TEST_CASE("every refusal a player can be given is a sentence") {
    // THE THREE WIRING MISTAKES, each pinned at its own surface.
    for (int r = 0; r <= static_cast<int>(ServiceResult::Refused); ++r) {
        const ServiceResult result = static_cast<ServiceResult>(r);
        for (const Goods goods : {Goods::Drink, Goods::Room}) {
            const std::string_view line = counterRefusal(result, goods);
            if (result == ServiceResult::Served) {
                CHECK(line.empty());
                continue;
            }
            INFO("ServiceResult ", r);
            REQUIRE_FALSE(line.empty());
            mustDraw("counterRefusal", line);
            mustNotBeAnIdentifier("counterRefusal", line);
            // A sentence, which is what the enum's name never was.
            CHECK(line.back() == '.');
            CHECK(line != serviceResultName(result));
        }
        const std::string_view rest = restRefusal(result);
        if (result != ServiceResult::Served) {
            REQUIRE_FALSE(rest.empty());
            mustDraw("restRefusal", rest);
            mustNotBeAnIdentifier("restRefusal", rest);
            CHECK(rest.back() == '.');
            CHECK(rest != serviceResultName(result));
        }
    }
    // The barrels and the beds are the same ServiceResult and not the same
    // answer, which is the whole reason counterRefusal takes the goods.
    CHECK(counterRefusal(ServiceResult::OutOfStock, Goods::Drink) !=
          counterRefusal(ServiceResult::OutOfStock, Goods::Room));

    for (int m = 0; m <= static_cast<int>(RoofMove::Blocked); ++m) {
        const RoofMove move = static_cast<RoofMove>(m);
        const std::string_view line = roofRefusal(move);
        if (move == RoofMove::Done) {
            CHECK(line.empty());
            continue;
        }
        INFO("RoofMove ", m);
        REQUIRE_FALSE(line.empty());
        mustDraw("roofRefusal", line);
        mustNotBeAnIdentifier("roofRefusal", line);
        CHECK(line.back() == '.');
        CHECK(line != roofMoveName(move));
    }

    for (int e = 0; e <= static_cast<int>(ForgeError::BeyondRank); ++e) {
        const ForgeError error = static_cast<ForgeError>(e);
        const std::string_view reason = forgeErrorReason(error);
        if (error == ForgeError::None) {
            CHECK(reason.empty());
            continue;
        }
        INFO("ForgeError ", e);
        REQUIRE_FALSE(reason.empty());
        mustDraw("forgeErrorReason", reason);
        mustNotBeAnIdentifier("forgeErrorReason", reason);
        CHECK(reason != forgeErrorName(error));
    }
}

TEST_CASE("the ward's own nouns and titles are drawable") {
    for (int g = 0; g <= static_cast<int>(Contraband::Artifact); ++g) {
        mustDraw("contrabandLabel", contrabandLabel(static_cast<Contraband>(g)));
    }
    for (const Goods goods : {Goods::Drink, Goods::Room}) {
        mustDraw("goodsName", goodsName(goods));
    }
    // Every rung of every track the ward remembers you by -- all twenty-five
    // titles, which is what the casebook prints in "THEY CALL YOU ...".
    for (std::size_t t = 0; t < kLegendTracks; ++t) {
        const LegendTrack track = static_cast<LegendTrack>(t);
        mustDraw("legendTrackName", legendTrackName(track));
        for (std::int32_t rung = 0; rung <= kLegendRungs; ++rung) {
            const std::string_view title = legendTitle(track, rung);
            REQUIRE_FALSE(title.empty());
            mustDraw("legendTitle", title);
            mustNotBeAnIdentifier("legendTitle", title);
        }
    }
}

TEST_CASE("every authored line in every raws file is drawable") {
    // THE AUTHORED SIDE. The owner's barks.json is the reference standard and
    // is included deliberately: if a character ever enters his canon that the
    // font cannot draw, this is where it is caught, and it is a report to him
    // rather than an edit to his file.
    const BarkTables barks = BarkTables::load(content::contentDir());
    REQUIRE(barks.loaded());
    std::size_t rows = 0;
    for (const BarkTables::Table& table : barks.tables()) {
        for (const std::string& row : table.rows) {
            mustDraw(table.key, row);
            ++rows;
        }
    }
    // The whole authored corpus, not a handful of it.
    CHECK(rows > 700U);

    const CasebookRaws casebook = CasebookRaws::load(content::contentDir());
    REQUIRE(casebook.loaded());
    mustDraw("case title", casebook.title());
    mustDraw("case hook", casebook.hook());
    mustDraw("case close", casebook.close());
    for (const DreadBand& band : casebook.dreadBands()) {
        mustDraw("dread band", band.label);
    }
    for (const Lead& lead : casebook.leads()) {
        mustDraw("lead place", lead.place);
        mustDraw("lead brief", lead.brief);
        mustDraw("lead what", lead.what);
        mustDraw("lead found", lead.found);
        mustDraw("lead detail", lead.detail);
    }

    const QuestBook quests = QuestBook::load(content::contentDir());
    REQUIRE(quests.loaded());
    for (const Questline& line : quests.lines()) {
        mustDraw("quest title", line.title);
        for (const QuestStage& stage : line.stages) {
            mustDraw("stage label", stage.label);
            mustDraw("stage objective", stage.objective);
            mustDraw("stage log", stage.log);
        }
    }
}
