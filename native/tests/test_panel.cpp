// THE FRAME PRIMITIVES AND THE COMPOSITION RULES.
//
// Four phases of UI work draw into render/panel.hpp after this one, so the
// interesting failures here are not "does it put ink on the screen" -- they are
// the CONTRACTS those phases will be coding against:
//
//   * the panel grid is the FONT'S grid, so text laid out in cells lands where
//     drawText puts it,
//   * a span split adds back up to its bounds and lands on the grid, so edges
//     align and gutters are shared,
//   * the |/! alternation runs through interior dividers as well as the border,
//     because that texture is the register and it is not optional,
//   * an option list picks its COLUMN COUNT FROM THE CONTENT against the width
//     available, at every resolution the game runs at,
//   * a pane HOLDS ITS HEIGHT when its content shrinks, and a list drawn
//     against a plan keeps its geometry when the page turns,
//   * the value column is common across a block and is computed rather than
//     typed in.
//
// And then the same composition, resolved at 320x180, 640x360, 960x540,
// 1280x720 and 1920x1080, because "width-aware" is a claim and these are the
// numbers that make it one.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/keys_page.hpp"
#include "granadad/render/panel.hpp"

using namespace granadad::render;

namespace {

/// Every window size this suite claims the composition holds at. The 960x540
/// baseline is the one docs/HUD-REAL-ESTATE.md measures at; the other four are
/// the ends of the range the game actually runs in.
struct Size {
    int w;
    int h;
};
constexpr Size kSizes[] = {{320, 180}, {640, 360}, {960, 540}, {1280, 720}, {1920, 1080}};

[[nodiscard]] std::vector<PanelOption> namedOptions(const std::vector<std::string>& labels) {
    std::vector<PanelOption> out;
    out.reserve(labels.size());
    for (const std::string& label : labels) {
        PanelOption option;
        option.label = label;
        out.push_back(option);
    }
    return out;
}

[[nodiscard]] int inkCount(const Framebuffer& frame) {
    int lit = 0;
    for (const std::uint32_t pixel : frame.pixels()) {
        if ((pixel & 0x00FFFFFFU) != 0U) {
            ++lit;
        }
    }
    return lit;
}

}  // namespace

TEST_CASE("the panel grid is the font's own grid") {
    // panel.cpp restates the font's cell metric because hud.cpp does not export
    // it. THIS is what stops the restatement drifting: a cell is exactly one
    // glyph advance wide, and a row is the glyph plus the drop shadow under it.
    for (int scale = 1; scale <= 6; ++scale) {
        const PanelMetric metric{scale};
        // textWidth is (n * advance - 1) * scale, so two glyphs minus one
        // glyph is exactly one advance.
        CHECK(textWidth("AA", scale) - textWidth("A", scale) == metric.cellW());
        CHECK(metric.cellH() == metric.cellW() / 5 * 7);
        CHECK(metric.widthOf(metric.cellsIn(1000)) <= 1000);
    }
    // And the metric a composed screen picks is the register hud.hpp reserves
    // for reference material, at every size the game runs at.
    for (const Size& size : kSizes) {
        CHECK(panelMetric(size.h).scale == hudMinorScale(size.h));
    }
}

TEST_CASE("a span split adds up to its bounds and lands on the grid") {
    const PanelMetric metric{2};
    const PanelRect bounds{10, 20, metric.widthOf(97), metric.heightOf(41)};

    const std::vector<PanelRect> rows =
        splitRows(bounds, metric, {spanCells(1), spanCells(2), spanWeight(3), spanWeight(1),
                                   spanCells(1)});
    REQUIRE(rows.size() == 5);
    int total = 0;
    int y = bounds.y;
    for (const PanelRect& row : rows) {
        // Every band starts where the last one ended -- that is the whole of
        // "aligned edges" -- and every band is a whole number of rows.
        CHECK(row.y == y);
        CHECK(row.h % metric.cellH() == 0);
        CHECK(row.x == bounds.x);
        CHECK(row.w == bounds.w);
        y += row.h;
        total += row.h;
    }
    // Nothing is lost and nothing is invented: the remainder that does not
    // divide evenly goes to the last weighted band rather than vanishing.
    CHECK(total == metric.heightOf(41));
    CHECK(rows[0].h == metric.heightOf(1));
    CHECK(rows[1].h == metric.heightOf(2));
    CHECK(rows[4].h == metric.heightOf(1));

    const std::vector<PanelRect> cols =
        splitColumns(bounds, metric, {spanWeight(2), spanCells(1), spanWeight(3)});
    REQUIRE(cols.size() == 3);
    CHECK(cols[1].w == metric.widthOf(1));
    CHECK(cols[0].right() == cols[1].x);
    CHECK(cols[1].right() == cols[2].x);
    CHECK(cols[0].w + cols[1].w + cols[2].w == metric.widthOf(97));
}

