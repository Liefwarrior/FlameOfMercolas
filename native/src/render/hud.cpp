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

namespace {

/// THE RETICLE AND THE PROMPT, RESOLVED ONCE.
///
/// hudAimRect() promises a rectangle and drawAim() writes pixels; if those two
/// were computed from two different pieces of arithmetic the promise would be
/// a comment rather than a clamp. Both call this.
///
/// Everything is in units of hudMinorScale -- the register the player reads
/// DELIBERATELY, which this is: you are pointing at something and asking what
/// it is. Drawing it at hudScale would put the loudest type in the game in the
/// middle of the play space, which is the failure this whole file is against.
struct AimBox {
    int unit = 1;
    /// The frame centre. The reticle sits astride it and the aim point IS it.
    int cx = 0;
    int cy = 0;
    /// A tick is `arm` long and `unit` thick, standing `gap` clear of centre,
    /// so the pixel actually being aimed at is never painted over.
    int gap = 1;
    int arm = 2;
    int reach = 3;
    int rowStep = 7;
    /// The prompt's left edge and the top of each of its two rows. UP AND TO
    /// THE RIGHT, per the owner's own sentence: every pixel of both rows is
    /// above the horizontal centre line and right of the vertical one, so the
    /// prompt never sits on the thing it is naming.
    int textX = 0;
    int verbY = 0;
    int subjectY = 0;
    /// The HUD's ordinary margin. A long name is clipped here rather than run
    /// off the frame -- the S6 alert's own lesson, in the one element that
    /// takes an arbitrary proper noun straight off the sign table.
    int rightEdge = 0;
};

[[nodiscard]] AimBox aimBox(int width, int height) noexcept {
    AimBox box;
    box.unit = hudMinorScale(height);
    box.cx = width / 2;
    box.cy = height / 2;
    box.gap = box.unit;
    box.arm = 2 * box.unit;
    box.reach = box.gap + box.arm;
    box.rowStep = rowHeight(box.unit);
    // Two units of air past the tick, so the prompt reads as ATTACHED to the
    // reticle without touching it. Chosen off captures at 320x180, 960x540 and
    // 1920x1080, not from a constant that looked right in one of them.
    box.textX = box.cx + box.reach + 2 * box.unit;
    box.verbY = box.cy - 2 * box.unit - box.rowStep;
    box.subjectY = box.verbY - box.rowStep;
    box.rightEdge = width - 6 * hudScale(height);
    return box;
}

/// WHAT THE TWO PROMPT ROWS ACTUALLY COME TO, WITHOUT DRAWING ANYTHING.
///
/// drawAim lays out its rows from this and the signage renderer asks it for
/// the rectangle to keep clear, so there is ONE description of where the
/// prompt is. The alternative -- signage carrying its own copy of "textX plus
/// the verb's width" -- is the S4 mistake in miniature: a paging fix tested
/// through arithmetic the draw code never called.
///
/// PURE. No framebuffer. `draws` is false when drawAim would return early, so
/// a caller can ask about a frame with nothing under the reticle.
struct AimRows {
    bool draws = false;
    int textX = 0;
    int subjectY = 0;
    int verbY = 0;
    int unit = 1;
    int gutter = 0;
    int subjectW = 0;
    int noteW = 0;
    int verbW = 0;
    std::string subject;
    std::string note;
    std::string verbRow;
};

[[nodiscard]] AimRows aimRows(const HudState& state, int width, int height) {
    AimRows out;
    if (state.aimVerb.empty() || std::clamp(state.interactFade, 0.0F, 1.0F) <= 0.0F) {
        return out;
    }
    const AimBox box = aimBox(width, height);
    const CentreRect fence = hudAimRect(width, height);
    // TWO UNITS OF SLACK ON THE BUDGET, NOT ONE. drawText hangs a one-unit
    // drop shadow off the right of the last glyph it draws and clipToWidth
    // measures the glyphs alone, so a budget of exactly the room available
    // puts the shadow of the final letter one pixel past the fence. Found by
    // the case that counts escaped pixels, which is what it is for.
    const int budget = fence.x1 - box.textX - 2 * box.unit;
    if (budget <= 0) {
        return out;
    }
    out.draws = true;
    out.textX = box.textX;
    out.subjectY = box.subjectY;
    out.verbY = box.verbY;
    out.unit = box.unit;
    // THE SUBJECT ROW, above the verb. Absent when nothing in reach has a name
    // -- and absent is the honest answer, not a bug: LOOK at open cobbles is
    // LOOK at open cobbles, and inventing a label for it would be the machine
    // talking rather than the ward.
    out.gutter = 2 * kGlyphAdvance * box.unit;
    if (!state.aimSubject.empty()) {
        out.subject = clipToWidth(state.aimSubject, budget, box.unit);
        out.subjectW = textWidth(out.subject, box.unit);
        // The note takes what is left after the subject and a gutter of two
        // glyph advances, and is DROPPED WHOLE rather than cut to a stub: a
        // qualifier reading "ALREADY R.." qualifies nothing.
        const int left = budget - out.subjectW - out.gutter;
        if (!state.aimNote.empty() && left >= 6 * kGlyphAdvance * box.unit) {
            out.note = clipToWidth(state.aimNote, left, box.unit);
        }
    }
    out.noteW = out.note.empty() ? 0 : textWidth(out.note, box.unit);

    // THE VERB ROW, AND IT IS THE ANCHOR. "E - TALK", the reference's own key
    // grammar (`e - Establish`, `0 - Back`), in the key colour. Its y never
    // moves: the subject row grows upward off it, so sweeping the crosshair
    // across a doorway does not make the verb jump a row under the player's
    // eye. That is "panes hold their height" on the smallest surface here.
    std::string verb(state.aimKey);
    if (!verb.empty()) {
        verb += " - ";
    }
    verb += std::string(state.aimVerb);
    out.verbRow = clipToWidth(verb, budget, box.unit);
    out.verbW = textWidth(out.verbRow, box.unit);
    return out;
}

}  // namespace

