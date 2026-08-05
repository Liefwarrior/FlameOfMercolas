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
constexpr Rgb kHealth{0.72F, 0.16F, 0.14F};
constexpr Rgb kHealthBack{0.10F, 0.06F, 0.06F};
constexpr Rgb kFrame{0.55F, 0.50F, 0.40F};

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
    // Chunky segments rather than a smooth bar: it reads at a glance and it is
    // the register the rest of the art is in.
    const int segments = 16;
    const int filled = (clamped * segments + maxHealth - 1) / maxHealth;
    const int segW = barW / segments;
    for (int i = 0; i < filled; ++i) {
        target.fillRect(x + i * segW + 1, y + 1, segW - 1, barH - 2, kHealth, 0.95F);
    }
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
    const int stripH = rowHeight(scale) + scale;
    const int x = (target.width() - stripW) / 2;
    const int y = 3 * scale;

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
        drawText(target, (target.width() - width) / 2, y + stripH + scale, state.locationLabel,
                 Rgb{0.70F, 0.68F, 0.60F}, 0.85F, minor);
    }
}

/// Top-right: the hour, and then everything the ward, the Watch, your pockets
/// and the dark have to say about you. Right-aligned against the edge, because
/// that is the edge it belongs to.
///
/// THE STACK DROPS ITS DEAREST ROW RATHER THAN CROSSING THE RECTANGLE. Six rows
/// at nine scale units each is 54 units of sky and the exclusion rectangle
/// starts at 39 of them at 320x180 -- so the old stack ran straight through the
/// play space the moment the Watch had heard about you and there was something
/// in your sack. Nothing caught it, because no case ever filled more than three
/// of the six rows at once.
void drawTopRight(Framebuffer& target, const HudState& state) {
    const int scale = hudScale(target.height());
    const int minor = hudMinorScale(target.height());
    const int margin = 6 * scale;
    const int ceiling = hudCentreRect(target.width(), target.height()).y0;
    int y = 4 * scale;

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
        drawText(target, target.width() - margin - textWidth(clock, scale), y, clock, kInk, 0.9F,
                 scale);
        y += rowHeight(scale) + minor;
    }

    // Every remaining row, in the order they have always been drawn in, each
    // carrying what it would cost to lose it. When the stack runs out of room
    // above the rectangle the DEAREST row goes and not the last one: a burglar
    // reads the stealth line every second and the ward's opinion of him once a
    // week.
    struct Row {
        std::string_view text;
        Rgb ink;
        float alpha;
        int cost;
    };
    std::array<Row, 5> rows{};
    std::size_t count = 0;
    const auto add = [&](std::string_view text, const Rgb& ink, float alpha, int cost) {
        if (!text.empty() && count < rows.size()) {
            rows[count++] = Row{text, ink, alpha, cost};
        }
    };
    std::string purse;
    if (state.coin >= 0) {
        purse = std::to_string(std::min(state.coin, 99999)) + " C";
        add(purse, Rgb{0.82F, 0.72F, 0.38F}, 0.9F, 3);
    }
    add(state.standingLabel, Rgb{0.62F, 0.66F, 0.72F}, 0.82F, 5);
    // Red for anything the ward has decided about you -- a warrant, a hand
    // taken, a rope waiting -- and ash for the rest.
    const bool wanted = state.heatLabel.find("WANTED") != std::string_view::npos ||
                        state.heatLabel.find("MAIMED") != std::string_view::npos ||
                        state.heatLabel.find("CONDEMNED") != std::string_view::npos;
    add(state.heatLabel, wanted ? Rgb{0.88F, 0.34F, 0.26F} : Rgb{0.70F, 0.62F, 0.50F}, 0.86F, 2);
    add(state.stashLabel, Rgb{0.58F, 0.66F, 0.52F}, 0.84F, 4);
    // Green while the room cannot see you, amber the moment it can.
    const bool seen = state.stealthLabel.substr(0, 4) == "SEEN";
    add(state.stealthLabel, seen ? Rgb{0.86F, 0.66F, 0.28F} : Rgb{0.44F, 0.72F, 0.50F}, 0.86F, 1);

    const int step = rowHeight(minor) + minor;
    std::size_t room = 0;
    while (y + static_cast<int>(room) * step + rowHeight(minor) <= ceiling) {
        ++room;
    }
    while (count > room) {
        std::size_t dearest = 0;
        for (std::size_t i = 1; i < count; ++i) {
            if (rows[i].cost > rows[dearest].cost) {
                dearest = i;
            }
        }
        for (std::size_t i = dearest + 1; i < count; ++i) {
            rows[i - 1] = rows[i];
        }
        --count;
    }
    for (std::size_t i = 0; i < count; ++i) {
        drawText(target, target.width() - margin - textWidth(rows[i].text, minor), y, rows[i].text,
                 rows[i].ink, rows[i].alpha, minor);
        y += step;
    }
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
                 0.88F, minor);
    }

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
            drawText(target, std::max(margin, (width - drawn) / 2), y, alert,
                     Rgb{0.90F, 0.62F, 0.30F}, 0.95F, alertScale);
        }
    }
    // The lock under the wire. A lockpicking minigame is exactly the element
    // that would otherwise become a panel in the middle of the screen, which is
    // the failure this HUD is built against; it gets one row on an edge.
    if (!state.lockLabel.empty()) {
        const std::string lock = clipToWidth(state.lockLabel, width - 2 * margin, minor);
        const int y = band.take(minor);
        if (y >= 0) {
            const int drawn = textWidth(lock, minor);
            drawText(target, std::max(margin, (width - drawn) / 2), y, lock,
                     Rgb{0.78F, 0.74F, 0.56F}, 0.92F, minor);
        }
    }
    // The case. IT IS THE COLOUR OF THE WARD'S NERVE and not a fixed one: a
    // player who has frightened the district enough that nobody walks the
    // Gullet alone should see that without reading the words.
    if (!state.caseLabel.empty()) {
        const int y = band.take(minor);
        if (y >= 0) {
            const bool afraid = state.caseLabel.find("EMPTYING") != std::string_view::npos ||
                                state.caseLabel.find("ALONE") != std::string_view::npos;
            drawText(target, margin, y, state.caseLabel,
                     afraid ? Rgb{0.86F, 0.44F, 0.36F} : Rgb{0.70F, 0.72F, 0.66F}, 0.90F, minor);
        }
    }
    // The man who put you here, anchored to the right edge so a long name
    // cannot run off the frame the way S6 alert did.
    if (!state.rivalLabel.empty()) {
        const int y = band.take(minor);
        if (y >= 0) {
            // A hunted man should not have to read the line to notice it.
            const bool hunting = state.rivalLabel.find("HUNTING") != std::string_view::npos;
            drawText(target, width - margin - textWidth(state.rivalLabel, minor), y,
                     state.rivalLabel,
                     hunting ? Rgb{0.88F, 0.40F, 0.30F} : Rgb{0.74F, 0.60F, 0.52F}, 0.90F, minor);
        }
    }
    if (!state.guildLabel.empty()) {
        const int y = band.take(minor);
        if (y >= 0) {
            drawText(target, margin, y, state.guildLabel, Rgb{0.86F, 0.74F, 0.44F}, 0.92F, minor);
        }
    }
    if (!state.objectiveLabel.empty()) {
        const int y = band.take(minor);
        if (y >= 0) {
            drawText(target, margin, y, state.objectiveLabel, Rgb{0.62F, 0.66F, 0.72F}, 0.80F,
                     minor);
        }
    }
}

}  // namespace

void drawHud(Framebuffer& target, const HudState& state) {
    BottomBand band(target.width(), target.height());
    if (state.showHealth) {
        drawHealth(target, state, band);
    }
    if (state.showCompass) {
        drawCompass(target, state);
    }
    drawTopRight(target, state);
    drawBottomBand(target, state, band);
}

}  // namespace granadad::render
