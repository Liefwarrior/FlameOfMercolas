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
    /// Content rows used: the head, a blank leading row, then the prose and
    /// the top row when present.
    int rows = 0;
    int headCells = 0;
    int proseCells = 0;
    int topCells = 0;
    /// Zero when the state carries no tick OR the frame has no row to spend
    /// on it (320x180): the row is dropped, never squeezed.
    int tickCells = 0;
};

[[nodiscard]] PlateLayout layoutFor(int width, int height, const RungPlateState& state) {
    PlateLayout out;
    out.metric = rungPlateMetric(height);
    if (state.head.empty()) {
        return out;
    }
    out.headCells = static_cast<int>(state.head.size());
    out.proseCells = static_cast<int>(state.prose.size());
    out.topCells = static_cast<int>(state.top.size());
    // ONE CELL ROW OF LEADING between the knocked-out head and the prose --
    // the critic's finding: zero air let the prose's caps touch the head
    // block's own underside. Spent only when there is a prose row to leave
    // air above; the top row, when a track is capped, sits directly under
    // the prose with none of its own, the dateline's own proximity.
    const bool hasProse = !state.prose.empty();
    out.tickCells = static_cast<int>(state.tick.size());
    out.rows = 1 + (hasProse ? 2 : 0) + (state.top.empty() ? 0 : 1) + (out.tickCells > 0 ? 1 : 0);
    // THE TICK ROW IS SPENT ONLY WHERE THERE IS A ROW TO SPEND. Between the
    // plate's floor (kRungPlateFloorRows) and the aim prompt's fence the
    // smallest window has room for the card and nothing else; a tick there
    // would put the keyline on the stat stack or the prompt on the card. So
    // it is dropped, whole, at 320x180, and at 1920x1080 for a topped-out
    // track whose card is already a row taller (640x360 and 960x540 hold
    // both), and the Character tile has the level. A row of clearance is
    // what it asks for; the seat then splits what is left.
    if (out.tickCells > 0) {
        const int floorY = kRungPlateFloorRows * hudScale(height);
        const CentreRect fence = hudAimRect(width, height);
        if (floorY + out.metric.heightOf(out.rows + 2) + out.metric.cellH() > fence.y0) {
            out.tickCells = 0;
            --out.rows;
        }
    }
    // SIZED TO ITS CONTENT (the spec's "size to content where content is
    // static"): the widest row, where the head wears a cell of the accent
    // fill either side of its words; TWO CELLS OF GUTTER inside each edge
    // (kRungPlateGutterCells -- one was the critic's finding, a row all but
    // touching its keyline); the two border cells. NEVER PAST THE WINDOW less
    // a cell of margin a side -- a row wider than that is clipped by the cell
    // drawer's own mark, which the authored sheet is pinned never to need at
    // 320x180: 56 glyphs, two gutters, two borders is the 64-cell frame less
    // its margin exactly.
    const int widest = std::max({out.headCells + 2, out.proseCells, out.topCells, out.tickCells});
    const int room = out.metric.cellsIn(width) - 2;
    out.cells = std::min(widest + 2 + 2 * kRungPlateGutterCells, room);
    if (out.cells < 6) {
        return out;
    }
    out.draws = true;
    return out;
}

}  // namespace

PanelMetric rungPlateMetric(int height) noexcept {
    const PanelMetric minor = panelMetric(height);
    const int glance = hudScale(height);
    if (minor.scale == 1 && glance >= 2) {
        return PanelMetric{glance};
    }
    return minor;
}