TEST_CASE("a composition asking for more than the window has is trimmed, never overlapped") {
    const PanelMetric metric{3};
    // Six fixed rows into a four-row window. The first four are honoured in the
    // order they were declared and the rest come out empty -- what must NOT
    // happen is two bands landing on the same pixels.
    const PanelRect tiny{0, 0, metric.widthOf(20), metric.heightOf(4)};
    const std::vector<PanelRect> rows = splitRows(
        tiny, metric,
        {spanCells(1), spanCells(1), spanCells(1), spanCells(1), spanCells(1), spanCells(1)});
    REQUIRE(rows.size() == 6);
    CHECK(rows[3].h == metric.heightOf(1));
    CHECK(rows[4].h == 0);
    CHECK(rows[5].h == 0);
    for (std::size_t i = 1; i < rows.size(); ++i) {
        CHECK(rows[i].y == rows[i - 1].y + rows[i - 1].h);
    }
}

TEST_CASE("the vertical edges alternate, and the alternation runs through an interior divider") {
    // THE TEXTURE IS THE REGISTER. `!` has a gap at glyph row 4 and `|` does
    // not, which is the whole one-pixel flicker -- so this asks the frame which
    // motif each row wears rather than counting pixels, and then asks whether
    // the divider agrees with the border on every row.
    const PanelMetric metric{2};
    Framebuffer frame(400, 300);
    FrameStyle style;
    PanelFrame panel(frame, PanelRect{0, 0, 400, 300}, metric, style);
    REQUIRE(panel.rowCount() > 4);
    const auto edge = [&panel](int r) { return static_cast<int>(panel.edgeAt(r)); };
    CHECK(edge(0) == static_cast<int>(Motif::Bang));
    CHECK(edge(1) == static_cast<int>(Motif::Bar));
    CHECK(edge(2) == static_cast<int>(Motif::Bang));
    for (int r = 0; r + 1 < panel.rowCount(); ++r) {
        CHECK(edge(r) != edge(r + 1));
    }

    // rowPhase is what lets a nested frame stay in step with its parent rather
    // than restarting the flicker halfway down a screen.
    FrameStyle shifted = style;
    shifted.rowPhase = 1;
    PanelFrame nested(frame, PanelRect{0, 0, 400, 300}, metric, shifted);
    CHECK(static_cast<int>(nested.edgeAt(0)) == static_cast<int>(Motif::Bar));
    CHECK(static_cast<int>(nested.edgeAt(1)) == static_cast<int>(Motif::Bang));
}

TEST_CASE("a frame draws its border and leaves an interior a caller can trust") {
    const PanelMetric metric{2};
    Framebuffer frame(400, 300);
    frame.clear(Rgb{0.0F, 0.0F, 0.0F});
    FrameStyle style;
    style.junction = Motif::Diamond;
    PanelFrame panel(frame, PanelRect{0, 0, 400, 300}, metric, style);
    panel.addRule(3);
    panel.addDivider(10, 4, panel.rowCount() - 4);
    panel.draw();
    CHECK(inkCount(frame) > 0);

    // The interior sits one cell in and one row down, and holds exactly the
    // rows the frame says it does.
    CHECK(panel.interior().x == metric.cellW());
    CHECK(panel.interior().y == metric.cellH());
    CHECK(panel.interior().h == metric.heightOf(panel.rowCount()));
    // A band is clamped to the interior rather than running past it, so a
    // composition that over-asks gets a short band and not a buffer overrun.
    const PanelRect over = panel.band(panel.rowCount() - 1, 40);
    CHECK(over.h == metric.heightOf(1));
    CHECK(panel.band(0, 3).h == metric.heightOf(3));
}

