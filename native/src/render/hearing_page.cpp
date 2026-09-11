#include "granadad/render/hearing_page.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "granadad/render/panel.hpp"

namespace granadad::render {

namespace {

/// The master's share of the body, out of 100. The widest row this list can
/// build is the ARMED plea with the device's confirm on its tail -- "2 - I
/// DID NOT. -- SURE? A" (the keyboard's return motif is one cell too),
/// twenty-five cells -- and it has to stay whole at the narrowest window:
/// 320x180 has 62 interior cells, and forty-six per cent of that is
/// twenty-eight. The detail pane keeps what is left, which at every size is
/// above kMinDetailCells and wraps the check block inside kHearingBodyRows.
inline constexpr int kMasterShare = 46;
inline constexpr int kMinMasterCells = 20;
/// The widest row, as measured: the armed denial with a one-cell confirm.
inline constexpr std::string_view kWidestRow = "2 - I DID NOT. -- SURE? A";
/// The check block's arithmetic line wraps; twenty-six cells is creation's
/// "a wrapped sentence that still reads", and under it the split collapses to
/// the one-pane fallback rather than two slots.
inline constexpr int kMinDetailCells = 26;

struct Composition {
    PanelMetric metric;
    PanelRect bounds;
    PanelRect interior;
    int tabRow = 0;
    int readingRow = 0;
    int bodyRow = 0;
    int bodyRows = 0;
    int navRow = 0;
    std::vector<int> ruleRows;
    PanelRect readingBand;
    PanelRect bodyBand;
    PanelRect navBand;
    MasterDetail body;
    bool usable = false;
};

[[nodiscard]] Composition compose(int frameWidth, int frameHeight, int gridCellsOverride) {
    Composition out;
    out.metric = panelMetric(frameHeight);
    const int cells = gridCellsOverride > 0 ? gridCellsOverride : out.metric.cellsIn(frameWidth);
    // THE FRAME ENDS AFTER ITS CONTENT AND IS SEATED, on both axes -- the
    // creation page's and the casebook's own rule. The height is fixed by
    // composition: two border rows, the tab row, the reading band, the body's
    // held rows, the nav row, and the three rules between them. One number,
    // so pleading moves nothing.
    const int rows = 2 + 1 + 1 + kHearingReadingRows + 1 + kHearingBodyRows + 1 + 1;
    const int full = out.metric.rowsIn(frameHeight);
    if (cells < 8 || full < 10) {
        return out;
    }
    const int gridRows = std::min(rows, full);
    out.bounds = PanelRect{panelSeatX(frameWidth, out.metric.widthOf(cells)),
                           panelSeatY(frameHeight, out.metric.heightOf(gridRows)),
                           out.metric.widthOf(cells), out.metric.heightOf(gridRows)};
    out.interior = PanelRect{out.bounds.x + out.metric.cellW(), out.bounds.y + out.metric.cellH(),
                             out.metric.widthOf(cells - 2), out.metric.heightOf(gridRows - 2)};
    const std::vector<PanelRect> bands = splitRows(out.interior, out.metric,
                                                   {
                                                       spanCells(1),                    // tab row
                                                       spanCells(1),                    // rule
                                                       spanCells(kHearingReadingRows),  // reading
                                                       spanCells(1),                    // rule
                                                       spanWeight(1),                   // body
                                                       spanCells(1),                    // rule
                                                       spanCells(1),                    // nav
                                                   });
    const auto rowOf = [&out](const PanelRect& band) {
        return (band.y - out.interior.y) / out.metric.cellH();
    };
    out.tabRow = rowOf(bands[0]);
    out.readingBand = bands[2];
    out.readingRow = rowOf(bands[2]);
    out.bodyBand = bands[4];
    out.bodyRow = rowOf(bands[4]);
    out.bodyRows = out.metric.rowsIn(bands[4].h);
    out.navBand = bands[6];
    out.navRow = rowOf(bands[6]);
    out.ruleRows = {rowOf(bands[1]), rowOf(bands[3]), rowOf(bands[5])};
    out.body =
        splitMasterDetail(out.bodyBand, out.metric, kMasterShare, kMinMasterCells, kMinDetailCells);
    out.usable = out.bodyRows >= 4;
    return out;
}

[[nodiscard]] std::vector<PanelOption> optionsFor(const HearingPageState& state) {
    std::vector<PanelOption> out;
    out.reserve(state.rows.size());
    for (std::size_t i = 0; i < state.rows.size(); ++i) {
        const HearingRow& row = state.rows[i];
        PanelOption option;
        option.key = row.key;
        option.label = row.label;
        option.accent = row.accent;
        option.valueInk = InkRole::Dim;
        // THE ARMED ROW TAKES ITS OWN ACCENT ON THE LABEL -- the state
        // changes the row (the label already carries "-- SURE?"), and the
        // colour says it a second way for the eye that skips words.
        option.labelTakesAccent = static_cast<int>(i) == state.armed;
        option.selectable = true;
        out.push_back(std::move(option));
    }
    return out;
}

[[nodiscard]] OptionListStyle listStyle() {
    OptionListStyle style;
    style.showKeys = true;
    // ONE COLUMN, ALWAYS: three pleas are one question.
    style.maxColumns = 1;
    style.gutterCells = 2;
    style.minRows = 0;
    style.alignValues = true;
    return style;
}

/// The reading, as prose: three plain lines, the charge allowed to wrap.
[[nodiscard]] std::vector<PanelLine> readingLines(const HearingPageState& state) {
    std::vector<PanelLine> lines;
    const auto plain = [&lines](const std::string& text, InkRole ink) {
        if (text.empty()) {
            return;
        }
        PanelLine line;
        line.body = text;
        line.bodyInk = ink;
        lines.push_back(std::move(line));
    };
    plain(state.laid, InkRole::Dim);
    plain(state.charge, InkRole::Prose);
    plain(state.asks, InkRole::Prose);
    return lines;
}

/// THE CHECK BLOCK, as prose lines between its two badges: the arithmetic in
/// number ink (the reference's yellow-then-green, translated: the sum is a
/// mechanical fact), the plea's term the same, the line(s) it was read
/// against dim -- reference material -- and the sentence in number ink
/// because it is the consequence stated as a figure.
[[nodiscard]] std::vector<PanelLine> weighingLines(const HearingPageState& state) {
    std::vector<PanelLine> lines;
    const auto plain = [&lines](const std::string& text, InkRole ink) {
        if (text.empty()) {
            return;
        }
        PanelLine line;
        line.body = text;
        line.bodyInk = ink;
        lines.push_back(std::move(line));
    };
    plain(state.arithmetic, InkRole::Number);
    plain(state.pleaTerm, InkRole::Number);
    plain(state.lines, InkRole::Dim);
    return lines;
}

[[nodiscard]] std::vector<PanelLine> priestLines(const HearingPageState& state) {
    std::vector<PanelLine> lines;
    if (!state.sentence.empty()) {
        PanelLine line;
        line.body = state.sentence;
        line.bodyInk = InkRole::Number;
        lines.push_back(std::move(line));
    }
    if (!state.priest.empty()) {
        PanelLine line;
        line.body = state.priest;
        line.bodyInk = InkRole::Prose;
        lines.push_back(std::move(line));
    }
    return lines;
}

/// The officer's block under the rows: "WATCHMAN CULL:" in the subject's
/// accent, the court.taken row in dim ink -- the reference's own `Name:`
/// line shape, the Watch's voice kept apart from the priest's.
[[nodiscard]] std::vector<PanelLine> officerLines(const HearingPageState& state) {
    std::vector<PanelLine> lines;
    if (state.officerSays.empty()) {
        return lines;
    }
    PanelLine line;
    line.name = state.officerName.empty() ? std::string("THE WATCH:") : state.officerName + ":";
    line.body = state.officerSays;
    line.bodyInk = InkRole::Dim;
    lines.push_back(std::move(line));
    return lines;
}

/// The Plea view's pane: the priest's opening in prose ink, then the
/// consequence of the hovered row in number ink -- flavour, then the
/// mechanical consequence, colour-sorted (the reference's own order).
[[nodiscard]] std::vector<PanelLine> consequenceLines(const HearingPageState& state) {
    std::vector<PanelLine> lines;
    if (!state.priest.empty()) {
        PanelLine line;
        line.body = state.priest;
        line.bodyInk = InkRole::Prose;
        lines.push_back(std::move(line));
    }
    if (!state.consequence.empty()) {
        PanelLine line;
        line.body = state.consequence;
        line.bodyInk = InkRole::Number;
        lines.push_back(std::move(line));
    }
    return lines;
}

/// The rows the detail pane wants for the JUDGED view at `detail`'s width:
/// badge, blank, the weighing, blank, the verdict badge, the sentence and
/// the priest. The tallest thing any view can ask of the pane, which is what
/// kHearingBodyRows was chosen against.
[[nodiscard]] int judgedRowsWanted(const HearingPageState& state, const PanelRect& detail,
                                   const PanelMetric& metric) {
    int at = 0;
    at += 1;  // THE PRIEST WEIGHS
    at += measureProse(detail, metric, weighingLines(state));
    at += 1;  // air
    at += 1;  // the verdict
    at += measureProse(detail, metric, priestLines(state));
    return at;
}

/// THE NAV BAND: the one key this page honours, worded for the tutor tier.
/// No opener, no back (the grammar exception): at rest it prints the back
/// keycap only when ESC does something -- the paper is open, or a plea is
/// armed -- and the confirm keycap always, because a row is always live.
[[nodiscard]] std::vector<PanelOption> navOptionsFor(const HearingPageState& state) {
    const Rgb accent = panelInk().accent;
    std::vector<PanelOption> out;
    out.push_back(PanelOption{std::string(kGlyphUpDown), "ROW", "", accent, InkRole::Dim, false});
    out.push_back(PanelOption{state.confirmKey.empty() ? std::string(kGlyphReturn)
                                                       : state.confirmKey,
                              state.view == HearingView::Judged ? "TAKE IT" : "PLEAD", "", accent,
                              InkRole::Dim, false});
    if (!state.backVerb.empty()) {
        out.push_back(PanelOption{state.backKey.empty() ? std::string("ESC") : state.backKey,
                                  state.backVerb, "", accent, InkRole::Dim, false});
    }
    return out;
}

[[nodiscard]] OptionListStyle navStyleOf() {
    OptionListStyle style;
    style.showKeys = true;
    style.maxColumns = 3;
    style.gutterCells = 2;
    style.minRows = 1;
    return style;
}

/// THE WIDTH MEASURE -- the widest row this page will draw, plus the border,
/// through panelMeasureCells. Everything here is a maximum over every view
/// and every row, so arrowing the list, opening the paper or pleading moves no
/// border. The reading and the check block are prose and wrap; they vote at
/// the prose measure, never at their unwrapped length.
[[nodiscard]] int measuredCells(const HearingPageState& state, const PanelMetric& metric,
                                int frameWidth) {
    int master = kMinMasterCells;
    if (!state.rows.empty()) {
        const PanelRect roomy{0, 0, metric.widthOf(200), metric.heightOf(80)};
        const OptionListPlan plan = planOptionList(optionsFor(state), roomy, metric, listStyle());
        master = std::max(master, optionListNaturalCells(plan, listStyle().gutterCells));
    }
    // THE ARMED ROW'S TAIL is the widest a row gets, and the measure holds it
    // whether or not a row is armed right now, so arming one moves no
    // border. The fixed variant, never the live key: both devices' confirms
    // are one cell here (the return motif, "A").
    master = std::max(master, static_cast<int>(kWidestRow.size()));
    int detail = std::max(kMinDetailCells, panelHeldDetailCells(kMasterShare, kMinMasterCells));
    detail = std::max(detail, kPanelProseMeasureCells);
    for (const std::string& fact : state.paper) {
        detail = std::max(detail, static_cast<int>(fact.size()));
    }
    detail = std::max(detail, static_cast<int>(state.lines.size()));
    detail = std::max(detail, static_cast<int>(state.verdict.size()) + 2);
    detail = std::max(detail, static_cast<int>(state.weighsBadge.size()) + 2);
    int want = masterDetailCellsFor(kMasterShare, kMinMasterCells, master, detail);
    want = std::max(want, tabRowCells(state.title, {}, state.readout));
    want = std::max(want, std::min(kPanelProseMeasureCells + 12,
                                   static_cast<int>(state.charge.size())));
    return panelMeasureCells(metric.cellsIn(frameWidth), want + 2);
}

[[nodiscard]] Composition composeFor(const HearingPageState& state, int frameWidth,
                                     int frameHeight) {
    Composition out = compose(frameWidth, frameHeight, -1);
    if (!out.usable) {
        return out;
    }
    const int gridCells = measuredCells(state, out.metric, frameWidth);
    if (gridCells < out.metric.cellsIn(frameWidth)) {
        const Composition sized = compose(frameWidth, frameHeight, gridCells);
        if (sized.usable) {
            out = sized;
        }
    }
    return out;
}

[[nodiscard]] PanelRect detailRectOf(const Composition& comp) {
    return comp.body.split ? comp.body.detail : PanelRect{};
}

/// A badge: an inverted fill the width of the word plus a cell of air either
/// side, the word knocked out dark -- the selection idiom doing the status
/// job, exactly as the reference's `[SUCCESS]` does.
void drawBadge(Framebuffer& target, const PanelRect& pane, const PanelMetric& metric, int row,
               std::string_view word, const Rgb& accent, float alpha) {
    if (word.empty()) {
        return;
    }
    const int cells = std::min(metric.cellsIn(pane.w), static_cast<int>(word.size()) + 2);
    drawInvertedFill(target, pane, metric, 0, row, cells, accent, alpha);
    drawCellTextKnockout(target, pane, metric, 1, row, word, panelInk().knockout, alpha);
}

void drawDetail(Framebuffer& target, const Composition& comp, const HearingPageState& state,
                float alpha) {
    const PanelRect detail = detailRectOf(comp);
    if (detail.empty()) {
        return;
    }
    const PanelMetric& metric = comp.metric;
    const PanelInk& ink = panelInk();
    const int rows = metric.rowsIn(detail.h);
    switch (state.view) {
        case HearingView::Plea: {
            // The hovered row's consequence, wrapped -- the informed choice.
            drawProse(target, detail, metric, consequenceLines(state), alpha);
            break;
        }
        case HearingView::Paper: {
            // THE SHEET, AS PHRASES. One per row, prose ink; the first is the
            // badge naming what is being read.
            drawBadge(target, detail, metric, 0, "THE PAPER", ink.accent, alpha);
            int at = 2;
            for (const std::string& fact : state.paper) {
                if (at >= rows) {
                    break;
                }
                drawCellText(target, detail, metric, 0, at, fact, ink.prose, alpha);
                ++at;
            }
            break;
        }
        case HearingView::Judged: {
            // THE CHECK BLOCK. The name of the check, its arithmetic, the
            // threshold, the verdict -- then the consequence, in prose.
            int at = 0;
            drawBadge(target, detail, metric, at, state.weighsBadge, ink.accent, alpha);
            at += 1;
            if (at < rows) {
                const PanelRect weighRect{detail.x, detail.y + metric.heightOf(at), detail.w,
                                          metric.heightOf(rows - at)};
                at += drawProse(target, weighRect, metric, weighingLines(state), alpha);
            }
            at += 1;  // air before the verdict
            if (at < rows) {
                drawBadge(target, detail, metric, at, state.verdict, state.verdictAccent, alpha);
                at += 1;
            }
            if (at < rows) {
                const PanelRect priestRect{detail.x, detail.y + metric.heightOf(at), detail.w,
                                           metric.heightOf(rows - at)};
                drawProse(target, priestRect, metric, priestLines(state), alpha);
            }
            break;
        }
    }
}

}  // namespace

HearingPageMetrics hearingPageMetrics(const HearingPageState& state, int frameWidth,
                                      int frameHeight) {
    HearingPageMetrics out;
    const Composition comp = composeFor(state, frameWidth, frameHeight);
    out.usable = comp.usable;
    if (!comp.usable) {
        return out;
    }
    out.split = comp.body.split;
    out.metric = comp.metric;
    out.bounds = comp.bounds;
    out.reading = comp.readingBand;
    out.master = comp.body.master;
    out.detail = comp.body.split ? comp.body.detail : PanelRect{};
    out.nav = comp.navBand;
    out.masterCells = comp.metric.cellsIn(comp.body.master.w);
    out.detailCells = comp.body.split ? comp.metric.cellsIn(comp.body.detail.w) : 0;
    out.readingRows = comp.metric.rowsIn(comp.readingBand.h);
    out.readingRowsWanted = measureProse(comp.readingBand, comp.metric, readingLines(state));
    out.bodyRows = comp.bodyRows;
    out.detailRowsWanted =
        comp.body.split ? judgedRowsWanted(state, comp.body.detail, comp.metric) : 0;
    return out;
}

int hearingRowAtPixel(const HearingPageState& state, int frameWidth, int frameHeight, int px,
                      int py) {
    if (!state.open || state.rows.empty()) {
        return -1;
    }
    const Composition comp = composeFor(state, frameWidth, frameHeight);
    if (!comp.usable) {
        return -1;
    }
    const std::vector<PanelOption> options = optionsFor(state);
    const OptionListPlan plan = planOptionList(options, comp.body.master, comp.metric, listStyle());
    return optionListAt(comp.body.master, comp.metric, plan, static_cast<int>(options.size()), px,
                        py);
}

void drawHearingPage(Framebuffer& target, const HearingPageState& state) {
    if (!state.open || state.openAmount <= 0.0F) {
        return;
    }
    const Composition comp = composeFor(state, target.width(), target.height());
    if (!comp.usable) {
        return;
    }
    const float alpha = std::clamp(state.openAmount, 0.0F, 1.0F);
    const PanelMetric metric = comp.metric;
    const PanelInk& ink = panelInk();

    FrameStyle style;
    style.junction = Motif::Diamond;
    style.alpha = alpha;
    style.stipple = false;
    style.groundAlpha = kPageGroundAlpha;
    // A FULL TAKEOVER: the ground takes the whole frame and the border only
    // what it encloses -- the casebook's ruling, for the casebook's reason. A
    // court is not a thing you look past.
    target.fillRect(0, 0, target.width(), target.height(), style.ground,
                    style.groundAlpha * alpha);

    PanelFrame frame(target, comp.bounds, metric, style);
    for (const int r : comp.ruleRows) {
        frame.addRule(r);
    }
    if (comp.body.split) {
        frame.addDivider(comp.body.dividerCell, comp.bodyRow, comp.bodyRows);
    }
    frame.draw();

    // --- the one header line: the breadcrumb and the clock ------------------
    if (!state.alert.empty()) {
        drawCellText(target, frame.band(comp.tabRow, 1), metric, 0, 0, state.alert,
                     Rgb{0.90F, 0.52F, 0.30F}, alpha);
    } else {
        drawTabRow(target, frame.band(comp.tabRow, 1), metric, state.title, {}, -1, state.readout,
                   ink.accent, alpha);
    }

    // --- the reading ---------------------------------------------------------
    drawProse(target, comp.readingBand, metric, readingLines(state), alpha);

    // --- the rows --------------------------------------------------------------
    const std::vector<PanelOption> options = optionsFor(state);
    if (!options.empty()) {
        const OptionListPlan plan =
            planOptionList(options, comp.body.master, metric, listStyle());
        const int count = static_cast<int>(options.size());
        const int at = std::clamp(state.cursor, 0, count - 1);
        drawOptionListPlanned(target, comp.body.master, metric, options, at, plan, alpha);
        // THE OFFICER BY THE WALL: his line under the rows, a row of air
        // between, wrapped to the pane and stopped at its bottom (drawProse's
        // own rule). Then the master pane's dead space, textured -- the rule
        // every pane keeps -- below whatever he said.
        const int listRows = metric.rowsIn(comp.body.master.h);
        int used = plan.rows;
        if (!state.officerSays.empty() && listRows - used >= 3) {
            const PanelRect escort{comp.body.master.x,
                                   comp.body.master.y + metric.heightOf(used + 1),
                                   comp.body.master.w, metric.heightOf(listRows - used - 1)};
            used += 1 + drawProse(target, escort, metric, officerLines(state), alpha);
        }
        const int spare = listRows - used;
        if (spare >= 3) {
            const PanelRect rest{comp.body.master.x,
                                 comp.body.master.y + metric.heightOf(used + 1),
                                 comp.body.master.w, metric.heightOf(spare - 1)};
            drawStipple(target, rest, metric, ink.rule, kPaneStippleAlpha * alpha);
        }
    }

    // --- the detail pane --------------------------------------------------------
    if (comp.body.split) {
        drawDetail(target, comp, state, alpha);
        // Contract (b): the commit beat on the verdict badge, for the few
        // steps after a plea lands -- rendered as a solid accent flash over
        // the badge row, easing back to the plain badge as the pulse decays.
        if (state.view == HearingView::Judged && state.commitPulse > 0.0F &&
            !state.verdict.empty()) {
            const PanelRect detail = detailRectOf(comp);
            const int row = 1 + measureProse(detail, metric, weighingLines(state)) + 1;
            if (row < metric.rowsIn(detail.h)) {
                const int cells = std::min(metric.cellsIn(detail.w),
                                           static_cast<int>(state.verdict.size()) + 2);
                drawInvertedFill(target, detail, metric, 0, row, cells, state.verdictAccent,
                                 alpha * std::clamp(state.commitPulse, 0.0F, 1.0F));
            }
        }
    }

    // --- global nav, below its own rule --------------------------------------------
    const std::vector<PanelOption> nav = navOptionsFor(state);
    const PanelRect navRect{comp.navBand.x, comp.navBand.y,
                            std::max(0, comp.navBand.w - metric.cellW()), comp.navBand.h};
    const OptionListPlan navPlan = planOptionList(nav, navRect, metric, navStyleOf());
    std::vector<PanelOption> navCaps = nav;
    for (PanelOption& option : navCaps) {
        option.label.clear();
    }
    drawOptionListPlanned(target, navRect, metric, navCaps, -1, navPlan, alpha);
    if (state.tutor > 0.0F) {
        drawOptionListPlanned(target, navRect, metric, nav, -1, navPlan,
                              alpha * std::min(1.0F, state.tutor));
    }
}

}  // namespace granadad::render
