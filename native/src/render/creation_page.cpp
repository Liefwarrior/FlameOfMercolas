#include "granadad/render/creation_page.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "granadad/render/framebuffer.hpp"

namespace granadad::render {

namespace {

/// The header band holds THREE rows whether or not all three are spoken -- the
/// breadcrumb path on the first, and TWO for the task, because on the quiz and
/// the biography the task is the question itself and a question is allowed to
/// need a second line at a narrow window. Holding the band open is what keeps
/// every rule below it in exactly the same place on every step of the flow, so
/// walking origin -> calling -> quiz -> review does not make the frame twitch.
inline constexpr int kHeaderRows = 3;

/// Twenty-two cells is a wrapped sentence that still reads. Deliberately not
/// larger: the biggest WINDOW is the narrowest in CELLS (hudMinorScale steps up
/// with the frame height, so 1920x1080 has 76 cells across where 960x540 has
/// 96), and a split that only survives at one resolution is not a split.
inline constexpr int kMinDetailCells = 26;
inline constexpr int kMinMasterCells = 18;

/// The commit verb sits on the detail pane's LAST row -- drawCommitVerb's own
/// contract -- so the clickable target is that row.
[[nodiscard]] PanelRect commitRowOf(const PanelRect& detail, const PanelMetric& metric) {
    const int rows = metric.rowsIn(detail.h);
    if (rows <= 0) {
        return PanelRect{};
    }
    return PanelRect{detail.x, detail.y + metric.heightOf(rows - 1), detail.w, metric.cellH()};
}

[[nodiscard]] std::vector<PanelOption> listOptions(const CreationPage& page) {
    std::vector<PanelOption> out;
    out.reserve(page.rows.size());
    for (const CreationPageRow& row : page.rows) {
        PanelOption option;
        option.key = row.key;
        option.label = row.label;
        option.value = row.value;
        option.accent = row.accent;
        option.selectable = row.selectable;
        option.labelTakesAccent = row.labelTakesAccent;
        option.valueInk = InkRole::Number;
        out.push_back(std::move(option));
    }
    return out;
}

/// listOptions() with each value HELD at its row's declared measure width --
/// see CreationPageRow::measureValueCells. This is what the width measure and
/// the master-share walk judge, so that neither responds to a value that
/// changes live under the player's hands. The one row that declares a width
/// is the sheet's NAME row (the live typed name): judged at its own length,
/// every second keystroke moved the frame a 2-cell step -- the ship note's
/// sheet wiggle. MEASURE ONLY: every draw call still lays out listOptions()'s
/// real values, and the held cells read as ordinary pane emptiness.
[[nodiscard]] std::vector<PanelOption> measureListOptions(const CreationPage& page) {
    std::vector<PanelOption> out = listOptions(page);
    const std::size_t shared = std::min(out.size(), page.rows.size());
    for (std::size_t i = 0; i < shared; ++i) {
        const int hold = page.rows[i].measureValueCells;
        if (hold > static_cast<int>(out[i].value.size())) {
            out[i].value.append(static_cast<std::size_t>(hold) - out[i].value.size(), ' ');
        }
    }
    return out;
}

[[nodiscard]] OptionListStyle columnStyle(const CreationPage& page) {
    OptionListStyle style;
    style.maxColumns = std::max(1, page.maxColumns);
    style.gutterCells = 2;
    // NO minRows. The pane holds its height because the COMPOSITION fixed the
    // rect, not because the list was padded -- so the columns come out balanced
    // rather than filling the first one to the floor.
    style.minRows = 0;
    style.alignValues = true;
    style.showKeys = true;
    for (const CreationPageRow& row : page.rows) {
        if (!row.key.empty()) {
            return style;
        }
    }
    style.showKeys = false;
    return style;
}

[[nodiscard]] KeyGridStyle gridStyle(const CreationPage& page) {
    KeyGridStyle style;
    style.columns = std::max(1, page.maxColumns);
    return style;
}

[[nodiscard]] OptionBlockStyle blockStyle() {
    OptionBlockStyle style;
    style.gapRows = 1;
    style.showKeys = true;
    style.minRows = 1;
    return style;
}

/// The rows the MASTER list actually wants, asked of the SAME pure planner the
/// draw call walks rather than a second description of it.
[[nodiscard]] int masterContentRows(const CreationPage& page, const PanelRect& master,
                                    const PanelMetric& metric) {
    // The MEASURE copy here too -- this is the height half of the same
    // judgement, and a row count that changed when the typed NAME crossed a
    // column threshold would bounce the frame's foot while typing.
    const std::vector<PanelOption> options = measureListOptions(page);
    if (options.empty() || master.empty()) {
        return 0;
    }
    if (page.shape == CreationListShape::Blocks) {
        int used = 0;
        for (const OptionBlock& block : planOptionBlocks(options, master, metric, blockStyle())) {
            if (block.rows > 0) {
                used = std::max(used, metric.rowsIn(block.rect.y + block.rect.h - master.y));
            }
        }
        return used;
    }
    if (page.shape == CreationListShape::Keys) {
        const KeyGridPlan grid = planKeyGrid(options, master, metric, gridStyle(page));
        if (grid.usable) {
            return (grid.rows - 1) * grid.strideRows + 1;
        }
        // The grid could not take the pane's shape, so the fallback is the one
        // that gets drawn and the one that gets measured. See drawCreationPage.
    }
    return planOptionList(options, master, metric, columnStyle(page)).rows;
}

/// The rows the DETAIL pane wants, walked in exactly the order the draw call
/// lays it out -- badge, facts, bars, prose, and the two rows the commit verb
/// and its restatement own at the foot.
[[nodiscard]] int detailContentRows(const CreationPage& page, const PanelRect& detail,
                                    const PanelMetric& metric) {
    if (detail.empty()) {
        return 0;
    }
    int at = 0;
    if (!page.detailBadge.empty()) {
        at += 2;
    }
    if (!page.facts.empty()) {
        at += static_cast<int>(page.facts.size()) + 1;
    }
    if (!page.bars.empty()) {
        at += static_cast<int>(page.bars.size()) + 1;
    }
    if (!page.lines.empty()) {
        at += measureProse(detail, metric, page.lines);
    }
    if (!page.commitVerb.empty()) {
        at += 2;
    }
    return at;
}

/// Below this a body band is not a pane, it is a slot.
inline constexpr int kMinBodyRows = 6;

/// ONE COMPOSITION AT A CHOSEN WIDTH AND HEIGHT. Everything creationLayout()
/// used to do, with the grid's cell count AND row count handed in rather than
/// taken from the window -- which is the whole mechanism behind sizing the
/// frame to its content, now on both axes. The body is the only `spanWeight`
/// in the stack, so a grid one row shorter is a BODY one row shorter and
/// every rule above it stays exactly where it was.
[[nodiscard]] CreationLayout composeCreation(const CreationPage& page, int frameWidth,
                                             int frameHeight, int gridCells, int gridRows,
                                             int topY) {
    CreationLayout out;
    out.metric = panelMetric(frameHeight);
    const int cells = gridCells;
    const int rows = gridRows;
    if (cells < 8 || rows < 10) {
        return out;
    }
    // AND NOW IT KNOWS HOW WIDE IT IS, SO IT CAN SIT SOMEWHERE ACROSS TOO.
    // The TOP is handed in -- creationLayout() seats it with panelSeatY() once
    // it knows how tall the page actually came out -- and the LEFT is the
    // mirror rule, panelSeatX() off the measured width. A full-width grid has
    // a spare of a few sub-cell pixels and lands where the old centring put
    // it; a measured one is PLACED, 45/55, matching the vertical judgement.
    out.bounds = PanelRect{panelSeatX(frameWidth, out.metric.widthOf(cells)), topY,
                           out.metric.widthOf(cells), out.metric.heightOf(rows)};
    out.interior =
        PanelRect{out.bounds.x + out.metric.cellW(), out.bounds.y + out.metric.cellH(),
                  out.metric.widthOf(cells - 2), out.metric.heightOf(rows - 2)};

    // ONE COMPOSITION, SEVEN STEPS. There is not a pixel constant in it: every
    // number is a count of grid rows, and the grid comes from the frame height.
    const std::vector<PanelRect> bands = splitRows(out.interior, out.metric,
                                                   {
                                                       spanCells(1),            // tab row
                                                       spanCells(1),            // rule
                                                       spanCells(kHeaderRows),  // crumbs + task
                                                       spanCells(1),            // rule
                                                       spanWeight(1),           // body
                                                       spanCells(1),            // rule
                                                       spanCells(1),            // global nav
                                                   });
    const auto rowOf = [&out](const PanelRect& band) {
        return (band.y - out.interior.y) / out.metric.cellH();
    };
    out.tabRow = rowOf(bands[0]);
    out.headerBand = bands[2];
    out.headerRow = rowOf(bands[2]);
    out.bodyBand = bands[4];
    out.bodyRow = rowOf(bands[4]);
    out.bodyRows = out.metric.rowsIn(bands[4].h);
    out.navBand = bands[6];
    out.navRow = rowOf(bands[6]);
    out.ruleRows = {rowOf(bands[1]), rowOf(bands[3]), rowOf(bands[5])};

    if (page.hasDetail) {
        // THE MASTER SHARE IS A GUESS, SO IT IS ALLOWED ONE CORRECTION.
        //
        // Still a FIXED composition: this responds to the window and to the
        // length of the list, never to a player. The defect it fixes is real
        // and was photographed -- the custom sheet's longest value is an
        // appearance label ("SHOPKEEPER"), which pushed one entry width past
        // half the master pane and dropped a twenty-five-row list into a single
        // tall column with a whole empty column beside it. Widening the master
        // by a notch buys the second column back.
        //
        // Fewest ROWS wins, ties go to the NARROWEST share, and a share that
        // would collapse the detail pane is never taken -- so a list that cannot
        // use more width (every one-column list on this screen) keeps the share
        // it asked for and nothing moves.
        int bestRows = -1;
        bool have = false;
        for (const int share : {page.masterShare, page.masterShare + 6, page.masterShare + 12}) {
            const MasterDetail candidate = splitMasterDetail(out.bodyBand, out.metric, share,
                                                             kMinMasterCells, kMinDetailCells);
            if (!candidate.split) {
                continue;
            }
            if (page.shape != CreationListShape::Columns || page.rows.empty()) {
                if (!have) {
                    out.body = candidate;
                    have = true;
                }
                break;
            }
            // JUDGED ON THE MEASURE COPY (values held at their declared
            // measure width -- measureListOptions), for the same stillness
            // the width measure buys: a share choice that flipped when the
            // typed NAME crossed a column threshold would move the divider
            // under the player's hands mid-keystroke.
            const OptionListPlan plan = planOptionList(measureListOptions(page), candidate.master,
                                                       out.metric, columnStyle(page));
            if (!have || plan.rows < bestRows) {
                out.body = candidate;
                bestRows = plan.rows;
                have = true;
            }
        }
        if (!have) {
            // Every share collapsed, which is the honest answer at a small
            // window: one pane, and the caller composes its fallback off
            // body.split rather than guessing a pixel breakpoint.
            out.body = splitMasterDetail(out.bodyBand, out.metric, page.masterShare,
                                         kMinMasterCells, kMinDetailCells);
        }
    } else {
        out.body = MasterDetail{out.bodyBand, PanelRect{}, 0, false};
    }
    out.listRect = out.body.master;
    out.detailRect = out.body.split ? out.body.detail : PanelRect{};
    if (page.shape == CreationListShape::Keys && !page.rows.empty()) {
        // PLANNED ONCE, HERE. The drawing and the hit-test both read this
        // field rather than each calling planKeyGrid themselves, which is the
        // same reason CreationLayout exists at all: two descriptions of one
        // layout is a thing that can drift.
        out.grid = planKeyGrid(listOptions(page), out.listRect, out.metric, gridStyle(page));
    }
    out.usable = out.bodyRows > 0;
    return out;
}

/// THE WIDTH MEASURE -- the widest row this page will actually draw, plus its
/// border, clamped by panelMeasureCells. The mirror of the row measure below:
/// masterContentRows asks "how tall does the content want to be", this asks
/// "how wide", and both ask the SAME pure planners the drawing walks.
///
/// What gets a vote and what does not, per the reference's own cursor rule:
///
///   * THE MASTER LIST votes at its natural width -- it is static under the
///     cursor. A key grid is its block, a column list is its columns at
///     content width, and a BLOCK list (sentences) votes at the prose measure
///     rather than at its unwrapped length, because a hundred-glyph answer
///     asking for the whole frame back is not a measurement.
///   * THE DETAIL PANE does not vote AT ALL -- it is the half the cursor
///     swaps, so a frame that widened when the cursor reached the wordiest
///     trade would be the twitch bodyHoldRows exists to prevent, and a commit
///     cost that grows as a name is typed must not drag the border with it.
///     It takes what the master share's arithmetic leaves it, held at
///     panelHeldDetailCells (the width its bodyHoldRows floors were judged
///     against -- see that function), and its prose re-wraps into that: the
///     row measure below runs AFTER this one, which is the wrap feedback that
///     makes a narrower page honestly taller.
///   * The tab row, the crumb path and the one-row nav band vote in full --
///     each is a single row that must shed nothing, and the nav names the way
///     out.
[[nodiscard]] int measuredGridCells(const CreationPage& page, const CreationLayout& full,
                                    int frameWidth) {
    const PanelMetric& metric = full.metric;
    const std::vector<PanelOption> options = listOptions(page);
    int master = 0;
    if (!options.empty()) {
        if (full.grid.usable) {
            master = keyGridNaturalCells(full.grid);
        } else if (page.shape == CreationListShape::Blocks) {
            int key = 0;
            int label = 0;
            for (const PanelOption& option : options) {
                key = std::max(key, static_cast<int>(option.key.size()));
                label = std::max(label, static_cast<int>(option.label.size()));
            }
            master = (key > 0 ? key + 1 : 0) + std::min(label, kPanelProseMeasureCells);
        } else {
            const OptionListStyle style = columnStyle(page);
            // THE MEASURE COPY -- values held at their declared measure
            // width, so the frame is sized once for the widest legal NAME
            // and holds still under every keystroke. See measureListOptions.
            const OptionListPlan plan =
                planOptionList(measureListOptions(page), full.listRect, metric, style);
            master = optionListNaturalCells(plan, style.gutterCells);
        }
    }
    int want = master;
    if (page.hasDetail && full.body.split) {
        const int heldDetail =
            std::max(kMinDetailCells, panelHeldDetailCells(page.masterShare, kMinMasterCells));
        want = masterDetailCellsFor(page.masterShare, kMinMasterCells,
                                    std::max(master, kMinMasterCells), heldDetail);
    }
    want = std::max(want, tabRowCells(page.title, page.tabs, page.readout));
    if (page.crumbs.size() > 1) {
        want = std::max(want, breadcrumbCells(page.crumbs));
    }
    if (!page.nav.empty()) {
        // The nav band is ONE fixed row in this composition (unlike the
        // casebook's, which may take two), so a frame its list overflows
        // silently drops the last verb -- which is the way back. It votes.
        OptionListStyle navStyle;
        navStyle.showKeys = true;
        navStyle.maxColumns = static_cast<int>(page.nav.size());
        navStyle.gutterCells = 2;
        navStyle.minRows = 1;
        const OptionListPlan navPlan = planOptionList(page.nav, full.navBand, metric, navStyle);
        want = std::max(want, optionListNaturalCells(navPlan, navStyle.gutterCells));
    }
    return panelMeasureCells(metric.cellsIn(frameWidth), want + 2);
}

}  // namespace

CreationLayout creationLayout(const CreationPage& page, int frameWidth, int frameHeight) {
    const PanelMetric metric = panelMetric(frameHeight);
    const int fullCells = metric.cellsIn(frameWidth);
    const int rows = metric.rowsIn(frameHeight);
    const int topY = panelSeatY(frameHeight, metric.heightOf(rows));

    // PASS ONE: the whole window, which is what this screen used to ship as.
    // It is measured at full size on purpose -- a planner asked against a
    // pane too short to hold its content answers with the pane, not with the
    // content, and the answer we want here is the content.
    CreationLayout full = composeCreation(page, frameWidth, frameHeight, fullCells, rows, topY);
    if (!full.usable) {
        return full;
    }

    // THE MEASURE, WIDTH FIRST -- then the height measure runs against the
    // measured panes, so prose that re-wraps taller in a narrower detail pane
    // is COUNTED taller. That ordering is the wrap feedback; without it a
    // measured page would clip the exact lines the height rule exists to keep.
    int gridCells = measuredGridCells(page, full, frameWidth);
    if (gridCells < fullCells) {
        const CreationLayout sized =
            composeCreation(page, frameWidth, frameHeight, gridCells, rows, topY);
        if (sized.usable) {
            full = sized;
        } else {
            gridCells = fullCells;
        }
    } else {
        gridCells = fullCells;
    }

    // SIZE TO CONTENT, WHICH THE REFERENCE LICENSES BY NAME. Its rule is about
    // cursor movement, not about panels in general: hold height where moving
    // the cursor SWAPS the content, size to content where the content is
    // static. A quiz answer set does not change while the cursor moves within
    // it, and neither does a nine-trade roster -- so the master list sizes.
    //
    // The detail pane is the half the cursor DOES swap, so it may not set the
    // height on its own: it enters the max so nothing is ever clipped, and
    // CreationPage::bodyHoldRows is the step's own floor, chosen once against
    // the tallest thing any row of that step can put in the pane. That floor is
    // what keeps the frame still while you arrow -- it is the same kind of
    // per-step composition decision masterShare already is, and
    // test_creation's "moving the cursor moves nothing" case is its guard.
    const int need = std::max({masterContentRows(page, full.listRect, full.metric),
                               detailContentRows(page, full.detailRect, full.metric),
                               page.bodyHoldRows});
    // One blank row of breathing space under the taller pane -- the same the
    // panes already leave between their content and their stippled field.
    const int want = std::clamp(need + 1, kMinBodyRows, full.bodyRows);
    if (want >= full.bodyRows) {
        return full;
    }
    // AND NOW IT KNOWS HOW TALL IT IS, SO IT CAN SIT SOMEWHERE. Sprint 1 taught
    // this page to end after its content and left it pinned to the top, which
    // is how THE DOOR came out as a 164px panel with 2px above it and 194px of
    // black below. panelSeatY() splits that remainder -- one rule, shared with
    // the casebook, and slightly top-weighted because a box of type centred by
    // arithmetic reads low. The width keeps the cells the measure chose.
    const int shortRows = rows - (full.bodyRows - want);
    return composeCreation(page, frameWidth, frameHeight, gridCells, shortRows,
                           panelSeatY(frameHeight, metric.heightOf(shortRows)));
}

void drawCreationPage(Framebuffer& target, const CreationPage& page) {
    const CreationLayout layout = creationLayout(page, target.width(), target.height());
    if (!layout.usable) {
        return;
    }
    const float alpha = std::clamp(page.alpha, 0.0F, 1.0F);
    if (alpha <= 0.0F) {
        return;
    }
    const PanelMetric metric = layout.metric;
    const PanelInk& ink = panelInk();

    FrameStyle style;
    // `◆` at every corner and junction, consistently, across the whole flow --
    // the reference allows either and asks only that a screen not mix them.
    style.junction = Motif::Diamond;
    style.alpha = alpha;
    style.stipple = false;
    // This screen runs before there is a world at all: there is nothing behind
    // it for a translucent ground to reveal, so it is very nearly opaque and the
    // near-black reads as a deliberate backdrop rather than an uncleared buffer.
    // It takes kPageGroundAlpha anyway, because a full-screen page having its
    // own number is exactly the drift that constant exists to end -- and this
    // one is reachable from the pause menu, where there IS a world behind it.
    style.groundAlpha = kPageGroundAlpha;

    // AND THE TAKEOVER IS THE PAGE'S, NOT THE BORDER'S. The frame now ends
    // after its content, and this screen is reachable from the pause menu where
    // there IS a world behind it -- so the ground takes the whole frame and the
    // border takes only what it has to enclose. Over a new game there is
    // nothing behind it and this paints the same near-black the empty rows were
    // already going to be. Same ruling as casebook_page.cpp's, same reason.
    target.fillRect(0, 0, target.width(), target.height(), style.ground,
                    style.groundAlpha * alpha);

    PanelFrame frame(target, layout.bounds, metric, style);
    for (const int r : layout.ruleRows) {
        frame.addRule(r);
    }
    if (layout.body.split) {
        frame.addDivider(layout.body.dividerCell, layout.bodyRow, layout.bodyRows);
    }
    frame.draw();

    // --- the tab row -------------------------------------------------------
    drawTabRow(target, frame.band(layout.tabRow, 1), metric, page.title, page.tabs,
               page.currentTab, page.readout, page.accent, alpha);

    // --- breadcrumb, then the task ----------------------------------------
    const PanelRect crumbRow{layout.headerBand.x, layout.headerBand.y, layout.headerBand.w,
                             metric.cellH()};
    drawBreadcrumb(target, crumbRow, metric, page.crumbs, page.accent, alpha);
    if (!page.instruction.empty()) {
        // A SINGLE CRUMB IS AN INSTRUCTION, drawBreadcrumb's own documented
        // second job -- the reference's `Select a tile of the Pearl Lands...`
        // row. It wraps, which is why the question a quiz asks can live here
        // instead of being clipped into a header.
        const PanelRect taskRow{layout.headerBand.x, layout.headerBand.y + metric.cellH(),
                                layout.headerBand.w, metric.heightOf(kHeaderRows - 1)};
        drawBreadcrumb(target, taskRow, metric, {page.instruction}, ink.prose, alpha);
    }

    // --- the master list ---------------------------------------------------
    const std::vector<PanelOption> options = listOptions(page);
    if (!options.empty()) {
        // How many rows the list will actually spend, asked of the SAME pure
        // planner the draw call walks -- not a second description of it. (The
        // S4 lesson, and the reason both shapes have a plan* twin at all.)
        int usedRows = 0;
        const KeyGridPlan& grid = layout.grid;
        if (grid.usable) {
            usedRows = (grid.rows - 1) * grid.strideRows + 1;
            drawKeyGrid(target, layout.listRect, metric, options, page.cursor, grid, alpha);
        } else if (page.shape == CreationListShape::Blocks) {
            for (const OptionBlock& block : planOptionBlocks(options, layout.listRect, metric,
                                                             blockStyle())) {
                if (block.rows > 0) {
                    usedRows = std::max(usedRows, metric.rowsIn(block.rect.y + block.rect.h -
                                                               layout.listRect.y));
                }
            }
            drawOptionBlocks(target, layout.listRect, metric, options, page.cursor, blockStyle(),
                             alpha);
        } else {
            usedRows = planOptionList(options, layout.listRect, metric, columnStyle(page)).rows;
            drawOptionList(target, layout.listRect, metric, options, page.cursor,
                           columnStyle(page), alpha);
        }
        // THE MASTER PANE'S OWN DEAD SPACE, TEXTURED -- which it was not.
        //
        // The detail pane has had the field since it was written; the master
        // pane never did, and the master pane is the one that empties out. The
        // quiz is the case that proves it: three answers spend eight rows of a
        // forty-one-row block list at 640x360, so 104,000 pixels -- roughly two
        // thirds of the pane -- were flat black with nothing in them at all.
        // Same rule, same alpha, same one blank row of breathing space between
        // content and field that the detail pane leaves.
        const int listRows = metric.rowsIn(layout.listRect.h);
        const int spare = listRows - usedRows;
        if (page.stipple && spare >= 3) {
            const PanelRect rest{layout.listRect.x,
                                 layout.listRect.y + metric.heightOf(usedRows + 1),
                                 layout.listRect.w, metric.heightOf(spare - 1)};
            drawStipple(target, rest, metric, ink.rule, kPaneStippleAlpha * alpha);
        }
    }

    // --- the detail pane ---------------------------------------------------
    if (layout.body.split && !layout.detailRect.empty()) {
        const PanelRect detail = layout.detailRect;
        const int detailRows = metric.rowsIn(detail.h);
        int at = 0;
        if (!page.detailBadge.empty() && detailRows > 0) {
            // THE SUBJECT, INVERTED -- the identical shape the selected row in
            // the list wears, so the two read as the same object seen twice.
            const int badge = static_cast<int>(page.detailBadge.size()) + 2;
            drawInvertedFill(target, detail, metric, 0, at, badge, page.accent, alpha);
            drawCellTextKnockout(target, detail, metric, 1, at, page.detailBadge, ink.knockout,
                                 alpha);
            if (!page.detailStatus.empty()) {
                drawCellTextRight(target, detail, metric, 1, at, page.detailStatus, ink.dim,
                                  alpha);
            }
            at += 2;
        }
        if (!page.facts.empty() && at < detailRows) {
            const PanelRect factRect{detail.x, detail.y + metric.heightOf(at), detail.w,
                                     metric.heightOf(detailRows - at)};
            at += static_cast<int>(page.facts.size());
            drawFacts(target, factRect, metric, page.facts, -1, alpha);
            ++at;
        }
        if (!page.bars.empty() && at < detailRows) {
            const PanelRect barRect{detail.x, detail.y + metric.heightOf(at), detail.w,
                                    metric.heightOf(detailRows - at)};
            at += drawBars(target, barRect, metric, page.bars, page.barCells, alpha);
            ++at;
        }
        // The commit verb owns the last row and the restatement the row above
        // it, so the prose never runs into either.
        const int reserved = page.commitVerb.empty() ? 0 : 2;
        const int proseRows = std::max(0, detailRows - at - reserved);
        int used = 0;
        if (!page.lines.empty() && proseRows > 0) {
            const PanelRect proseRect{detail.x, detail.y + metric.heightOf(at), detail.w,
                                      metric.heightOf(proseRows)};
            used = drawProse(target, proseRect, metric, page.lines, alpha);
        }
        // DELIBERATE EMPTINESS, TEXTURED. What the pane did not need is left
        // empty on purpose and given the reference's faint field rather than
        // being crammed or collapsed away.
        const int spare = proseRows - used;
        if (page.stipple && spare >= 3) {
            const PanelRect rest{detail.x, detail.y + metric.heightOf(at + used + 1), detail.w,
                                 metric.heightOf(spare - 1)};
            drawStipple(target, rest, metric, ink.rule, kPaneStippleAlpha * alpha);
        }
        if (!page.commitVerb.empty()) {
            drawCommitVerb(target, detail, metric, page.commitVerb, page.commitCost, ink.key,
                           alpha);
        }
    }

    // --- global nav, below its own rule ------------------------------------
    if (!page.nav.empty()) {
        OptionListStyle navStyle;
        navStyle.showKeys = true;
        navStyle.maxColumns = static_cast<int>(page.nav.size());
        navStyle.gutterCells = 2;
        navStyle.minRows = 1;
        drawOptionList(target, layout.navBand, metric, page.nav, -1, navStyle, alpha);
    }
}

CreationHit creationPageHitTest(const CreationPage& page, int frameWidth, int frameHeight, int px,
                                int py) {
    const CreationLayout layout = creationLayout(page, frameWidth, frameHeight);
    if (!layout.usable) {
        return CreationHit{};
    }
    const std::vector<PanelOption> options = listOptions(page);
    if (!options.empty()) {
        int index = -1;
        if (layout.grid.usable) {
            index = keyGridAt(layout.listRect, layout.metric, layout.grid,
                              static_cast<int>(options.size()), px, py);
        } else if (page.shape == CreationListShape::Blocks) {
            index = optionBlockAt(planOptionBlocks(options, layout.listRect, layout.metric,
                                                   blockStyle()),
                                  px, py);
        } else {
            const OptionListPlan plan =
                planOptionList(options, layout.listRect, layout.metric, columnStyle(page));
            index = optionListAt(layout.listRect, layout.metric, plan,
                                 static_cast<int>(options.size()), px, py);
        }
        if (index >= 0 && page.rows[static_cast<std::size_t>(index)].selectable) {
            return CreationHit{CreationHit::Zone::Row, index};
        }
    }
    if (layout.body.split && !layout.detailRect.empty() && !page.commitVerb.empty()) {
        const PanelRect row = commitRowOf(layout.detailRect, layout.metric);
        if (!row.empty() && px >= row.x && px < row.right() && py >= row.y && py < row.bottom()) {
            return CreationHit{CreationHit::Zone::Commit, -1};
        }
    }
    if (!page.nav.empty()) {
        // ENTRY 0 IS THE WAY BACK and it is the only nav entry a pointer may
        // press. The rest of the row states what the other two inputs do; a
        // click target that reported "UP DOWN - MOVE" was pressed would be
        // advertising an affordance that does not exist.
        OptionListStyle navStyle;
        navStyle.showKeys = true;
        navStyle.maxColumns = static_cast<int>(page.nav.size());
        navStyle.gutterCells = 2;
        navStyle.minRows = 1;
        const OptionListPlan plan =
            planOptionList(page.nav, layout.navBand, layout.metric, navStyle);
        const int index = optionListAt(layout.navBand, layout.metric, plan,
                                       static_cast<int>(page.nav.size()), px, py);
        if (index == 0) {
            return CreationHit{CreationHit::Zone::Back, 0};
        }
    }
    return CreationHit{};
}

}  // namespace granadad::render
