#include "granadad/render/panel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

#include "granadad/render/dialogue_view.hpp"
#include "granadad/render/hud.hpp"

namespace granadad::render {

namespace {

// THE CELL, AND WHY THE NUMBERS ARE HERE RATHER THAN INCLUDED.
//
// hud.cpp's font is 4 pixels wide on a 5-pixel advance, 6 rows tall, with one
// row of drop shadow under it -- so a cell is 5 x 7 at scale 1. Those constants
// live in hud.cpp's anonymous namespace and are not exported, and exporting
// them would have been a change to the font's own file, which this pass does
// not make (see panel.hpp's header on why).
//
// So they are restated here and PINNED BY A CASE rather than by a comment:
// test_panel.cpp's "the panel grid is the font's own grid" asks textWidth() and
// hudMinorScale() what they think and fails if this file ever disagrees with
// them. A restated constant that nothing checks is a bug waiting; a restated
// constant with a case over it is a contract.
constexpr int kAdvance = 5;
constexpr int kGlyphRows = 6;
constexpr int kRowRows = 7;
static_assert(kRowRows == kGlyphRows + 1, "a row is the glyph plus its drop shadow");
static_assert(kAdvance == 5, "the cell is the font's own advance");

constexpr Rgb kShadow{0.02F, 0.02F, 0.03F};

/// THE SEVEN GLYPHS THE GRAMMAR NEEDS AND THE FONT DOES NOT HAVE.
///
/// Same 4x6 cell, same bit order (bit 3 leftmost), same everything as
/// hud.cpp's kGlyphs -- these are simply not in it, and putting them in it
/// would have been editing the font the owner asked to be left alone.
///
/// `|` versus the font's own `!` is the whole texture: the bang has a gap at
/// row 4 and the bar does not, so a border alternating them flickers by exactly
/// one pixel per row. That is what reads as a terminal rather than as a modern
/// hard border.
///
/// `~` puts its ink on rows 2 and 3 -- the rows the font's own `-` occupies --
/// so a rule alternating the two reads as one continuous line with texture in
/// it rather than as two lines at different heights.
struct MotifGlyph {
    std::array<std::uint8_t, kGlyphRows> rows;
};

constexpr MotifGlyph kTilde{{0x0, 0x0, 0xC, 0x3, 0x0, 0x0}};
constexpr MotifGlyph kBar{{0x4, 0x4, 0x4, 0x4, 0x4, 0x4}};
constexpr MotifGlyph kDiamond{{0x0, 0x6, 0xF, 0x6, 0x0, 0x0}};
constexpr MotifGlyph kDot{{0x0, 0x0, 0x6, 0x6, 0x0, 0x0}};
constexpr MotifGlyph kRing{{0x0, 0x6, 0x9, 0x6, 0x0, 0x0}};

void blitMotif(Framebuffer& target, int x, int y, const MotifGlyph& glyph, const Rgb& colour,
               float alpha, int scale) {
    for (int row = 0; row < kGlyphRows; ++row) {
        for (int col = 0; col < 4; ++col) {
            const bool on = (glyph.rows[static_cast<std::size_t>(row)] >> (3 - col)) & 1U;
            if (!on) {
                continue;
            }
            // The identical one-pixel drop shadow drawText gives every glyph,
            // in the identical order (shadow first, ink over it), so a motif
            // glyph and a font glyph side by side are indistinguishable in
            // treatment.
            target.fillRect(x + col * scale + scale, y + row * scale + scale, scale, scale, kShadow,
                            alpha * 0.75F);
            target.fillRect(x + col * scale, y + row * scale, scale, scale, colour, alpha);
        }
    }
}

/// Uppercased, because the font draws lowercase as uppercase anyway and every
/// surface in this build prints in caps. Doing it here means a caller can pass
/// authored prose straight through.
[[nodiscard]] std::string shout(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

/// Width of a string in whole cells. One glyph is one cell.
[[nodiscard]] int cellsOf(std::string_view text) noexcept {
    return static_cast<int>(text.size());
}

const PanelInk kInk{};

}  // namespace

// ---------------------------------------------------------------------------
// grid and geometry
// ---------------------------------------------------------------------------

int PanelMetric::cellsIn(int pixels) const noexcept {
    return pixels <= 0 ? 0 : pixels / cellW();
}

int PanelMetric::rowsIn(int pixels) const noexcept {
    return pixels <= 0 ? 0 : pixels / cellH();
}

PanelMetric panelMetric(int frameHeight) noexcept {
    return PanelMetric{std::max(1, hudMinorScale(std::max(1, frameHeight)))};
}

PanelRect PanelRect::inset(int dx, int dy) const noexcept {
    PanelRect out{x + dx, y + dy, w - 2 * dx, h - 2 * dy};
    if (out.w < 0) {
        out.w = 0;
    }
    if (out.h < 0) {
        out.h = 0;
    }
    return out;
}

namespace {

/// The one arithmetic both splits share: hand out `total` grid units among
/// spans, fixed first and the remainder by weight, with what does not divide
/// evenly going to the LAST weighted span so the answer is deterministic.
[[nodiscard]] std::vector<int> shareUnits(const std::vector<Span>& spans, int total) {
    std::vector<int> out(spans.size(), 0);
    if (spans.empty() || total <= 0) {
        return out;
    }
    int fixed = 0;
    int weightTotal = 0;
    for (const Span& span : spans) {
        fixed += std::max(0, span.cells);
        weightTotal += std::max(0, span.weight);
    }
    // A composition that asks for more fixed units than the window has gets
    // them trimmed from the END rather than silently overlapping: the last
    // bands to be declared are the first to go, which is the same priority a
    // caller wrote them in.
    int remaining = total;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const int want = std::max(0, spans[i].cells);
        const int give = std::min(want, std::max(0, remaining));
        out[i] = give;
        remaining -= give;
    }
    if (weightTotal <= 0 || remaining <= 0) {
        return out;
    }
    std::size_t last = spans.size();
    int handed = 0;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const int weight = std::max(0, spans[i].weight);
        if (weight <= 0) {
            continue;
        }
        const int give = remaining * weight / weightTotal;
        out[i] += give;
        handed += give;
        last = i;
    }
    if (last < spans.size()) {
        out[last] += remaining - handed;
    }
    return out;
}

}  // namespace