TEST_CASE("a key grid is a block at a constant advance, and never a spread list") {
    // The defect, stated as a number: an option list SPREADS its columns so
    // the last one ends at the pane's right edge, which turned thirty
    // one-glyph keys into six sparse vertical strings on a ten-cell stride.
    // A grid spends the pane on the keys and stops.
    const PanelMetric metric{1};
    std::vector<std::string> letters;
    for (char c = 'A'; c <= 'Z'; ++c) {
        letters.emplace_back(1, c);
    }
    letters.insert(letters.end(), {"-", "'", "_", "<"});
    const std::vector<PanelOption> keys = namedOptions(letters);
    KeyGridStyle style;
    style.columns = 10;

    // A WIDE pane does not stretch the block: the stride is the cap plus one,
    // and on a grid of single glyphs that is TWO cells however wide the pane
    // is. Nineteen of ninety cells, and the block ends there.
    const PanelRect wide{0, 0, metric.widthOf(90), metric.heightOf(8)};
    const KeyGridPlan roomy = planKeyGrid(keys, wide, metric, style);
    CHECK(roomy.usable);
    CHECK(roomy.columns == 10);
    CHECK(roomy.rows == 3);
    CHECK(roomy.capCells == 1);
    CHECK(roomy.strideCells == 2);
    CHECK(roomy.columns * roomy.capCells + (roomy.columns - 1) == 19);

    // Padding is what a caller with word-length keys spends width on, and it
    // is what makes the advance grow -- which is why the keyboard leaves it at
    // zero. See KeyGridStyle::padCells.
    KeyGridStyle padded = style;
    padded.padCells = 1;
    CHECK(planKeyGrid(keys, wide, metric, padded).capCells == 3);
    CHECK(planKeyGrid(keys, wide, metric, padded).strideCells == 4);

    // The SAME list through the option list, at the same pane, is the thing
    // this replaces: its stride runs to the far edge.
    OptionListStyle spread;
    spread.showKeys = false;
    spread.maxColumns = 10;
    CHECK(planOptionList(keys, wide, metric, spread).stride > roomy.strideCells);

    // A NARROW pane keeps the column count -- the caller's cursor walks a
    // fixed rectangle -- and pays for it out of the padding instead of
    // dropping to fewer columns the way an option list would.
    const PanelRect narrow{0, 0, metric.widthOf(19), metric.heightOf(8)};
    const KeyGridPlan tight = planKeyGrid(keys, narrow, metric, padded);
    CHECK(tight.usable);
    CHECK(tight.columns == 10);
    CHECK(tight.capCells == 1);
    CHECK(tight.strideCells == 2);

    // Narrower than the keys THEMSELVES -- gap and padding both gone -- and it
    // says so rather than drawing a grid of the wrong shape. Ten columns need
    // ten cells and this pane has nine.
    const PanelRect impossible{0, 0, metric.widthOf(9), metric.heightOf(8)};
    CHECK_FALSE(planKeyGrid(keys, impossible, metric, style).usable);
    // At exactly ten it keeps all ten columns and pays with the gap, because
    // fewer columns than the cursor walks is the one thing it may not do.
    const PanelRect exact{0, 0, metric.widthOf(10), metric.heightOf(8)};
    const KeyGridPlan squeezed = planKeyGrid(keys, exact, metric, style);
    CHECK(squeezed.usable);
    CHECK(squeezed.columns == 10);
    CHECK(squeezed.strideCells == 1);
    // ...and so does a pane too short for the rows.
    const PanelRect flat{0, 0, metric.widthOf(90), metric.heightOf(2)};
    CHECK_FALSE(planKeyGrid(keys, flat, metric, style).usable);
}

TEST_CASE("a key grid's hit-test is the inverse of its drawing, cell for cell") {
    const PanelMetric metric{2};
    std::vector<std::string> letters;
    for (char c = 'A'; c <= 'Z'; ++c) {
        letters.emplace_back(1, c);
    }
    const std::vector<PanelOption> keys = namedOptions(letters);
    KeyGridStyle style;
    style.columns = 7;
    const PanelRect pane{11, 23, metric.widthOf(40), metric.heightOf(8)};
    const KeyGridPlan plan = planKeyGrid(keys, pane, metric, style);
    REQUIRE(plan.usable);
    const int count = static_cast<int>(keys.size());
    for (int i = 0; i < count; ++i) {
        INFO("key ", i);
        const int x = pane.x + metric.widthOf((i % plan.columns) * plan.strideCells) +
                      metric.cellW() / 2;
        const int y = pane.y + metric.heightOf((i / plan.columns) * plan.strideRows) +
                      metric.cellH() / 2;
        CHECK(keyGridAt(pane, metric, plan, count, x, y) == i);
    }
    // Outside the block is nobody, not the nearest key.
    CHECK(keyGridAt(pane, metric, plan, count, pane.x - 4, pane.y + 2) == -1);
    CHECK(keyGridAt(pane, metric, plan, count, pane.x + 2, pane.bottom() + 4) == -1);
}

