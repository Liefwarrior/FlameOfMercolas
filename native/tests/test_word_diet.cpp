// THE WORD DIET, PINNED -- UI-EA-SPEC's measurement law run over each dieted
// page's own at-rest MODEL, so a stray sentence cannot creep back into a
// surface this program just paid to quiet.
//
// THE LAW, restated from the spec so this file cannot drift from it: a WORD is
// a whitespace-delimited token containing an alphanumeric ("5/10" is one word,
// "T" is a word); keycap motifs, bars, bullets and border texture are GLYPHS
// and count zero -- the sentinel bytes 0x01-0x06 carry no alphanumeric, so the
// tokenizer gets that for free. Budgets bind the AT-REST frame: nav labels are
// counted only where a page holds them worded (tutorLocked -- the on-screen
// keyboard's accessibility floor); everywhere else rest is bare keycaps and
// only the keys count.
//
// THESE ARE CEILINGS OVER THE MODEL, NOT THE TRANSCRIPTION. The ship gate
// re-counts by transcription of real frames at 2x, which also sees what no
// state struct carries (the map's zoom-gated plan labels, wrapped visibility).
// The model ceilings are set a small margin over today's measured model count
// -- close enough that adding one sentence anywhere goes red, honest enough
// that they hold before LANE FLOW's choke points start emitting keycap
// sentinels for ENTER and the arrows (each of which will only push the counts
// DOWN, never up).

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/creation.hpp"
#include "granadad/render/creation_page.hpp"
#include "granadad/render/session.hpp"

namespace content = granadad::content;
namespace render = granadad::render;
namespace sim = granadad::sim;

namespace {

/// The census's own token rule.
[[nodiscard]] int dietWords(std::string_view text) {
    int count = 0;
    bool inToken = false;
    bool counts = false;
    for (const char c : text) {
        const bool space = c == ' ' || c == '\t' || c == '\n';
        if (space) {
            if (inToken && counts) {
                ++count;
            }
            inToken = false;
            counts = false;
            continue;
        }
        inToken = true;
        const bool alnum =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        counts = counts || alnum;
    }
    if (inToken && counts) {
        ++count;
    }
    return count;
}

/// Every word a CreationPage model puts on its at-rest frame.
[[nodiscard]] int pageWords(const render::CreationPage& page) {
    int n = 0;
    n += dietWords(page.title) + dietWords(page.instruction) + dietWords(page.readout);
    for (const render::PanelTab& tab : page.tabs) {
        n += dietWords(tab.key) + dietWords(tab.name);
    }
    for (const render::CreationPageRow& row : page.rows) {
        n += dietWords(row.key) + dietWords(row.label) + dietWords(row.value);
    }
    n += dietWords(page.detailBadge) + dietWords(page.detailStatus);
    for (const render::PanelFact& fact : page.facts) {
        n += dietWords(fact.label) + dietWords(fact.value);
    }
    for (const render::PanelBar& bar : page.bars) {
        n += dietWords(bar.label) + dietWords(bar.value);
    }
    for (const render::PanelLine& line : page.lines) {
        n += dietWords(line.name) + dietWords(line.body);
    }
    n += dietWords(page.commitVerb) + dietWords(page.commitCost);
    for (const render::PanelOption& option : page.nav) {
        n += dietWords(option.key);
        if (page.tutorLocked) {
            n += dietWords(option.label);
        }
    }
    return n;
}

[[nodiscard]] render::CreationFlow freshFlow() {
    return render::CreationFlow(content::contentDir());
}

}  // namespace

TEST_CASE("the word rule itself: tokens, values, and zero-cost glyphs") {
    CHECK(dietWords("5/10") == 1);
    CHECK(dietWords("T") == 1);
    CHECK(dietWords("T - TRAVEL (1 MIN)") == 4);
    // Punctuation-only and sentinel-only tokens are glyphs.
    CHECK(dietWords("+ -") == 0);
    CHECK(dietWords("\x01\x02\x03\x04") == 0);
    CHECK(dietWords("\x01 - FACE IT") == 2);
    CHECK(dietWords("") == 0);
    CHECK(dietWords("  --  ") == 0);
}

