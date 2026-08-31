#include "granadad/render/menu_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "granadad/render/hud.hpp"
#include "granadad/render/panel.hpp"

namespace granadad::render {

namespace {

// SAME PALETTE dialogue_view.cpp DRAWS THE OTHER SEVEN PAGES WITH. A player
// who has already read a conversation, the casebook or the keys page should
// not have to learn a second colour language for this one.
constexpr Rgb kPanel{0.04F, 0.04F, 0.05F};
constexpr Rgb kEdge{0.44F, 0.40F, 0.31F};
constexpr Rgb kHeaderInk{0.94F, 0.88F, 0.70F};
constexpr Rgb kEpithetInk{0.60F, 0.58F, 0.50F};
constexpr Rgb kBodyInk{0.88F, 0.86F, 0.80F};
constexpr Rgb kTopicInk{0.66F, 0.68F, 0.66F};
constexpr Rgb kTopicPicked{0.98F, 0.86F, 0.42F};
constexpr Rgb kCaseRefInk{0.58F, 0.56F, 0.48F};
constexpr Rgb kParchmentInk{0.86F, 0.74F, 0.52F};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

/// The panel's own frame: a near-opaque fill, a hairline edge, and -- for the
/// tile that currently has (or is easing toward/away from) input focus -- a
/// brighter, thicker border in the SAME accent colour drawPickHighlight
/// already means "this one is selected" with everywhere else in this widget
/// family. That reuse is deliberate: a player who has read one page of this
/// game already knows what that colour means before they ever see four boxes
/// at once.
///
/// INNOVATION SPRINT ITEM #2. `focusAmount` (0 unfocused .. 1 focused) used
/// to be a bare bool, and border colour and thickness both switched on the
/// exact step focus moved -- a hard cut every time PageNext/PagePrev stepped
/// to a new tile. Both are interpolated off the SAME continuous amount now
/// (Session eases it with a short render::EasedToggle -- see MenuTileState's
/// own header on why it is snappy rather than leisurely), so a tile's border
/// visibly grows in and brightens as focus arrives and does the reverse as it
/// leaves, instead of the old on/off swap. `focusAmount` at exactly 0 or 1 --
/// what a hand-built MenuTileState already means and what a settled frame
/// always reaches -- draws bit-for-bit the same border the old bool gave.
void drawPanelFrame(Framebuffer& target, const Rect& r, float focusAmount, int edgeScale,
                    float fade) {
    target.fillRect(r.x, r.y, r.w, r.h, kPanel, 0.88F * fade);
    const float amount = std::clamp(focusAmount, 0.0F, 1.0F);
    const Rgb edgeColour = lerp(kEdge, kTopicPicked, amount);
    const float edgeAlpha = (0.55F + 0.40F * amount) * fade;
    // Unfocused is edgeScale, focused is 2*edgeScale -- the exact two values
    // the old bool switch drew, now the two ends of a lerp instead of a jump.
    const int thickness =
        std::max(edgeScale, static_cast<int>(std::round(static_cast<float>(edgeScale) *
                                                        (1.0F + amount))));
    target.fillRect(r.x, r.y, r.w, thickness, edgeColour, edgeAlpha);
    target.fillRect(r.x, r.y + r.h - thickness, r.w, thickness, edgeColour, edgeAlpha);
    target.fillRect(r.x, r.y, thickness, r.h, edgeColour, edgeAlpha);
    target.fillRect(r.x + r.w - thickness, r.y, thickness, r.h, edgeColour, edgeAlpha);
}

/// The title (hudScale -- this game's own chunky 90s register, which Eli
/// explicitly said to keep) and, under it, the epithet (hudMinorScale --
/// "reference material read deliberately"). Returns the y a caller should
/// start drawing the body at.
int drawPanelHeader(Framebuffer& target, const Rect& r, int margin, const DialogueViewState& view,
                    int headerScale, int bodyScale, float fade) {
    int y = r.y + margin;
    const int roomPx = r.w - 2 * margin;
    drawText(target, r.x + margin, y, clipToWidth(view.speaker, roomPx, headerScale), kHeaderInk,
             0.98F * fade, headerScale);
    y += (6 + 1) * headerScale;
    if (!view.epithet.empty()) {
        drawText(target, r.x + margin, y, clipToWidth(view.epithet, roomPx, bodyScale), kEpithetInk,
                 0.85F * fade, bodyScale);
        y += (6 + 1) * bodyScale;
    }
    return y;
}

/// Up to `maxLines` wrapped lines of prose, the same word-boundary wrap the
/// rest of this widget family already uses (dialogue_view.hpp's wrapText),
/// with the last line marked "..." when there was more than fit -- the exact
/// convention DialogueViewState::speechRevealChars uses for a cut mid-reveal,
/// reused here for a cut that will never un-cut.
int drawWrappedProse(Framebuffer& target, const Rect& r, int margin, int y,
                     const std::string& text, int bodyScale, int maxLines, const Rgb& ink,
                     float fade) {
    if (text.empty()) {
        return y;
    }
    const int roomPx = r.w - 2 * margin;
    const std::size_t columns = static_cast<std::size_t>(std::max(4, roomPx / (5 * bodyScale)));
    std::vector<std::string> lines = wrapText(text, columns);
    const int step = (6 + 1) * bodyScale;
    for (int i = 0; i < static_cast<int>(lines.size()) && i < maxLines; ++i) {
        std::string line = lines[static_cast<std::size_t>(i)];
        if (i == maxLines - 1 && static_cast<int>(lines.size()) > maxLines) {
            // Marks the cut rather than silently dropping the rest -- the
            // same rule clipLabel() and the speech reveal both already keep.
            while (line.size() + 3 > columns && !line.empty()) {
                line.pop_back();
            }
            line += "...";
        }
        drawText(target, r.x + margin, y, line, ink, 0.90F * fade, bodyScale);
        y += step;
    }
    return y;
}

/// The single-column row list every tile but an open Letters document draws:
/// numbered, clipped to the panel's own width (WIDER per row than the old
/// three-column grid ever gave a label, because a tile is one column instead
/// of three -- see menu_view.hpp's own density note), with the focused
/// tile's cursor row picked out by drawPickHighlight, the identical shape
/// dialogue_view.cpp already uses for "this one is selected".
///
/// Returns the y the row after the last drawn one would have started at, so
/// the caller can word whatever room the list did not want -- see
/// DialogueViewState::emptyLine.
int drawPanelRows(Framebuffer& target, const Rect& r, int margin, int bodyTop,
                  const DialogueViewState& view, int bodyScale, bool focused, float fade) {
    const int rowStep = (6 + 1) * bodyScale;
    const int glyphAdvance = 5 * bodyScale;
    const int bottom = r.y + r.h - margin;
    const int capacity = std::max(0, (bottom - bodyTop) / rowStep);
    if (capacity <= 0) {
        return bodyTop;
    }
    const std::vector<TopicRow> rows =
        topicRowsFor(view.topics, view.page, view.cursor,
                     std::min(capacity, kTopicPageSize + 1));
    const std::size_t room =
        static_cast<std::size_t>(std::max(1, (r.w - 2 * margin) / glyphAdvance - 2));
    int y = bodyTop;
    for (const TopicRow& row : rows) {
        const std::string label = clipLabel(row.label, room);
        const int x = r.x + margin + glyphAdvance;
        if (row.picked) {
            // THE FILL IS THE AFFORDANCE -- and this was the last surface in
            // the build that had not been told. UI-REFERENCE-TERMINAL.md
            // forbids the arrow BY NAME ("Selection is an inverted highlight
            // ... Not an arrow, not a bracket. The fill is the affordance"),
            // the conversation panel was converted a pass ago and
            // drawPickHighlight's own header has said since then that these
            // four tiled panels are "the next surface to convert". They are
            // converted here: a solid band in the accent with the label knocked
            // out of it, which is the identical shape drawOptionList draws for
            // every other list in the game.
            //
            // AN UNFOCUSED TILE STILL SHOWS WHERE ITS CURSOR SAT -- Morrowind's
            // own four panes hold their place while unfocused -- so the fill
            // dims rather than disappearing. State moves the whole row
            // together; nothing is greyed out and nothing is dropped.
            const int fillX = x - glyphAdvance / 2;
            const int fillW = std::max(
                glyphAdvance, std::min(static_cast<int>(room + 2) * glyphAdvance,
                                       textWidth(label, bodyScale) + 2 * glyphAdvance));
            target.fillRect(fillX, y - bodyScale, fillW, rowStep, kTopicPicked,
                            (focused ? 0.92F : 0.20F) * fade);
            drawText(target, x + glyphAdvance / 2, y, label,
                     focused ? panelInk().knockout : kTopicPicked,
                     (focused ? 0.98F : 0.85F) * fade, bodyScale);
        } else {
            drawText(target, x + glyphAdvance / 2, y, label, kTopicInk, 0.82F * fade, bodyScale);
        }
        y += rowStep;
        if (y + (6 + 1) * bodyScale > bottom + rowStep) {
            break;
        }
    }
    return y;
}

/// WHAT THE BLANK ROWS ARE WAITING FOR, worded, in whatever room the list did
/// not want -- UI-REFERENCE-TERMINAL.md's "Empty states are worded, not blank"
/// applied to the three tiles that ship a stranger a header over a void.
///
/// TWO RULES KEEP IT FROM BECOMING CLUTTER. It is drawn only where the rows
/// left at least two whole rows spare, so a tile whose list has grown into its
/// pane loses it without anything else moving; and it takes the EPITHET's ink
/// rather than the body's, because it is a note about the panel and not a row
/// of the panel's content.
void drawEmptyState(Framebuffer& target, const Rect& r, int margin, int rowsBottom,
                    const DialogueViewState& view, int bodyScale, float fade) {
    if (view.emptyLine.empty()) {
        return;
    }
    // A BLANK ROW ABOVE THE NOTE, WHETHER OR NOT THERE WERE ROWS. A tile with
    // nothing in it at all is the one that needs it most: the note would
    // otherwise butt straight against the epithet and read as a third line of
    // the header rather than as what the panel is waiting for.
    const int rowStep = (6 + 1) * bodyScale;
    const int y = rowsBottom + rowStep;
    const int room = (r.y + r.h - margin - y) / rowStep;
    if (room < 2) {
        return;
    }
    drawWrappedProse(target, r, margin, y, view.emptyLine, bodyScale, room, kEpithetInk, fade);
}

/// An open letter's own wrapped, paged body -- the tile-sized equivalent of
/// dialogue_view.cpp's `state.letter` bottom band, just confined to one
/// panel's rect instead of the whole frame width.
void drawLetterBody(Framebuffer& target, const Rect& r, int margin, int bodyTop,
                    const DialogueViewState& view, int bodyScale, float fade) {
    const int roomPx = r.w - 2 * margin;
    const std::size_t columns = static_cast<std::size_t>(std::max(4, roomPx / (5 * bodyScale)));
    std::vector<std::string> allLines;
    for (const std::string& paragraph : view.letterLines) {
        if (paragraph.empty()) {
            allLines.emplace_back();
            continue;
        }
        for (std::string& row : wrapText(paragraph, columns)) {
            allLines.push_back(std::move(row));
        }
    }
    const int rowStep = (6 + 1) * bodyScale;
    const int footY = r.y + r.h - margin - (6 + 1) * bodyScale;
    const int rowsAvail = std::max(1, (footY - bodyTop) / rowStep);
    const int pages =
        std::max(1, (static_cast<int>(allLines.size()) + rowsAvail - 1) / rowsAvail);
    const int page = std::clamp(view.page, 0, pages - 1);
    const std::size_t first = static_cast<std::size_t>(page) * static_cast<std::size_t>(rowsAvail);
    int y = bodyTop;
    for (std::size_t i = first; i < allLines.size() && i < first + static_cast<std::size_t>(rowsAvail);
         ++i) {
        drawText(target, r.x + margin, y, allLines[i], kParchmentInk, 0.92F * fade, bodyScale);
        y += rowStep;
    }
    if (pages > 1) {
        const std::string foot = "0 MORE (" + std::to_string(page + 1) + "/" +
                                 std::to_string(pages) + ")";
        drawText(target, r.x + margin, footY, foot, kParchmentInk, 0.62F * fade, bodyScale);
    }
}

void drawTile(Framebuffer& target, const Rect& r, const DialogueViewState& view, bool focused,
             float focusAmount, int edgeScale, int headerScale, int bodyScale, float fade,
             bool showProse) {
    drawPanelFrame(target, r, focusAmount, edgeScale, fade);
    if (!view.open) {
        return;
    }
    const int margin = 2 * edgeScale;
    int y = drawPanelHeader(target, r, margin, view, headerScale, bodyScale, fade);
    if (view.letter) {
        drawLetterBody(target, r, margin, y, view, bodyScale, fade);
        return;
    }
    if (showProse && !view.line.empty()) {
        y = drawWrappedProse(target, r, margin, y, view.line, bodyScale, 2, kBodyInk, fade);
    }
    if (showProse && !view.caseRef.empty()) {
        y = drawWrappedProse(target, r, margin, y, view.caseRef, bodyScale, 1, kCaseRefInk, fade);
    }
    y += edgeScale;
    const int rowsBottom = drawPanelRows(target, r, margin, y, view, bodyScale, focused, fade);
    drawEmptyState(target, r, margin, rowsBottom, view, bodyScale, fade);
}

}  // namespace

void drawMenuTiles(Framebuffer& target, const MenuTileState& state) {
    if (!state.open) {
        return;
    }
    const float fade = std::clamp(state.openAmount, 0.0F, 1.0F);
    const int width = target.width();
    const int height = target.height();
    const int edgeScale = std::max(1, height / 180);
    const int headerScale = hudScale(height);
    const int bodyScale = hudMinorScale(height);
    const int margin = 4 * edgeScale;
    const int gutter = 3 * edgeScale;

    const int outerX0 = margin;
    const int outerX1 = width - margin;
    const int outerY0 = margin;
    const int outerY1 = height - margin;

    // THE BOTTOM PANEL (Journal) TAKES ROUGHLY A THIRD OF THE FRAME. Bounded
    // below so a tiny capture resolution still leaves the list panel
    // something to draw in, and above so the top row never starves.
    const int span = outerY1 - outerY0;
    const int bottomH = std::clamp(static_cast<int>(static_cast<float>(span) * 0.36F),
                                   10 * edgeScale, span - gutter - 1);
    const int topH = span - bottomH - gutter;
    const int topY = outerY0;
    const int bottomY = outerY1 - bottomH;

    const int topW = outerX1 - outerX0;
    const int colW = (topW - 2 * gutter) / 3;
    const int col0X = outerX0;
    const int col1X = col0X + colW + gutter;
    const int col2X = col1X + colW + gutter;
    const int col2W = outerX1 - col2X;

    const Rect characterRect{col0X, topY, colW, topH};
    const Rect mapRect{col1X, topY, colW, topH};
    const Rect lettersRect{col2X, topY, col2W, topH};
    const Rect journalRect{outerX0, bottomY, topW, bottomH};

    // CHARACTER, MAP, LETTERS: the three top tiles skip their own
    // instructional `line` prose (each already said once, in full, on the
    // page that first taught a new player to open this) so a quarter of the
    // frame is spent on ROWS -- the actual state a player opened the Menu to
    // read -- instead of a repeat of the sentence the opening casebook page
    // already put on screen. Letters' own `line` reappears the moment a
    // letter is picked, as that document's own first line of body text, via
    // `view.letter`'s own branch in drawTile -- this flag only governs the
    // TITLE-LIST state.
    drawTile(target, characterRect, state.character, state.focus == kMenuFocusCharacter,
             state.characterFocus, edgeScale, headerScale, bodyScale, fade, false);
    drawTile(target, mapRect, state.map, state.focus == kMenuFocusMap, state.mapFocus, edgeScale,
             headerScale, bodyScale, fade, false);
    drawTile(target, lettersRect, state.letters, state.focus == kMenuFocusLetters,
             state.lettersFocus, edgeScale, headerScale, bodyScale, fade, false);
    // JOURNAL: full width along the bottom, the tile with the most room, and
    // the one whose prose (the hook, the ward's dread, a picked lead's own
    // found/detail text, its dateline) is the actual point of the page -- so
    // it keeps showing it, wrapped to two lines and marked when cut.
    drawTile(target, journalRect, state.journal, state.focus == kMenuFocusJournal,
             state.journalFocus, edgeScale, headerScale, bodyScale, fade, true);
}

}  // namespace granadad::render