std::vector<PanelRect> splitRows(const PanelRect& bounds, const PanelMetric& metric,
                                 const std::vector<Span>& spans) {
    const std::vector<int> units = shareUnits(spans, metric.rowsIn(bounds.h));
    std::vector<PanelRect> out;
    out.reserve(spans.size());
    int y = bounds.y;
    for (const int rows : units) {
        out.push_back(PanelRect{bounds.x, y, bounds.w, metric.heightOf(rows)});
        y += metric.heightOf(rows);
    }
    return out;
}

std::vector<PanelRect> splitColumns(const PanelRect& bounds, const PanelMetric& metric,
                                    const std::vector<Span>& spans) {
    const std::vector<int> units = shareUnits(spans, metric.cellsIn(bounds.w));
    std::vector<PanelRect> out;
    out.reserve(spans.size());
    int x = bounds.x;
    for (const int cells : units) {
        out.push_back(PanelRect{x, bounds.y, metric.widthOf(cells), bounds.h});
        x += metric.widthOf(cells);
    }
    return out;
}

// ---------------------------------------------------------------------------
// colour roles
// ---------------------------------------------------------------------------

const PanelInk& panelInk() noexcept { return kInk; }

Rgb inkFor(InkRole role) noexcept {
    switch (role) {
        case InkRole::Dim:
            return kInk.dim;
        case InkRole::Key:
            return kInk.key;
        case InkRole::Number:
            return kInk.number;
        case InkRole::Accent:
            return kInk.accent;
        case InkRole::Rule:
            return kInk.rule;
        case InkRole::Prose:
        default:
            return kInk.prose;
    }
}

// ---------------------------------------------------------------------------
// the motif glyphs
// ---------------------------------------------------------------------------

void drawMotif(Framebuffer& target, int x, int y, Motif motif, const Rgb& colour, float alpha,
               int scale) {
    if (alpha <= 0.0F || scale <= 0) {
        return;
    }
    switch (motif) {
        // THE TWO THE FONT ALREADY HAS go through the font, so there is exactly
        // one `!` and one `+` in this build and they cannot drift apart.
        case Motif::Bang:
            drawText(target, x, y, "!", colour, alpha, scale);
            return;
        case Motif::Plus:
            drawText(target, x, y, "+", colour, alpha, scale);
            return;
        case Motif::Tilde:
            blitMotif(target, x, y, kTilde, colour, alpha, scale);
            return;
        case Motif::Bar:
            blitMotif(target, x, y, kBar, colour, alpha, scale);
            return;
        case Motif::Diamond:
            blitMotif(target, x, y, kDiamond, colour, alpha, scale);
            return;
        case Motif::Dot:
            blitMotif(target, x, y, kDot, colour, alpha, scale);
            return;
        case Motif::Ring:
            blitMotif(target, x, y, kRing, colour, alpha, scale);
            return;
    }
}

void drawRuleRun(Framebuffer& target, int x, int y, int cells, const PanelMetric& metric,
                 Motif junction, const Rgb& colour, float alpha,
                 const std::vector<int>& interiorJunctions) {
    if (alpha <= 0.0F || cells <= 0) {
        return;
    }
    for (int i = 0; i < cells; ++i) {
        const int cx = x + i * metric.cellW();
        const bool isEnd = i == 0 || i == cells - 1;
        const bool isJoin =
            isEnd || std::find(interiorJunctions.begin(), interiorJunctions.end(), i) !=
                         interiorJunctions.end();
        if (isJoin) {
            drawMotif(target, cx, y, junction, colour, alpha, metric.scale);
        } else if ((i % 2) == 1) {
            drawMotif(target, cx, y, Motif::Tilde, colour, alpha, metric.scale);
        } else {
            drawText(target, cx, y, "-", colour, alpha, metric.scale);
        }
    }
}

void drawStipple(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                 const Rgb& colour, float alpha) {
    if (alpha <= 0.0F || rect.empty()) {
        return;
    }
    const int cells = metric.cellsIn(rect.w);
    const int rows = metric.rowsIn(rect.h);
    for (int row = 0; row < rows; ++row) {
        for (int cell = 0; cell < cells; ++cell) {
            // Deterministic from the cell's own coordinates -- no RNG, no
            // frame counter -- so the field is identical every frame and a
            // capture is reproducible. A stipple that shimmers is worse than
            // no stipple.
            const int h = (cell * 7 + row * 13) % 11;
            if (h != 0 && h != 5) {
                continue;
            }
            drawText(target, rect.x + cell * metric.cellW(), rect.y + row * metric.cellH(),
                     h == 0 ? "." : "'", colour, alpha, metric.scale);
        }
    }
}

// ---------------------------------------------------------------------------
// the frame
// ---------------------------------------------------------------------------

PanelFrame::PanelFrame(Framebuffer& target, const PanelRect& bounds, const PanelMetric& metric,
                       const FrameStyle& style)
    : target_(&target), bounds_(bounds), metric_(metric), style_(style) {
    cellsWide_ = metric_.cellsIn(bounds_.w);
    const int rowsTall = metric_.rowsIn(bounds_.h);
    // The border spends the outer row top and bottom and the outer cell left
    // and right. A frame too small to hold a border AND a content row has no
    // interior at all, and says so rather than producing a negative one.
    rows_ = std::max(0, rowsTall - 2);
    const int interiorCells = std::max(0, cellsWide_ - 2);
    interior_ = PanelRect{bounds_.x + metric_.cellW(), bounds_.y + metric_.cellH(),
                          metric_.widthOf(interiorCells), metric_.heightOf(rows_)};
}

void PanelFrame::addRule(int r) {
    if (r < 0 || r >= rows_) {
        return;
    }
    rules_.push_back(r);
}

void PanelFrame::addDivider(int c, int first, int count) {
    if (c < 0 || count <= 0) {
        return;
    }
    dividers_.push_back(Divider{c, std::max(0, first), count});
}

int PanelFrame::rowY(int r) const noexcept { return interior_.y + r * metric_.cellH(); }

PanelRect PanelFrame::band(int first, int count) const {
    const int start = std::clamp(first, 0, rows_);
    const int end = std::clamp(first + count, start, rows_);
    return PanelRect{interior_.x, rowY(start), interior_.w, metric_.heightOf(end - start)};
}

