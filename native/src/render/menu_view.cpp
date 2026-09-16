#include "granadad/render/menu_view.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "granadad/render/panel.hpp"

namespace granadad::render {

namespace {

/// THE LETTERS TILE'S OWN ACCENT -- the "parchment-style panel" out of this
/// engine's pixel-ink vocabulary (DialogueViewState::letter's own header), the
/// identical value the old drawing used for an open letter's body. The tile's
/// badge and its selection fill take it too, because the reference fills a row
/// in the ENTITY'S own accent and this tile's entity is paper.
constexpr Rgb kParchmentInk{0.86F, 0.74F, 0.52F};

/// A warning routed into the Journal tile (Session's own choice of landing
/// site -- see the drawMenuTiles call site) outranks the tile's epithet for
/// its row. The same orange casebook_page.cpp already prints an alert in, so
/// a bouncer sounds like one bouncer on both surfaces.
constexpr Rgb kAlertInk{0.90F, 0.52F, 0.30F};

/// The rect covering content rows [first, first + count) of a pane -- the
/// same arithmetic PanelFrame::band does for the frame's own interior,
/// re-done here because a tile pane is a slice of that interior with its own
/// row 0.
[[nodiscard]] PanelRect rowBand(const PanelRect& pane, const PanelMetric& metric, int first,
                                int count) {
    const int rows = metric.rowsIn(pane.h);
    const int start = std::clamp(first, 0, rows);
    const int end = std::clamp(first + count, start, rows);
    return PanelRect{pane.x, pane.y + metric.heightOf(start), pane.w,
                     metric.heightOf(end - start)};
}

/// The Journal tile's prose: the entry (or the hook, or the ward's nerve) as
/// up to two wrapped rows with the cut MARKED -- the convention this file has
/// always kept, "a label that stops in the middle of a word reads as a
/// rendering bug" -- and then the dateline/cross-reference as a single
/// `•` bulleted row, clipped out loud by clipLabel. The bullet is the
/// reference's own hierarchy: the sentence is flavour, the dateline is the
/// mechanical record under it.
///
/// Returns the row after the last one drawn.
[[nodiscard]] int drawJournalProse(Framebuffer& target, const PanelRect& pane,
                                   const PanelMetric& metric, const DialogueViewState& view,
                                   int row, const Rgb& accent, float fade) {
    const PanelInk& ink = panelInk();
    const int paneRows = metric.rowsIn(pane.h);
    const std::size_t cols = static_cast<std::size_t>(std::max(4, metric.cellsIn(pane.w)));
    if (!view.line.empty()) {
        const std::vector<std::string> lines = wrapText(view.line, cols);
        constexpr int kLineRows = 2;
        for (int i = 0; i < static_cast<int>(lines.size()) && i < kLineRows && row < paneRows;
             ++i) {
            std::string text = lines[static_cast<std::size_t>(i)];
            if (i == kLineRows - 1 && static_cast<int>(lines.size()) > kLineRows) {
                // Marks the cut rather than silently dropping the rest -- the
                // same rule clipLabel() and the speech reveal both keep.
                while (!text.empty() && text.size() + 3 > cols) {
                    text.pop_back();
                }
                text += "...";
            }
            drawCellText(target, pane, metric, 0, row, text, ink.prose, 0.92F * fade);
            ++row;
        }
    }
    if (!view.caseRef.empty() && row < paneRows) {
        drawMotif(target, pane.x, pane.y + metric.heightOf(row), Motif::Dot, accent, 0.90F * fade,
                  metric.scale);
        drawCellText(target, pane, metric, 2, row,
                     clipLabel(view.caseRef,
                               static_cast<std::size_t>(std::max(1, metric.cellsIn(pane.w) - 2))),
                     ink.dim, 0.85F * fade);
        ++row;
    }
    return row;
}

/// An open letter's wrapped, paged body -- a DOCUMENT in somebody else's hand,
/// parchment-toned, paged by the rows THIS pane actually holds at THIS window
/// size (Session has no window to wrap against; see
/// DialogueViewState::letterLines). `view.page` is clamped into the range the
/// wrap produces, which is why Session may increment it blind.
///
/// The foot keeps its `0` -- unlike a title list's MORE marker below, the 0
/// key really does turn an open letter's page (session.cpp increments
/// lettersBodyPage_ on it), so the printed key is the key that works.
void drawLetterPane(Framebuffer& target, const PanelRect& pane, const PanelMetric& metric,
                    const DialogueViewState& view, int row, float fade) {
    const int paneRows = metric.rowsIn(pane.h);
    const std::size_t cols = static_cast<std::size_t>(std::max(4, metric.cellsIn(pane.w)));
    std::vector<std::string> all;
    for (const std::string& paragraph : view.letterLines) {
        if (paragraph.empty()) {
            all.emplace_back();
            continue;
        }
        for (std::string& line : wrapText(paragraph, cols)) {
            all.push_back(std::move(line));
        }
    }
    // The last row is the foot's whether or not there is a page to turn, so
    // the body never gains a row when the foot goes -- geometry holds.
    const int rowsAvail = std::max(1, paneRows - row - 1);
    const int pages =
        std::max(1, (static_cast<int>(all.size()) + rowsAvail - 1) / rowsAvail);
    const int page = std::clamp(view.page, 0, pages - 1);
    const std::size_t first = static_cast<std::size_t>(page) * static_cast<std::size_t>(rowsAvail);
    for (std::size_t i = first; i < all.size() && i < first + static_cast<std::size_t>(rowsAvail);
         ++i) {
        drawCellText(target, pane, metric, 0, row, all[i], kParchmentInk, 0.92F * fade);
        ++row;
    }
    if (pages > 1) {
        drawCellText(target, pane, metric, 0, paneRows - 1,
                     "0 MORE (" + std::to_string(page + 1) + "/" + std::to_string(pages) + ")",
                     kParchmentInk, 0.62F * fade);
    }
}

/// WHERE A TILE'S LIST STARTS, in pane rows -- the row spend drawTile makes
/// before it reaches its list: the badge (row 0), the epithet's row (spent
/// whether or not one prints), the Journal's prose (up to two wrapped rows of
/// `line`, then the `•` dateline row -- drawJournalProse's own clamps), and
/// the row of air over the list. THE POINTER PASS'S half of drawTile's walk:
/// menuTileHitAtPixel inverts the list against this, and a case in
/// test_menu_view.cpp pins the two against the drawn cursor row so they
/// cannot drift apart silently.
[[nodiscard]] int tileListStartRow(const DialogueViewState& view, int paneRows, int paneCells,
                                   bool journal) {
    int row = 2;
    if (journal) {
        const std::size_t cols = static_cast<std::size_t>(std::max(4, paneCells));
        if (!view.line.empty()) {
            constexpr int kLineRows = 2;
            const int lines = static_cast<int>(wrapText(view.line, cols).size());
            row += std::max(0, std::min({lines, kLineRows, paneRows - row}));
        }
        if (!view.caseRef.empty() && row < paneRows) {
            ++row;
        }
    }
    // One row of air between the header (or the prose) and the list.
    return row + 1;
}

/// The whole-list plan drawTile draws its rows against -- synthetic one-digit
/// keys, one column, the shared gutter. One function, two callers (the
/// drawing and the hit-test), so the fill and the click cannot disagree about
/// a row's pixels.
[[nodiscard]] OptionListPlan tileListPlan(const std::vector<std::string>& topics,
                                          const PanelRect& listRect, const PanelMetric& metric) {
    std::vector<PanelOption> whole;
    whole.reserve(topics.size());
    for (const std::string& topic : topics) {
        PanelOption option;
        option.key = "1";
        option.label = topic;
        whole.push_back(std::move(option));
    }
    OptionListStyle style;
    style.maxColumns = 1;
    style.gutterCells = 2;
    style.minRows = 0;
    style.alignValues = true;
    style.showKeys = true;
    return planOptionList(whole, listRect, metric, style);
}

/// ONE TILE, in the register. The pane is a slice of the SHARED frame's
/// interior -- the frame itself (rules, edges, dividers, junctions) is drawn
/// once by drawMenuTiles; this draws only content.
///
///   row 0    the subject badge -- the speaker knocked out of an inverted
///            accent fill, brightest on the focused tile (`focusAmount` eases
///            it, the job the old hairline border's thickness lerp did), with
///            the state word (`OPEN`/`CLOSED`) right-aligned a cell off the
///            frame edge, the reference's own "subject then status".
///   row 1    the epithet, dim -- or a routed alert, which outranks it.
///   row 2    air (spent by the Journal's prose instead, which is its own
///            separation).
///   then     the list, paged by the pane (menuTilePageFor), selection an
///            INVERTED FILL in the tile's accent -- never an arrow -- full on
///            the focused tile and dim-but-present on the others, so an
///            unfocused tile still shows where its cursor sat.
///   then     the empty-state sentence, in room the rows did not want, and a
///            stippled field under whatever is left -- emptiness textured,
///            not blank.
/// HOW MUCH OF ITSELF A TILE SHOWS (UI-EA-SPEC 1.6) -- the Law of Earned Text
/// applied to the hub: the FOCUSED tile is the real surface (its list, its
/// prose); an unfocused tile is a SUMMARY -- badge, epithet, the picked row
/// and a `+N` count -- because its forty rows are not news until you turn to
/// them; and in READING MODE (a letter open) every other tile collapses to a
/// CHIP, its one-word badge, so the letter has the room and the quiet.
enum class TileMode : std::uint8_t { Full, Summary, Chip };

void drawTile(Framebuffer& target, const PanelRect& pane, const PanelMetric& metric,
              const DialogueViewState& view, bool focused, float focusAmount, float fade,
              bool journal, const Rgb& accent, TileMode mode) {
    const PanelInk& ink = panelInk();
    const int paneRows = metric.rowsIn(pane.h);
    const int paneCells = metric.cellsIn(pane.w);
    if (!view.open || paneRows < 4 || paneCells < 8) {
        return;
    }
    const float amount = std::clamp(focusAmount, 0.0F, 1.0F);

    // --- row 0: the badge ---------------------------------------------------
    if (!view.speaker.empty()) {
        const std::string name =
            clipLabel(view.speaker, static_cast<std::size_t>(std::max(1, paneCells - 2)));
        const int badge = static_cast<int>(name.size()) + 2;
        drawInvertedFill(target, pane, metric, 0, 0, badge, accent,
                         (0.18F + 0.74F * amount) * fade);
        if (amount >= 0.5F) {
            drawCellTextKnockout(target, pane, metric, 1, 0, name, ink.knockout, fade);
        } else {
            drawCellText(target, pane, metric, 1, 0, name, accent, 0.95F * fade);
        }
        // A cell of air off the frame's own edge -- a value flush against the
        // border reads as punctuated by the `|`/`!` flicker.
        if (mode != TileMode::Chip && !view.attitude.empty() &&
            badge + static_cast<int>(view.attitude.size()) + 3 <= paneCells) {
            drawCellTextRight(target, pane, metric, 1, 0, view.attitude, ink.dim, 0.85F * fade);
        }
    }
    // A CHIP IS ITS BADGE -- reading mode's collapsed form. An alert still
    // outranks the quiet: a bouncer does not wait for you to finish a letter.
    if (mode == TileMode::Chip) {
        if (!view.alert.empty()) {
            drawCellText(target, pane, metric, 0, 1,
                         clipLabel(view.alert, static_cast<std::size_t>(paneCells)), kAlertInk,
                         fade);
        }
        return;
    }

    // --- row 1: the epithet, or a warning, which outranks it ----------------
    int row = 1;
    if (!view.alert.empty()) {
        drawCellText(target, pane, metric, 0, row,
                     clipLabel(view.alert, static_cast<std::size_t>(paneCells)), kAlertInk, fade);
    } else if (!view.epithet.empty()) {
        drawCellText(target, pane, metric, 0, row,
                     clipLabel(view.epithet, static_cast<std::size_t>(paneCells)), ink.dim,
                     0.85F * fade);
    }
    ++row;

    // --- the letter branch: a document, not a menu --------------------------
    if (view.letter) {
        drawLetterPane(target, pane, metric, view, row, fade);
        return;
    }

    // --- the summary form: what an unfocused tile earns ---------------------
    // The picked row (the active lead, the newest-read letter, wherever the
    // tile's own cursor sat) in the tile's accent, and `+N` for the rest --
    // count and headline, no wall. The full list waits for focus.
    if (mode == TileMode::Summary) {
        ++row;
        const int count = static_cast<int>(view.topics.size());
        if (count > 0 && row < paneRows) {
            const int at = std::clamp(view.cursor, 0, count - 1);
            drawCellText(target, pane, metric, 0, row,
                         clipLabel(view.topics[static_cast<std::size_t>(at)],
                                   static_cast<std::size_t>(std::max(1, paneCells - 4))),
                         accent, 0.95F * fade);
            if (count > 1) {
                drawCellTextRight(target, pane, metric, 1, row, "+" + std::to_string(count - 1),
                                  ink.dim, 0.85F * fade);
            }
            ++row;
        } else if (count == 0 && !view.emptyLine.empty() && row < paneRows) {
            // An empty tile's summary IS its empty state -- worded, six words
            // or fewer, so a fresh hub still says what each pane is for.
            const PanelRect say = rowBand(pane, metric, row, std::max(1, paneRows - row - 1));
            const std::vector<PanelLine> lines{
                PanelLine{Bullet::None, "", view.emptyLine, InkRole::Dim, accent}};
            row += drawProse(target, say, metric, lines, fade);
        }
        const int spare = paneRows - row - 1;
        if (spare >= 3) {
            drawStipple(target, rowBand(pane, metric, row + 1, spare - 1), metric, ink.rule,
                        kPaneStippleAlpha * fade);
        }
        return;
    }

    if (journal) {
        row = drawJournalProse(target, pane, metric, view, row, accent, fade);
    }
    // One row of air between the header (or the prose) and the list.
    ++row;

    // --- the list, paged by the pane ----------------------------------------
    const int capacity = paneRows - row;
    if (capacity <= 0) {
        return;
    }
    const MenuTilePage page = menuTilePageFor(view.topics, view.page, view.cursor, capacity);
    const PanelRect listRect = rowBand(pane, metric, row, capacity);
    int used = 0;
    if (!page.rows.empty()) {
        std::vector<PanelOption> options;
        options.reserve(page.rows.size());
        for (const MenuTileRow& tileRow : page.rows) {
            PanelOption option;
            option.key = tileRow.key;
            option.label = tileRow.label;
            option.accent = accent;
            // AN UNFOCUSED TILE STILL SHOWS WHERE ITS CURSOR SAT -- the
            // picked label takes the accent while the fill under it is dim,
            // so state moves the whole row together and nothing is dropped.
            option.labelTakesAccent = tileRow.picked && !focused;
            options.push_back(std::move(option));
        }
        // PLANNED AGAINST THE WHOLE LIST, drawn against the page -- panel.hpp's
        // own instruction, so the fill's width and the label column are sized
        // for the widest row that EXISTS and do not jump when the screen
        // turns. The synthetic keys only carry the key column's width, which
        // is one digit on every screen. tileListPlan is the one copy of this
        // plan; menuTileHitAtPixel inverts against the same call.
        const OptionListPlan plan = tileListPlan(view.topics, listRect, metric);
        if (page.selected >= 0 && !focused) {
            // The dim fill, exactly the 0.20 the pre-conversion drawing used
            // for the same statement.
            drawInvertedFill(target, listRect, metric, 0, page.selected, plan.columnCells, accent,
                             0.20F * fade);
        }
        drawOptionListPlanned(target, listRect, metric, options, focused ? page.selected : -1,
                              plan, fade);
        used = static_cast<int>(page.rows.size());
        if (page.more) {
            // NO PRINTED KEY, deliberately -- the 0 key steps Session's
            // nine-topic windows, not these pane-sized screens, and a printed
            // key that does something adjacent to what it says is worse than
            // an unkeyed marker. The cursor pages this list: the screen shown
            // follows it. Same unkeyed shape casebook_page.cpp's own MORE
            // indicator settled on.
            drawCellText(target, listRect, metric, 0, capacity - 1,
                         "MORE (" + std::to_string(page.screen + 1) + "/" +
                             std::to_string(page.screens) + ")",
                         ink.dim, 0.75F * fade);
            used = capacity;
        }
    }

    // --- the empty state, in room the rows did not want ----------------------
    // A blank row above the note, whether or not there were rows -- a tile
    // with nothing in it is the one that needs the gap most, or the note
    // reads as a third line of the header. Two whole rows spare or nothing,
    // the same anti-clutter rule the pre-conversion drawing kept.
    int spare = capacity - used;
    if (!view.emptyLine.empty() && spare >= 2) {
        const PanelRect say = rowBand(pane, metric, row + used + 1, spare - 1);
        const std::vector<PanelLine> lines{
            PanelLine{Bullet::None, "", view.emptyLine, InkRole::Dim, accent}};
        used += 1 + drawProse(target, say, metric, lines, fade);
        spare = capacity - used;
    }

    // --- deliberate emptiness, textured --------------------------------------
    if (spare >= 3) {
        drawStipple(target, rowBand(pane, metric, row + used + 1, spare - 1), metric, ink.rule,
                    kPaneStippleAlpha * fade);
    }
}

}  // namespace

MenuTileHit menuTileHitAtPixel(const MenuTileState& state, int width, int height, int px,
                               int py) {
    MenuTileHit hit;
    if (!state.open) {
        return hit;
    }
    const MenuTileLayout comp = menuTileLayout(width, height);
    if (!comp.usable) {
        return hit;
    }
    struct Pane {
        int tile;
        const PanelRect* rect;
        const DialogueViewState* view;
        bool journal;
    };
    const Pane panes[] = {
        Pane{kMenuFocusCharacter, &comp.character, &state.character, false},
        Pane{kMenuFocusMap, &comp.map, &state.map, false},
        Pane{kMenuFocusLetters, &comp.letters, &state.letters, false},
        Pane{kMenuFocusJournal, &comp.journal, &state.journal, true},
    };
    for (const Pane& pane : panes) {
        const PanelRect& rect = *pane.rect;
        if (px < rect.x || px >= rect.right() || py < rect.y || py >= rect.bottom()) {
            continue;
        }
        hit.tile = pane.tile;
        const DialogueViewState& view = *pane.view;
        const PanelMetric& metric = comp.metric;
        const int paneRows = metric.rowsIn(rect.h);
        const int paneCells = metric.cellsIn(rect.w);
        // drawTile's own refusals: nothing listed on a closed view, a pane too
        // small to compose, or an open letter (a document, not a menu). The
        // pane itself is still the answer -- a click there is on the TILE.
        // AND ONLY THE FULL TILE ANSWERS ROWS: an unfocused tile draws a
        // summary now (UI-EA-SPEC 1.6), so a row hit there would point at
        // pixels no list occupies -- the click focuses the tile instead.
        const bool letterReading = state.letters.open && state.letters.letter;
        const bool full = letterReading ? pane.tile == kMenuFocusLetters
                                        : state.focus == pane.tile;
        if (!view.open || paneRows < 4 || paneCells < 8 || view.letter || !full) {
            return hit;
        }
        const int row = tileListStartRow(view, paneRows, paneCells, pane.journal);
        const int capacity = paneRows - row;
        if (capacity <= 0) {
            return hit;
        }
        const MenuTilePage page = menuTilePageFor(view.topics, view.page, view.cursor, capacity);
        if (page.rows.empty()) {
            return hit;
        }
        const PanelRect listRect = rowBand(rect, metric, row, capacity);
        const OptionListPlan plan = tileListPlan(view.topics, listRect, metric);
        const int at =
            optionListAt(listRect, metric, plan, static_cast<int>(page.rows.size()), px, py);
        if (at >= 0) {
            // Back to the ABSOLUTE index Session's cursor holds --
            // menuTilePageFor's own screen arithmetic, inverted.
            const int perScreen = page.more ? std::max(1, capacity - 1) : capacity;
            hit.row = page.screen * perScreen + at;
            return hit;
        }
        if (page.more) {
            const int footY = listRect.y + metric.heightOf(capacity - 1);
            if (py >= footY && py < footY + metric.cellH()) {
                hit.more = true;
            }
        }
        return hit;
    }
    return hit;
}

MenuTileLayout menuTileLayout(int width, int height) {
    MenuTileLayout out;
    out.metric = panelMetric(height);
    const int cells = out.metric.cellsIn(width);
    const int rows = out.metric.rowsIn(height);
    // Below this there is no composition: four panes need a border, a rule,
    // two dividers and something to say. 320x180 (64x25 cells, the smoke
    // size) clears it with room to spare.
    if (cells < 24 || rows < 12) {
        return out;
    }
    out.bounds = PanelRect{(width - out.metric.widthOf(cells)) / 2,
                           panelSeatY(height, out.metric.heightOf(rows)), out.metric.widthOf(cells),
                           out.metric.heightOf(rows)};
    const PanelRect interior{out.bounds.x + out.metric.cellW(), out.bounds.y + out.metric.cellH(),
                             out.metric.widthOf(cells - 2), out.metric.heightOf(rows - 2)};

    // THE BOTTOM BAND (Journal) TAKES ROUGHLY A THIRD -- 9/25 of the interior,
    // the same 36 percent the pre-conversion layout gave it, so the two
    // frames of this screen agree about where the Journal lives. NINE AND
    // THE STICKS: two rows under it are the foot -- a rule and the nav band
    // that names the ring, the casebook page's own shape.
    const std::vector<PanelRect> bands = splitRows(interior, out.metric,
                                                   {
                                                       spanWeight(16),  // the three tiles
                                                       spanCells(1),    // the rule
                                                       spanWeight(9),   // the Journal
                                                       spanCells(1),    // the foot's rule
                                                       spanCells(1),    // the nav band
                                                   });
    out.topRows = out.metric.rowsIn(bands[0].h);
    out.ruleRow = (bands[1].y - interior.y) / out.metric.cellH();
    out.journal = bands[2];
    out.footRuleRow = (bands[3].y - interior.y) / out.metric.cellH();
    out.nav = bands[4];

    // Three columns over the top band, split by two one-cell dividers that
    // the shared frame draws in step with its own edges.
    const std::vector<PanelRect> cols = splitColumns(bands[0], out.metric,
                                                     {
                                                         spanWeight(1),  // Character
                                                         spanCells(1),   // divider
                                                         spanWeight(1),  // Map
                                                         spanCells(1),   // divider
                                                         spanWeight(1),  // Letters
                                                     });
    out.character = cols[0];
    out.map = cols[2];
    out.letters = cols[4];
    out.dividerCellA = (cols[1].x - interior.x) / out.metric.cellW();
    out.dividerCellB = (cols[3].x - interior.x) / out.metric.cellW();
    out.usable = true;
    return out;
}

MenuTilePage menuTilePageFor(const std::vector<std::string>& topics, int page, int cursor,
                             int capacity) {
    MenuTilePage out;
    const int total = static_cast<int>(topics.size());
    if (total <= 0 || capacity <= 0) {
        return out;
    }
    // THE NINE-KEY WINDOW SESSION'S DIRECT-SELECT BELIEVES IN. `page` tracks
    // the cursor on every tile (wrapCursorAndPage/advancePage both keep
    // page == cursor / kTopicPageSize), and a number key resolves
    // page * kTopicPageSize + slot -- so digits are printed on that window
    // and on nothing else, wherever the pane-sized screen happens to sit.
    const int window =
        std::clamp(page, 0, topicPageCount(topics.size()) - 1) * kTopicPageSize;
    int first = 0;
    int last = total;
    if (total > capacity) {
        // One of the pane's rows is spent on the MORE foot; the rest are the
        // screen. Anchored on the CURSOR, so the row being driven is always
        // the one on show.
        const int perScreen = std::max(1, capacity - 1);
        out.screens = (total + perScreen - 1) / perScreen;
        out.screen = std::clamp(cursor, 0, total - 1) / perScreen;
        first = out.screen * perScreen;
        last = std::min(total, first + perScreen);
        out.more = true;
    }
    for (int i = first; i < last; ++i) {
        const int slot = i - window;
        MenuTileRow row;
        if (slot >= 0 && slot < kTopicPageSize) {
            row.key = std::to_string(slot + 1);
        }
        row.label = topics[static_cast<std::size_t>(i)];
        row.picked = i == cursor;
        if (row.picked) {
            out.selected = i - first;
        }
        out.rows.push_back(std::move(row));
    }
    return out;
}

void drawMenuTiles(Framebuffer& target, const MenuTileState& state) {
    if (!state.open) {
        return;
    }
    const float fade = std::clamp(state.openAmount, 0.0F, 1.0F);
    if (fade <= 0.0F) {
        return;
    }
    const PanelInk& ink = panelInk();
    // A FULL TAKEOVER IS A FULL TAKEOVER: the ground covers the whole frame at
    // the one opacity every full-screen page uses, and the border encloses
    // only what it has to -- the identical call casebook_page.cpp (this same
    // Menu, one bumper away on the Journal) makes, so stepping focus between
    // the book and the tiles never changes what the street behind them is
    // doing.
    target.fillRect(0, 0, target.width(), target.height(), ink.ground, kPageGroundAlpha * fade);

    const MenuTileLayout comp = menuTileLayout(target.width(), target.height());
    if (!comp.usable) {
        return;
    }

    // ONE FRAME, FOUR PANES -- the reference's "one full-screen frame,
    // divided into stacked panels", not four boxes. `◆` at every corner and
    // junction, consistently, the same choice the casebook page made; one
    // interior rule under the three tiles; two dividers alternating `|`/`!`
    // in step with the outer edges, because the texture runs through the
    // whole frame.
    FrameStyle style;
    style.junction = Motif::Diamond;
    style.alpha = fade;
    style.stipple = false;
    style.groundAlpha = kPageGroundAlpha;
    PanelFrame frame(target, comp.bounds, comp.metric, style);
    frame.addRule(comp.ruleRow);
    frame.addRule(comp.footRuleRow);
    frame.addDivider(comp.dividerCellA, 0, comp.topRows);
    frame.addDivider(comp.dividerCellB, 0, comp.topRows);
    frame.draw();

    // THE LAW OF EARNED TEXT ON THE HUB (UI-EA-SPEC 1.6): the focused tile is
    // the real surface; the others are summaries -- badge, epithet, headline
    // row, `+N`. And READING MODE: with a letter open, the letter is the
    // point of the whole page -- every other tile collapses to its chip so
    // 169 words of neighbouring chrome stop shouting over 175 words of
    // authored prose. The Journal keeps its prose when focused: the hook,
    // the ward's dread and a picked lead's dateline are the page.
    const bool reading = state.letters.open && state.letters.letter;
    const auto modeFor = [&state, reading](int tile) {
        if (reading) {
            return tile == kMenuFocusLetters ? TileMode::Full : TileMode::Chip;
        }
        return state.focus == tile ? TileMode::Full : TileMode::Summary;
    };
    drawTile(target, comp.character, comp.metric, state.character,
             state.focus == kMenuFocusCharacter, state.characterFocus, fade, false, ink.accent,
             modeFor(kMenuFocusCharacter));
    drawTile(target, comp.map, comp.metric, state.map, state.focus == kMenuFocusMap,
             state.mapFocus, fade, false, ink.accent, modeFor(kMenuFocusMap));
    drawTile(target, comp.letters, comp.metric, state.letters, state.focus == kMenuFocusLetters,
             state.lettersFocus, fade, false, kParchmentInk, modeFor(kMenuFocusLetters));
    drawTile(target, comp.journal, comp.metric, state.journal, state.focus == kMenuFocusJournal,
             state.journalFocus, fade, true, ink.accent, modeFor(kMenuFocusJournal));

    // THE FOOT: the ring, learnable from the hub itself. Bare keycaps with
    // their words -- the hub is where a stranger learns them, so the words
    // stay; a page a player has learned is the casebook page, whose foot
    // fades its words on the tutor tier.
    const std::vector<PanelOption> nav{
        PanelOption{state.navMoveKeys, "ROW", "", ink.accent, InkRole::Dim, false},
        PanelOption{state.navPageKeys, "NOTES", "", ink.accent, InkRole::Dim, false},
        PanelOption{state.confirmKey, "PICK", "", ink.accent, InkRole::Dim, false},
        PanelOption{state.closeKey, "CLOSE", "", ink.accent, InkRole::Dim, false},
    };
    OptionListStyle navStyle;
    navStyle.showKeys = true;
    navStyle.maxColumns = 4;
    navStyle.gutterCells = 2;
    navStyle.minRows = 1;
    const PanelRect navRect{comp.nav.x, comp.nav.y, std::max(0, comp.nav.w - comp.metric.cellW()),
                            comp.nav.h};
    const OptionListPlan navPlan = planOptionList(nav, navRect, comp.metric, navStyle);
    drawOptionListPlanned(target, navRect, comp.metric, nav, -1, navPlan, fade);
}

}  // namespace granadad::render