TEST_CASE("an option list picks its column count from the content, not from a setting") {
    const PanelMetric metric{2};
    OptionListStyle style;
    style.showKeys = false;
    style.maxColumns = 4;

    // The reference's own example: four short elemental names take two columns
    // and nine long ones take one, at the SAME width.
    const PanelRect pane{0, 0, metric.widthOf(40), metric.heightOf(12)};
    const std::vector<PanelOption> shortNames =
        namedOptions({"WATER", "EARTH", "FIRE", "AIR"});
    const std::vector<PanelOption> longNames =
        namedOptions({"HOMUNCULUS THEORY", "MARTIAL PRACTICES", "WILDSPEAKING",
                      "SANGUINE GEOMETRY", "TIDEREADING", "DROWNED CARTOGRAPHY",
                      "LINKCRAFT", "STREETWISE", "CHANNELLING"});
    CHECK(planOptionList(shortNames, pane, metric, style).columns >= 2);
    CHECK(planOptionList(longNames, pane, metric, style).columns <
          planOptionList(shortNames, pane, metric, style).columns);
    // Narrow the pane and the long list is down to the one column the
    // reference draws it in.
    const PanelRect narrowPane{0, 0, metric.widthOf(24), metric.heightOf(12)};
    CHECK(planOptionList(longNames, narrowPane, metric, style).columns == 1);

    // And the count follows the WIDTH: the identical list re-columns as the
    // pane grows. This is the whole width-awareness claim, in one assertion.
    int previous = 0;
    for (const int cells : {12, 24, 40, 60, 90}) {
        const PanelRect wider{0, 0, metric.widthOf(cells), metric.heightOf(12)};
        const int columns = planOptionList(shortNames, wider, metric, style).columns;
        CHECK(columns >= previous);
        previous = columns;
    }
    CHECK(previous > planOptionList(shortNames, PanelRect{0, 0, metric.widthOf(12),
                                                          metric.heightOf(12)},
                                    metric, style)
                         .columns);
    // maxColumns is a ceiling and not a target.
    const PanelRect huge{0, 0, metric.widthOf(400), metric.heightOf(12)};
    CHECK(planOptionList(shortNames, huge, metric, style).columns <= style.maxColumns);
    CHECK(planOptionList(shortNames, huge, metric, style).columns <= 4);
}

TEST_CASE("a pane holds its height when the list inside it shrinks") {
    // STABLE GEOMETRY. A list that loses entries must not drag the rule under
    // it upwards, and a list drawn against a plan made for the WHOLE list must
    // keep its column stride when only a page of it is on screen.
    const PanelMetric metric{2};
    const PanelRect pane{0, 0, metric.widthOf(60), metric.heightOf(16)};
    OptionListStyle style;
    style.showKeys = false;
    style.minRows = 14;

    const std::vector<PanelOption> full =
        namedOptions({"ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT"});
    const std::vector<PanelOption> fewer = namedOptions({"ONE", "TWO"});
    CHECK(planOptionList(full, pane, metric, style).rows == 14);
    CHECK(planOptionList(fewer, pane, metric, style).rows == 14);

    // And without minRows the pane is honest about how tall it needs to be.
    OptionListStyle loose = style;
    loose.minRows = 0;
    CHECK(planOptionList(fewer, pane, metric, loose).rows < 14);

    // Overflow is REPORTED rather than silently truncated.
    OptionListStyle tight;
    tight.showKeys = false;
    tight.maxColumns = 1;
    const PanelRect narrow{0, 0, metric.widthOf(20), metric.heightOf(4)};
    CHECK(planOptionList(full, narrow, metric, tight).overflowed);
    CHECK_FALSE(planOptionList(fewer, pane, metric, style).overflowed);
}

