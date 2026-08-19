#include "granadad/render/hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

#include "granadad/sim/angle.hpp"

namespace granadad::render {

namespace {

/// A 4x6 pixel font, one bit per pixel, rows top to bottom, bit 3 leftmost.
/// Digits, capitals and the handful of punctuation the HUD needs. Chunky on
/// purpose: this is meant to read at 640x360 upscaled with nearest neighbour.
struct Glyph {
    char code;
    std::array<std::uint8_t, 6> rows;
};

constexpr Glyph kGlyphs[] = {
    {' ', {0x0, 0x0, 0x0, 0x0, 0x0, 0x0}}, {'0', {0x6, 0x9, 0xB, 0xD, 0x9, 0x6}},
    {'1', {0x2, 0x6, 0x2, 0x2, 0x2, 0x7}}, {'2', {0x6, 0x9, 0x1, 0x2, 0x4, 0xF}},
    {'3', {0xE, 0x1, 0x6, 0x1, 0x1, 0xE}}, {'4', {0x2, 0x6, 0xA, 0xF, 0x2, 0x2}},
    {'5', {0xF, 0x8, 0xE, 0x1, 0x9, 0x6}}, {'6', {0x6, 0x8, 0xE, 0x9, 0x9, 0x6}},
    {'7', {0xF, 0x1, 0x2, 0x4, 0x4, 0x4}}, {'8', {0x6, 0x9, 0x6, 0x9, 0x9, 0x6}},
    {'9', {0x6, 0x9, 0x9, 0x7, 0x1, 0x6}}, {'A', {0x6, 0x9, 0x9, 0xF, 0x9, 0x9}},
    {'B', {0xE, 0x9, 0xE, 0x9, 0x9, 0xE}}, {'C', {0x6, 0x9, 0x8, 0x8, 0x9, 0x6}},
    {'D', {0xE, 0x9, 0x9, 0x9, 0x9, 0xE}}, {'E', {0xF, 0x8, 0xE, 0x8, 0x8, 0xF}},
    {'F', {0xF, 0x8, 0xE, 0x8, 0x8, 0x8}}, {'G', {0x6, 0x9, 0x8, 0xB, 0x9, 0x7}},
    {'H', {0x9, 0x9, 0xF, 0x9, 0x9, 0x9}}, {'I', {0x7, 0x2, 0x2, 0x2, 0x2, 0x7}},
    {'J', {0x1, 0x1, 0x1, 0x1, 0x9, 0x6}}, {'K', {0x9, 0xA, 0xC, 0xC, 0xA, 0x9}},
    {'L', {0x8, 0x8, 0x8, 0x8, 0x8, 0xF}}, {'M', {0x9, 0xF, 0xF, 0x9, 0x9, 0x9}},
    {'N', {0x9, 0xD, 0xF, 0xB, 0x9, 0x9}}, {'O', {0x6, 0x9, 0x9, 0x9, 0x9, 0x6}},
    {'P', {0xE, 0x9, 0xE, 0x8, 0x8, 0x8}}, {'Q', {0x6, 0x9, 0x9, 0xB, 0xA, 0x5}},
    {'R', {0xE, 0x9, 0xE, 0xC, 0xA, 0x9}}, {'S', {0x7, 0x8, 0x6, 0x1, 0x1, 0xE}},
    {'T', {0xF, 0x4, 0x4, 0x4, 0x4, 0x4}}, {'U', {0x9, 0x9, 0x9, 0x9, 0x9, 0x6}},
    {'V', {0x9, 0x9, 0x9, 0x9, 0x6, 0x6}}, {'W', {0x9, 0x9, 0x9, 0xF, 0xF, 0x9}},
    {'X', {0x9, 0x9, 0x6, 0x6, 0x9, 0x9}}, {'Y', {0x9, 0x9, 0x6, 0x4, 0x4, 0x4}},
    {'Z', {0xF, 0x1, 0x2, 0x4, 0x8, 0xF}}, {'-', {0x0, 0x0, 0xF, 0x0, 0x0, 0x0}},
    {'.', {0x0, 0x0, 0x0, 0x0, 0x0, 0x4}}, {':', {0x0, 0x4, 0x0, 0x0, 0x4, 0x0}},
    {'/', {0x1, 0x1, 0x2, 0x4, 0x8, 0x8}}, {'%', {0x9, 0x1, 0x2, 0x4, 0x8, 0x9}},
    // S3. The authored barks are prose, not HUD labels: they are full of
    // apostrophes, commas, questions and parenthetical stage directions. Every
    // one of those used to advance the cursor and draw nothing, so
    // "You've got sand, showing that face at the ropes." came out as a line
    // with holes in it. These are the characters content/raws/barks/barks.json
    // actually uses, and nothing else.
    {',', {0x0, 0x0, 0x0, 0x0, 0x4, 0x8}}, {'\'', {0x4, 0x4, 0x0, 0x0, 0x0, 0x0}},
    {'!', {0x4, 0x4, 0x4, 0x4, 0x0, 0x4}}, {'?', {0x6, 0x9, 0x1, 0x2, 0x0, 0x2}},
    {'(', {0x2, 0x4, 0x4, 0x4, 0x4, 0x2}}, {')', {0x4, 0x2, 0x2, 0x2, 0x2, 0x4}},
    {';', {0x0, 0x4, 0x0, 0x0, 0x4, 0x8}}, {'"', {0xA, 0xA, 0x0, 0x0, 0x0, 0x0}},
    {'+', {0x0, 0x4, 0xE, 0x4, 0x0, 0x0}}, {'>', {0x8, 0x4, 0x2, 0x2, 0x4, 0x8}},
    {'<', {0x2, 0x4, 0x8, 0x8, 0x4, 0x2}}, {'*', {0x0, 0xA, 0x4, 0xA, 0x0, 0x0}},
};

constexpr int kGlyphW = 4;
constexpr int kGlyphH = 6;
constexpr int kGlyphAdvance = 5;

[[nodiscard]] const Glyph* glyphFor(char c) noexcept {
    const char upper = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.code == upper) {
            return &glyph;
        }
    }
    return nullptr;
}

constexpr Rgb kInk{0.90F, 0.87F, 0.78F};
constexpr Rgb kShadow{0.02F, 0.02F, 0.03F};
// HARDENING PASS. THREE STOPS, NOT ONE. A fixed red bar reads identically at
// 100 HP and at 4 -- the near-universal genre convention (Barony included) is
// a bar that goes red before the number does, so a glance at the corner
// answers "am I in danger" without reading the segment count. kHealthLow is
// the ORIGINAL fixed colour this pass replaces, kept as the near-death end of
// the gradient rather than invented fresh, so a nearly-dead player still sees
// the same red this HUD has always shipped.
constexpr Rgb kHealthFull{0.30F, 0.62F, 0.20F};
constexpr Rgb kHealthMid{0.82F, 0.68F, 0.16F};
constexpr Rgb kHealthLow{0.72F, 0.16F, 0.14F};
constexpr Rgb kHealthBack{0.10F, 0.06F, 0.06F};
constexpr Rgb kFrame{0.55F, 0.50F, 0.40F};