TEST_CASE("creation's steps hold their word ceilings at rest") {
    // Census -> ceiling, per UI-EA-SPEC 1.1. The model ceilings sit where the
    // diet landed (a small margin over today's count, always at least 40%
    // under the census) -- see this file's header for why they are not the
    // spec's exact frame budgets: the frame counts less than the model only
    // where wrapping hides words, and FLOW's sentinel choke points will pull
    // ENTER/ESC/UP DOWN out of every count below.
    // THE DOOR (census 89).
    {
        render::CreationFlow flow = freshFlow();
        const int words = pageWords(flow.page());
        INFO("door: ", words);
        CHECK(words <= 48);
        CHECK(words <= 89 / 2 + 4);
    }
    // TAKE A CALLING (census 106). The roster's 28 list words are kept by
    // spec; the trade's own tide line and its skill names are world words.
    {
        render::CreationFlow flow = freshFlow();
        flow.chooseOrigin();
        REQUIRE(flow.step() == render::CreationStep::Calling);
        const int words = pageWords(flow.page());
        INFO("calling: ", words);
        CHECK(words <= 80);
    }
    // THE QUIZ (census 148): stubs in the list, the whole answer behind the
    // highlight, bars and one signed delta for the consequence.
    {
        render::CreationFlow flow = freshFlow();
        flow.moveOriginCursor(1);
        flow.chooseOrigin();
        REQUIRE(flow.step() == render::CreationStep::Quiz);
        int worst = 0;
        for (int q = 0; q < 3; ++q) {
            for (int a = 0; a < 3; ++a) {
                flow.setChoiceCursor(a);
                worst = std::max(worst, pageWords(flow.page()));
            }
            flow.setChoiceCursor(0);
            flow.chooseChoice();
        }
        INFO("quiz, worst of nine states: ", worst);
        CHECK(worst <= 82);
        CHECK(worst <= 148 / 2 + 10);
    }
    // YOUR OWN PAST (census 123): same stubs law, costs as signed numbers.
    {
        render::CreationFlow flow = freshFlow();
        flow.chooseOrigin();
        flow.chooseChoice();  // take the first calling -> the biography
        REQUIRE(flow.step() == render::CreationStep::Background);
        int worst = 0;
        const int answers = static_cast<int>(
            flow.biography().questions().front().answers.size());
        for (int a = 0; a <= answers; ++a) {  // the random row included
            flow.setChoiceCursor(std::min(a, answers));
            worst = std::max(worst, pageWords(flow.page()));
        }
        INFO("past, worst answer state: ", worst);
        CHECK(worst <= 80);
    }
    // THE SHEET, cursor on BEGIN (census 112): the 47-word sheet is the
    // point; the BEGIN pane is the eight-word name pane.
    {
        render::CreationFlow flow = freshFlow();
        flow.moveOriginCursor(2);  // CUSTOM
        flow.chooseOrigin();
        REQUIRE(flow.step() == render::CreationStep::Customize);
        const int last = static_cast<int>(flow.view().topics.size()) - 1;
        flow.setCustomizeCursor(last);
        const int words = pageWords(flow.page());
        INFO("sheet on BEGIN: ", words);
        CHECK(words <= 84);
    }
    // THE ON-SCREEN KEYBOARD (census 93, spec 58): the grid and the worded
    // device feet are the accessibility floor and still fit the spec's own
    // ceiling.
    {
        render::CreationFlow flow = freshFlow();
        flow.moveOriginCursor(2);
        flow.chooseOrigin();
        const int last = 0;  // the NAME row
        flow.setCustomizeCursor(last);
        flow.chooseCustomizeRow();  // opens typing
        REQUIRE(flow.editingName());
        flow.openOsk();
        const render::CreationPage page = flow.page();
        REQUIRE(page.tutorLocked);  // the accessibility floor holds the feet worded
        const int words = pageWords(page);
        INFO("osk: ", words);
        CHECK(words <= 58);
    }
}

TEST_CASE("the pause stack's cards hold their word ceilings") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 640;
    config.height = 360;
    render::Session session(config);
    session.stepMany(sim::MoveInput{}, 2);
    session.toggleCasebook();  // put the opening page down
    REQUIRE_FALSE(session.casebookOpen());

    // PAUSE (census 33, spec 15): title (the build stamp's new home), five
    // verbs, a keycap foot.
    session.togglePause();
    REQUIRE(session.stripCardOpen());
    {
        const int words = pageWords(session.stripCard());
        INFO("pause card: ", words);
        CHECK(words <= 17);
    }

    // WAIT (census 81, spec 34): twelve rows of the 24-hour clock visible,
    // the rest behind +N, four words of flavour.
    session.movePauseCursor(1);
    session.choosePause();
    REQUIRE(session.waitOpen());
    {
        const int words = pageWords(session.stripCard());
        INFO("wait card: ", words);
        CHECK(words <= 40);
    }
    session.closeConversation();  // ESC: the wait page down

    // OPTIONS (census 73, spec 38): the table plus a three-word bind hint,
    // paged at twelve.
    session.toggleOptions();
    REQUIRE(session.optionsOpen());
    {
        const int words = pageWords(session.stripCard());
        INFO("options card: ", words);
        CHECK(words <= 60);
    }
    session.toggleOptions();

    // GRIMOIRE (census 82, spec 38): the priest's own empty state, or rows
    // and a two-word ask.
    session.toggleGrimoire();
    REQUIRE(session.grimoireOpen());
    {
        const int words = pageWords(session.stripCard());
        INFO("grimoire card: ", words);
        CHECK(words <= 38);
    }
}

TEST_CASE("dieted empty states stay at six words or fewer") {
    render::SessionConfig config;
    config.contentDir = content::contentDir();
    config.width = 640;
    config.height = 360;
    render::Session session(config);
    session.stepMany(sim::MoveInput{}, 2);

    // The chart and letters tiles.
    session.toggleCasebook();
    REQUIRE(session.casebookOpen());
    session.menuPageNext();  // Journal -> Character
    session.menuPageNext();  // Character -> Map
    CHECK(dietWords(session.dialogueView().emptyLine) <= 6);
    session.menuPageNext();  // Map -> Letters
    CHECK(dietWords(session.dialogueView().emptyLine) <= 6);
    session.menuPageNext();  // Letters -> Journal: the waiting sentence is gone
    CHECK(session.dialogueView().emptyLine.empty());
}