Motif PanelFrame::edgeAt(int r) const noexcept {
    // The reference's first content row wears `!`, and the alternation runs
    // from there. rowPhase lets a nested frame start where its parent left off
    // rather than restarting the flicker mid-screen.
    return ((r + style_.rowPhase) % 2) == 0 ? Motif::Bang : Motif::Bar;
}

void PanelFrame::draw() {
    if (style_.alpha <= 0.0F || cellsWide_ < 2 || rows_ <= 0) {
        return;
    }
    Framebuffer& target = *target_;
    const int cw = metric_.cellW();

    target.fillRect(bounds_.x, bounds_.y, metric_.widthOf(cellsWide_),
                    metric_.heightOf(rows_ + 2), style_.ground,
                    style_.groundAlpha * style_.alpha);
    if (style_.stipple) {
        drawStipple(target, interior_, metric_, style_.rule, 0.28F * style_.alpha);
    }

    // Which interior cells a rule at content row `r` should wear a junction on:
    // every divider that runs into it from above or below.
    const auto junctionsFor = [this](int r) {
        std::vector<int> out;
        for (const Divider& d : dividers_) {
            const int last = d.first + d.count - 1;
            if (d.first == r + 1 || last == r - 1) {
                out.push_back(d.cell + 1);
            }
        }
        return out;
    };
    // The two border rules sit one row outside the content, so a divider that
    // starts at content row 0 meets the top rule and one that ends at the last
    // content row meets the bottom rule.
    drawRuleRun(target, bounds_.x, bounds_.y, cellsWide_, metric_, style_.junction, style_.rule,
                style_.alpha, junctionsFor(-1));
    drawRuleRun(target, bounds_.x, rowY(rows_), cellsWide_, metric_, style_.junction, style_.rule,
                style_.alpha, junctionsFor(rows_));

    for (const int r : rules_) {
        drawRuleRun(target, bounds_.x, rowY(r), cellsWide_, metric_, style_.junction, style_.rule,
                    style_.alpha, junctionsFor(r));
    }

    const auto isRuleRow = [this](int r) {
        return std::find(rules_.begin(), rules_.end(), r) != rules_.end();
    };
    const int rightX = bounds_.x + (cellsWide_ - 1) * cw;
    for (int r = 0; r < rows_; ++r) {
        if (isRuleRow(r)) {
            continue;
        }
        const Motif edge = edgeAt(r);
        const int y = rowY(r);
        drawMotif(target, bounds_.x, y, edge, style_.rule, style_.alpha, metric_.scale);
        drawMotif(target, rightX, y, edge, style_.rule, style_.alpha, metric_.scale);
    }

    for (const Divider& d : dividers_) {
        const int x = interior_.x + d.cell * cw;
        for (int r = d.first; r < d.first + d.count && r < rows_; ++r) {
            if (isRuleRow(r)) {
                continue;
            }
            drawMotif(target, x, rowY(r), edgeAt(r), style_.rule, style_.alpha, metric_.scale);
        }
    }
}

// ---------------------------------------------------------------------------
// text on the grid
// ---------------------------------------------------------------------------

int drawCellText(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric, int cell,
                 int row, std::string_view text, const Rgb& colour, float alpha) {
    if (alpha <= 0.0F || text.empty() || rect.empty()) {
        return 0;
    }
    const int cells = metric.cellsIn(rect.w) - cell;
    if (cells <= 0 || row < 0 || row >= metric.rowsIn(rect.h) || cell < 0) {
        return 0;
    }
    // EVERY LINE GOES THROUGH THE CLIPPER, which is the rule hud.hpp's
    // clipToWidth already states: a line that runs off its pane is drawn cut
    // mid-glyph and reads as a rendering bug rather than as a line that was too
    // long. The pane knows its own width; this takes it.
    const std::string cut = clipToWidth(shout(text), metric.widthOf(cells), metric.scale);
    drawText(target, rect.x + cell * metric.cellW(), rect.y + row * metric.cellH(), cut, colour,
             alpha, metric.scale);
    return cellsOf(cut);
}

int drawCellTextRight(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                      int cellFromRight, int row, std::string_view text, const Rgb& colour,
                      float alpha) {
    const std::string shouted = shout(text);
    const int cells = metric.cellsIn(rect.w);
    const int start = cells - cellFromRight - cellsOf(shouted);
    if (start < 0) {
        return drawCellText(target, rect, metric, 0, row, shouted, colour, alpha);
    }
    return drawCellText(target, rect, metric, start, row, shouted, colour, alpha);
}

void drawInvertedFill(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                      int cell, int row, int cells, const Rgb& accent, float alpha) {
    if (alpha <= 0.0F || cells <= 0) {
        return;
    }
    const int room = metric.cellsIn(rect.w) - cell;
    const int wide = std::min(cells, room);
    if (wide <= 0 || row < 0 || row >= metric.rowsIn(rect.h)) {
        return;
    }
    // The fill covers the glyph box and the row of shadow under it, so a
    // knocked-out row is a solid block rather than a block with a dark seam
    // along the bottom of every glyph.
    target.fillRect(rect.x + cell * metric.cellW(), rect.y + row * metric.cellH(),
                    metric.widthOf(wide), metric.cellH(), accent, alpha);
}

int drawCellTextKnockout(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                         int cell, int row, std::string_view text, const Rgb& colour,
                         float alpha) {
    if (alpha <= 0.0F || text.empty() || rect.empty()) {
        return 0;
    }
    const int cells = metric.cellsIn(rect.w) - cell;
    if (cells <= 0 || row < 0 || row >= metric.rowsIn(rect.h) || cell < 0) {
        return 0;
    }
    const std::string cut = clipToWidth(shout(text), metric.widthOf(cells), metric.scale);
    if (cut.empty()) {
        return 0;
    }
    // The real font, the real drawText, into a scratch surface -- then only the
    // pixels the INK pass lit come back. See the header on why the shadow has
    // to go and why the font itself is not the place to fix it.
    const int scale = metric.scale;
    const int w = textWidth(cut, scale) + scale;
    const int h = kRowRows * scale;
    Framebuffer scratch(std::max(1, w), std::max(1, h));
    scratch.clear(Rgb{0.0F, 0.0F, 0.0F});
    drawText(scratch, 0, 0, cut, Rgb{1.0F, 1.0F, 1.0F}, 1.0F, scale);
    const int originX = rect.x + cell * metric.cellW();
    const int originY = rect.y + row * metric.cellH();
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            // The ink pass wrote white; the shadow pass wrote kShadow at 0.75
            // over black, which lands near 4/255. Half brightness separates them
            // with an enormous margin.
            if ((scratch.pixels()[scratch.index(px, py)] & 0xFFU) < 128U) {
                continue;
            }
            target.fillRect(originX + px, originY + py, 1, 1, colour, alpha);
        }
    }
    return cellsOf(cut);
}