/// Green above half health, ambering through yellow at half, reddening into
/// kHealthLow as the segments run out -- see the constants' own note above.
/// `fraction` is clamped here rather than trusted, so a caller passing a
/// stale health/healthMax pair (healthMax 0, health negative) still gets a
/// legal colour instead of extrapolating off the end of the gradient.
[[nodiscard]] Rgb healthColor(float fraction) noexcept {
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    if (fraction >= 0.5F) {
        const float t = (fraction - 0.5F) / 0.5F;
        return Rgb{lerp(kHealthMid.r, kHealthFull.r, t), lerp(kHealthMid.g, kHealthFull.g, t),
                   lerp(kHealthMid.b, kHealthFull.b, t)};
    }
    const float t = fraction / 0.5F;
    return Rgb{lerp(kHealthLow.r, kHealthMid.r, t), lerp(kHealthLow.g, kHealthMid.g, t),
               lerp(kHealthLow.b, kHealthMid.b, t)};
}

/// A drawn row is six glyph rows plus the one-pixel drop shadow under them.
[[nodiscard]] constexpr int rowHeight(int scale) noexcept { return (kGlyphH + 1) * scale; }

}  // namespace

int hudScale(int height) noexcept { return std::max(1, height / 180); }

int hudMinorScale(int height) noexcept { return std::max(1, hudScale(height) - 1); }

int hudTopRightReserve(int height) noexcept {
    const int scale = hudScale(height);
    const int minor = hudMinorScale(height);
    // WHAT IS STILL DRAWN THERE WHILE SOMEBODY IS TALKING, and nothing else.
    // Standing, heat, the sack and the stealth line all stand down for the
    // length of a conversation -- Session sets them empty -- so the corner is
    // the hour at the full size and a purse of at most five figures under it at
    // the minor one. Reserving for the whole stack would cost the speech four
    // columns of every line for rows that are not there.
    return std::max(textWidth("00:00", scale), textWidth("99999 C", minor)) +
           kGlyphAdvance * scale;
}

namespace {

/// THE BOTTOM BAND IS A GRID OF SLOTS AND EVERY SLOT IS PROVEN CLEAR.
///
/// Every row down here used to carry its own offset written by hand in scale
/// units -- the alert at 23, the lock at 31, the case at 16, the guild at 24 --
/// and the two defects that produced are both in the git log: S9 drew the lock
/// row through the guild row ("THE 1OCKUNPINS -TENADEPTH ....+...."), S10 drew
/// a clue through the case row. Both were found in a PNG. Neither could be
/// found by a test, because a collision between two hand-written constants is
/// not a thing a test knows to look for.
///
/// Rows are handed slots now, in priority order, and a row that has no legal
/// slot left is dropped. Two rows cannot land on the same pixels because no
/// slot is ever handed out twice, and no row lands in the play space because
/// the allocator refuses a slot that crosses the exclusion rectangle.
class BottomBand {
  public:
    BottomBand(int width, int height) noexcept
        : scale_(hudScale(height)),
          minor_(hudMinorScale(height)),
          step_(rowHeight(hudMinorScale(height)) + hudMinorScale(height)),
          margin_(6 * hudScale(height)),
          floor_(hudCentreRect(width, height).y1) {
        // Slot 1 is the row directly above the health bar's frame.
        base_ = height - margin_ - barHeight() - scale_;
    }

    [[nodiscard]] int scale() const noexcept { return scale_; }
    [[nodiscard]] int minor() const noexcept { return minor_; }
    [[nodiscard]] int margin() const noexcept { return margin_; }
    [[nodiscard]] int barWidth() const noexcept { return 48 * scale_; }
    [[nodiscard]] int barHeight() const noexcept { return 6 * scale_; }

    /// Takes the next free slot tall enough for a row drawn at `scale`, and
    /// answers the y it should be drawn at. -1 when the band is full: the row
    /// is dropped rather than drawn over the play space.
    [[nodiscard]] int take(int scale) noexcept {
        const int span = std::max(1, (rowHeight(scale) + step_ - 1) / step_);
        const int top = base_ - (taken_ + span) * step_;
        if (top < floor_) {
            return -1;
        }
        taken_ += span;
        return top;
    }

  private:
    int scale_;
    int minor_;
    int step_;
    int margin_;
    int floor_;
    int base_ = 0;
    int taken_ = 0;
};

}  // namespace

CentreRect hudCentreRect(int width, int height) noexcept {
    const int marginX = static_cast<int>(static_cast<float>(width) * kHudEdgeFraction);
    const int marginY = static_cast<int>(static_cast<float>(height) * kHudEdgeFraction);
    return CentreRect{marginX, marginY, width - marginX, height - marginY};
}

int textWidth(std::string_view text, int scale) noexcept {
    if (text.empty()) {
        return 0;
    }
    return (static_cast<int>(text.size()) * kGlyphAdvance - 1) * scale;
}

std::string clipToWidth(std::string_view text, int pixels, int scale) {
    const int step = kGlyphAdvance * std::max(1, scale);
    const int room = pixels / step;
    if (room <= 0) {
        return {};
    }
    if (static_cast<int>(text.size()) <= room) {
        return std::string(text);
    }
    // Two glyphs of the room are the mark, so the cut line is never wider than
    // the whole line would have been -- and the mark is what says "there was
    // more of this" rather than leaving a sentence that looks finished.
    if (room <= 2) {
        return std::string(text.substr(0, static_cast<std::size_t>(room)));
    }
    std::string out(text.substr(0, static_cast<std::size_t>(room - 2)));
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    out += "..";
    return out;
}

bool isDrawableGlyph(char c) noexcept { return glyphFor(c) != nullptr; }

int drawText(Framebuffer& target, int x, int y, std::string_view text, const Rgb& colour,
             float alpha, int scale) {
    const int step = kGlyphAdvance * scale;
    int cursor = x;
    for (const char c : text) {
        const Glyph* glyph = glyphFor(c);
        if (glyph != nullptr) {
            for (int row = 0; row < kGlyphH; ++row) {
                for (int col = 0; col < kGlyphW; ++col) {
                    const bool on =
                        (glyph->rows[static_cast<std::size_t>(row)] >> (kGlyphW - 1 - col)) & 1U;
                    if (!on) {
                        continue;
                    }
                    // A one-pixel drop shadow, so the text survives being drawn
                    // over a pale sky as well as over black water.
                    target.fillRect(cursor + col * scale + scale, y + row * scale + scale, scale,
                                    scale, kShadow, alpha * 0.75F);
                    target.fillRect(cursor + col * scale, y + row * scale, scale, scale, colour,
                                    alpha);
                }
            }
        }
        cursor += step;
    }
    return cursor - x;
}

