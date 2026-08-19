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

[[nodiscard]] OptionBlockStyle blockStyle() {
    OptionBlockStyle style;
    style.gapRows = 1;
    style.showKeys = true;
    style.minRows = 1;
    return style;
}

}  // namespace

CreationLayout creationLayout(const CreationPage& page, int frameWidth, int frameHeight) {
    CreationLayout out;
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
            const OptionListPlan plan =
                planOptionList(listOptions(page), candidate.master, out.metric, columnStyle(page));
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
    out.usable = out.bodyRows > 0;
    return out;
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
    style.groundAlpha = 0.98F;

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
        if (page.shape == CreationListShape::Blocks) {
            drawOptionBlocks(target, layout.listRect, metric, options, page.cursor, blockStyle(),
                             alpha);
        } else {
            drawOptionList(target, layout.listRect, metric, options, page.cursor,
                           columnStyle(page), alpha);
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
            drawStipple(target, rest, metric, ink.rule, 0.20F * alpha);
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
        if (page.shape == CreationListShape::Blocks) {
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