TEST_CASE("the value column is common across a block and is computed, not typed in") {
    const PanelMetric metric{2};
    const PanelRect pane{0, 0, metric.widthOf(60), metric.heightOf(8)};
    const std::vector<PanelFact> facts{
        PanelFact{"FACTION", "INDEPENDENTS"},
        PanelFact{"MAJOR RELIGION", "AGNOSTICISM"},
        PanelFact{"TERRAIN", "DESERT"},
    };
    const int column = factValueColumn(facts, pane, metric);
    // One clear of the longest label -- "MAJOR RELIGION" is fourteen.
    CHECK(column == 16);
    // A runaway label cannot push every value on the screen to the margin.
    std::vector<PanelFact> runaway = facts;
    runaway.push_back(PanelFact{std::string(90, 'X'), "1"});
    CHECK(factValueColumn(runaway, pane, metric) <= 30);
}

TEST_CASE("the master/detail split collapses honestly instead of making two unreadable panes") {
    const PanelMetric metric{2};
    const PanelRect wide{0, 0, metric.widthOf(94), metric.heightOf(20)};
    const MasterDetail split = splitMasterDetail(wide, metric, 58, 18, 30);
    CHECK(split.split);
    CHECK(split.master.x == wide.x);
    // A cell of air, the divider, a cell of air -- so no text ever touches the
    // flicker column.
    CHECK(split.detail.x > split.master.right());
    CHECK(metric.cellsIn(split.master.w) + metric.cellsIn(split.detail.w) < 94);
    CHECK(split.dividerCell > metric.cellsIn(split.master.w) - 1);

    const PanelRect thin{0, 0, metric.widthOf(40), metric.heightOf(20)};
    const MasterDetail one = splitMasterDetail(thin, metric, 58, 18, 30);
    CHECK_FALSE(one.split);
    CHECK(one.master.w == thin.w);
    CHECK(one.detail.w == 0);
}

TEST_CASE("the controls page composes at every window size the game runs at") {
    // THE PROOF THAT THE FOUNDATION HOLDS. The same declared composition, five
    // resolutions, and at every one of them: a frame with a usable interior, a
    // list that fits more of itself than the four-page topic grid ever did, and
    // ink on the screen that did not come from the world underneath.
    KeysPageState state;
    state.open = true;
    state.title = "CONTROLS";
    state.readout = "GRANADAD 0.10.0";
    state.instruction =
        "EVERY KEY THIS GAME ANSWERS TO. THE ONE UNDER THE CURSOR IS SPELT OUT ON THE RIGHT.";
    for (int i = 0; i < 29; ++i) {
        KeysPageRow row;
        row.binding = i % 3 == 0 ? "LSHIFT" : "W";
        row.verb = i % 2 == 0 ? "FORWARD" : "QUICK WHEEL";
        row.help = "ONE SENTENCE ABOUT WHAT THIS KEY DOES, LONG ENOUGH TO NEED WRAPPING IN "
                   "ANY PANE THIS PAGE WILL EVER GIVE IT.";
        row.group = i % kKeysGroupCount;
        state.rows.push_back(row);
    }

    for (const Size& size : kSizes) {
        INFO("size " << size.w << "x" << size.h);
        Framebuffer frame(size.w, size.h);
        frame.clear(Rgb{0.0F, 0.0F, 0.0F});
        state.cursor = 0;
        drawKeysPage(frame, state);
        CHECK(inkCount(frame) > 0);

        const KeysPageScroll scroll = keysPageScroll(state, size.w, size.h);
        CHECK(scroll.perScreen > 0);
        CHECK(scroll.screens >= 1);
        // THE NUMBER THAT MADE THIS WORTH DOING. The surface this replaced
        // showed NINE rows per page whatever the window was, which is four
        // pages for this list. Every size here beats that, and 960x540 shows
        // the whole list at once.
        CHECK(scroll.perScreen > 9);
        CHECK(scroll.screens <= 2);

        // The cursor on the last row lands on the last screenful, and the
        // first row lands on the first: the page follows the cursor rather
        // than the player having to find the cursor.
        state.cursor = static_cast<int>(state.rows.size()) - 1;
        const KeysPageScroll end = keysPageScroll(state, size.w, size.h);
        CHECK(end.screen == end.screens - 1);
        CHECK(end.firstRow <= state.cursor);
    }

    // At the baseline the whole list is on one screen -- no page turn at all,
    // which is the thing the old layout could not do at any resolution.
    state.cursor = 0;
    CHECK(keysPageScroll(state, 960, 540).screens == 1);
}