/// HARDENING PASS. See hud.hpp's own header on where these two colours come
/// from -- pulled out of client-observer's PlaceSignArt.java by its exact
/// values via git history, not re-guessed, since that file (and the client it
/// belonged to) no longer exists in this tree.
constexpr Rgb kPlateBlack{0.0F, 0.0F, 0.0F};
constexpr Rgb kPlateBone{0.90F, 0.87F, 0.76F};

void drawTextPlate(Framebuffer& target, int x0, int y0, int x1, int y1, int border, float alpha) {
    if (alpha <= 0.0F || x1 <= x0 || y1 <= y0) {
        return;
    }
    border = std::clamp(border, 1, std::min(x1 - x0, y1 - y0) / 2);
    // THE FIELD, THEN THE FOUR BORDER RAILS -- the identical order the
    // retired renderer's own box() drew in, so the border is never eaten by
    // the fill it is drawn over.
    target.fillRect(x0, y0, x1 - x0, y1 - y0, kPlateBlack, alpha);
    target.fillRect(x0, y0, x1 - x0, border, kPlateBone, alpha);                 // top
    target.fillRect(x0, y1 - border, x1 - x0, border, kPlateBone, alpha);        // bottom
    target.fillRect(x0, y0, border, y1 - y0, kPlateBone, alpha);                 // left
    target.fillRect(x1 - border, y0, border, y1 - y0, kPlateBone, alpha);        // right
}

