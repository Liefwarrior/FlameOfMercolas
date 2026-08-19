#include "granadad/render/keys_page.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "granadad/render/panel.hpp"

namespace granadad::render {

namespace {

/// ONE COLOUR PER FAMILY. The reference fills a selected row in the ENTITY'S
/// OWN accent rather than in one global highlight hue, which is what lets a
/// dense page be scanned by colour before a word of it is read. Movement is the
/// cool one because it is the background hum of playing; the verbs take the
/// build's own amber because they are what you press on purpose; the screens
/// are the paper colour; the quick bar is green because it spends something;
/// and the mode notes are grey because they are not bindings at all.
[[nodiscard]] Rgb groupAccent(int group) noexcept {
    switch (group) {
        case kKeysGroupMove:
            return Rgb{0.52F, 0.72F, 0.92F};
        case kKeysGroupScreen:
            return Rgb{0.86F, 0.74F, 0.52F};
        case kKeysGroupQuick:
            return Rgb{0.52F, 0.82F, 0.48F};
        case kKeysGroupNote:
            return Rgb{0.66F, 0.64F, 0.58F};
        case kKeysGroupAct:
        default:
            return Rgb{0.98F, 0.86F, 0.42F};
    }
}

[[nodiscard]] const char* groupName(int group) noexcept {
    switch (group) {
        case kKeysGroupMove:
            return "MOVEMENT";
        case kKeysGroupScreen:
            return "SCREENS";
        case kKeysGroupQuick:
            return "QUICK BAR";
        case kKeysGroupNote:
            return "WHILE PICKING A LOCK";
        case kKeysGroupAct:
        default:
            return "WHAT YOU DO";
    }
}

/// THE COMPOSITION, RESOLVED AGAINST THE WINDOW AND NOTHING ELSE.
///
/// Seven bands down the frame, three of which are rules, and a master/detail
/// split in the body. There is not one pixel constant in it: every number below
/// is a count of grid ROWS or grid CELLS, and the grid comes from the frame
/// height. The same declaration produces a legible page at 320x180 and at
/// 1920x1080 -- what changes between them is how many columns the list takes
/// and whether the detail pane fits beside it.
struct Composition {
    PanelMetric metric;
    PanelRect bounds;
    PanelRect interior;
    int tabRow = 0;
    int headerRow = 0;
    int headerRows = 0;
    int bodyRow = 0;
    int bodyRows = 0;
    int navRow = 0;
    std::vector<int> ruleRows;
    MasterDetail body;
    PanelRect headerBand;
    PanelRect bodyBand;
    PanelRect navBand;
    bool usable = false;
};

/// The rows the body actually gives the list, once the page-indicator row at
/// its foot is accounted for.
inline constexpr int kIndicatorRows = 1;

/// The master's share of the body, out of 100. Deliberately generous: this
/// list's entries are short (a verb and a key), so a wide master RE-COLUMNS
/// rather than running a single ribbon of rows down a huge empty pane, and the
/// detail pane still has room for a wrapped paragraph beside it.
inline constexpr int kMasterShare = 58;
inline constexpr int kMinMasterCells = 18;
// Twenty-six cells is a paragraph that still reads. It is deliberately not
// larger, because the biggest WINDOW is not the widest in CELLS: hudMinorScale
// steps up with the frame height, so 1920x1080 has 76 cells across where
// 960x540 has 96. The split has to survive that inversion or the detail pane
// would vanish at the resolution with the most pixels in it.
inline constexpr int kMinDetailCells = 26;

/// The shares the composition will try for the master pane, WIDEST LAST.
///
/// A fixed share is a guess, and at 1280x720 the guess was four cells short of
/// a second column: the master came out 48 cells wide when two columns of this
/// list want 50, so the page spent half its own pane on nothing and put five
/// rows on a second screen. The composition is still FIXED -- nothing here
/// responds to a player, only to the window and to the length of the list --
/// but it is allowed to ask "would one more notch fit the whole list", and it
/// stops the moment the detail pane would stop being readable.
constexpr int kMasterShares[] = {58, 62, 66};

[[nodiscard]] Composition compose(int frameWidth, int frameHeight) {
    Composition out;
    out.metric = panelMetric(frameHeight);
    const int cells = out.metric.cellsIn(frameWidth);
    const int rows = out.metric.rowsIn(frameHeight);
    if (cells < 8 || rows < 10) {
        return out;
    }
    // The grid is CENTRED in the window rather than pinned to the top left, so
    // the pixels that do not divide into whole cells are split between the two
    // margins instead of all landing on one edge.
    out.bounds = PanelRect{(frameWidth - out.metric.widthOf(cells)) / 2,
                           (frameHeight - out.metric.heightOf(rows)) / 2,
                           out.metric.widthOf(cells), out.metric.heightOf(rows)};
    // The border spends a row top and bottom and a cell left and right.
    out.interior = PanelRect{out.bounds.x + out.metric.cellW(), out.bounds.y + out.metric.cellH(),
                             out.metric.widthOf(cells - 2), out.metric.heightOf(rows - 2)};

    const std::vector<PanelRect> bands = splitRows(out.interior, out.metric,
                                                   {
                                                       spanCells(1),   // the tab row
                                                       spanCells(1),   // rule
                                                       spanCells(2),   // the instruction
                                                       spanCells(1),   // rule
                                                       spanWeight(1),  // the body
                                                       spanCells(1),   // rule
                                                       spanCells(1),   // global nav
                                                   });
    const auto rowOf = [&out](const PanelRect& band) {
        return (band.y - out.interior.y) / out.metric.cellH();
    };
    out.tabRow = rowOf(bands[0]);
    out.headerBand = bands[2];
    out.headerRow = rowOf(bands[2]);
    out.headerRows = out.metric.rowsIn(bands[2].h);
    out.bodyBand = bands[4];
    out.bodyRow = rowOf(bands[4]);
    out.bodyRows = out.metric.rowsIn(bands[4].h);
    out.navBand = bands[6];
    out.navRow = rowOf(bands[6]);
    out.ruleRows = {rowOf(bands[1]), rowOf(bands[3]), rowOf(bands[5])};
    out.body = splitMasterDetail(out.bodyBand, out.metric, kMasterShare, kMinMasterCells,
                                 kMinDetailCells);
    out.usable = out.bodyRows > kIndicatorRows;
    return out;
}

/// The list, as options. The VALUE is the bound key -- which is what makes the
/// common value column earn its place here: every key on the page lines up in
/// one column and the eye runs straight down it.
[[nodiscard]] std::vector<PanelOption> optionsFor(const std::vector<KeysPageRow>& rows) {
    std::vector<PanelOption> out;
    out.reserve(rows.size());
    for (const KeysPageRow& row : rows) {
        PanelOption option;
        option.label = row.verb;
        option.value = row.binding;
        option.accent = groupAccent(row.group);
        option.valueInk = InkRole::Key;
        option.labelTakesAccent = true;
        option.selectable = true;
        out.push_back(std::move(option));
    }
    return out;
}

/// NO PRINTED HOTKEY COLUMN ON THIS LIST, AND THE REASON IS NOT LAZINESS.
///
/// The reference's option lists are numbered because they are CHOICES: nine
/// things you might pick, `1` through `9`, direct-select. This list is
/// twenty-nine bindings, which is four times as many rows as there are digits,
/// and its value column is ALREADY a key name. "1 FORWARD W" on a page about
/// keys asks the reader to work out which of the two keys on the row is the one
/// they are being told about. So the bindings list is cursor-driven and the
/// numbers are spent where they mean something: the global nav row below it.
///
/// This is the one place the grammar and this screen genuinely disagreed. It is
/// written down rather than quietly fudged.
[[nodiscard]] OptionListStyle listStyle() {
    OptionListStyle style;
    style.showKeys = false;
    style.maxColumns = 3;
    style.gutterCells = 2;
    // NO minRows. The pane holds its height because the COMPOSITION fixed the
    // rect, not because the list was padded -- so the columns come out balanced
    // (ten, ten, nine) instead of filling one column to the floor and leaving
    // the next one empty. minRows is for a pane whose height would otherwise
    // follow its content; this one's does not.
    style.minRows = 0;
    style.alignValues = true;
    return style;
}

/// The same composition, then widened as far as it usefully can be: the first
/// share that fits the WHOLE list wins, and a share that would collapse the
/// detail pane is never taken. Falls back to the narrowest share, which is what
/// the frame would have had anyway.
[[nodiscard]] Composition composeFor(int frameWidth, int frameHeight,
                                     const std::vector<KeysPageRow>& rows) {
    Composition out = compose(frameWidth, frameHeight);
    if (!out.usable || rows.empty() || !out.body.split) {
        return out;
    }
    const std::vector<PanelOption> options = optionsFor(rows);
    const int count = static_cast<int>(rows.size());
    const int listRows = out.bodyRows - kIndicatorRows;
    for (const int share : kMasterShares) {
        const MasterDetail candidate =
            splitMasterDetail(out.bodyBand, out.metric, share, kMinMasterCells, kMinDetailCells);
        if (!candidate.split) {
            continue;
        }
        const PanelRect listRect{candidate.master.x, candidate.master.y, candidate.master.w,
                                 out.metric.heightOf(listRows)};
        const OptionListPlan plan =
            planOptionList(options, listRect, out.metric, listStyle());
        out.body = candidate;
        if (plan.columns * plan.rows >= count) {
            break;
        }
    }
    return out;
}

}  // namespace

KeysPageScroll keysPageScroll(const KeysPageState& state, int frameWidth, int frameHeight) {
    KeysPageScroll out;
    const Composition comp = composeFor(frameWidth, frameHeight, state.rows);
    if (!comp.usable || state.rows.empty()) {
        return out;
    }
    const int listRows = comp.bodyRows - kIndicatorRows;
    const PanelRect listRect{comp.body.master.x, comp.body.master.y, comp.body.master.w,
                             comp.metric.heightOf(listRows)};
    // PLANNED AGAINST THE WHOLE LIST, never against one screenful -- so the
    // column count and the value column are sized for the widest binding that
    // exists and do not shuffle when the page turns.
    const OptionListPlan plan =
        planOptionList(optionsFor(state.rows), listRect, comp.metric, listStyle());
    out.perScreen = std::max(1, plan.columns * plan.rows);
    const int count = static_cast<int>(state.rows.size());
    out.screens = std::max(1, (count + out.perScreen - 1) / out.perScreen);
    out.screen = std::clamp(std::max(0, state.cursor) / out.perScreen, 0, out.screens - 1);
    out.firstRow = out.screen * out.perScreen;
    return out;
}

void drawKeysPage(Framebuffer& target, const KeysPageState& state) {
    if (!state.open || state.openAmount <= 0.0F) {
        return;
    }
    const Composition comp = composeFor(target.width(), target.height(), state.rows);
    if (!comp.usable) {
        return;
    }
    const float alpha = std::clamp(state.openAmount, 0.0F, 1.0F);
    const PanelMetric metric = comp.metric;
    const PanelInk& ink = panelInk();

    FrameStyle style;
    // `◆` at every corner and junction, consistently, across this whole screen
    // -- the reference allows either and asks only that a screen not mix them.
    style.junction = Motif::Diamond;
    style.alpha = alpha;
    style.stipple = false;
    // A LITTLE DENSER THAN THE DEFAULT. This page takes the whole frame, and at
    // the default ground the ward's own signage reads straight through a column
    // of key names -- "THE GILDED GULL" was legible through the middle of the
    // list in the first capture. Six per cent of a lamplit street is not
    // atmosphere behind text this small; three is.
    style.groundAlpha = 0.97F;

    PanelFrame frame(target, comp.bounds, metric, style);
    for (const int r : comp.ruleRows) {
        frame.addRule(r);
    }
    if (comp.body.split) {
        frame.addDivider(comp.body.dividerCell, comp.bodyRow, comp.bodyRows);
    }
    frame.draw();

    // --- the tab row -------------------------------------------------------
    // F1 and F2 are the real keys; the tabs say so rather than inventing a
    // letter that does nothing.
    const std::vector<PanelTab> tabs{PanelTab{"F1", "KEYS"}, PanelTab{"F2", "OPTIONS"}};
    drawTabRow(target, frame.band(comp.tabRow, 1), metric, state.title, tabs, 0, state.readout,
               ink.accent, alpha);

    // --- the instruction header -------------------------------------------
    drawBreadcrumb(target, comp.headerBand, metric, {state.instruction}, ink.prose, alpha);
    if (!state.alert.empty() && comp.headerRows > 1) {
        // A warning outranks a menu, and it lands on the header's second row --
        // which the band holds open whether or not there is one, so nothing
        // below moves when a bouncer starts talking.
        const PanelRect alertRow{comp.headerBand.x, comp.headerBand.y + metric.cellH(),
                                 comp.headerBand.w, metric.cellH()};
        drawCellText(target, alertRow, metric, 0, 0, state.alert, Rgb{0.90F, 0.52F, 0.30F}, alpha);
    }

    // --- the master list ---------------------------------------------------
    const int listRows = comp.bodyRows - kIndicatorRows;
    const PanelRect listRect{comp.body.master.x, comp.body.master.y, comp.body.master.w,
                             metric.heightOf(listRows)};
    const std::vector<PanelOption> options = optionsFor(state.rows);
    const OptionListStyle rowStyle = listStyle();
    const OptionListPlan plan = planOptionList(options, listRect, metric, rowStyle);
    const KeysPageScroll scroll = keysPageScroll(state, target.width(), target.height());

    const int count = static_cast<int>(state.rows.size());
    const int first = std::clamp(scroll.firstRow, 0, std::max(0, count));
    const int last = std::min(count, first + scroll.perScreen);
    const std::vector<PanelOption> page(options.begin() + first, options.begin() + last);
    drawOptionListPlanned(target, listRect, metric, page,
                          std::clamp(state.cursor, 0, std::max(0, count - 1)) - first, plan,
                          alpha);

    // The indicator row at the foot of the master pane holds its place whether
    // or not there is anything to say, so the rule under the body never moves.
    const PanelRect indicator{comp.body.master.x, comp.body.master.y + metric.heightOf(listRows),
                              comp.body.master.w, metric.cellH()};
    if (scroll.screens > 1) {
        drawCellText(target, indicator, metric, 0, 0,
                     "MORE  " + std::to_string(scroll.screen + 1) + "/" +
                         std::to_string(scroll.screens),
                     ink.dim, alpha);
    }

    // --- the detail pane ---------------------------------------------------
    if (comp.body.split && count > 0) {
        const int at = std::clamp(state.cursor, 0, count - 1);
        const KeysPageRow& row = state.rows[static_cast<std::size_t>(at)];
        const Rgb accent = groupAccent(row.group);
        const PanelRect detail = comp.body.detail;

        const std::vector<PanelRect> panes = splitRows(detail, metric,
                                                       {
                                                           spanCells(1),   // the subject badge
                                                           spanCells(1),   // air
                                                           spanCells(3),   // the facts
                                                           spanCells(1),   // air
                                                           spanWeight(1),  // prose, then the verb
                                                       });
        // THE SUBJECT, INVERTED. The reference's own way of saying "this is
        // what you are looking at" -- an accent fill with the text knocked out
        // dark, the identical shape the selected row in the list wears, so the
        // two read as the same object seen twice.
        const int badge = static_cast<int>(row.verb.size()) + 2;
        drawInvertedFill(target, panes[0], metric, 0, 0, badge, accent, alpha);
        drawCellTextKnockout(target, panes[0], metric, 1, 0, row.verb, ink.knockout, alpha);
        drawCellTextRight(target, panes[0], metric, 0, 0, groupName(row.group), ink.dim, alpha);

        // Labels left, values at ONE column. Facts, not paragraphs.
        const std::vector<PanelFact> facts{
            PanelFact{"BOUND TO", row.binding.empty() ? "NOTHING" : row.binding, InkRole::Key},
            PanelFact{"ALSO", row.alternate.empty() ? "NOTHING ELSE" : row.alternate,
                      row.alternate.empty() ? InkRole::Dim : InkRole::Key},
        };
        // The family is already named at the right of the badge row above, in
        // the reference's own "subject then status" shape. Saying it twice in
        // one pane is the clutter the composition rules say to cut rather than
        // shrink -- so the facts block holds three rows and spends two.
        drawFacts(target, panes[2], metric, facts, -1, alpha);

        // The prose, and the commit verb pinned to the pane's LAST row so it
        // holds still while the cursor runs through rows whose help is a
        // different number of lines long.
        const int proseRows = std::max(0, metric.rowsIn(panes[4].h) - 2);
        const PanelRect prose{panes[4].x, panes[4].y, panes[4].w, metric.heightOf(proseRows)};
        int used = 0;
        if (!row.help.empty()) {
            // NO BULLET. `•` means "an effect" in this register and this is a
            // paragraph of description, not a mechanical consequence -- see
            // PanelLine's own note on the flavour -> effect -> number order.
            used = drawProse(target, prose, metric, {PanelLine{Bullet::None, "", row.help,
                                                               InkRole::Prose, accent}},
                             alpha);
        }
        // DELIBERATE EMPTINESS, TEXTURED. What the paragraph did not need is
        // left empty on purpose and given the reference's faint `.`/`'` field
        // rather than being crammed with something or collapsed away.
        const int spare = proseRows - used;
        if (spare >= 3) {
            const PanelRect rest{prose.x, prose.y + metric.heightOf(used + 1), prose.w,
                                 metric.heightOf(spare - 1)};
            drawStipple(target, rest, metric, ink.rule, 0.22F * alpha);
        }

        // STATE CHANGES THE VERB, not the button's enabled-ness. A row that is
        // a mode rather than a binding gets a state label where the verb would
        // be -- never a greyed-out ENTER that would do nothing.
        if (row.bindable) {
            drawCommitVerb(target, detail, metric, "ENTER - REBIND",
                           "(STEALS THE KEY)", ink.key, alpha);
        } else {
            const int lastRow = metric.rowsIn(detail.h) - 1;
            drawCellText(target, detail, metric, 0, lastRow, "FIXED -- NOT A KEY", ink.dim,
                         alpha);
        }
    }

    // --- global nav, below its own rule ------------------------------------
    // NAV VERBS LIVE IN A LIST LIKE ANY OTHER, numbered and keyed, and they
    // re-column off the same rule everything else does: four of them take four
    // columns at 320x180 and four columns at 1920x1080, because four short
    // entries always fit.
    const std::vector<PanelOption> nav{
        PanelOption{"UP DOWN", "MOVE", "", panelInk().accent, InkRole::Dim, false},
        PanelOption{"ENTER", "REBIND", "", panelInk().accent, InkRole::Dim, false},
        PanelOption{"F2", "OPTIONS", "", panelInk().accent, InkRole::Dim, false},
        PanelOption{"0", "BACK", "", panelInk().accent, InkRole::Dim, false},
    };
    OptionListStyle navStyle;
    navStyle.showKeys = true;
    navStyle.maxColumns = 4;
    navStyle.gutterCells = 2;
    navStyle.minRows = 1;
    drawOptionList(target, comp.navBand, metric, nav, -1, navStyle, alpha);
}

}  // namespace granadad::render