CentreRect hudAimPromptRect(const HudState& state, int width, int height) {
    const AimRows rows = aimRows(state, width, height);
    if (!rows.draws) {
        return CentreRect{0, 0, 0, 0};
    }
    // The union of both rows INCLUDING each row's scrim air -- one unit out on
    // every side, which is what scrim() adds. A caller keeping clear of this
    // keeps clear of what is actually painted, not of the glyph boxes alone.
    const int subjectRowW =
        rows.subject.empty() ? 0 : rows.subjectW + (rows.note.empty() ? 0 : rows.gutter + rows.noteW);
    const int widest = std::max(subjectRowW, rows.verbW);
    const int top = rows.subject.empty() ? rows.verbY : rows.subjectY;
    return CentreRect{rows.textX - rows.unit, top - rows.unit,
                      rows.textX + widest + rows.unit,
                      rows.verbY + kGlyphH * rows.unit + rows.unit};
}

CentreRect hudAimRect(int width, int height) noexcept {
    const AimBox box = aimBox(width, height);
    // `subjectY - unit` and not `subjectY`: the row's own scrim stands a unit
    // of air above its glyphs, and a fence that cut it would clip the top of
    // the very thing it exists to bound.
    // `+ unit` on the bottom: the lowest tick carries the font's own one-unit
    // drop shadow (see drawAim) and a fence that stopped at the tick would
    // shave it off on the one edge where nothing else is drawn.
    return CentreRect{box.cx - box.reach, std::min(box.subjectY - box.unit, box.cy - box.reach),
                      std::max(box.rightEdge, box.cx + box.reach + 1),
                      box.cy + box.reach + box.unit + 1};
}