TEST_CASE("a closed page and a zero ease draw nothing at all") {
    // The contract every overlay in this build keeps: a state that never heard
    // of the page is pixel-identical to no page.
    KeysPageState state;
    state.open = false;
    state.rows.push_back(KeysPageRow{"W", "FORWARD", "", "WALK.", true, "", 0});
    Framebuffer closed(960, 540);
    closed.clear(Rgb{0.1F, 0.1F, 0.1F});
    const std::vector<std::uint32_t> before = closed.pixels();
    drawKeysPage(closed, state);
    CHECK(closed.pixels() == before);

    state.open = true;
    state.openAmount = 0.0F;
    drawKeysPage(closed, state);
    CHECK(closed.pixels() == before);
}

TEST_CASE("prose wraps to its pane and never draws past it") {
    // A pane two rows tall handed a paragraph that needs ten draws two rows.
    // The alternative -- a primitive that writes past its rect -- is how a
    // detail pane ends up printed through the rule under it.
    const PanelMetric metric{2};
    Framebuffer frame(400, 300);
    const PanelRect pane{0, 0, metric.widthOf(20), metric.heightOf(2)};
    const std::vector<PanelLine> lines{
        PanelLine{Bullet::Dot, "BURNING SPIRIT:",
                  "YOUR DEVOTEES GAIN A COMBAT DAMAGE BONUS EQUAL TO THEIR FAITH, WHICH IS "
                  "A GREAT DEAL MORE TEXT THAN TWO ROWS CAN HOLD.",
                  InkRole::Number}};
    CHECK(drawProse(frame, pane, metric, lines, 1.0F) <= 2);

    // Given room, it uses what it needs and reports it, so a caller can put
    // something under it.
    const PanelRect roomy{0, 0, metric.widthOf(30), metric.heightOf(20)};
    const int used = drawProse(frame, roomy, metric, lines, 1.0F);
    CHECK(used > 2);
    CHECK(used < 20);
}

TEST_CASE("a column is as wide as its content, and the columns are spread across the pane") {
    // THE FIRST CAPTURE OF THE CONVERTED PAGE GOT THIS WRONG. A column that
    // takes its whole share of a wide pane puts the value column at the far
    // right margin, forty cells past the label it belongs to, with nothing in
    // between -- a dot leader with no dots. The column is the CONTENT's width;
    // the columns are then spread so the gutters are even and the last one
    // still ends at the pane's right edge.
    const PanelMetric metric{2};
    OptionListStyle style;
    style.showKeys = false;
    style.maxColumns = 3;

    std::vector<PanelOption> options = namedOptions({"FORWARD", "BACK", "STEP LEFT", "SNEAK",
                                                     "QUICK WHEEL", "CLIMB A LEDGE"});
    for (PanelOption& option : options) {
        option.value = "LSHIFT";
    }
    const PanelRect wide{0, 0, metric.widthOf(90), metric.heightOf(20)};
    const OptionListPlan spread = planOptionList(options, wide, metric, style);
    // key 0 + label 13 + (value 6 + 2 gutter) = 21 cells of content, and the
    // column is that and not thirty.
    CHECK(spread.columnCells == 21);
    CHECK(spread.columnCells < 90 / spread.columns);
    // The last column's content ends flush with the right edge, give or take
    // the cell that does not divide evenly among the gutters.
    const int rightEdge = (spread.columns - 1) * spread.stride + spread.columnCells;
    CHECK(rightEdge <= 90);
    CHECK(rightEdge >= 90 - spread.columns);
    // And no column can reach into the next one.
    CHECK(spread.stride > spread.columnCells);

    // One column and a pane far wider than it needs: the content stays tight
    // on the left rather than being stretched across the whole pane.
    OptionListStyle single = style;
    single.maxColumns = 1;
    const OptionListPlan alone = planOptionList(options, wide, metric, single);
    CHECK(alone.stride == 0);
    CHECK(alone.columnCells == 21);
}