// ---------------------------------------------------------------------------
// the breadcrumb / instruction header
// ---------------------------------------------------------------------------

int drawBreadcrumb(Framebuffer& target, const PanelRect& row, const PanelMetric& metric,
                   const std::vector<std::string>& crumbs, const Rgb& leafInk, float alpha) {
    if (alpha <= 0.0F || crumbs.empty() || row.empty()) {
        return 0;
    }
    const int cells = metric.cellsIn(row.w);
    const int maxRows = metric.rowsIn(row.h);
    if (cells <= 0 || maxRows <= 0) {
        return 0;
    }
    // A SINGLE CRUMB IS AN INSTRUCTION, not a path -- the reference's own
    // "Select a tile of the Pearl Lands to preach the word of Xaleon to". It
    // takes the leaf colour whole and wraps like the prose it is.
    if (crumbs.size() == 1) {
        const std::vector<std::string> lines =
            wrapText(shout(crumbs.front()), static_cast<std::size_t>(cells));
        int used = 0;
        for (const std::string& line : lines) {
            if (used >= maxRows) {
                break;
            }
            drawCellText(target, row, metric, 0, used, line, leafInk, alpha);
            ++used;
        }
        return used;
    }
    // A PATH. Dim ancestors, the leaf in the colour of what is being looked at,
    // slashes between. Laid out cell by cell so the leaf's colour change lands
    // exactly on the glyph and never a pixel out.
    int cell = 0;
    int used = 1;
    for (std::size_t i = 0; i < crumbs.size(); ++i) {
        const bool leaf = i + 1 == crumbs.size();
        const std::string text = shout(crumbs[i]);
        if (i > 0) {
            if (cell + 3 > cells) {
                break;
            }
            drawCellText(target, row, metric, cell, used - 1, " / ", kInk.dim, alpha);
            cell += 3;
        }
        if (cell >= cells) {
            break;
        }
        cell += drawCellText(target, row, metric, cell, used - 1, text, leaf ? leafInk : kInk.dim,
                             alpha);
    }
    return used;
}

// ---------------------------------------------------------------------------
// the tab row
// ---------------------------------------------------------------------------

void drawTabRow(Framebuffer& target, const PanelRect& row, const PanelMetric& metric,
                std::string_view title, const std::vector<PanelTab>& tabs, int current,
                std::string_view readout, const Rgb& accent, float alpha) {
    if (alpha <= 0.0F || row.empty()) {
        return;
    }
    const int cells = metric.cellsIn(row.w);
    if (cells <= 0) {
        return;
    }
    const std::string readoutText = shout(readout);
    const int readoutCells = readoutText.empty() ? 0 : cellsOf(readoutText);
    // The readout is right-aligned and is the LAST thing to be given up,
    // because it is the number the player is watching while they choose.
    if (readoutCells > 0) {
        // ONE CELL OF AIR BEFORE THE EDGE. Flush against the border, the last
        // glyph of the readout sits immediately left of the frame's own `|`/`!`
        // and the eye reads the two together -- a first capture of the creation
        // flow had a readout that said "NAMELESS!". The edge is furniture; it
        // must not be able to punctuate a sentence.
        drawCellTextRight(target, row, metric, 1, 0, readoutText, kInk.number, alpha);
    }
    const int roomForLeft = cells - (readoutCells > 0 ? readoutCells + 3 : 0);
    if (roomForLeft <= 0) {
        return;
    }

    // Cost each piece before drawing any of it, so nothing is drawn and then
    // discovered not to fit. The title goes first, then tabs left to right;
    // the CURRENT tab is never dropped.
    const std::string titleText = shout(title);
    struct Piece {
        std::string text;
        bool inverted = false;
    };
    std::vector<Piece> pieces;
    int want = 0;
    if (!titleText.empty()) {
        pieces.push_back(Piece{titleText, false});
        want += cellsOf(titleText) + 2;
    }
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        std::string text = shout(tabs[i].key);
        if (!tabs[i].name.empty()) {
            // A TAB WITH NO KEY IS ITS NAME AND NOTHING ELSE. The reference
            // prints `d - Dominions` because `d` is a key you can actually
            // press; a row that shows WHERE YOU ARE in a flow rather than a
            // view you may switch to has no key to promise, and printing
            // ` - ORIGIN` with a leading separator would be advertising one
            // that is not there. Same reason keys_page.cpp drops the hotkey
            // column off the bindings list: never draw an affordance that
            // does not exist.
            if (!text.empty()) {
                text += " - ";
            }
            text += shout(tabs[i].name);
        }
        pieces.push_back(Piece{text, static_cast<int>(i) == current});
        want += cellsOf(text) + 2;
    }
    // Drop siblings from the right while it does not fit; then, only if it
    // still does not, drop the title. The current tab survives both.
    while (want > roomForLeft && pieces.size() > 1) {
        std::size_t victim = pieces.size();
        for (std::size_t i = pieces.size(); i-- > 0;) {
            if (!pieces[i].inverted && !(i == 0 && !titleText.empty())) {
                victim = i;
                break;
            }
        }
        if (victim == pieces.size()) {
            victim = 0;
        }
        want -= cellsOf(pieces[victim].text) + 2;
        pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(victim));
    }

    int cell = 0;
    for (const Piece& piece : pieces) {
        const int wide = cellsOf(piece.text);
        if (cell + wide > roomForLeft) {
            break;
        }
        if (piece.inverted) {
            // THE CURRENT TAB IS AN INVERTED FILL. Not a bracket, not an
            // arrow. One cell of padding either side so the fill reads as a
            // block and not as a tight box round the letters.
            drawInvertedFill(target, row, metric, std::max(0, cell - 1), 0, wide + 2, accent,
                             alpha);
            drawCellTextKnockout(target, row, metric, cell, 0, piece.text, kInk.knockout, alpha);
        } else {
            drawCellText(target, row, metric, cell, 0, piece.text, kInk.key, alpha);
        }
        cell += wide + 2;
    }
}

