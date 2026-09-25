#include "granadad/render/rung_plate.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "granadad/render/hud.hpp"
#include "granadad/render/panel.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// the watch
// ---------------------------------------------------------------------------

std::vector<LegendRise> LegendRiseWatch::diff(const sim::Legend& legend) {
    std::vector<LegendRise> rises;
    const sim::LegendRow* rows = legend.rows();
    if (!seeded_) {
        // SEED, never report -- SkillRiseWatch's own first-call rule. A
        // session that loads with rungs on the sheet did not earn them this
        // step, and the plate is for the step they are earned on.
        for (std::size_t i = 0; i < sim::kLegendTracks; ++i) {
            rungs_[i] = rows[i].rung;
        }
        seeded_ = true;
        return rises;
    }
    for (std::size_t i = 0; i < sim::kLegendTracks; ++i) {
        const std::int32_t was = rungs_[i];
        const std::int32_t now = rows[i].rung;
        rungs_[i] = now;
        if (now > was) {
            rises.push_back(LegendRise{rows[i].track, was, now});
        }
    }
    return rises;
}

// ---------------------------------------------------------------------------
// the keys and the text
// ---------------------------------------------------------------------------

std::string_view legendTrackKey(sim::LegendTrack track) noexcept {
    switch (track) {
        case sim::LegendTrack::Wire:
            return "wire";
        case sim::LegendTrack::Roofs:
            return "roofs";
        case sim::LegendTrack::Flame:
            return "flame";
        case sim::LegendTrack::Trade:
            return "trade";
        case sim::LegendTrack::Law:
            return "law";
    }
    return "wire";
}

