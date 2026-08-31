// THE TILED MENU'S CONVERSION TO THE TERMINAL REGISTER, stated as claims.
//
// The ship note called this the worst screen in the game for two measured
// reasons: four hairline rectangles instead of the `+~-~-` grammar, and
// topicRowsFor's nine-topic page drawn into a pane four times that deep --
// "0 MORE (1/2)" under twenty rows of black. The fix is menu_view.cpp's
// menuTileLayout (one shared frame, three dividers' worth of tiles, the
// Journal along the bottom) and menuTilePageFor (a tile's page derived from
// the rows its pane actually holds). Both are PUBLIC AND PURE for exactly the
// reason dialogueTopicLayout is: the defect was a layout defect, the only
// evidence against it used to be a screenshot, and a screenshot proves one
// resolution on one day. These cases state the claim at every size the game
// runs at, with no Session and no content directory in sight.
//
// What is NOT here: tile exclusivity, the focus cycle and the empty-state
// wiring, which are Session behaviour and stay proved by
// test_vertical_menu.cpp, unchanged.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/menu_view.hpp"

using namespace granadad::render;

namespace {

/// Twelve labels -- Master Venn's own list length, the longest topic list in
/// the game, and one past the old ten-row cap in both directions.
[[nodiscard]] std::vector<std::string> twelveTopics() {
    std::vector<std::string> topics;
    for (int i = 0; i < 12; ++i) {
        topics.push_back("ROW " + std::to_string(i + 1));
    }
    return topics;
}

[[nodiscard]] DialogueViewState listTile(int count, int cursor) {
    DialogueViewState view;
    view.open = true;
    view.speaker = "CHARACTER";
    view.epithet = "A NOBODY OFF A BOAT";
    for (int i = 0; i < count; ++i) {
        view.topics.push_back("ROW " + std::to_string(i + 1));
    }
    view.cursor = cursor;
    view.page = topicPageOf(cursor);
    return view;
}

}  // namespace

TEST_CASE("the four tiles compose one frame at every size the game runs at") {
    for (const int height : {180, 360, 540, 720, 1080}) {
        const int width = height * 16 / 9;
        INFO("at ", width, "x", height);
        const MenuTileLayout layout = menuTileLayout(width, height);
        REQUIRE(layout.usable);

        // The three top tiles share one band: same top, same height, ordered
        // left to right with the two divider columns between them.
        CHECK(layout.character.y == layout.map.y);
        CHECK(layout.map.y == layout.letters.y);
        CHECK(layout.character.h == layout.map.h);
        CHECK(layout.map.h == layout.letters.h);
        CHECK(layout.character.right() < layout.map.x);
        CHECK(layout.map.right() < layout.letters.x);
        CHECK(0 < layout.dividerCellA);
        CHECK(layout.dividerCellA < layout.dividerCellB);

        // The Journal keeps the bottom band, full interior width, below the
        // rule -- the pre-conversion composition's own shape, reframed.
        const int interiorCells = layout.metric.cellsIn(width) - 2;
        const int interiorRows = layout.metric.rowsIn(height) - 2;
        CHECK(layout.journal.w == layout.metric.widthOf(interiorCells));
        CHECK(layout.journal.y > layout.character.bottom());
        const int journalRows = layout.metric.rowsIn(layout.journal.h);
        CHECK(layout.topRows + 1 + journalRows == interiorRows);
        // Roughly a third: 9/25 of the interior, never starved to nothing and
        // never eating the tiles above it.
        CHECK(journalRows * 100 >= interiorRows * 30);
        CHECK(journalRows * 100 <= interiorRows * 42);
    }
}