TEST_CASE("knocked-out text carries no drop shadow, which is what makes it readable on a fill") {
    // drawText's one-pixel dark shadow is invisible on this build's near-black
    // panels and essential over a pale sky. Over a BRIGHT accent fill with dark
    // text it is a second dark copy of every glyph, and at hudMinorScale the
    // two run together. The knockout path draws the ink and nothing else, so it
    // must light strictly FEWER pixels than the ordinary path for the same run.
    const PanelMetric metric{2};
    const PanelRect pane{0, 0, metric.widthOf(20), metric.heightOf(2)};

    Framebuffer plain(200, 40);
    plain.clear(Rgb{0.0F, 0.0F, 0.0F});
    drawCellText(plain, pane, metric, 0, 0, "QUICK WHEEL", Rgb{1.0F, 1.0F, 1.0F}, 1.0F);

    Framebuffer knocked(200, 40);
    knocked.clear(Rgb{0.0F, 0.0F, 0.0F});
    const int cells = drawCellTextKnockout(knocked, pane, metric, 0, 0, "QUICK WHEEL",
                                           Rgb{1.0F, 1.0F, 1.0F}, 1.0F);
    CHECK(cells == 11);
    CHECK(inkCount(knocked) > 0);
    CHECK(inkCount(knocked) < inkCount(plain));
    // And it is the SAME glyphs: every pixel the knockout lit, the ordinary
    // path lit too. It is the font, not a second font.
    for (std::size_t i = 0; i < knocked.pixels().size(); ++i) {
        if ((knocked.pixels()[i] & 0x00FFFFFFU) != 0U) {
            REQUIRE((plain.pixels()[i] & 0x00FFFFFFU) != 0U);
        }
    }
}

// ===========================================================================
// #93. THE BLOCK LIST, THE BARS, AND THE HIT-TESTS
// ===========================================================================
//
// Three primitives the chargen pass needed and the vocabulary did not have.
// The interesting contracts, again, are the ones later screens will code
// against rather than "does it draw":
//
//   * a list of SENTENCES wraps instead of clipping, and the blocks do not
//     overlap or run off the pane,
//   * the inverse of a layout agrees with the layout, which is the only reason
//     a mouse can be trusted to click the row a player is looking at,
//   * a bar is shape AND figure, with the unfilled part textured rather than
//     empty.

namespace {

[[nodiscard]] std::vector<PanelOption> sentenceOptions() {
    std::vector<PanelOption> out;
    out.push_back(PanelOption{"1", "THEY PAID CLEAN, EVERY QUARTER, THE WAY THEY ALWAYS HAD.",
                              "", panelInk().accent});
    out.push_back(PanelOption{"2", "THEY WERE ROOFED, AND YOU GREW UP ON A DECK.", "",
                              panelInk().accent});
    out.push_back(PanelOption{"3", "NOTHING. NOBODY CAME.", "", panelInk().accent});
    return out;
}

}  // namespace

TEST_CASE("a list of sentences wraps into blocks instead of clipping, and the blocks stack") {
    // THE DEFECT THIS EXISTS TO KILL: the old chargen grid clipped every quiz
    // and biography answer at eighteen glyphs INCLUDING the row number, so a
    // choice read "1 THEY PAID CLEAN," and a player could not make it.
    const PanelMetric metric{2};
    const PanelRect pane{0, 0, metric.widthOf(30), metric.heightOf(20)};
    OptionBlockStyle style;
    const std::vector<OptionBlock> blocks = planOptionBlocks(sentenceOptions(), pane, metric, style);
    REQUIRE(blocks.size() == 3);
    // The longest answer does not fit 30 cells, so it must have taken more than
    // one row -- that is the whole point.
    CHECK(blocks[0].rows > 1);
    CHECK(blocks[0].lines.size() == static_cast<std::size_t>(blocks[0].rows));
    for (const OptionBlock& block : blocks) {
        REQUIRE(block.rows > 0);
        CHECK(block.rect.bottom() <= pane.bottom());
        for (const std::string& line : block.lines) {
            // Nothing was cut: every wrapped line fits the text column.
            CHECK(line.size() <= 30U);
        }
    }
    // Stacked with a gap, never overlapping.
    CHECK(blocks[1].rect.y >= blocks[0].rect.bottom());
    CHECK(blocks[2].rect.y >= blocks[1].rect.bottom());
}