CentreRect hudAimVerbRow(int width, int height) noexcept {
    const AimBox box = aimBox(width, height);
    // THE GLYPH BAND, NOT THE SCRIM'S. The two rows' scrims deliberately meet
    // and overlap by their shared unit of air, so a band that included the
    // verb scrim's top margin would report the SUBJECT row appearing as the
    // verb row moving. What must not move is the verb's own ink, and this is
    // exactly the band that holds it.
    return CentreRect{box.textX - box.unit, box.verbY,
                      std::max(box.rightEdge, box.textX + box.unit),
                      box.verbY + box.rowStep};
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
    // THE PULL PACK: TICKS ON THE RIBBON FOR DISCOVERED NAMED PLACES (owner
    // ruling D8). On the strip's TOP rail -- the frame line above the strip
    // and the strip's own first row, which the letters never reach (their
    // first glyph row is y + scale) and which no drop shadow can cross
    // (shadows fall down and right). Bone for a named place, amber -- the
    // fixed mark's own ink -- for the followed lead's site, and wider, so
    // the eye lines it up with the mark without reading a word; when the two
    // coincide you are facing it. Drawn BEFORE the mark so the mark stays on
    // top. Every tick sits on its TRUE bearing (pull.hpp's pullTickBam, a
    // real atan2): a notch snapped to a compass letter would sit under the
    // letter, not on the bearing. A bearing on a strip of sky, not an arrow
    // in the world: the doctrine's own shape.
    const auto tickDelta = [&](std::int32_t bam) -> std::int32_t {
        std::int32_t delta = (bam & 65535) - (state.yawBam & 65535);
        return ((delta + 32768) & 65535) - 32768;  // to [-32768, 32768)
    };
    const auto tickX = [&](std::int32_t delta) -> int {
        return x + stripW / 2 + static_cast<int>(static_cast<float>(delta) * pixelsPerBam);
    };
    const int tickY = y - scale;
    const auto notch = [&](int px, int halfWidth, const Rgb& ink, float alpha) {
        // A tick at the strip's very edge (a place dead abeam) is clipped to
        // the strip rather than dropped: half a notch at the rail still says
        // "there, just off the arc", which is the fact a body turning wants.
        const int x0 = std::max(x, px - halfWidth);
        const int x1 = std::min(x + stripW, px + halfWidth + scale);
        if (x1 > x0) {
            target.fillRect(x0, tickY, x1 - x0, 2 * scale, ink, alpha);
        }
    };
    for (const std::int32_t bam : state.placeTickBams) {
        const std::int32_t delta = tickDelta(bam);
        if (delta < -16384 || delta > 16384) {
            continue;  // outside the arc: a place has no peg, only the pull does
        }
        notch(tickX(delta), scale / 2, Rgb{0.90F, 0.87F, 0.78F}, 0.95F);
    }
    if (state.pullTickBam >= 0) {
        // BEHIND YOU THE PULL DOES NOT VANISH: it pegs at the nearer rail,
        // half a notch's worth, so a turn keeps something to turn toward.
        const std::int32_t delta = tickDelta(state.pullTickBam);
        const bool behind = delta < -16384 || delta > 16384;
        const int px = behind ? (delta < 0 ? x : x + stripW - scale) : tickX(delta);
        notch(px, behind ? scale / 2 : scale, Rgb{0.95F, 0.80F, 0.35F}, behind ? 0.85F : 1.0F);
    }
    // The fixed mark. One pixel column, at the very top edge of the strip, so
    // it never encroaches on the view.
    target.fillRect(x + stripW / 2, y, scale, 2 * scale, Rgb{0.95F, 0.80F, 0.35F}, 1.0F);

    // UI-EA (LANE HUD): THE SUB-LABEL ROW WAS EMPTIED. The place name used
    // to be printed here every frame; the word diet deleted it -- the
    // threshold plate announces every crossing at the moment it happens, and
    // that plate still settles on this band's own labelY-derived row (see
    // topBand/drawAnnouncePlate), so the geometry the blessed travel frames
    // were taken against has not moved a pixel.
    //
    // THE PULL PACK PUTS ONE LINE BACK, and it is the one line the rest
    // budget grew by (UI-EA-SPEC 1.2 #11): the FOLLOWED LEAD -- "NE 40  THE
    // WEIGHHOUSE" -- which changes every step you walk, so it is a word on
    // screen because it moves, not because it is true. Drawn by drawPullLine
    // from drawHud, AFTER the top-right stack has measured itself, because
    // this is a centred line in the same band as a right-anchored corner and
    // the corner wins -- the announce plate's own rule, one row up.
}

/// THE FOLLOWED LEAD, under the ribbon. Centred on the frame at the minor
/// size (reference material, read deliberately), on the sub-label row the
/// diet emptied. `rightBlock` is what drawTopRight just claimed, and the
/// line's budget is what is left between two of them -- symmetric, because a
/// centred line that stays centred by eating its own left margin is not
/// centred. When the whole line does not fit it sheds the place's leading
/// article first (the map plan's own label rule), then clips with the mark,
/// so the bearing and the paces -- the two numbers that change as you walk
/// -- are never the part that goes.
void drawPullLine(Framebuffer& target, const HudState& state, int rightBlock) {
    if (state.pullLabel.empty()) {
        return;
    }
    const int width = target.width();
    const int height = target.height();
    const int scale = hudScale(height);
    const int minor = hudMinorScale(height);
    const int margin = 6 * scale;
    // THREE SCALE UNITS OF AIR OFF THE CORNER, each side: a line that ended
    // flush against SEEN read as one word with it.
    const int budget = width - 2 * (margin + std::max(0, rightBlock) + 3 * scale);
    if (budget <= 0) {
        return;
    }
    std::string line(state.pullLabel);
    if (textWidth(line, minor) > budget) {
        // "NE 40  THE WEIGHHOUSE" -> "NE 40  WEIGHHOUSE": the article is the
        // first thing to go, exactly as casebook_page.cpp's shortPlace and
        // map_view.cpp's label rule remove it.
        const std::size_t gap = line.find("  THE ");
        if (gap != std::string::npos) {
            line.erase(gap + 2, 4);
        }
    }
    if (textWidth(line, minor) > budget) {
        // STILL TOO WIDE: the PLACE is what gets cut, marked, and the two
        // numbers that change as you walk -- the point and the paces in
        // front, the plane behind -- stay whole. The line is "bearing
        // place" or "bearing  place  BELOW", two cells of air between parts.
        // THE CUT IS ON A WORD, and never after an article: "MISSION OF
        // THE.." names nothing, "MISSION.." names the Mission.
        const std::size_t first = line.find("  ");
        const std::size_t tailAt = line.rfind("  ");
        if (first != std::string::npos) {
            const std::string head = line.substr(0, first + 2);
            const bool hasTail = tailAt != std::string::npos && tailAt > first;
            const std::string tail = hasTail ? line.substr(tailAt) : std::string();
            std::string place =
                line.substr(first + 2, hasTail ? tailAt - (first + 2) : std::string::npos);
            const int room = budget - textWidth(head, minor) - textWidth(tail, minor);
            // Whole words off the end until the place and its mark fit, then
            // the articles and joints a cut leaves dangling.
            const auto fits = [&](const std::string& text) {
                return textWidth(text + "..", minor) <= room;
            };
            bool cut = false;
            while (!place.empty() && !fits(place)) {
                const std::size_t space = place.rfind(' ');
                place = space == std::string::npos ? std::string() : place.substr(0, space);
                cut = true;
            }
            for (bool trimmed = true; trimmed && cut;) {
                trimmed = false;
                for (const char* joint : {" THE", " OF", " AND", " O'"}) {
                    const std::string_view word(joint);
                    if (place.size() > word.size() &&
                        place.compare(place.size() - word.size(), word.size(), word) == 0) {
                        place.erase(place.size() - word.size());
                        trimmed = true;
                    }
                }
            }
            if (cut && !place.empty()) {
                place += "..";
            }
            line = place.empty() ? head.substr(0, first) + tail : head + place + tail;
        }
    }
    if (textWidth(line, minor) > budget) {
        line = clipToWidth(line, budget, minor);
    }
    if (line.empty()) {
        return;
    }
    const TopBand band = topBand(height);
    const int drawn = textWidth(line, minor);
    // The old sub-label's own ink and strength, so the blessed frames that
    // carried a place name here read the same weight of bone.
    drawText(target, (width - drawn) / 2, band.labelY, line, Rgb{0.86F, 0.82F, 0.68F}, 0.90F,
             minor);
}