TEST_CASE("a tile's page is its pane: twelve topics in a deep pane are twelve rows and no MORE") {
    // At 640x360 a top tile holds 30 rows -- 27 of them list capacity after
    // the badge, the epithet and a row of air. The old drawing put nine
    // topics and a "0 MORE (1/2)" there; the pane-sized page is the whole
    // list.
    const MenuTileLayout layout = menuTileLayout(640, 360);
    REQUIRE(layout.usable);
    const int capacity = layout.metric.rowsIn(layout.character.h) - 3;
    REQUIRE(capacity >= 12);

    const MenuTilePage page = menuTilePageFor(twelveTopics(), 0, 0, capacity);
    CHECK(page.rows.size() == 12);
    CHECK_FALSE(page.more);
    CHECK(page.screens == 1);
    CHECK(page.screen == 0);
    CHECK(page.selected == 0);
    // The nine-key window keeps its digits and ONLY its digits -- a printed
    // number is always the number Session's direct-select resolves.
    CHECK(page.rows[0].key == "1");
    CHECK(page.rows[8].key == "9");
    CHECK(page.rows[9].key.empty());
    CHECK(page.rows[11].key.empty());
    CHECK(page.rows[0].picked);
}

TEST_CASE("the printed digits follow the cursor's nine-key window, not the screen") {
    // Cursor on row eleven: Session's own arithmetic has page == 1 and key 2
    // resolving 9 * 1 + 1 == 10 -- so row ten is the one wearing "2",
    // wherever the pane-sized screen sits.
    const MenuTilePage page = menuTilePageFor(twelveTopics(), 1, 10, 27);
    REQUIRE(page.rows.size() == 12);
    CHECK(page.rows[0].key.empty());
    CHECK(page.rows[8].key.empty());
    CHECK(page.rows[9].key == "1");
    CHECK(page.rows[10].key == "2");
    CHECK(page.rows[11].key == "3");
    CHECK(page.selected == 10);
    CHECK(page.rows[10].picked);
}

TEST_CASE("a shallow pane paginates instead of clipping, anchored on the cursor") {
    // Six rows of pane: one is the MORE foot, five are the screen. Twelve
    // topics is three screens, and the screen shown is always the cursor's --
    // nothing visible is ever dropped and nothing addressable is ever off
    // every screen.
    const std::vector<std::string> topics = twelveTopics();

    const MenuTilePage first = menuTilePageFor(topics, 0, 0, 6);
    CHECK(first.rows.size() == 5);
    CHECK(first.more);
    CHECK(first.screens == 3);
    CHECK(first.screen == 0);
    CHECK(first.selected == 0);
    CHECK(first.rows[0].key == "1");
    CHECK(first.rows[4].key == "5");

    // Cursor row nine (page 1): the second screen, rows five to nine, and the
    // one digit on show is the "1" that really picks row nine.
    const MenuTilePage second = menuTilePageFor(topics, 1, 9, 6);
    CHECK(second.rows.size() == 5);
    CHECK(second.screen == 1);
    CHECK(second.selected == 4);
    CHECK(second.rows[4].key == "1");
    CHECK(second.rows[0].key.empty());
    CHECK(second.rows[4].picked);

    // Cursor row twelve: the last screen holds the tail, two rows, keyed "2"
    // and "3" off the same window.
    const MenuTilePage third = menuTilePageFor(topics, 1, 11, 6);
    CHECK(third.rows.size() == 2);
    CHECK(third.screen == 2);
    CHECK(third.selected == 1);
    CHECK(third.rows[0].key == "2");
    CHECK(third.rows[1].key == "3");
}

TEST_CASE("rows past the old ten-row cap actually draw, and the cursor's fill moves among them") {
    // The claim the arithmetic cases cannot make alone: the DRAWING path
    // shows rows ten and twelve at 640x360. Two frames differing only in
    // which of those two rows the cursor sits on must differ in pixels --
    // under the old nine-plus-MORE cap both cursors drew the identical
    // "0 MORE (2/2)" page.
    MenuTileState tiles;
    tiles.open = true;
    tiles.focus = kMenuFocusCharacter;
    tiles.characterFocus = 1.0F;
    tiles.journalFocus = 0.0F;
    tiles.character = listTile(12, 9);

    Framebuffer onTen(640, 360);
    drawMenuTiles(onTen, tiles);

    tiles.character = listTile(12, 11);
    Framebuffer onTwelve(640, 360);
    drawMenuTiles(onTwelve, tiles);

    CHECK(onTen.pixels() != onTwelve.pixels());
}