// ---------------------------------------------------------------------------
// the numbered option list
// ---------------------------------------------------------------------------

namespace {

struct OptionWidths {
    int key = 0;
    int label = 0;
    int value = 0;
};

[[nodiscard]] OptionWidths measure(const std::vector<PanelOption>& options,
                                   const OptionListStyle& style) {
    OptionWidths out;
    for (const PanelOption& option : options) {
        if (style.showKeys) {
            out.key = std::max(out.key, cellsOf(option.key));
        }
        out.label = std::max(out.label, cellsOf(option.label));
        if (style.alignValues) {
            out.value = std::max(out.value, cellsOf(option.value));
        }
    }
    return out;
}

}  // namespace

OptionListPlan planOptionList(const std::vector<PanelOption>& options, const PanelRect& rect,
                              const PanelMetric& metric, const OptionListStyle& style) {
    OptionListPlan plan;
    const int cells = metric.cellsIn(rect.w);
    const int rows = metric.rowsIn(rect.h);
    plan.rows = std::max(0, style.minRows);
    if (cells <= 0 || rows <= 0) {
        return plan;
    }
    const OptionWidths widths = measure(options, style);
    // One cell between the key and the label, two between the label and the
    // value -- the same gutter the whole vocabulary uses, so a value column in
    // a list lines up with a value column in a facts block beside it.
    const int keyCells = widths.key > 0 ? widths.key + 1 : 0;
    const int valueCells = widths.value > 0 ? widths.value + 2 : 0;
    const int entryCells = keyCells + widths.label + valueCells;

    // COLUMN COUNT FOLLOWS CONTENT. How many entry-widths plus gutters fit
    // across, capped by maxColumns, by the number of entries, and by the
    // height (there is no point in four columns of one row each).
    const int wide = std::max(1, entryCells);
    int columns = 1;
    for (int candidate = 2; candidate <= std::max(1, style.maxColumns); ++candidate) {
        const int need = candidate * wide + (candidate - 1) * style.gutterCells;
        if (need > cells) {
            break;
        }
        columns = candidate;
    }
    const int count = static_cast<int>(options.size());
    if (count > 0) {
        columns = std::min(columns, count);
    }
    columns = std::max(1, columns);

    int perColumn = count <= 0 ? 0 : (count + columns - 1) / columns;
    // Having decided the columns, do not leave a ragged tail: with 10 entries
    // in 3 columns that is 4/4/2 rather than 4/4/4-with-two-empty, which is
    // the same thing, but it means the pane's height is honest.
    plan.rows = std::max(perColumn, std::max(0, style.minRows));
    if (plan.rows > rows) {
        plan.rows = rows;
    }
    // Says so out loud rather than silently dropping the tail: a caller with an
    // overflowed list wants a taller pane, a shorter list or a page turn, and
    // the only way it can know is if this tells it.
    plan.overflowed = columns * plan.rows < count;
    plan.columns = columns;
    const int gutters = (columns - 1) * style.gutterCells;
    const int share = std::max(1, (cells - gutters) / columns);
    // THE COLUMN IS AS WIDE AS ITS CONTENT, not as wide as its share of the
    // pane. A row's fill then hugs its own text and the value column sits a
    // gutter past the longest label -- rather than the value being flung to the
    // right margin of a wide pane with forty cells of nothing in front of it,
    // which is what the first capture of the converted controls page did at
    // 1920x1080.
    plan.columnCells = std::max(1, std::min(entryCells, share));
    // ...and the columns are then SPREAD, so the last one's content still ends
    // at the pane's right edge and the gutters between them are even.
    plan.stride = columns > 1 ? (cells - plan.columnCells) / (columns - 1) : 0;
    plan.keyCells = keyCells;
    plan.valueCells = std::min(valueCells, std::max(0, plan.columnCells - keyCells - 1));
    plan.labelCells = std::max(0, plan.columnCells - keyCells - plan.valueCells);
    return plan;
}

void drawOptionList(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                    const std::vector<PanelOption>& options, int selected,
                    const OptionListStyle& style, float alpha) {
    drawOptionListPlanned(target, rect, metric, options, selected,
                          planOptionList(options, rect, metric, style), alpha);
}

void drawOptionListPlanned(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                           const std::vector<PanelOption>& options, int selected,
                           const OptionListPlan& plan, float alpha) {
    if (alpha <= 0.0F || rect.empty() || options.empty()) {
        return;
    }
    if (plan.rows <= 0 || plan.columns <= 0) {
        return;
    }
    const int count = static_cast<int>(options.size());
    // THE COLUMN STRIDE IS THE PANE'S HELD HEIGHT, not ceil(count/columns).
    // That is what keeps the geometry still: a list that loses entries -- a
    // page of it, a filter, a row that got acquired -- keeps every remaining
    // row exactly where it was instead of resettling into shorter columns.
    const int stride = plan.rows;
    for (int i = 0; i < count; ++i) {
        // COLUMN-MAJOR: the printed keys read DOWN each column, so the
        // numbering runs continuously across the layout and the selection
        // model underneath stays one flat list.
        const int column = i / stride;
        const int row = i % stride;
        if (column >= plan.columns || row >= plan.rows) {
            break;
        }
        const PanelOption& option = options[static_cast<std::size_t>(i)];
        const int cell = column * plan.stride;
        const bool picked = i == selected && option.selectable;

        if (picked) {
            // SELECTION IS AN INVERTED FILL IN THE ENTITY'S OWN ACCENT. Not an
            // arrow, not a bracket. It spans the whole column width including
            // the value, so an affordable row and its price light up together.
            drawInvertedFill(target, rect, metric, cell, row, plan.columnCells, option.accent,
                             alpha);
        }
        int at = cell;
        if (plan.keyCells > 0) {
            if (picked) {
                drawCellTextKnockout(target, rect, metric, at, row, option.key, kInk.knockout,
                                     alpha);
            } else {
                drawCellText(target, rect, metric, at, row, option.key, kInk.key, alpha);
            }
            at += plan.keyCells;
        }
        const std::string label = clipToWidth(shout(option.label), metric.widthOf(plan.labelCells),
                                              metric.scale);
        if (picked) {
            drawCellTextKnockout(target, rect, metric, at, row, label, kInk.knockout, alpha);
        } else {
            const Rgb labelInk = option.labelTakesAccent
                                     ? option.accent
                                     : (option.selectable ? kInk.prose : kInk.dim);
            drawCellText(target, rect, metric, at, row, label, labelInk, alpha);
        }
        if (!option.value.empty() && plan.valueCells > 0) {
            // THE COMMON VALUE COLUMN. Right-aligned inside the column so
            // every price, key or state label in the list shares one edge --
            // and so a row that DROPS its value (because it is now owned)
            // leaves the column exactly where it was for everybody else.
            const std::string value = shout(option.value);
            const int start = std::max(at, cell + plan.columnCells - cellsOf(value));
            if (picked) {
                drawCellTextKnockout(target, rect, metric, start, row, value, kInk.knockout,
                                     alpha);
            } else {
                drawCellText(target, rect, metric, start, row, value, inkFor(option.valueInk),
                             alpha);
            }
        }
    }
}