/// THE SKILL-UP TOAST, top-left. Oblivion prints "Heavy Armor increased to
/// 39" in the corner mid-fight and never pauses; this is that beat in the
/// HUD's own register -- a plate, the way every announcement in this HUD is a
/// plate, rising through its resting row and drifting out as it fades.
///
/// TOP-LEFT BECAUSE IT IS THE ONE FREE CORNER. The compass owns the top
/// centre and the announce plate under it (a lead opening, a crossing); the
/// hour and the ward's opinion own the top right; the bars own the bottom
/// left and the state rows the bottom centre. A skill rising is the fourth
/// kind of news and it gets the fourth corner, so it never fights the case
/// plate for the one announcement slot and never lands on FISTS UP mid-brawl.
///
/// WHOLE OR SMALLER, NEVER CUT. Its budget is the sky between the left margin
/// and the ribbon's own left edge; it takes the largest size the whole line
/// fits at, and when the longest name in the raws ("CRACKSMANSHIP RISES TO
/// 12") fits at no size -- which is 320x180 -- it breaks into two rows at the
/// name, because a skill name cut in half is not a skill.
void drawSkillToast(Framebuffer& target, const HudState& state) {
    const float fade = std::clamp(state.skillToastFade, 0.0F, 1.0F);
    if (state.skillToast.empty() || fade <= 0.0F) {
        return;
    }
    const int width = target.width();
    const int height = target.height();
    const int scale = hudScale(height);
    const int margin = 6 * scale;
    const int stripW = std::min(width / 4, 120 * scale);
    const int stripX = (width - stripW) / 2;
    // Two scale units of air off the ribbon's frame, and the plate's own
    // padding inside the budget (the announce plate's rule).
    const int budget = stripX - scale - margin - 2 * scale - 2 * scale;
    if (budget <= 0) {
        return;
    }
    std::string_view rows[2] = {state.skillToast, std::string_view{}};
    int rowCount = 1;
    int plateScale = scale;
    while (plateScale > 1 && textWidth(rows[0], plateScale) > budget) {
        --plateScale;
    }
    if (textWidth(rows[0], plateScale) > budget) {
        // At the name: "CRACKSMANSHIP" / "RISES TO 12".
        const std::size_t cut = state.skillToast.rfind(" RISES TO ");
        if (cut == std::string_view::npos) {
            return;
        }
        rows[0] = state.skillToast.substr(0, cut);
        rows[1] = state.skillToast.substr(cut + 1);
        rowCount = 2;
        if (textWidth(rows[0], plateScale) > budget || textWidth(rows[1], plateScale) > budget) {
            return;
        }
    }
    int drawn = 0;
    for (int i = 0; i < rowCount; ++i) {
        drawn = std::max(drawn, textWidth(rows[i], plateScale));
    }
    const TopBand band = topBand(height);
    const int lift = 3 * scale;
    // Settles on the ribbon's own top row, so the two top blocks share a
    // line -- the corner's rule, mirrored.
    const int settledY = band.stripY + lift;
    const int padX = plateScale * 2;
    // A FULL SCALE UNIT OF PAD, and the text's height counts every row's
    // drop shadow: a plate shallower than the shadow let the letters' feet
    // spill under its bottom rail.
    const int padY = plateScale;
    const int textH = rowCount * rowHeight(plateScale);
    const int lowest = settledY + lift + textH + padY;
    if (lowest >= hudCentreRect(width, height).y0) {
        return;
    }
    const float drift = std::clamp(state.skillToastDrift, -1.0F, 1.0F);
    const int y = settledY - static_cast<int>(std::round(drift * static_cast<float>(lift)));
    const int textX = margin + padX;
    drawTextPlate(target, textX - padX, y - padY, textX + drawn + plateScale + padX,
                  y + textH + padY, std::max(1, plateScale / 2), fade);
    // The number ink, the same green the case plate spends on "the trail
    // grew": a level is the one other place on this frame where green means
    // you gained something.
    for (int i = 0; i < rowCount; ++i) {
        drawText(target, textX, y + i * rowHeight(plateScale), rows[i],
                 Rgb{0.62F, 0.88F, 0.56F}, 0.95F * fade, plateScale);
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

    // UI-EA (LANE HUD): THE CLOCK IS EARNED TEXT. clockFade is Session's own
    // EasedToggle -- up on an hour tick, a time charge or the wait page,
    // asleep otherwise -- and a fully asleep clock costs neither its row nor
    // its width: the corner packs up as if it were never there, exactly what
    // "absence costs nothing" has meant on this stack since polish-1.
    const float clockFade = std::clamp(state.clockFade, 0.0F, 1.0F);
    if (state.timeOfDaySeconds >= 0 && clockFade > 0.0F && y + rowHeight(scale) <= ceiling) {
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
        drawText(target, target.width() - margin - textWidth(clock, scale), y, clock, kInk,
                 0.9F * clockFade, scale);
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
    // UI-EA (LANE HUD): AN INVISIBLE ROW COSTS NOTHING. Every row's cache
    // outlives its own fade on purpose (so the fade has words to fade), which
    // used to mean a row eased to zero still claimed a slot and pushed live
    // rows down the sky while drawing no ink at all. With most of this stack
    // asleep at rest that ghost cost would be most of the corner, so a row at
    // zero alpha is skipped before it can take a slot -- visually a no-op for
    // every caller there has ever been.
    const auto add = [&](std::string_view text, const Rgb& ink, float alpha, int rank) {
        if (!text.empty() && alpha > 0.0F && count < rows.size()) {
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
        // UI-EA (LANE HUD): the purse wakes on a coin delta and sleeps after
        // -- purseFade is Session's toggle, default 1 for every hand-built
        // state, and add() above drops the row whole once it is fully asleep.
        purse = clipToWidth(std::to_string(std::min(state.coin, 99999)) + " C", rowBudget, minor);
        add(purse, Rgb{0.82F, 0.72F, 0.38F}, 0.9F * std::clamp(state.purseFade, 0.0F, 1.0F), 3);
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
///
/// THE CASEBOOK PASS MADE IT SERVE TWO NOTICES rather than one. The lead-opened
/// plate (HudState::casePlate) is the same shape saying the same kind of thing
/// -- an edge, once, then gone -- and it wants the same slot, so the text, the
/// fade, the drift and the ink arrive as arguments and drawHud picks which
/// notice is on the frame. Two announcements stacked in one band is two notices
/// fighting; the case plate OUTRANKS the crossing, because a lead opening is
/// rarer and is the thing that was never visible at all.
void drawAnnouncePlate(Framebuffer& target, std::string_view text, float rawFade, float rawDrift,
                       const Rgb& ink, int rightBlock) {
    const float fade = std::clamp(rawFade, 0.0F, 1.0F);
    if (text.empty() || fade <= 0.0F) {
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
    while (plateScale > 1 && textWidth(text, plateScale) > budget) {
        --plateScale;
    }
    const int drawn = textWidth(text, plateScale);
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
    const float drift = std::clamp(rawDrift, -1.0F, 1.0F);
    const int y = settledY - static_cast<int>(std::round(drift * static_cast<float>(lift)));
    drawTextPlate(target, textX - padX, y - padY, textX + drawn + padX,
                  y + kGlyphH * plateScale + padY, std::max(1, plateScale / 2), fade);
    // kInk, not the alert's amber. This is the HUD's own register saying where
    // you are, not the house telling you to get out.
    drawText(target, textX, y, text, ink, 0.95F * fade, plateScale);
}

// ---------------------------------------------------------------------------
// THE CROSSHAIR PASS -- the reticle, and the prompt hanging off it
// ---------------------------------------------------------------------------

/// Keys and verbs are yellow. The reference's colour table (the "Colour
/// discipline" section of docs/design/UI-REFERENCE-TERMINAL.md) gives one
/// colour per ROLE, and this row is entirely role: a binding and the verb it
/// runs.
constexpr Rgb kAimVerb{0.92F, 0.80F, 0.38F};
/// The qualifier beside the subject -- a trade, a state, what you came for.
/// Deliberately the quietest ink on the frame: it is the third thing read.
constexpr Rgb kAimNote{0.60F, 0.58F, 0.52F};

/// ONE ACCENT PER KIND OF THING A CROSSHAIR CAN LAND ON. Scannable by hue
/// before a word of it is read, which is the whole argument for entity accents
/// in the reference, applied to the one element that appears every second of
/// play.
[[nodiscard]] Rgb aimAccent(int kind) noexcept {
    switch (static_cast<AimKind>(kind)) {
        case AimKind::Person:
            return Rgb{0.90F, 0.72F, 0.58F};  // warm, because it is a body
        case AimKind::Place:
            return Rgb{0.78F, 0.84F, 0.90F};  // cool stone
        case AimKind::Thing:
            return Rgb{0.88F, 0.78F, 0.50F};  // brass and lids
        case AimKind::Clue:
            // THE CASE HAS ITS OWN COLOUR AND NOTHING ELSE ON THIS HUD USES
            // IT. The reference bracket-labels a check in magenta for exactly
            // this reason; a player has to be able to tell "this is the
            // investigation" from "this is a door" without reading.
            return Rgb{0.76F, 0.58F, 0.88F};
        case AimKind::Nothing:
        default:
            return Rgb{0.74F, 0.72F, 0.66F};
    }
}

/// The reticle, and the two rows up and to the right of it.
///
/// NOT A FRAMED PANE, on purpose and per the spec: no border motif, no rule,
/// no plate, no panel grid. What it does carry is a SCRIM -- a soft dark field
/// behind each row at low alpha, no border -- because this is the one surface
/// in the game guaranteed to be drawn over whatever the player happens to be
/// looking at, and the 1px drop shadow that carries the bottom band over a
/// street does not carry a name over a lit sky.
///
/// EVERY PIXEL IS CLAMPED TO hudAimRect. The prompt takes a proper noun
/// straight off the sign table and a trade off the roster; neither has a
/// length this file controls.
void drawAim(Framebuffer& target, const HudState& state) {
    // ACTION-COMBAT BUILD (section 5, channel 1). THE RETICLE IS THE WEAPON, so
    // it draws during a swing hold even with nothing in reach: the interact
    // prompt (aimVerb) is no longer the sole gate -- a live charge fraction
    // draws the reticle on its own, and only the two prompt ROWS below still
    // ride the interact verb. At rest (no verb, no charge) it still returns.
    const float charge = std::clamp(state.aimChargeFrac, 0.0F, 1.0F);
    const bool charging = charge > 0.0F;
    if (state.aimVerb.empty() && !charging) {
        return;
    }
    const float alpha = std::clamp(state.interactFade, 0.0F, 1.0F);
    // The reticle rides its OWN alpha: loud while charging (a deliberate press
    // earns feedback even over nothing), the interact fade otherwise.
    const float reticleAlpha = charging ? std::max(alpha, 0.85F) : alpha;
    if (reticleAlpha <= 0.0F) {
        return;
    }
    const int width = target.width();
    const int height = target.height();
    const AimBox box = aimBox(width, height);
    const CentreRect fence = hudAimRect(width, height);
    // THE ACCENT TAKES THE WARM CHARGE HUE at the hard threshold -- a swing
    // about to land twice as hard reads hot -- and the subject's accent
    // otherwise.
    const Rgb warmCharge{0.95F, 0.55F, 0.20F};
    const Rgb accent = state.aimChargeHard ? warmCharge : aimAccent(state.aimKind);
    // BRIGHT when something is in reach to interact with OR a body sits on the
    // look-ray a swing would land on -- the reticle answering "this will hit".
    const bool onSomething = static_cast<AimKind>(state.aimKind) != AimKind::Nothing ||
                             state.aimChargeOnLine;

    // Clamped fill. Everything below goes through it, reticle included.
    const auto fill = [&](int x, int y, int w, int h, const Rgb& colour, float a) {
        const int x0 = std::max(x, fence.x0);
        const int y0 = std::max(y, fence.y0);
        const int x1 = std::min(x + w, fence.x1);
        const int y1 = std::min(y + h, fence.y1);
        if (x1 > x0 && y1 > y0) {
            target.fillRect(x0, y0, x1 - x0, y1 - y0, colour, a);
        }
    };

    // THE RETICLE. Four ticks around an open centre. It brightens and takes
    // the subject's accent when something is in reach and sits back to a dim
    // bone when nothing is -- the reference frame's "the spatial view
    // highlights the current interaction target", in eight small rectangles.
    const float tickAlpha = (onSomething ? 0.85F : 0.40F) * reticleAlpha;
    const int half = box.unit / 2;
    // THE TICKS RETRACT toward centre across the hold: each slides inward by up
    // to one gap-unit as the charge fills, so the reticle visibly closes on the
    // aim point the longer the swing is held. Zero pull at rest, so an ordinary
    // interact reticle is drawn at exactly the pixels it always was.
    const int pull = static_cast<int>(std::lround(charge * static_cast<float>(box.gap)));
    const int ticks[4][4] = {
        {box.cx - box.reach + pull, box.cy - half, box.arm, box.unit},
        {box.cx + box.gap - pull, box.cy - half, box.arm, box.unit},
        {box.cx - half, box.cy - box.reach + pull, box.unit, box.arm},
        {box.cx - half, box.cy + box.gap - pull, box.unit, box.arm},
    };
    for (const auto& tick : ticks) {
        // THE FONT'S OWN DROP SHADOW, ON A SHAPE THAT IS NOT A GLYPH. The
        // first bright capture of this pass (docs/frames/crosshair/) put a
        // warm reticle over a pale noon sea and it very nearly vanished --
        // the same failure drawText's one-pixel shadow exists to stop, on the
        // one element that has to survive being pointed at anything.
        fill(tick[0] + box.unit, tick[1] + box.unit, tick[2], tick[3], kShadow,
             tickAlpha * 0.75F);
    }
    for (const auto& tick : ticks) {
        fill(tick[0], tick[1], tick[2], tick[3], accent, tickAlpha);
    }

    // A row's own scrim: sized to what is about to be drawn on it, one unit of
    // air around it, and no border. Drawn first so it never paints over ink.
    //
    // AND IT IS PAID FOR BY THE PIXEL, NOT BY THE FRAME.
    //
    // The first measurement of this pass put the street HUD's claimed area up
    // by three points of the frame, nearly all of it this rectangle -- which
    // is the exact thing the owner's "just be more careful with real estate"
    // was about. But the capture over a noon sea (docs/frames/crosshair/) is
    // just as real: the 4x6 font's own drop shadow carries a row over a dark
    // street and does NOT carry a name over a pale sky.
    //
    // So the scrim reads the ground it is about to sit on and charges for
    // exactly as much as that ground costs. Over the ward at night it is
    // effectively not there; over sky, sea or a lit doorway it comes up. The
    // ramp is CONTINUOUS rather than a threshold on purpose -- a step would
    // pop the plate on and off as the player turned, which is worse than
    // either state.
    const auto groundLuma = [&](int x, int y, int w, int h) {
        const int x0 = std::clamp(x, 0, width - 1);
        const int y0 = std::clamp(y, 0, height - 1);
        const int x1 = std::clamp(x + w, x0 + 1, width);
        const int y1 = std::clamp(y + h, y0 + 1, height);
        // Every fourth pixel each way: this is a brightness question, not a
        // measurement, and the answer does not change in the samples between.
        float sum = 0.0F;
        int taken = 0;
        for (int py = y0; py < y1; py += 4) {
            for (int px = x0; px < x1; px += 4) {
                const Rgb ground = unpackRgb(target.pixels()[target.index(px, py)]);
                sum += 0.299F * ground.r + 0.587F * ground.g + 0.114F * ground.b;
                ++taken;
            }
        }
        return taken > 0 ? sum / static_cast<float>(taken) : 0.0F;
    };
    const auto scrim = [&](int x, int y, int w) {
        if (w <= 0) {
            return;
        }
        const int sx = x - box.unit;
        const int sy = y - box.unit;
        const int sw = w + 2 * box.unit;
        // ONE UNIT OF AIR ABOVE AND NONE BELOW, so the two rows' plates MEET
        // rather than overlap: an overlap would make the lower one's sampled
        // ground depend on whether the upper one drew, which is the same
        // stability bug from the other direction. The glyph's own drop shadow
        // is the air below.
        const int sh = kGlyphH * box.unit + box.unit;
        const float luma = groundLuma(sx, sy, sw, sh);
        // BOTH NUMBERS CAME OFF THE CAPTURES, NOT OFF TASTE. Measured under
        // this exact rectangle in the --nohud control: the ward's lamplit
        // Tarwalk reads 0.24 and the drop shadow carries the row over it
        // unaided, so nothing is spent there; the noon sea off the rooftops
        // reads 0.67 and the row is unreadable without help, so it gets all
        // of it. Full weight by 0.46, which is a bright wall in daylight.
        const float need = std::clamp((luma - 0.26F) / 0.20F, 0.0F, 1.0F);
        if (need <= 0.0F) {
            return;
        }
        fill(sx, sy, sw, sh, kPlateBlack, 0.55F * need * alpha);
    };

    // LAY BOTH ROWS OUT FIRST, THEN PAINT -- and lay them out THROUGH THE SAME
    // PURE FUNCTION the signage renderer asks for the prompt's footprint. See
    // aimRows()'s own header: two descriptions of one layout is how a fix gets
    // tested against arithmetic the draw code never calls.
    const AimRows rows = aimRows(state, width, height);
    if (!rows.draws) {
        return;
    }
    const int gutter = rows.gutter;
    const std::string& subject = rows.subject;
    const std::string& note = rows.note;
    const int subjectW = rows.subjectW;
    const int noteW = rows.noteW;
    const std::string& verbRow = rows.verbRow;

    if (!subject.empty()) {
        scrim(box.textX, box.subjectY, subjectW + (note.empty() ? 0 : gutter + noteW));
    }
    scrim(box.textX, box.verbY, textWidth(verbRow, box.unit));

    if (!subject.empty()) {
        drawText(target, box.textX, box.subjectY, subject, accent, 0.95F * alpha, box.unit);
        if (!note.empty()) {
            drawText(target, box.textX + subjectW + gutter, box.subjectY, note, kAimNote,
                     0.90F * alpha, box.unit);
        }
    }
    drawText(target, box.textX, box.verbY, verbRow, kAimVerb, 0.95F * alpha, box.unit);
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
    // row lands is not negotiable -- centred (lockLabel, blockLabel),
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
    // UI-EA (LANE HUD): THE Q-HOLD TUTOR TOAST, directly under the strip it
    // teaches. Session raises it the first two times the bar ever comes up
    // and never again; it rides the bar's own countdown, takes a slot out of
    // the same grid (so it can never land in the play space or on another
    // row), and speaks in the quiet reference ink -- a hint, not a shout.
    if (!state.wheelHint.empty() && state.wheelHintFade > 0.0F) {
        takeCentred(state.wheelHint, Rgb{0.62F, 0.60F, 0.54F},
                    0.85F * std::clamp(state.wheelHintFade, 0.0F, 1.0F));
    }
    // THE CROSSHAIR PASS TOOK A ROW OFF THIS BAND AND DID NOT REPLACE IT.
    //
    // #85's "E  TALK" sat here, centred, right after the alert. The owner's
    // note -- "The 'E' button shouldn't have that label text be at the bottom
    // of the screen" -- is why it is gone from the bottom edge entirely rather
    // than moved a slot up it. It is drawn by drawAim(), on the reticle, and
    // the slot it used to take is now free for the lock and the guard.
    //
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
    // STANCE & ROOM BUILD. FIGHTING MODE, right behind the guard: "FISTS UP"
    // while the room's own hands-up bit is true. Bone ink -- the plate's own
    // register, neither the guard's steel-cool nor the charge's heat, because
    // a raised fist is a STATE and not a moment. A guard and raised hands can
    // both be true (a guard raises the hands), so this takes its own slot.
    takeCentred(state.handsLabel, Rgb{0.82F, 0.76F, 0.60F},
                0.90F * std::clamp(state.handsFade, 0.0F, 1.0F));
    // ACTION-COMBAT BUILD (section 5, channel 2). THE HELD HARD charge row,
    // adjacent to the guard row and in the hot charge register -- the same warm
    // hue the reticle takes at the hard threshold, so the row and the reticle
    // read as one fact. A swing charging hard and a guard held are mutually
    // exclusive (the guard drops the instant the hand leaves Idle), so they
    // never both claim the band; and it sits behind the alert and the lock, a
    // shout and a lock both outranking a swing the player is still winding up.
    takeCentred(state.chargeLabel, Rgb{0.90F, 0.58F, 0.24F},
                0.94F * std::clamp(state.chargeFade, 0.0F, 1.0F));
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
    // have to read the line to notice it -- and under the word diet he no
    // longer CAN read it: the HUNTING word is off the label (rank 6 -> 3, the
    // spec's own cut) and the fact arrives as rivalHunts, set by Session off
    // the same Nemesis the line is built from. The label sniff stays as the
    // fallback so every pre-diet hand-built HudState keeps its colour.
    {
        const bool hunting =
            state.rivalHunts || state.rivalLabel.find("HUNTING") != std::string_view::npos;
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
    // THE PULL PACK. The followed lead on the ribbon's sub-label row, and the
    // skill-up toast in the free corner -- both gated on showCompass, because
    // the top band belongs to whoever is talking to you and every page stands
    // the compass down (the same test written once, not twice).
    if (state.showCompass) {
        drawPullLine(target, state, rightBlock);
        drawSkillToast(target, state);
    }
    // THE CASEBOOK PASS. ONE ANNOUNCEMENT SLOT, AND THE CASE OUTRANKS THE
    // CROSSING. Both notices are edges announced once in the same band; two of
    // them stacked would be two notices fighting. Session already guarantees
    // only one is non-empty at a time -- this is the guarantee enforced rather
    // than trusted, which is what keeps a future caller from discovering the
    // collision in a screenshot.
    if (!state.casePlate.empty() && state.casePlateFade > 0.0F) {
        // The number ink, not the HUD's bone: this is the ward's own good news
        // and it is the one place on the frame where green means "the trail
        // grew". The place plate stays bone, because a boundary is not news.
        drawAnnouncePlate(target, state.casePlate, state.casePlateFade, state.casePlateDrift,
                          Rgb{0.62F, 0.88F, 0.56F}, rightBlock);
    } else {
        drawAnnouncePlate(target, state.placePlate, state.placePlateFade, state.placePlateDrift,
                          kInk, rightBlock);
    }
    drawBottomBand(target, state, band);
    // LAST, AND IN THE MIDDLE. The one documented exemption from the rule at
    // the top of hud.hpp, clamped to hudAimRect and drawn after everything
    // else so nothing on an edge can be painted over it. With no verb to
    // show it draws nothing at all, which is what keeps the old guarantee --
    // "the HUD leaves the centre completely clear" -- true for every other
    // row on this frame.
    drawAim(target, state);
}

}  // namespace granadad::render