TEST_CASE("an unfocused tile still shows where its cursor sat") {
    // Focus is on the Journal; the Character tile's cursor moves anyway. The
    // dim fill has to move with it -- state is dimmed, never dropped.
    MenuTileState tiles;
    tiles.open = true;
    tiles.focus = kMenuFocusJournal;
    tiles.character = listTile(6, 1);

    Framebuffer onTwo(640, 360);
    drawMenuTiles(onTwo, tiles);

    tiles.character = listTile(6, 4);
    Framebuffer onFive(640, 360);
    drawMenuTiles(onFive, tiles);

    CHECK(onTwo.pixels() != onFive.pixels());
}

TEST_CASE("focus is visible: the same content draws differently when the keyboard moves") {
    MenuTileState tiles;
    tiles.open = true;
    tiles.character = listTile(6, 1);
    tiles.focus = kMenuFocusCharacter;
    tiles.characterFocus = 1.0F;
    tiles.journalFocus = 0.0F;

    Framebuffer focused(640, 360);
    drawMenuTiles(focused, tiles);

    tiles.focus = kMenuFocusJournal;
    tiles.characterFocus = 0.0F;
    tiles.journalFocus = 1.0F;
    Framebuffer unfocused(640, 360);
    drawMenuTiles(unfocused, tiles);

    CHECK(focused.pixels() != unfocused.pixels());
}

TEST_CASE("an open letter pages to its pane and clamps a blind page increment") {
    // Session increments lettersBodyPage_ without knowing how many screens
    // the wrap produced at this window size (it cannot -- it has no window).
    // The view clamps: a page far past the end is the last page, drawn
    // identically however far past it the counter has run.
    DialogueViewState letter;
    letter.open = true;
    letter.speaker = "FATHER MAELL";
    letter.letter = true;
    for (int i = 0; i < 60; ++i) {
        letter.letterLines.push_back("PARAGRAPH " + std::to_string(i + 1) +
                                     " OF AN UNREASONABLY LONG LETTER");
    }

    MenuTileState tiles;
    tiles.open = true;
    tiles.focus = kMenuFocusLetters;
    tiles.lettersFocus = 1.0F;
    tiles.journalFocus = 0.0F;
    tiles.letters = letter;

    Framebuffer pageOne(320, 180);
    drawMenuTiles(pageOne, tiles);

    tiles.letters.page = 1;
    Framebuffer pageTwo(320, 180);
    drawMenuTiles(pageTwo, tiles);
    CHECK(pageOne.pixels() != pageTwo.pixels());

    tiles.letters.page = 500;
    Framebuffer farPast(320, 180);
    drawMenuTiles(farPast, tiles);
    tiles.letters.page = 1000;
    Framebuffer furtherPast(320, 180);
    drawMenuTiles(furtherPast, tiles);
    CHECK(farPast.pixels() == furtherPast.pixels());
    CHECK(farPast.pixels() != pageOne.pixels());
}

TEST_CASE("the empty-state sentence draws in room the rows did not want, at every size") {
    // The same claim test_vertical_menu.cpp proves through a live Session,
    // restated here without one so it pins the DRAWING path alone -- and adds
    // the tile that has rows AND a sentence, which is the harder case.
    DialogueViewState chart;
    chart.open = true;
    chart.speaker = "THE CHART";
    chart.topics = {"A ROW", "ANOTHER ROW", "A THIRD"};
    chart.emptyLine = "THE CHART KNOWS WHAT THE CASEBOOK KNOWS, AND NOT A STREET MORE.";

    for (const int height : {180, 360, 540, 1080}) {
        const int width = height * 16 / 9;
        INFO("at ", width, "x", height);
        MenuTileState tiles;
        tiles.open = true;
        tiles.map = chart;
        Framebuffer worded(width, height);
        drawMenuTiles(worded, tiles);

        MenuTileState blank = tiles;
        blank.map.emptyLine.clear();
        Framebuffer silent(width, height);
        drawMenuTiles(silent, blank);
        CHECK(worded.pixels() != silent.pixels());
    }
}