int optionListAt(const PanelRect& rect, const PanelMetric& metric, const OptionListPlan& plan,
                 int count, int px, int py) noexcept {
    if (rect.empty() || plan.rows <= 0 || plan.columns <= 0 || count <= 0) {
        return -1;
    }
    // THE INVERSE OF drawOptionListPlanned, and deliberately written as the
    // same walk rather than as arithmetic solved backwards: solved backwards it
    // would be a second description of the layout, and a second description is
    // a thing that can drift. Walking the entries means the only way this can
    // disagree with what was drawn is if the loop above changes and this one
    // does not, which is one file and one review away rather than two.
    const int stride = plan.rows;
    for (int i = 0; i < count; ++i) {
        const int column = i / stride;
        const int row = i % stride;
        if (column >= plan.columns || row >= plan.rows) {
            break;
        }
        const int x0 = rect.x + metric.widthOf(column * plan.stride);
        const int y0 = rect.y + metric.heightOf(row);
        if (px >= x0 && px < x0 + metric.widthOf(plan.columnCells) && py >= y0 &&
            py < y0 + metric.cellH()) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// the block list
// ---------------------------------------------------------------------------

std::vector<OptionBlock> planOptionBlocks(const std::vector<PanelOption>& options,
                                          const PanelRect& rect, const PanelMetric& metric,
                                          const OptionBlockStyle& style) {
    std::vector<OptionBlock> out;
    out.reserve(options.size());
    const int cells = metric.cellsIn(rect.w);
    const int rows = metric.rowsIn(rect.h);
    if (cells <= 0 || rows <= 0) {
        out.resize(options.size());
        return out;
    }
    int keyCells = 0;
    if (style.showKeys) {
        for (const PanelOption& option : options) {
            keyCells = std::max(keyCells, cellsOf(option.key));
        }
        if (keyCells > 0) {
            keyCells += 1;
        }
    }
    const int textCells = std::max(1, cells - keyCells);
    int y = rect.y;
    // ONCE ONE ENTRY DOES NOT FIT, NOTHING AFTER IT DOES EITHER -- and that is a
    // rule about ORDER, not about space. Letting a shorter entry further down
    // the list jump into the gap a long one could not use would print entry 3
    // above entry 2, which makes the printed numbers a lie and makes the
    // selection model and the screen disagree about what "the next one down"
    // means. The list stops where it stops.
    bool stopped = false;
    for (const PanelOption& option : options) {
        OptionBlock block;
        block.lines = wrapText(shout(option.label), static_cast<std::size_t>(textCells));
        if (block.lines.empty()) {
            block.lines.push_back(std::string{});
        }
        const int want = std::max(static_cast<int>(block.lines.size()), std::max(1, style.minRows));
        const int roomRows = metric.rowsIn(rect.y + metric.heightOf(rows) - y);
        if (stopped || roomRows < want) {
            stopped = true;
            // DOES NOT FIT: handed back with rows == 0 rather than squeezed or
            // dropped. A half-drawn answer is worse than an absent one, and the
            // index has to survive either way so the caller's cursor and this
            // vector cannot disagree about what entry 4 is.
            block.rows = 0;
            out.push_back(std::move(block));
            continue;
        }
        block.rows = want;
        block.rect = PanelRect{rect.x, y, rect.w, metric.heightOf(want)};
        y += metric.heightOf(want + std::max(0, style.gapRows));
        out.push_back(std::move(block));
    }
    return out;
}

void drawOptionBlocks(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
                      const std::vector<PanelOption>& options, int selected,
                      const OptionBlockStyle& style, float alpha) {
    if (alpha <= 0.0F || rect.empty() || options.empty()) {
        return;
    }
    const std::vector<OptionBlock> blocks = planOptionBlocks(options, rect, metric, style);
    const int cells = metric.cellsIn(rect.w);
    int keyCells = 0;
    if (style.showKeys) {
        for (const PanelOption& option : options) {
            keyCells = std::max(keyCells, cellsOf(option.key));
        }
        if (keyCells > 0) {
            keyCells += 1;
        }
    }
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const OptionBlock& block = blocks[i];
        if (block.rows <= 0) {
            continue;
        }
        const PanelOption& option = options[i];
        const bool picked = static_cast<int>(i) == selected && option.selectable;
        if (picked) {
            // THE SAME INVERTED FILL, AT BLOCK SCALE. The selection idiom does
            // not change because the entry got longer -- it spans every row the
            // answer wrapped to, so a three-line answer is as unmistakably
            // picked as a one-line one.
            for (int r = 0; r < block.rows; ++r) {
                drawInvertedFill(target, block.rect, metric, 0, r, cells, option.accent, alpha);
            }
        }
        const Rgb labelInk = picked ? kInk.knockout
                                    : (option.labelTakesAccent
                                           ? option.accent
                                           : (option.selectable ? kInk.prose : kInk.dim));
        if (keyCells > 0 && !option.key.empty()) {
            if (picked) {
                drawCellTextKnockout(target, block.rect, metric, 0, 0, option.key, kInk.knockout,
                                     alpha);
            } else {
                drawCellText(target, block.rect, metric, 0, 0, option.key, kInk.key, alpha);
            }
        }
        for (int r = 0; r < block.rows && r < static_cast<int>(block.lines.size()); ++r) {
            if (picked) {
                drawCellTextKnockout(target, block.rect, metric, keyCells, r, block.lines[
                                         static_cast<std::size_t>(r)], labelInk, alpha);
            } else {
                drawCellText(target, block.rect, metric, keyCells, r,
                             block.lines[static_cast<std::size_t>(r)], labelInk, alpha);
            }
        }
        if (!option.value.empty()) {
            const std::string value = shout(option.value);
            if (picked) {
                drawCellTextRight(target, block.rect, metric, 0, 0, value, kInk.knockout, alpha);
            } else {
                drawCellTextRight(target, block.rect, metric, 0, 0, value, inkFor(option.valueInk),
                                  alpha);
            }
        }
    }
}

int optionBlockAt(const std::vector<OptionBlock>& blocks, int px, int py) noexcept {
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const OptionBlock& block = blocks[i];
        if (block.rows <= 0 || block.rect.empty()) {
            continue;
        }
        if (px >= block.rect.x && px < block.rect.right() && py >= block.rect.y &&
            py < block.rect.bottom()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// bars
// ---------------------------------------------------------------------------

int drawBars(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
             const std::vector<PanelBar>& bars, int barCells, float alpha) {
    if (alpha <= 0.0F || rect.empty() || bars.empty()) {
        return 0;
    }
    const int cells = metric.cellsIn(rect.w);
    const int rows = metric.rowsIn(rect.h);
    if (cells <= 0 || rows <= 0) {
        return 0;
    }
    int labelCells = 0;
    int valueCells = 0;
    for (const PanelBar& bar : bars) {
        labelCells = std::max(labelCells, cellsOf(shout(bar.label)));
        valueCells = std::max(valueCells, cellsOf(shout(bar.value)));
    }
    labelCells += 1;
    if (valueCells > 0) {
        valueCells += 1;
    }
    // The bar gets what is left, never less than three cells and never more
    // than it was asked for -- so the same block reads at 320x180 and does not
    // sprawl into a runway at 1920x1080.
    const int wide = std::clamp(cells - labelCells - valueCells, 0, std::max(0, barCells));
    int used = 0;
    for (const PanelBar& bar : bars) {
        if (used >= rows) {
            break;
        }
        drawCellText(target, rect, metric, 0, used, shout(bar.label), kInk.prose, alpha);
        if (wide > 0) {
            const int total = std::max(1, static_cast<int>(bar.total));
            const int filled = std::clamp(
                static_cast<int>((static_cast<std::int64_t>(bar.filled) * wide + total - 1) /
                                 total),
                0, wide);
            const int y = rect.y + metric.heightOf(used);
            // FILLED: a solid block run in the bar's own colour, one pixel of
            // air between cells so it reads as a run of blocks rather than one
            // long slab -- the reference's `████` and not a progress bar.
            for (int i = 0; i < filled; ++i) {
                const int x = rect.x + metric.widthOf(labelCells + i);
                target.fillRect(x, y + metric.scale, metric.cellW() - metric.scale,
                                metric.cellH() - 3 * metric.scale, bar.accent, alpha);
            }
            // REMAINDER: the dotted track, never an empty gap. Same instinct as
            // the stippled panel grounds -- emptiness is textured.
            if (filled < wide) {
                const PanelRect track{rect.x + metric.widthOf(labelCells + filled), y,
                                      metric.widthOf(wide - filled), metric.cellH()};
                drawStipple(target, track, metric, kInk.rule, 0.55F * alpha);
            }
        }
        if (!bar.value.empty()) {
            drawCellText(target, rect, metric, labelCells + wide + 1, used, shout(bar.value),
                         kInk.number, alpha);
        }
        ++used;
    }
    return used;
}

// ---------------------------------------------------------------------------
// aligned key/value rows
// ---------------------------------------------------------------------------

int factValueColumn(const std::vector<PanelFact>& facts, const PanelRect& rect,
                    const PanelMetric& metric) {
    int longest = 0;
    for (const PanelFact& fact : facts) {
        longest = std::max(longest, cellsOf(fact.label));
    }
    const int cells = metric.cellsIn(rect.w);
    // Two cells of air after the longest label, and never past half the pane,
    // however long one label gets -- a single runaway label must not push
    // every value on the screen to the right margin.
    return std::clamp(longest + 2, 0, std::max(0, cells / 2));
}

void drawFacts(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
               const std::vector<PanelFact>& facts, int valueColumn, float alpha) {
    if (alpha <= 0.0F || rect.empty() || facts.empty()) {
        return;
    }
    const int column = valueColumn >= 0 ? valueColumn : factValueColumn(facts, rect, metric);
    const int rows = metric.rowsIn(rect.h);
    for (std::size_t i = 0; i < facts.size(); ++i) {
        const int row = static_cast<int>(i);
        if (row >= rows) {
            break;
        }
        drawCellText(target, rect, metric, 0, row, facts[i].label, kInk.dim, alpha);
        drawCellText(target, rect, metric, column, row, facts[i].value, inkFor(facts[i].valueInk),
                     alpha);
    }
}

// ---------------------------------------------------------------------------
// prose, effects and the bullet hierarchy
// ---------------------------------------------------------------------------

namespace {

/// Greedy word wrap with a HANGING INDENT: the first line gets `firstCells`
/// (what the effect's name left of it) and every line after gets `restCells`.
///
/// dialogue_view.cpp's wrapText() is the same algorithm at one width and is
/// still what the breadcrumb uses; it cannot express "the first line is
/// shorter", which is the whole shape of a `• Name: body...` effect. A word
/// longer than the column has nowhere to break and is cut rather than allowed
/// to run out of the pane.
[[nodiscard]] std::vector<std::string> wrapHanging(const std::string& text, int firstCells,
                                                   int restCells) {
    std::vector<std::string> out;
    if (restCells <= 0) {
        return out;
    }
    int width = firstCells;
    if (width <= 0) {
        // The name filled its own row. The body starts on the next one, and
        // the empty first line is what spends the row the name is on.
        out.emplace_back();
        width = restCells;
    }
    std::string current;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find(' ', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string word = text.substr(at, end - at);
        std::size_t next = end;
        while (next < text.size() && text[next] == ' ') {
            ++next;
        }
        if (word.empty()) {
            at = next;
            continue;
        }
        if (current.empty()) {
            if (static_cast<int>(word.size()) <= width) {
                current = word;
            } else {
                out.push_back(word.substr(0, static_cast<std::size_t>(width)));
                at += static_cast<std::size_t>(width);
                width = restCells;
                continue;
            }
        } else if (static_cast<int>(current.size() + 1 + word.size()) <= width) {
            current += ' ';
            current += word;
        } else {
            out.push_back(current);
            current.clear();
            width = restCells;
            continue;
        }
        at = next;
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

}  // namespace

int drawProse(Framebuffer& target, const PanelRect& rect, const PanelMetric& metric,
              const std::vector<PanelLine>& lines, float alpha) {
    if (alpha <= 0.0F || rect.empty()) {
        return 0;
    }
    const int cells = metric.cellsIn(rect.w);
    const int maxRows = metric.rowsIn(rect.h);
    if (cells <= 0 || maxRows <= 0) {
        return 0;
    }
    int used = 0;
    for (const PanelLine& line : lines) {
        if (used >= maxRows) {
            break;
        }
        // The bullet hangs in its own column and the wrapped continuation
        // indents under the TEXT, not under the bullet -- which is what makes
        // a two-line effect read as one item rather than as two.
        const int indent = line.bullet == Bullet::None ? 0 : (line.bullet == Bullet::Ring ? 6 : 2);
        const int textCells = std::max(1, cells - indent);
        const std::string body = shout(line.body);
        const std::string name = shout(line.name);

        if (line.bullet != Bullet::None) {
            drawMotif(target, rect.x + (indent - 2) * metric.cellW(),
                      rect.y + used * metric.cellH(),
                      line.bullet == Bullet::Ring ? Motif::Ring : Motif::Dot,
                      line.bullet == Bullet::Ring ? kInk.dim : line.nameInk, alpha, metric.scale);
        }
        // FLAVOUR -> NAMED EFFECT -> NUMBER, and the colour does the sorting.
        // The name is laid first at its own accent and the body wraps into
        // whatever it left of the first line, then to the full text column
        // after that: a hanging indent, so a named effect reads as one item.
        int nameCells = 0;
        if (!name.empty()) {
            nameCells = drawCellText(target, rect, metric, indent, used, name, line.nameInk,
                                     alpha) +
                        1;
        }
        if (body.empty()) {
            ++used;
            continue;
        }
        // A name that leaves fewer than four cells of its own row is treated as
        // having filled it: two-glyph fragments of the next word hanging off the
        // end of a label read as a rendering fault, not as a wrap.
        const int room = textCells - nameCells;
        const int firstCells = room >= 4 ? room : 0;
        const std::vector<std::string> wrapped =
            wrapHanging(body, firstCells, textCells);
        for (std::size_t w = 0; w < wrapped.size(); ++w) {
            if (used >= maxRows) {
                break;
            }
            const int cell = w == 0 ? indent + nameCells : indent;
            drawCellText(target, rect, metric, cell, used, wrapped[w], inkFor(line.bodyInk),
                         alpha);
            ++used;
        }
        // A name with nothing left of its own row to wrap into still spent
        // that row.
        if (wrapped.empty()) {
            ++used;
        }
    }
    return used;
}

// ---------------------------------------------------------------------------
// the master/detail split
// ---------------------------------------------------------------------------

MasterDetail splitMasterDetail(const PanelRect& interior, const PanelMetric& metric,
                               int masterShare, int minMasterCells, int minDetailCells) {
    MasterDetail out;
    const int cells = metric.cellsIn(interior.w);
    // The divider itself costs a cell, and a cell of air either side of it, so
    // no text ever touches the flicker column.
    const int chrome = 3;
    int master = std::clamp(cells * std::clamp(masterShare, 1, 66) / 100, minMasterCells,
                            std::max(minMasterCells, cells * 2 / 3));
    if (cells - master - chrome < minDetailCells || master + chrome >= cells) {
        // THE HONEST ANSWER AT A SMALL WINDOW is one pane, not two too thin to
        // read. A caller composes its fallback off `split` rather than guessing
        // a pixel breakpoint of its own.
        out.master = interior;
        out.detail = PanelRect{interior.x, interior.y, 0, interior.h};
        out.dividerCell = 0;
        out.split = false;
        return out;
    }
    out.master = PanelRect{interior.x, interior.y, metric.widthOf(master), interior.h};
    out.dividerCell = master + 1;
    const int detailCell = out.dividerCell + 1;
    out.detail = PanelRect{interior.x + metric.widthOf(detailCell), interior.y,
                           metric.widthOf(cells - detailCell), interior.h};
    out.split = true;
    return out;
}

void drawCommitVerb(Framebuffer& target, const PanelRect& pane, const PanelMetric& metric,
                    std::string_view verb, std::string_view cost, const Rgb& accent, float alpha) {
    if (alpha <= 0.0F || pane.empty() || verb.empty()) {
        return;
    }
    const int rows = metric.rowsIn(pane.h);
    if (rows <= 0) {
        return;
    }
    // THE LAST ROW OF THE PANE, always -- so it holds still while the cursor
    // moves between entries whose detail runs a different number of lines.
    const int row = rows - 1;
    const int cells = metric.cellsIn(pane.w);
    const int want = cellsOf(verb) + 1 + cellsOf(cost);
    if (!cost.empty() && want > cells && row > 0) {
        // THE RESTATEMENT GOES ABOVE THE VERB RATHER THAN GETTING CUT. The
        // point of restating a cost on the commit line is that the player reads
        // it before they press; "(TAKES IT OFF WHATEV.." does not do that job.
        // The VERB stays on the last row either way, which is the part that
        // must not move.
        drawCellText(target, pane, metric, 0, row - 1, cost, panelInk().number, alpha);
        drawCellText(target, pane, metric, 0, row, verb, accent, alpha);
        return;
    }
    const int wide = drawCellText(target, pane, metric, 0, row, verb, accent, alpha);
    if (!cost.empty()) {
        drawCellText(target, pane, metric, wide + 1, row, cost, panelInk().number, alpha);
    }
}

}  // namespace granadad::render