float alertWeightUnderRungPlate(float plateFade) noexcept {
    const float fade = std::clamp(plateFade, 0.0F, 1.0F);
    return 1.0F - (1.0F - kRungPlateAlertWeight) * fade;
}

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
    //
    // COLLISION CLEARANCE, one more cell row of it: the quarter-height seat
    // alone could still land close enough under a `NEW LEAD` announce plate
    // that the two keylines all but touched, reading as one pile of notices
    // rather than two. CLAMPED against hudAimRect() so that push never buys
    // its clearance at the reticle's own expense -- the one fence every
    // other HUD element already answers to, and the plate would rather sit
    // closer to the row above it than land on the thing the player is
    // looking at.
    const int rawSeatY = ((height - box.h) * kRungPlateSeat) / 100 + layout.metric.cellH();
    const CentreRect fence = hudAimRect(width, height);
    // A CELL ROW OF CLEARANCE above the prompt wherever the frame has one to
    // spend (the director's finding: at 1920x1080 the slab stopped 23 px
    // over the prompt, under one text row; at 320x180 two pixels). Between
    // the floor and the fence less the card: room for two rows or more, and
    // the quarter seat rides inside that band with a row kept clear under
    // it; less than that, and what there is splits evenly -- the smallest
    // window has seven pixels to share between the stat stack above and the
    // prompt below, and the card sits in the middle of them.
    const int floorY = kRungPlateFloorRows * hudScale(height);
    const int ceilY = fence.y0 - box.h;
    const int cellH = layout.metric.cellH();
    if (ceilY - floorY >= 2 * cellH) {
        box.y = std::clamp(rawSeatY, floorY, ceilY - cellH);
    } else {
        // NO ROW TO SPEND: the card keeps at least five glance units
        // between its keyline and whatever is above it, and splits the
        // rest. The fourth pass split evenly, and at 320x180 -- where the
        // top-right stat stack's fourth row reaches two rows UNDER this
        // floor -- that put the keyline two pixels under the stack's ink
        // while the aim prompt below had four to spare; the prompt
        // tolerates a tight neighbour, a keyline touching text does not
        // (the director's finding). Elsewhere the band is wider and the
        // split is what shows.
        const int room = std::max(0, ceilY - floorY);
        box.y = floorY + std::max(room / 2, std::min(room, 5 * hudScale(height)));
    }
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
    // THE RISE AND THE LIFT: through the seat and on out, so the same
    // continuous value drives the alpha and the offset and the plate can
    // never be caught bright and mid-slide. Twice the announce plates' three
    // units, over a lift three times as long (kRungPlateLiftSteps): a card
    // that held five seconds leaves, it is not switched off.
    const int lift = kRungPlateLiftUnits * hudScale(height);
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
    // THE SLAB IS DRAWN HERE, SYMMETRIC ABOUT THE KEYLINE, and the frame's
    // own ground is turned off. The panel vocabulary's border cells put the
    // rule's ink on the third of seven rows, so a page's slab carries two
    // units above its top rule and four below its bottom one, and a diamond
    // or a dash starts on the border cell's first column and ends a unit
    // short of its last -- a composed page sits on a dark frame and nobody
    // sees it; on a bright wall (the trade drive's) the director saw a black
    // card with its keyline off centre. TWO UNITS OF SLAB outside the rules'
    // dashes above and below and outside the side bars' column left and
    // right, the same on each pair of edges; the corner diamonds, a column
    // wider than the bars and a row taller than the dashes, sit one unit
    // inside that on every side, and a tilde's second row sits one closer
    // below. (The dash is the font's own, row two of six; the bar is
    // panel.cpp's kBar, column one of four; the diamond rows one to three
    // and columns nought to three.)
    style.groundAlpha = 0.0F;
    const int s = metric.scale;
    target.fillRect(bounds.x - s, bounds.y, bounds.w, bounds.h - 2 * s, style.ground,
                    kPageGroundAlpha * fade);
    PanelFrame pane(target, bounds, metric, style);
    pane.draw();
    const PanelRect body = pane.interior();
    const int interior = layout.cells - 2;

    // THE HEAD, knocked out of an inverted fill in the body accent -- this
    // build's one idiom for emphasis (the reference's own), never a second
    // glyph size. Centred on the interior, a cell of fill either side. THE
    // BLOCK IS DRAWN BY HAND rather than by drawInvertedFill, for two units
    // it cannot give: ONE UNIT OF GOLD ABOVE THE CAPS (the fill starts a
    // unit into the border cell's empty last row, so the caps no longer sit
    // on the block's own top edge with a whole row of gold under their
    // feet), and the font's trailing advance column TRIMMED off the block's
    // right end, so the pad after the last letter is the pad before the
    // first. The director's finding, at every size.
    const int fillCells = std::min(interior, layout.headCells + 2);
    const int fillCell = std::max(0, (interior - fillCells) / 2);
    target.fillRect(body.x + metric.widthOf(fillCell), body.y - s, metric.widthOf(fillCells) - s,
                    metric.cellH() + s, ink.accent, fade);
    (void)drawCellTextKnockout(target, body, metric, fillCell + 1, 0, state.head, ink.knockout,
                               fade);
    int row = 1;
    if (!state.prose.empty()) {
        // ONE CELL ROW OF LEADING, no ink, between the head and the prose --
        // layoutFor's own extra row, spent here as air rather than as a
        // second copy of the head's row.
        ++row;
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
    if (layout.tickCells > 0) {
        // THE TICK, folded in: the skill that levelled on the rung's own
        // step, in the dateline's ink and slot -- the game's word under the
        // ward's, told once. layoutFor has already dropped it where the
        // frame has no row for it. SET TWO UNITS LOW IN ITS ROW: set solid
        // under the prose it read as the docker's second line in the
        // system's voice (both critics); the row's own spare unit and two
        // more make three of air above it, and the bottom rule's ink is
        // still two units under its feet.
        PanelRect tickBody = body;
        tickBody.y += 2 * s;
        (void)drawCellText(target, tickBody, metric,
                           std::max(0, (interior - layout.tickCells) / 2), row, state.tick,
                           ink.dim, fade);
        ++row;
    }
}

}  // namespace granadad::render
