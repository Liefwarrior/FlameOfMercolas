#include "granadad/render/hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

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

void drawHealth(Framebuffer& target, const HudState& state) {
    const int scale = std::max(1, target.height() / 180);
    const int margin = 6 * scale;
    const int barW = 62 * scale;
    const int barH = 7 * scale;
    const int x = margin;
    const int y = target.height() - margin - barH;

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
    drawText(target, x, y - 8 * scale, "HP", kInk, 0.9F, scale);
    // The bottom-left cluster, stacked upward: HP, then the rung you hold, then
    // what the line you are on wants next. All of it hugs the corner and none
    // of it reaches the middle of the screen.
    if (!state.guildLabel.empty()) {
        drawText(target, x, y - 16 * scale, state.guildLabel, Rgb{0.86F, 0.74F, 0.44F}, 0.92F,
                 scale);
    }
    if (!state.objectiveLabel.empty()) {
        drawText(target, x, y - 24 * scale, state.objectiveLabel, Rgb{0.62F, 0.66F, 0.72F}, 0.80F,
                 scale);
    }
}

void drawCompass(Framebuffer& target, const HudState& state) {
    const int scale = std::max(1, target.height() / 180);
    const int stripW = std::min(target.width() / 3, 140 * scale);
    const int stripH = 9 * scale;
    const int x = (target.width() - stripW) / 2;
    const int y = 5 * scale;

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
        drawText(target, px - labelWidth / 2, y + 2 * scale, point.label,
                 cardinal ? kInk : Rgb{0.62F, 0.60F, 0.54F}, cardinal ? 0.95F : 0.7F, scale);
    }
    // The fixed mark. One pixel column, at the very top edge of the strip, so
    // it never encroaches on the view.
    target.fillRect(x + stripW / 2, y, scale, 2 * scale, Rgb{0.95F, 0.80F, 0.35F}, 1.0F);

    if (!state.locationLabel.empty()) {
        const int width = textWidth(state.locationLabel, scale);
        drawText(target, (target.width() - width) / 2, y + stripH + 3 * scale,
                 state.locationLabel, Rgb{0.70F, 0.68F, 0.60F}, 0.85F, scale);
    }
}

/// Top-right: the hour, and what is in the purse. Right-aligned against the
/// edge, because that is the edge it belongs to.
void drawClock(Framebuffer& target, const HudState& state) {
    const int scale = std::max(1, target.height() / 180);
    const int margin = 6 * scale;
    int y = 5 * scale;
    if (state.timeOfDaySeconds >= 0) {
        const int hour = (state.timeOfDaySeconds / 3600) % 24;
        const int minute = (state.timeOfDaySeconds / 60) % 60;
        char text[6] = {static_cast<char>('0' + hour / 10), static_cast<char>('0' + hour % 10),
                        ':', static_cast<char>('0' + minute / 10),
                        static_cast<char>('0' + minute % 10), '\0'};
        const std::string_view clock(text);
        drawText(target, target.width() - margin - textWidth(clock, scale), y, clock, kInk, 0.9F,
                 scale);
        y += 9 * scale;
    }
    if (state.coin >= 0) {
        char text[16] = {};
        int at = 0;
        int value = std::min(state.coin, 99999);
        char digits[8] = {};
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value > 0 && count < 8);
        while (count > 0) {
            text[at++] = digits[--count];
        }
        text[at++] = ' ';
        text[at++] = 'C';
        text[at] = '\0';
        const std::string_view purse(text);
        drawText(target, target.width() - margin - textWidth(purse, scale), y, purse,
                 Rgb{0.82F, 0.72F, 0.38F}, 0.9F, scale);
        y += 9 * scale;
    }
    // S3: reputation, readable, in the corner where the numbers live. The ward
    // has an opinion about you and it is not a hidden statistic.
    if (!state.standingLabel.empty()) {
        drawText(target, target.width() - margin - textWidth(state.standingLabel, scale), y,
                 state.standingLabel, Rgb{0.62F, 0.66F, 0.72F}, 0.82F, scale);
    }
}

/// Bottom-right: what the room is doing. Bottom edge, centred: what somebody
/// just said to you.
void drawRoom(Framebuffer& target, const HudState& state) {
    const int scale = std::max(1, target.height() / 180);
    const int margin = 6 * scale;
    if (!state.roomLabel.empty()) {
        const int width = textWidth(state.roomLabel, scale);
        drawText(target, target.width() - margin - width,
                 target.height() - margin - 7 * scale, state.roomLabel,
                 Rgb{0.72F, 0.70F, 0.62F}, 0.88F, scale);
    }
    if (!state.alert.empty()) {
        const int width = textWidth(state.alert, scale);
        const int x = std::max(margin, (target.width() - width) / 2);
        drawText(target, x, target.height() - margin - 15 * scale, state.alert,
                 Rgb{0.90F, 0.62F, 0.30F}, 0.95F, scale);
    }
}

}  // namespace

void drawHud(Framebuffer& target, const HudState& state) {
    if (state.showHealth) {
        drawHealth(target, state);
    }
    if (state.showCompass) {
        drawCompass(target, state);
    }
    drawClock(target, state);
    drawRoom(target, state);
}

}  // namespace granadad::render