bool legendTrackFromKey(std::string_view word, sim::LegendTrack& out) noexcept {
    std::string folded;
    folded.reserve(word.size());
    for (const char c : word) {
        folded.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    for (std::size_t i = 0; i < sim::kLegendTracks; ++i) {
        const auto track = static_cast<sim::LegendTrack>(i);
        if (folded == legendTrackKey(track)) {
            out = track;
            return true;
        }
    }
    return false;
}

std::string legendRungKey(sim::LegendTrack track, std::int32_t rung) {
    return "legend." + std::string(legendTrackKey(track)) + "." + std::to_string(rung);
}

std::string rungPlateHead(const LegendRise& rise) {
    // legend.cpp's own two tables, joined the way the sheet joins them -- the
    // project's dash between two cells of air. Neither word is chosen here.
    return std::string(sim::legendTrackName(rise.track)) + "  --  " +
           std::string(sim::legendTitle(rise.track, rise.to));
}

RungPlateText rungPlateFor(const LegendRise& rise, const sim::BarkTables& barks) {
    RungPlateText out;
    out.head = rungPlateHead(rise);
    // The FIRST row of the key, never rotated: a rung is taken once, and one
    // authored sentence is what it says. A key nobody authored is an empty
    // row -- the plate still names the rung, and it never invents the prose.
    out.prose = std::string(barks.line(legendRungKey(rise.track, rise.to), 0));
    if (rise.to >= sim::kLegendRungs) {
        out.top = std::string(barks.line(kLegendTopKey, 0));
    }
    return out;
}

// ---------------------------------------------------------------------------
// the plate
// ---------------------------------------------------------------------------

namespace {

/// The plate's grid, resolved once for both the geometry and the draw, so the
/// rectangle rungPlateBox promises is the rectangle drawRungPlate paints --
/// hud.cpp's aimBox discipline: one arithmetic, two callers.
struct PlateLayout {
    PanelMetric metric;
    bool draws = false;
    /// Outer cells wide, borders included.
    int cells = 0;
    /// Content rows used: the head, then the prose and the top row when present.
    int rows = 0;
    int headCells = 0;
    int proseCells = 0;
    int topCells = 0;
};

[[nodiscard]] PlateLayout layoutFor(int width, int height, const RungPlateState& state) {
    PlateLayout out;
    out.metric = panelMetric(height);
    if (state.head.empty()) {
        return out;
    }
    out.headCells = static_cast<int>(state.head.size());
    out.proseCells = static_cast<int>(state.prose.size());
    out.topCells = static_cast<int>(state.top.size());
    out.rows = 1 + (state.prose.empty() ? 0 : 1) + (state.top.empty() ? 0 : 1);
    // SIZED TO ITS CONTENT (the spec's "size to content where content is
    // static"): the widest row, where the head wears a cell of the accent
    // fill either side of its words; a cell of padding inside each edge; the
    // two border cells. NEVER PAST THE WINDOW less a cell of margin a side --
    // a row wider than that is clipped by the cell drawer's own mark, which
    // the authored sheet is pinned never to need at 320x180.
    const int widest = std::max(out.headCells + 2, std::max(out.proseCells, out.topCells));
    const int room = out.metric.cellsIn(width) - 2;
    out.cells = std::min(widest + 4, room);
    if (out.cells < 6) {
        return out;
    }
    out.draws = true;
    return out;
}

}  // namespace

RungPlateBox rungPlateBox(int width, int height, const RungPlateState& state) {
    RungPlateBox box;
    const PlateLayout layout = layoutFor(width, height, state);
    if (!layout.draws) {
        return box;
    }
    box.w = layout.metric.widthOf(layout.cells);
    box.h = layout.metric.heightOf(layout.rows + 2);
    box.x = (width - box.w) / 2;
    // SEATED HIGH, NOT CENTRED. A quarter of the spare above it: under the
    // compass band and its announce plates, and clear of the reticle at the
    // exact centre and the aim prompt that hangs up and to the right of it
    // (hud.cpp's aimBox) -- the plate must never sit on the thing the player
    // is looking at, and never on the words that name it.
    box.y = ((height - box.h) * kRungPlateSeat) / 100;
    box.draws = true;
    return box;
}

void drawRungPlate(Framebuffer& target, const RungPlateState& state) {
    const float fade = std::clamp(state.fade, 0.0F, 1.0F);
    if (fade <= 0.0F) {
        return;
    }
    const int width = target.width();
    const int height = target.height();
    const PlateLayout layout = layoutFor(width, height, state);
    const RungPlateBox box = rungPlateBox(width, height, state);
    if (!layout.draws || !box.draws) {
        return;
    }
    const PanelMetric& metric = layout.metric;
    const PanelInk& ink = panelInk();
    // THE RISE: the announce plates' own lift, three scale units, through the
    // seat and on out -- so the same continuous value drives the alpha and
    // the offset and the plate can never be caught bright and mid-slide.
    const int lift = 3 * hudScale(height);
    const float drift = std::clamp(state.drift, -1.0F, 1.0F);
    const int y = box.y - static_cast<int>(std::round(drift * static_cast<float>(lift)));

    PanelRect bounds;
    bounds.x = box.x;
    bounds.y = y;
    bounds.w = box.w;
    bounds.h = box.h;
    FrameStyle style;
    // The heavier junction, the demo card's own: a corner, not a crossing.
    style.junction = Motif::Diamond;
    style.alpha = fade;
    // NEARLY OPAQUE, like every composed page: a card the ward shows through
    // is a card nobody reads. No stipple -- three rows have no dead space.
    style.groundAlpha = kPageGroundAlpha;
    PanelFrame pane(target, bounds, metric, style);
    pane.draw();
    const PanelRect body = pane.interior();
    const int interior = layout.cells - 2;

    // THE HEAD, knocked out of an inverted fill in the body accent -- this
    // build's one idiom for emphasis (the reference's own), never a second
    // glyph size. Centred on the interior, a cell of fill either side.
    const int fillCells = std::min(interior, layout.headCells + 2);
    const int fillCell = std::max(0, (interior - fillCells) / 2);
    drawInvertedFill(target, body, metric, fillCell, 0, fillCells, ink.accent, fade);
    (void)drawCellTextKnockout(target, body, metric, fillCell + 1, 0, state.head, ink.knockout,
                               fade);
    int row = 1;
    if (!state.prose.empty()) {
        // What the ward says, in the prose ink, centred under the name.
        (void)drawCellText(target, body, metric,
                           std::max(0, (interior - layout.proseCells) / 2), row, state.prose,
                           ink.prose, fade);
        ++row;
    }
    if (!state.top.empty()) {
        // The top of the ladder: a dateline's role, dim, read last.
        (void)drawCellText(target, body, metric, std::max(0, (interior - layout.topCells) / 2),
                           row, state.top, ink.dim, fade);
        ++row;
    }
}

}  // namespace granadad::render