namespace {

/// The health bar, bottom-left, and the only thing down here still drawn at
/// full size.
///
/// THE WORD "HP" IS GONE, AND THAT IS THE POINT OF THIS PASS IN ONE ELEMENT. A
/// red segmented bar in the bottom-left corner of a first-person game is not
/// ambiguous -- Barony has never labelled its own -- and the label cost a whole
/// row of a band that is only three rows deep at 320x180. The bar lost a
/// quarter of its width and a seventh of its height with it: 48 scale units
/// divide by sixteen segments EXACTLY, where 62 left ten units of dead track on
/// the end of every full bar.
void drawHealth(Framebuffer& target, const HudState& state, const BottomBand& band) {
    const int scale = band.scale();
    const int barW = band.barWidth();
    const int barH = band.barHeight();
    const int x = band.margin();
    const int y = target.height() - band.margin() - barH;

    target.fillRect(x - scale, y - scale, barW + 2 * scale, barH + 2 * scale, kFrame, 0.55F);
    target.fillRect(x, y, barW, barH, kHealthBack, 0.85F);

    const int maxHealth = std::max(1, state.healthMax);
    const int clamped = std::clamp(state.health, 0, maxHealth);
    // HARDENING PASS. THE FILL FRACTION DRIVES THE COLOUR TOO, NOT JUST THE
    // SEGMENT COUNT. Both come off the identical clamped/maxHealth the bar
    // already needed to size itself -- see healthColor()'s own header --
    // so the colour can never disagree with the number of segments drawn.
    const Rgb colour = healthColor(static_cast<float>(clamped) / static_cast<float>(maxHealth));
    // Chunky segments rather than a smooth bar: it reads at a glance and it is
    // the register the rest of the art is in.
    const int segments = 16;
    const int filled = (clamped * segments + maxHealth - 1) / maxHealth;
    const int segW = barW / segments;
    for (int i = 0; i < filled; ++i) {
        target.fillRect(x + i * segW + 1, y + 1, segW - 1, barH - 2, colour, 0.95F);
    }
}

// FATIGUE BUILD. The wind's own ramp, healthColor's discipline on different
// ends: a full pool is a warm amber (effort in the bank, distinct at a
// glance from the health bar's green so the two stacked bars never read as
// one gauge split in two), cooling through a duller ochre to a spent
// grey-blue as it empties -- exhaustion reads cold, not wounded. Clamped
// exactly as healthColor clamps, for exactly its reason.
constexpr Rgb kFatigueFull{0.87F, 0.66F, 0.24F};
constexpr Rgb kFatigueMid{0.62F, 0.52F, 0.30F};
constexpr Rgb kFatigueLow{0.34F, 0.40F, 0.48F};

[[nodiscard]] Rgb fatigueColor(float fraction) noexcept {
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    if (fraction >= 0.5F) {
        const float t = (fraction - 0.5F) / 0.5F;
        return Rgb{lerp(kFatigueMid.r, kFatigueFull.r, t), lerp(kFatigueMid.g, kFatigueFull.g, t),
                   lerp(kFatigueMid.b, kFatigueFull.b, t)};
    }
    const float t = fraction / 0.5F;
    return Rgb{lerp(kFatigueLow.r, kFatigueMid.r, t), lerp(kFatigueLow.g, kFatigueMid.g, t),
               lerp(kFatigueLow.b, kFatigueMid.b, t)};
}

/// The fatigue bar: the health bar's width, HALF its height, tucked into the
/// bottom margin directly below it -- beside the number it is read with, and
/// costing no other row a pixel (the bottom band's slot grid starts above the
/// health bar and never knew the margin existed). Same segment discipline,
/// same clamp-drives-everything rule as drawHealth; the continuous state
/// (the fill fraction) drives the continuous visual (colour and segments),
/// per the DECISIONS.md UI row.
void drawFatigue(Framebuffer& target, const HudState& state, const BottomBand& band) {
    if (state.fatigueMax <= 0) {
        return;
    }
    const float fade = std::clamp(state.fatigueFade, 0.0F, 1.0F);
    if (fade <= 0.0F) {
        return;
    }
    const int scale = band.scale();
    const int barW = band.barWidth();
    const int barH = band.barHeight() / 2;
    const int x = band.margin();
    // Directly below the health bar, inside the bottom margin: this bar's
    // frame TOP lands exactly on the health frame's bottom edge (h - 5*scale)
    // and its bottom edge lands exactly on the frame's last row, so the pair
    // read as one stacked instrument hugging the corner and neither frame
    // over-draws the other.
    const int y = target.height() - band.margin() + 2 * scale;

    target.fillRect(x - scale, y - scale, barW + 2 * scale, barH + 2 * scale, kFrame,
                    0.55F * fade);
    target.fillRect(x, y, barW, barH, kHealthBack, 0.85F * fade);

    const int maxFatigue = std::max(1, state.fatigueMax);
    const int clamped = std::clamp(state.fatigue, 0, maxFatigue);
    const Rgb colour =
        fatigueColor(static_cast<float>(clamped) / static_cast<float>(maxFatigue));
    const int segments = 16;
    const int filled = (clamped * segments + maxFatigue - 1) / maxFatigue;
    const int segW = barW / segments;
    for (int i = 0; i < filled; ++i) {
        target.fillRect(x + i * segW + 1, y + 1, segW - 1, barH - 2, colour, 0.95F * fade);
    }
}

/// THE TOP BAND'S OWN THREE ROWS, COMPUTED ONCE.
///
/// DISTRICT PHASE D pulled these four numbers out of drawCompass rather than
/// re-deriving them a second time next door, and the reason is this file's own
/// header: every collision this HUD has ever shipped -- the lock row through
/// the guild row, a clue through the case row -- was two functions computing
/// the same offset by hand and only one of them remembering when it moved. The
/// threshold plate has to sit directly under the place-name sub-label; it now
/// asks where that is instead of knowing.
struct TopBand {
    /// The compass ribbon's own top edge, three scale units off the frame.
    int stripY;
    /// The ribbon's height, drop shadow included.
    int stripH;
    /// The place-name sub-label's own top edge, one scale unit under it.
    int labelY;
    /// The first pixel row below everything the ribbon block draws.
    int bottom;
};

[[nodiscard]] TopBand topBand(int height) noexcept {
    const int scale = hudScale(height);
    const int minor = hudMinorScale(height);
    TopBand band{};
    band.stripY = 3 * scale;
    band.stripH = rowHeight(scale) + scale;
    band.labelY = band.stripY + band.stripH + scale;
    band.bottom = band.labelY + kGlyphH * minor;
    return band;
}

void drawCompass(Framebuffer& target, const HudState& state) {
    const int scale = hudScale(target.height());
    const int minor = hudMinorScale(target.height());
    // NARROWER, SHALLOWER, AND HARD AGAINST THE EDGE. The ribbon took a third
    // of the frame width and started five scale units down from the top, which
    // at 960x540 is a 320x27 black block hanging in the middle of the sky with
    // a place name under it. A quarter of the width still shows a hundred and
    // eighty degrees of arc with every point on it legible.
    const int stripW = std::min(target.width() / 4, 120 * scale);
    // DISTRICT PHASE D: OFF topBand(), NOT re-derived here. Identical values,
    // one owner -- see the struct's own header on which defect that is about.
    const TopBand band = topBand(target.height());
    const int stripH = band.stripH;
    const int x = (target.width() - stripW) / 2;
    const int y = band.stripY;

    target.fillRect(x - scale, y - scale, stripW + 2 * scale, stripH + 2 * scale, kFrame, 0.45F);
    target.fillRect(x, y, stripW, stripH, Rgb{0.05F, 0.05F, 0.07F}, 0.70F);

    // A ribbon of the compass scrolling under a fixed centre mark: 180 degrees
    // of arc across the strip, so a landmark drifts at the rate you turn.
    struct Point {
        const char* label;
        std::int32_t bam;
    };
    const Point points[8] = {
        {"N", 0},      {"NE", 8192},  {"E", 16384},  {"SE", 24576},
        {"S", 32768},  {"SW", 40960}, {"W", 49152},  {"NW", 57344},
    };
    const float pixelsPerBam = static_cast<float>(stripW) / 32768.0F;
    for (const Point& point : points) {
        std::int32_t delta = point.bam - (state.yawBam & 65535);
        delta = ((delta + 32768) & 65535) - 32768;  // to [-32768, 32768)
        const float offset = static_cast<float>(delta) * pixelsPerBam;
        const int px = x + stripW / 2 + static_cast<int>(offset);
        const int labelWidth = textWidth(point.label, scale);
        if (px - labelWidth / 2 < x || px + labelWidth / 2 > x + stripW) {
            continue;
        }
        const bool cardinal = (point.bam & 16383) == 0;
        drawText(target, px - labelWidth / 2, y + scale, point.label,
                 cardinal ? kInk : Rgb{0.62F, 0.60F, 0.54F}, cardinal ? 0.95F : 0.7F, scale);
    }
    // The fixed mark. One pixel column, at the very top edge of the strip, so
    // it never encroaches on the view.
    target.fillRect(x + stripW / 2, y, scale, 2 * scale, Rgb{0.95F, 0.80F, 0.35F}, 1.0F);

    // The place name is REFERENCE, not register: you read it when you arrive
    // and never again until you arrive somewhere else. It is the sub-label of
    // the ribbon now rather than a second line the same size as it, which is
    // what a hierarchy looks like when it is doing its job.
    if (!state.locationLabel.empty()) {
        const int width = textWidth(state.locationLabel, minor);
        drawText(target, (target.width() - width) / 2, band.labelY, state.locationLabel,
                 Rgb{0.70F, 0.68F, 0.60F}, 0.85F, minor);
    }
}

/// Top-right: the hour, and then everything the ward, the Watch, your pockets
/// and the dark have to say about you. Right-aligned against the edge, because
/// that is the edge it belongs to.
///
/// THE STACK DROPS ITS LEAST IMPORTANT ROW RATHER THAN CROSSING THE
/// RECTANGLE. Six rows
/// at nine scale units each is 54 units of sky and the exclusion rectangle
/// starts at 39 of them at 320x180 -- so the old stack ran straight through the
/// play space the moment the Watch had heard about you and there was something
/// in your sack. Nothing caught it, because no case ever filled more than three
/// of the six rows at once.
///
/// DISTRICT PHASE D: RETURNS THE WIDTH IT ACTUALLY CLAIMED, and that return
/// value is load-bearing rather than informational. The threshold plate is
/// CENTRED and this stack is RIGHT-ANCHORED, and both live in the top band --
/// so the one question the plate has to be able to ask is "how far left does
/// the corner reach this frame". Every row here is clipped to `width - 2 *
/// margin`, which means a single heat line at its longest can reach past the
/// centre of the frame; a plate that assumed a fixed reserve (hudTopRightReserve
/// deliberately measures only what survives a conversation, which is the clock
/// and the purse) would have been the same class of guess that shipped the
/// lock row through the guild row. This is the measurement, not an estimate:
/// the widest row this call really drew, after the rank-order drops, in pixels.
/// 0 when the corner drew nothing at all.
int drawTopRight(Framebuffer& target, const HudState& state) {
    const int scale = hudScale(target.height());
    const int minor = hudMinorScale(target.height());
    const int margin = 6 * scale;
    const int ceiling = hudCentreRect(target.width(), target.height()).y0;
    // The same three scale units from the edge the compass ribbon starts at, so
    // the two top blocks share a line.
    int y = 3 * scale;
    // DISTRICT PHASE D. The widest row this call actually puts on the frame --
    // see the header on why the threshold plate needs it measured and not
    // guessed. Every drawText below feeds it, so a row added later cannot
    // forget to.
    int claimed = 0;

    if (state.timeOfDaySeconds >= 0 && y + rowHeight(scale) <= ceiling) {
        const int hour = (state.timeOfDaySeconds / 3600) % 24;
        const int minute = (state.timeOfDaySeconds / 60) % 60;
        const char text[6] = {static_cast<char>(48 + hour / 10),
                              static_cast<char>(48 + hour % 10),
                              ':',
                              static_cast<char>(48 + minute / 10),
                              static_cast<char>(48 + minute % 10),
                              0};
        const std::string_view clock(text);
        claimed = std::max(claimed, textWidth(clock, scale));
        drawText(target, target.width() - margin - textWidth(clock, scale), y, clock, kInk, 0.9F,
                 scale);
        y += rowHeight(scale) + 1;
    }

    // Every remaining row, in the order they have always been drawn in, each
    // carrying its place in the queue -- 1 is the last row this stack would ever
    // give up. When it runs out of room above the rectangle the row with the
    // HIGHEST number goes, not the one at the bottom: a burglar reads the
    // stealth line every second and the ward's opinion of him once a week.
    struct Row {
        std::string_view text;
        Rgb ink;
        float alpha;
        int rank;
    };
    // Ten now (was 6): the held-effects build adds up to four live-hold rows.
    std::array<Row, 10> rows{};
    std::size_t count = 0;
    const auto add = [&](std::string_view text, const Rgb& ink, float alpha, int rank) {
        if (!text.empty() && count < rows.size()) {
            rows[count++] = Row{text, ink, alpha, rank};
        }
    };
    // EVERY ROW HERE IS RIGHT-ANCHORED, AND CLIPPED TO WHAT THE FRAME
    // ACTUALLY HAS, not to a count session.cpp guessed. An anchor only
    // protects the edge it is anchored TO: an unclipped row wide enough to
    // overrun starts its draw at a negative x and loses its own FRONT off
    // the LEFT edge instead, which is the higher-priority half of every one
    // of these (heatLabel's "CONDEMNED  WANTED" reads before "HEAT 84"). See
    // test_render.cpp's "a bottom-band or top-right label does not run off
    // the frame at an off-16:9 window" -- a 320x180/640x360/960x540 capture
    // never has the aspect ratio to catch this, and --width/--height (see
    // main.cpp) are independent flags with no aspect check between them.
    const int rowBudget = std::max(0, target.width() - 2 * margin);
    std::string purse;
    if (state.coin >= 0) {
        purse = clipToWidth(std::to_string(std::min(state.coin, 99999)) + " C", rowBudget, minor);
        add(purse, Rgb{0.82F, 0.72F, 0.38F}, 0.9F, 3);
    }
    // PLANNING SPRINT (item #2, the sweep). `* clamp(state.*Fade)`, THE SAME
    // MULTIPLY stealthLabel already carries below -- see HudState::
    // standingFade's own header. A caller that never heard of it gets the
    // default 1.0, which is a no-op.
    // Rank 10 now (was 6): the held-effects build slotted its four rows in at
    // 5-8 and pushed the sack to 9 -- the ward's opinion is still the first
    // row this stack gives up, which was the whole argument for ranking it
    // last, and a hold with a live clock outranks both the sack and the
    // opinion: it is the row a caster reads every second it counts down.
    const std::string standing = clipToWidth(state.standingLabel, rowBudget, minor);
    add(standing, Rgb{0.62F, 0.66F, 0.72F}, 0.82F * std::clamp(state.standingFade, 0.0F, 1.0F),
        10);
    // Red for anything the ward has decided about you -- a warrant, a hand
    // taken, a rope waiting -- and ash for the rest. Checked against the
    // UNCLIPPED label: the keyword is always at the front and clipping only
    // ever removes the tail (clipToWidth), so the colour cannot be lost by
    // it even when the words that explain it are.
    const bool wanted = state.heatLabel.find("WANTED") != std::string_view::npos ||
                        state.heatLabel.find("MAIMED") != std::string_view::npos ||
                        state.heatLabel.find("CONDEMNED") != std::string_view::npos;
    const std::string heat = clipToWidth(state.heatLabel, rowBudget, minor);
    add(heat, wanted ? Rgb{0.88F, 0.34F, 0.26F} : Rgb{0.70F, 0.62F, 0.50F},
        0.86F * std::clamp(state.heatFade, 0.0F, 1.0F), 2);
    const std::string stash = clipToWidth(state.stashLabel, rowBudget, minor);
    add(stash, Rgb{0.58F, 0.66F, 0.52F}, 0.84F * std::clamp(state.stashFade, 0.0F, 1.0F), 9);
    // FIRST-PERSON COMBAT (S13). What the hand is holding. Ranked between the
    // holds and the purse, and that ordering is an argument: a readied
    // crafting is read in a fight, a sack is read at a door, and the ward's
    // week-old opinion goes first when the sky runs out.
    const std::string spell = clipToWidth(state.spellLabel, rowBudget, minor);
    add(spell, Rgb{0.66F, 0.58F, 0.76F}, 0.86F * std::clamp(state.spellFade, 0.0F, 1.0F), 4);
    // HELD-EFFECTS BUILD. Every live hold, name and clock, directly under the
    // CAST row that laid it and in the same violet family -- the two are one
    // subject read at one glance. Later rows carry later ranks, so when the
    // sky runs out the NEWEST hold is the first of the four to step aside:
    // the oldest clock is the one nearest to mattering.
    std::array<std::string, 4> effects{};
    for (std::size_t i = 0; i < state.effectLabels.size(); ++i) {
        effects[i] = clipToWidth(state.effectLabels[i], rowBudget, minor);
        add(effects[i], Rgb{0.58F, 0.66F, 0.78F},
            0.86F * std::clamp(state.effectFades[i], 0.0F, 1.0F), 5 + static_cast<int>(i));
    }
    // Green while the room cannot see you, amber the moment it can. Checked
    // against the UNCLIPPED label for the identical reason: SEEN is always
    // the first word.
    const bool seen = state.stealthLabel.substr(0, 4) == "SEEN";
    const std::string stealth = clipToWidth(state.stealthLabel, rowBudget, minor);
    add(stealth, seen ? Rgb{0.86F, 0.66F, 0.28F} : Rgb{0.44F, 0.72F, 0.50F},
        0.86F * std::clamp(state.stealthFade, 0.0F, 1.0F), 1);

    // THE GAP IS ONE PIXEL SHORT OF THE OBVIOUS ONE, AND THAT BOUGHT A ROW.
    // rowHeight already carries the drop shadow, so the visible gap between
    // rows is this on top of it. At 1280x720 a gap of `minor` put the fifth row
    // two pixels past the exclusion rectangle and the stack dropped the ward's
    // opinion of the player on the one resolution with the most sky to spare.
    const int step = rowHeight(minor) + std::max(1, minor - 1);
    std::size_t room = 0;
    while (y + static_cast<int>(room) * step + rowHeight(minor) <= ceiling) {
        ++room;
    }
    while (count > room) {
        std::size_t last = 0;
        for (std::size_t i = 1; i < count; ++i) {
            if (rows[i].rank > rows[last].rank) {
                last = i;
            }
        }
        for (std::size_t i = last + 1; i < count; ++i) {
            rows[i - 1] = rows[i];
        }
        --count;
    }
    for (std::size_t i = 0; i < count; ++i) {
        claimed = std::max(claimed, textWidth(rows[i].text, minor));
        drawText(target, target.width() - margin - textWidth(rows[i].text, minor), y, rows[i].text,
                 rows[i].ink, rows[i].alpha, minor);
        y += step;
    }
    return claimed;
}

/// DISTRICT PHASE D: THE THRESHOLD MOMENT -- the place the player has just
/// crossed into, announced once and briefly. See HudState::placePlate for what
/// it is and, more to the point, what it deliberately is not.
///
/// UNDER THE RIBBON, ON THE RIBBON'S OWN BAND. The compass already owns the
/// top centre and its sub-label already carries this exact string as reference
/// material; the plate is the same fact said once, LOUDLY, at the instant it
/// becomes true, and then it gets out of the way. Putting it anywhere else
/// would have meant either a title card in the play space (which this file's
/// own header forbids outright) or a second place-name element somewhere the
/// eye has no reason to be.
///
/// IT GIVES WAY TO THE CORNER, NOT THE OTHER WAY ROUND. `rightBlock` is the
/// width drawTopRight just measured for itself, and the plate's whole budget
/// is what is left between two of them -- symmetric, because the plate is
/// centred and a plate that stays centred by eating its own left margin is not
/// centred. When the corner is full the plate is drawn a size down, then
/// clipped, and then dropped; the hour, the sack and a warrant are up every
/// frame and this is up for two seconds.
///
/// AND IT IS DROPPED RATHER THAN DRAWN LOW. The lowest the drift can put it
/// (drift == -1, the instant of the crossing) is what gets checked against the
/// exclusion rectangle, so a frame short enough for the plate's own travel to
/// reach the play space simply does not get one -- BottomBand::take()'s rule,
/// applied at the other edge and for the same reason.
void drawPlacePlate(Framebuffer& target, const HudState& state, int rightBlock) {
    const float fade = std::clamp(state.placePlateFade, 0.0F, 1.0F);
    if (state.placePlate.empty() || fade <= 0.0F) {
        return;
    }
    const int width = target.width();
    const int height = target.height();
    const int scale = hudScale(height);
    const int margin = 6 * scale;
    // THE PLATE'S OWN PADDING IS INSIDE THE BUDGET, at the widest it can be
    // (padX below is 2 * plateScale, and plateScale never exceeds scale). A
    // budget that measured only the text would have let the black field and
    // its bone rail stick two or three pixels into the corner -- the whole
    // point of measuring instead of estimating, lost on the last four pixels.
    const int budget = width - 2 * (margin + std::max(0, rightBlock) + 2 * scale);
    if (budget <= 0) {
        return;
    }
    // THE ALERT ROW'S LADDER FOR ITS FIRST STEP AND NOT ITS SECOND: a size
    // smaller beats a cut, and here a cut is worse than nothing at all.
    //
    // WHOLE OR NOT AT ALL, and that is a real difference from the alert row
    // rather than an oversight. The alert is a SENTENCE, and clipToWidth
    // exists because the front of a bouncer's warning still carries the
    // warning ("KLED TARBECK: THAT IS YOUR ONE..."). A place name is four
    // words and the front of it is not a shorter name -- "THE GILDED GULL -
    // ROOMS" cut to what a full corner leaves at 1280x720 is "TH..", which is
    // not an announcement of anywhere. So the plate takes the largest size the
    // whole name fits at, and if the whole name fits at no size it is dropped,
    // exactly as it is dropped when the band is too short.
    int plateScale = scale;
    while (plateScale > 1 && textWidth(state.placePlate, plateScale) > budget) {
        --plateScale;
    }
    const int drawn = textWidth(state.placePlate, plateScale);
    if (drawn > budget) {
        return;
    }
    const int textX = (width - drawn) / 2;

    const TopBand band = topBand(height);
    // THE WHOLE TRAVEL FITS UNDER THE RIBBON BLOCK, NOT JUST THE SETTLED ROW.
    // The plate keeps rising as it fades, so its HIGHEST point (drift == +1,
    // the last frame of the fade) is `lift` above where it settles -- and the
    // first version of this measured the gap from the settled row only, which
    // put the top of the plate through the place-name sub-label's own drop
    // shadow on the last frame of every announcement. The clearance is
    // measured from the top of the travel: one full lift plus a register row
    // of air, which is more than the sub-label's minor row and shadow can be.
    const int lift = 3 * scale;
    const int settledY = band.bottom + lift + 3 * scale;
    const int padX = plateScale * 2;
    const int padY = std::max(1, plateScale / 2);
    const int lowest = settledY + lift + kGlyphH * plateScale + padY;
    if (lowest >= hudCentreRect(width, height).y0) {
        return;
    }
    const float drift = std::clamp(state.placePlateDrift, -1.0F, 1.0F);
    const int y = settledY - static_cast<int>(std::round(drift * static_cast<float>(lift)));
    drawTextPlate(target, textX - padX, y - padY, textX + drawn + padX,
                  y + kGlyphH * plateScale + padY, std::max(1, plateScale / 2), fade);
    // kInk, not the alert's amber. This is the HUD's own register saying where
    // you are, not the house telling you to get out.
    drawText(target, textX, y, state.placePlate, kInk, 0.95F * fade, plateScale);
}

/// The bottom band: the room you are standing in, and then every row that is
/// about you, each one handed a slot out of the space between the health bar
/// and the exclusion rectangle.
void drawBottomBand(Framebuffer& target, const HudState& state, BottomBand& band) {
    const int scale = band.scale();
    const int minor = band.minor();
    const int margin = band.margin();
    const int width = target.width();

    // The room line shares the bottom row with the health bar. It is
    // right-anchored, the bar is 48 scale units of the left edge, and the clip
    // is what PROVES they cannot meet in the middle rather than a hope that
    // they will not.
    if (!state.roomLabel.empty()) {
        const int room = width - 2 * margin - band.barWidth() - kGlyphAdvance * minor;
        const std::string line = clipToWidth(state.roomLabel, room, minor);
        drawText(target, width - margin - textWidth(line, minor),
                 target.height() - margin - rowHeight(minor), line, Rgb{0.72F, 0.70F, 0.62F},
                 0.88F * std::clamp(state.roomFade, 0.0F, 1.0F), minor);
    }

    // THE SHARED SHAPE EVERY ROW BELOW BUT THE ALERT ALREADY HAD: skip an
    // empty label before it can cost a slot, take one, clip to the row's own
    // pixel budget, draw. Three lambdas rather than one, because where the
    // row lands is not negotiable -- centred (interactLabel, lockLabel),
    // left-anchored (caseLabel, guildLabel, objectiveLabel) and right-anchored
    // (rivalLabel) are three different promises to the exclusion rectangle and
    // collapsing them into a single "anchor" flag would be one more thing a
    // caller could get backwards. Colour is still every caller's own choice --
    // caseLabel and rivalLabel pick theirs from the UNCLIPPED label, exactly
    // as the comment below explains, and that stays their code, not this one's.
    const int rowBudget = width - 2 * margin;
    const auto takeCentred = [&](std::string_view label, const Rgb& colour, float alpha) {
        if (label.empty()) {
            return;
        }
        const int y = band.take(minor);
        if (y < 0) {
            return;
        }
        const std::string line = clipToWidth(label, rowBudget, minor);
        const int drawn = textWidth(line, minor);
        drawText(target, std::max(margin, (width - drawn) / 2), y, line, colour, alpha, minor);
    };
    const auto takeLeft = [&](std::string_view label, const Rgb& colour, float alpha) {
        if (label.empty()) {
            return;
        }
        const int y = band.take(minor);
        if (y < 0) {
            return;
        }
        const std::string line = clipToWidth(label, rowBudget, minor);
        drawText(target, margin, y, line, colour, alpha, minor);
    };
    const auto takeRight = [&](std::string_view label, const Rgb& colour, float alpha) {
        if (label.empty()) {
            return;
        }
        const int y = band.take(minor);
        if (y < 0) {
            return;
        }
        const std::string line = clipToWidth(label, rowBudget, minor);
        drawText(target, width - margin - textWidth(line, minor), y, line, colour, alpha, minor);
    };

    // PRIORITY ORDER, AND IT IS AN ARGUMENT. A shout outranks a lock, a lock
    // outranks the case, the case outranks the man hunting you, and the rung
    // you hold and the errand you are on are the two things a player can go and
    // look up at leisure. At 640x360 all six fit and none of this ever fires.
    if (!state.alert.empty() && state.showAlert) {
        // AND DRAWN SMALLER RATHER THAN CUT.
        //
        // S10: clipping is the last resort and it used to be the first. The 4x6
        // font is scaled by the frame HEIGHT, so at 1280x720 a glyph is twenty
        // pixels wide and the row holds sixty-four characters however wide the
        // window is -- and the first S10 capture shipped a clue reading "...THE
        // FACE FIXED IN..". A line the player has to read is worth a size
        // smaller; a line nobody can finish is worth nothing. So take the
        // largest scale it fits at, down to 1, and only then clip.
        int alertScale = scale;
        while (alertScale > 1 && textWidth(state.alert, alertScale) > width - 2 * margin) {
            --alertScale;
        }
        // CLIPPED HERE, WHICH IS THE ONLY PLACE THAT CAN DO IT HONESTLY. S6
        // shipped docs/frames/s6-skyrun-quiet.png reading "KLED TARBECK: THAT
        // IS YOUR ONE. OUT OF THIS HOUSE, OR I PUT YOU" with the last two words
        // drawn off the right edge, cut mid-glyph, because Session clipped what
        // it composed itself to a guessed 56 columns and the bouncer's warning
        // never went through it. A caller-side column count is a guess about a
        // frame it cannot see. The frame is here.
        const std::string alert = clipToWidth(state.alert, width - 2 * margin, alertScale);
        const int y = band.take(alertScale);
        if (y >= 0) {
            const int drawn = textWidth(alert, alertScale);
            const int textX = std::max(margin, (width - drawn) / 2);
            const float rowAlpha = std::clamp(state.alertFade, 0.0F, 1.0F);
            // HARDENING PASS. THE ALERT IS THE SINGLE MOST URGENT LINE ON THIS
            // HUD -- a bouncer's own warning -- and used to draw with only the
            // engine's generic 1px drop shadow behind it, which a real capture
            // showed floating nearly unreadable over open floor. Backed now by
            // the S8 pop-up's own plate -- see drawTextPlate's own header --
            // sized tight to this row's glyph box with a small margin, drawn
            // BEFORE the text so the plate never paints over it.
            // Padding is generous on X (legibility matters most there) and
            // deliberately tight on Y -- band.take() reserved this row a slot
            // sized off `minor`, not `alertScale`, and a tall plate risks
            // eating into whatever row the priority order above stacks next.
            //
            // INNOVATION SPRINT ITEM #3. `pulse` (0..1, HudState::alertPulse's
            // own header) briefly overshoots the pad and the border by a
            // pixel or two, easing back to the ordinary settled size drawn
            // above -- the plate landing with a little weight instead of
            // merely popping into place. AT REST (pulse == 0, every caller
            // before this field existed) this is bit-for-bit the same plate
            // drawn before it existed.
            const float pulse = std::clamp(state.alertPulse, 0.0F, 1.0F);
            const int pulsePad = static_cast<int>(std::round(static_cast<float>(alertScale) * pulse));
            const int padX = alertScale * 2 + pulsePad;
            const int padY = std::max(1, alertScale / 2) + pulsePad;
            drawTextPlate(target, textX - padX, y - padY, textX + drawn + padX,
                          y + kGlyphH * alertScale + padY,
                          std::max(1, alertScale / 2 + pulsePad), rowAlpha);
            // TASK #83. Eased in Session, not here -- see HudState::alertFade.
            // A caller that never set it gets 1, which is 0.95F unchanged.
            drawText(target, textX, y, alert, Rgb{0.90F, 0.62F, 0.30F}, 0.95F * rowAlpha,
                     alertScale);
        }
    }
    // SPELLS BUILD. THE QUICK BAR STRIP, bottom-centre -- the hotbar slot
    // this file's own header has reserved in Barony's name since task #66,
    // finally holding something. Right after the alert in priority: while it
    // is up at all, it is up because the player's hand is ON it (a held
    // wheel, a number press), which is exactly when it must not be the row
    // that gets dropped. Out of the band's slot grid like every row here, so
    // it can never land in the play space or on another row's pixels; behind
    // its own plate, because ten dim digits over open floor is the alert's
    // own S8 legibility failure again; and TRANSIENT (quickBarFade is a
    // Session-side EasedToggle), because Barony's hotbar is furniture and
    // this game's TES-quiet bar is not.
    if (state.quickBarFade > 0.0F) {
        const float fade = std::clamp(state.quickBarFade, 0.0F, 1.0F);
        const int y = band.take(minor);
        if (y >= 0) {
            constexpr int kQuickSlots = 10;
            const int cellW = (kGlyphAdvance + 2) * minor;
            const int cellsW = kQuickSlots * cellW;
            // The selected slot's own name rides the same row -- the digits
            // say which slots are loaded, the name says with what.
            std::string name;
            if (state.quickSelected >= 0 && state.quickSelected < kQuickSlots) {
                const std::string_view picked =
                    state.quickSlots[static_cast<std::size_t>(state.quickSelected)];
                name = clipToWidth(picked, rowBudget - cellsW - 2 * kGlyphAdvance * minor,
                                   minor);
            }
            const int nameW =
                name.empty() ? 0 : 2 * kGlyphAdvance * minor + textWidth(name, minor);
            const int x0 = std::max(margin, (width - cellsW - nameW) / 2);
            const int padY = std::max(1, minor / 2);
            drawTextPlate(target, x0 - 2 * minor, y - padY, x0 + cellsW + nameW + 2 * minor,
                          y + kGlyphH * minor + padY, std::max(1, minor / 2), 0.80F * fade);
            for (int slot = 0; slot < kQuickSlots; ++slot) {
                const int cx = x0 + slot * cellW;
                const bool loaded = !state.quickSlots[static_cast<std::size_t>(slot)].empty();
                const bool equipped = slot == state.quickEquipped;
                const bool selected = slot == state.quickSelected;
                if (equipped) {
                    // INVERTED, the strongest mark on the strip: this cell is
                    // what the next press of Cast spends, the same fact the
                    // CAST row reads off the same equipped id.
                    target.fillRect(cx, y - padY, cellW - minor, kGlyphH * minor + 2 * padY,
                                    kPlateBone, 0.90F * fade);
                }
                if (selected) {
                    // FRAMED, one pixel-row of border: where the wheel or the
                    // number row last pointed, which need not be the equipped
                    // cell (an empty slot can be selected and says so).
                    const Rgb gold{0.85F, 0.80F, 0.60F};
                    const int frameY0 = y - padY;
                    const int frameY1 = y + kGlyphH * minor + padY;
                    target.fillRect(cx, frameY0, cellW - minor, minor, gold, fade);
                    target.fillRect(cx, frameY1 - minor, cellW - minor, minor, gold, fade);
                    target.fillRect(cx, frameY0, minor, frameY1 - frameY0, gold, fade);
                    target.fillRect(cx + cellW - 2 * minor, frameY0, minor,
                                    frameY1 - frameY0, gold, fade);
                }
                const char digit[2] = {static_cast<char>(slot == 9 ? '0' : '1' + slot), '\0'};
                const Rgb ink = equipped ? kPlateBlack : kPlateBone;
                const float inkAlpha = equipped ? fade : (loaded ? 0.95F : 0.35F) * fade;
                drawText(target, cx + (cellW - kGlyphW * minor) / 2, y, digit, ink, inkAlpha,
                         minor);
            }
            if (!name.empty()) {
                drawText(target, x0 + cellsW + 2 * kGlyphAdvance * minor, y, name,
                         Rgb{0.90F, 0.87F, 0.76F}, 0.92F * fade, minor);
            }
        }
    }
    // #85. THE RESOLVED INTERACT VERB. Right after the alert, ahead of the
    // lock -- the two never draw together (interactLabel is empty exactly
    // while a lock is open, Session::interactPrompt() stands down for
    // picking() the same way it does for talking()), but the alert (a
    // bouncer's own warning) still outranks everything on this edge.
    //
    // HARDENING PASS: EASED, LIKE THE ALERT ABOVE. state.interactFade is
    // Session's own render::EasedToggle for this row -- see hud.hpp's own
    // note on why it is not the alert's alertFade reused.
    takeCentred(state.interactLabel, Rgb{0.85F, 0.80F, 0.60F},
                0.92F * std::clamp(state.interactFade, 0.0F, 1.0F));
    // The lock under the wire. A lockpicking minigame is exactly the element
    // that would otherwise become a panel in the middle of the screen, which is
    // the failure this HUD is built against; it gets one row on an edge.
    takeCentred(state.lockLabel, Rgb{0.78F, 0.74F, 0.56F},
                0.92F * std::clamp(state.lockFade, 0.0F, 1.0F));
    // FIRST-PERSON COMBAT (S13). The held guard, right behind the lock: a
    // fight and a lock never run at once, and a shout still outranks both.
    // Steel-cool ink, the same register as the blocked-blow wash, so the row
    // and the flash read as one fact.
    takeCentred(state.blockLabel, Rgb{0.62F, 0.70F, 0.80F},
                0.92F * std::clamp(state.blockFade, 0.0F, 1.0F));
    // The case. IT IS THE COLOUR OF THE WARD'S NERVE and not a fixed one: a
    // player who has frightened the district enough that nobody walks the
    // Gullet alone should see that without reading the words.
    // caseLabel/guildLabel/objectiveLabel below are LEFT-anchored, and
    // rivalLabel is RIGHT-anchored; all four are now clipped to the pixel
    // budget the row actually has (the same `width - 2*margin` roomLabel and
    // lockLabel already use), not trusted bare off session.cpp's own guessed
    // character count. Left-anchored text session.cpp under-clipped for this
    // frame runs off the RIGHT edge mid-glyph with no mark that anything was
    // cut; right-anchored text under-clipped starts its draw at a negative x
    // and runs off the LEFT edge instead, losing its own front rather than
    // its tail. See test_render.cpp's "a bottom-band or top-right label does
    // not run off the frame at an off-16:9 window".
    {
        const bool afraid = state.caseLabel.find("EMPTYING") != std::string_view::npos ||
                            state.caseLabel.find("ALONE") != std::string_view::npos;
        takeLeft(state.caseLabel, afraid ? Rgb{0.86F, 0.44F, 0.36F} : Rgb{0.70F, 0.72F, 0.66F},
                 0.90F * std::clamp(state.caseFade, 0.0F, 1.0F));
    }
    // The man who put you here, anchored to the right edge so a long name
    // cannot run off the frame the way S6 alert did. A hunted man should not
    // have to read the line to notice it. Checked against the UNCLIPPED
    // label: HUNTING is always the last word rivalLine() appends, and
    // clipping only ever removes the tail, so the unclipped field still
    // answers this correctly even on the frame narrow enough to have dropped
    // the word.
    {
        const bool hunting = state.rivalLabel.find("HUNTING") != std::string_view::npos;
        takeRight(state.rivalLabel, hunting ? Rgb{0.88F, 0.40F, 0.30F} : Rgb{0.74F, 0.60F, 0.52F},
                  0.90F * std::clamp(state.rivalFade, 0.0F, 1.0F));
    }
    takeLeft(state.guildLabel, Rgb{0.86F, 0.74F, 0.44F},
             0.92F * std::clamp(state.guildFade, 0.0F, 1.0F));
    takeLeft(state.objectiveLabel, Rgb{0.62F, 0.66F, 0.72F},
             0.80F * std::clamp(state.objectiveFade, 0.0F, 1.0F));
}

}  // namespace

void drawHud(Framebuffer& target, const HudState& state) {
    BottomBand band(target.width(), target.height());
    if (state.showHealth) {
        drawHealth(target, state, band);
    }
    // FATIGUE BUILD: the wind bar rides the health bar's own gate AND its own
    // fade -- showHealth is the rule (the bottom band belongs to the topic
    // list mid-conversation), fatigueFade is the ease the Session's toggle
    // drives across that rule changing.
    if (state.showHealth) {
        drawFatigue(target, state, band);
    }
    if (state.showCompass) {
        drawCompass(target, state);
    }
    // DISTRICT PHASE D. THE CORNER FIRST, THEN THE PLATE THAT HAS TO CLEAR IT
    // -- drawTopRight returns the width it claimed, and the plate's budget is
    // what is left of the top band between two of them. See drawPlacePlate.
    //
    // NOT GATED ON showCompass. The plate carries its own fade and Session
    // puts that to zero for precisely the cases showCompass is false for (a
    // panel owns the screen), so a gate here would be the same test written
    // twice in two places -- which is the drift conversingNow() exists to stop.
    const int rightBlock = drawTopRight(target, state);
    drawPlacePlate(target, state, rightBlock);
    drawBottomBand(target, state, band);
}

}  // namespace granadad::render