TEST_CASE("an entry that will not fit is reported as unfittable, not half-drawn") {
    const PanelMetric metric{2};
    // Two rows of pane for three multi-line answers.
    const PanelRect pane{0, 0, metric.widthOf(20), metric.heightOf(2)};
    const std::vector<OptionBlock> blocks =
        planOptionBlocks(sentenceOptions(), pane, metric, OptionBlockStyle{});
    // The index survives even when the block does not, so a caller's cursor and
    // this vector can never disagree about what entry 2 is -- and NOTHING after
    // the first unfittable entry is drawn either, because a short entry jumping
    // into the gap a long one could not use would print entry 3 above entry 2.
    REQUIRE(blocks.size() == 3);
    CHECK(blocks[0].rows == 0);
    CHECK(blocks[1].rows == 0);
    CHECK(blocks.back().rows == 0);
}

TEST_CASE("the block hit-test agrees with the block layout, which is what a mouse rides on") {
    const PanelMetric metric{2};
    const PanelRect pane{40, 25, metric.widthOf(30), metric.heightOf(20)};
    const std::vector<OptionBlock> blocks =
        planOptionBlocks(sentenceOptions(), pane, metric, OptionBlockStyle{});
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        REQUIRE(blocks[i].rows > 0);
        const PanelRect& r = blocks[i].rect;
        CHECK(optionBlockAt(blocks, r.x, r.y) == static_cast<int>(i));
        CHECK(optionBlockAt(blocks, r.x + r.w / 2, r.y + r.h - 1) == static_cast<int>(i));
    }
    // Outside every block is nothing, not the nearest row.
    CHECK(optionBlockAt(blocks, pane.x - 1, pane.y) == -1);
    CHECK(optionBlockAt(blocks, pane.x, pane.bottom() + 200) == -1);
}

TEST_CASE("the column hit-test agrees with the column layout at every window size") {
    // The list is drawn COLUMN-MAJOR and re-columns with the width, so the
    // inverse has to as well -- a hit-test written as arithmetic solved
    // backwards would drift the first time the layout did.
    std::vector<PanelOption> options;
    for (int i = 0; i < 12; ++i) {
        options.push_back(PanelOption{std::to_string(i + 1), "SKILL " + std::to_string(i + 1),
                                      "MAJ", panelInk().accent});
    }
    for (const Size& window : kSizes) {
        const PanelMetric metric = panelMetric(window.h);
        const PanelRect pane{0, 0, metric.widthOf(metric.cellsIn(window.w) - 2),
                             metric.heightOf(8)};
        OptionListStyle style;
        style.maxColumns = 3;
        const OptionListPlan plan = planOptionList(options, pane, metric, style);
        const int drawn = std::min(static_cast<int>(options.size()), plan.columns * plan.rows);
        INFO("window ", window.w, "x", window.h);
        for (int i = 0; i < drawn; ++i) {
            const int column = i / plan.rows;
            const int row = i % plan.rows;
            const int x = pane.x + metric.widthOf(column * plan.stride);
            const int y = pane.y + metric.heightOf(row);
            CHECK(optionListAt(pane, metric, plan, drawn, x, y) == i);
            CHECK(optionListAt(pane, metric, plan, drawn, x + metric.cellW() / 2,
                               y + metric.cellH() - 1) == i);
        }
        CHECK(optionListAt(pane, metric, plan, drawn, pane.x - 5, pane.y) == -1);
    }
}

TEST_CASE("a bar is shape and figure, and the unfilled part is textured rather than empty") {
    const PanelMetric metric{2};
    const PanelRect pane{0, 0, metric.widthOf(40), metric.heightOf(4)};

    const auto litFor = [&](std::int32_t filled) {
        Framebuffer target(metric.widthOf(40), metric.heightOf(4));
        target.clear(Rgb{0.0F, 0.0F, 0.0F});
        const std::vector<PanelBar> bars{
            PanelBar{"MIGHT", std::to_string(40 + filled), filled, 15, Rgb{1.0F, 0.3F, 0.3F}}};
        const int rows = drawBars(target, pane, metric, bars, 12, 1.0F);
        CHECK(rows == 1);
        return inkCount(target);
    };
    // An empty bar is not an empty strip: the dotted track is drawn, so there
    // is ink even at zero.
    const int atZero = litFor(0);
    CHECK(atZero > 0);
    // And a fuller bar lights more of it. Solid blocks beat a dot field.
    CHECK(litFor(15) > atZero);
    CHECK(litFor(15) > litFor(4));
}
