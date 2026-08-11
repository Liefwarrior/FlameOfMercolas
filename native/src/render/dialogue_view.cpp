#include "granadad/render/dialogue_view.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace granadad::render {

namespace {

constexpr Rgb kPanel{0.04F, 0.04F, 0.05F};
constexpr Rgb kEdge{0.44F, 0.40F, 0.31F};
constexpr Rgb kSpeakerInk{0.94F, 0.88F, 0.70F};
constexpr Rgb kEpithetInk{0.60F, 0.58F, 0.50F};
constexpr Rgb kSpeechInk{0.88F, 0.86F, 0.80F};
constexpr Rgb kTopicInk{0.66F, 0.68F, 0.66F};
constexpr Rgb kTopicPicked{0.98F, 0.86F, 0.42F};
constexpr Rgb kHaggleInk{0.90F, 0.76F, 0.42F};
/// TASK #82. THE ONE ROW THAT SAYS "THIS IS A CASEBOOK ENTRY, NOT A LEAD ON
/// THE LIST" -- muted, the same register the epithet already reads at,
/// because a dateline is reference material read deliberately and not the
/// thing the eye should land on first.
constexpr Rgb kCaseRefInk{0.58F, 0.56F, 0.48F};
/// TASK #82. THE PARCHMENT PALETTE. Warm and dim rather than inverted to a
/// bright page -- this build's whole HUD is dark panels and light ink read
/// by what reads as lamplight, and a letter is a page held up to that same
/// lamp, not a sheet lit from behind. Warmer and richer than the ordinary
/// panel's near-black and the ordinary edge's neutral bronze, so the switch
/// from "menu" to "document" is a colour a player feels before they read a
/// word -- the same job attitudeInk already does for standing.
constexpr Rgb kParchmentPanel{0.16F, 0.11F, 0.06F};
constexpr Rgb kParchmentEdge{0.62F, 0.46F, 0.22F};
constexpr Rgb kParchmentInk{0.86F, 0.74F, 0.52F};

/// Attitude colouring. The one place standing is a COLOUR and not a word: you
/// should be able to tell across a room, without reading, that the man behind
/// the bar has stopped liking you.
[[nodiscard]] Rgb attitudeInk(const std::string& attitude) noexcept {
    if (attitude == "HOSTILE") {
        return Rgb{0.86F, 0.28F, 0.22F};
    }
    if (attitude == "COLD") {
        return Rgb{0.58F, 0.64F, 0.78F};
    }
    if (attitude == "WARM" || attitude == "FRIEND" || attitude == "KIN") {
        return Rgb{0.52F, 0.82F, 0.48F};
    }
    return Rgb{0.70F, 0.68F, 0.62F};
}

}  // namespace

// THE PICKED ROW'S HIGHLIGHT: a soft band under the label and a bright
// hairline where the cursor arrow sits, so "this one is selected" is a SHAPE
// on the screen and not only a colour the label happens to print in. Kept to
// the room the label itself was cut to fit, so it can never reach into the
// next column any more than the label already could.
//
// BREATHES GENTLY WITH `phase`, and 0 draws it at rest -- the same contract
// DialogueViewState::phase documents: a hand-built state that never heard of
// this gets the resting frame, which is what it always drew before the
// highlight existed.
//
// EXPOSED (was anonymous-namespace-private) so menu_view.cpp's four smaller
// panels draw the identical shape -- see the header on the .hpp declaration.
void drawPickHighlight(Framebuffer& target, int x, int y, int rowStep, int glyphAdvance, int scale,
                       int labelWidth, int roomWidth, float phase) {
    const float breathe = 0.5F + 0.5F * std::sin(phase * 6.0F);
    const int width = std::max(glyphAdvance, std::min(roomWidth, labelWidth + 2 * glyphAdvance));
    target.fillRect(x - glyphAdvance, y - scale, width, rowStep, kTopicPicked, 0.10F + 0.07F * breathe);
    target.fillRect(x - glyphAdvance, y - scale, scale, rowStep, kTopicPicked, 0.85F);
}

int topicPageCount(std::size_t topics) noexcept {
    if (topics == 0) {
        return 1;
    }
    return static_cast<int>((topics + static_cast<std::size_t>(kTopicPageSize) - 1) /
                            static_cast<std::size_t>(kTopicPageSize));
}

int topicPageOf(int index) noexcept {
    return std::max(0, index) / kTopicPageSize;
}

bool topicsPaginate(std::size_t topics) noexcept {
    return topicPageCount(topics) > 1;
}

std::string dialogueDetailLine(const DialogueViewState& state) {
    if (state.topics.empty()) {
        return {};
    }
    // The cursor is an index into the WHOLE list, never into the page -- which
    // is the contract DialogueViewState::cursor already states. A cursor that
    // has run off the end of a shrinking list names nothing rather than
    // reading past it.
    if (state.cursor < 0 || static_cast<std::size_t>(state.cursor) >= state.topics.size()) {
        return {};
    }
    // Only when the cursor is actually on the page being shown. Pressing 0 to
    // turn the page leaves the cursor behind on the old one, and a detail line
    // describing a row nobody can see is worse than no detail line.
    if (topicPageOf(state.cursor) != state.page) {
        return {};
    }
    return state.topics[static_cast<std::size_t>(state.cursor)];
}

std::vector<std::string> wrapText(const std::string& text, std::size_t columns) {
    std::vector<std::string> lines;
    if (columns == 0) {
        return lines;
    }
    std::string current;
    std::size_t at = 0;
    while (at < text.size()) {
        // One word, plus the run of spaces that follows it.
        std::size_t end = text.find(' ', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string word = text.substr(at, end - at);
        at = end;
        while (at < text.size() && text[at] == ' ') {
            ++at;
        }
        if (word.empty()) {
            continue;
        }
        if (current.empty()) {
            current = word;
        } else if (current.size() + 1 + word.size() <= columns) {
            current += ' ';
            current += word;
        } else {
            lines.push_back(current);
            current = word;
        }
        // A single word longer than the column has to be cut somewhere.
        while (current.size() > columns) {
            lines.push_back(current.substr(0, columns));
            current = current.substr(columns);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::string clipLabel(const std::string& label, std::size_t room) {
    if (room == 0) {
        return {};
    }
    if (label.size() <= room) {
        return label;
    }
    // One glyph of the room is the mark, so the cut label is never wider than
    // the whole label would have been.
    const std::size_t keep = room - 1;
    const std::size_t space = label.rfind(' ', keep);
    // A leading number and a space is how a topic row is composed ("6 THE
    // VANISHED CLERK"), so the FIRST space is furniture rather than a word
    // boundary -- cutting there would print "6." and name nothing.
    const std::size_t firstSpace = label.find(' ');
    // AND A WORD BOUNDARY IS A PREFERENCE, NOT A LAW. "6 PICK THEIR POCKET" in
    // eighteen columns breaks after "THEIR" and throws five usable columns
    // away, which reads worse than the mid-word cut it was meant to fix. So the
    // boundary wins only when it keeps nearly all the room; otherwise the cut
    // is where the column ends, and the mark is what makes that legible --
    // which was the whole finding. S5 had no mark at all, and "THE VANISHED
    // CLE" with nothing after it reads as a rendering fault rather than as a
    // label longer than its column.
    const std::size_t generous = keep >= 3 ? keep - 3 : 0;
    std::string cut;
    if (space != std::string::npos && space > 0 && space != firstSpace &&
        space >= generous) {
        cut = label.substr(0, space);
    } else {
        cut = label.substr(0, keep);
    }
    // Trailing spaces would put the mark out in the open.
    while (!cut.empty() && cut.back() == ' ') {
        cut.pop_back();
    }
    cut += '.';
    return cut;
}

std::vector<TopicRow> topicRowsFor(const std::vector<std::string>& topics, int page, int cursor,
                                   int capacity) {
    const int total = static_cast<int>(topics.size());
    const int pages = topicPageCount(topics.size());
    const int shown = std::clamp(page, 0, pages - 1);
    const int first = shown * kTopicPageSize;
    const int last = std::min(total, first + kTopicPageSize);

    std::vector<TopicRow> rows;
    rows.reserve(static_cast<std::size_t>(kTopicPageSize + 1));
    for (int i = first; i < last; ++i) {
        rows.push_back(TopicRow{std::to_string(i - first + 1) + " " +
                                    topics[static_cast<std::size_t>(i)],
                                i == cursor});
    }
    if (pages > 1) {
        rows.push_back(
            TopicRow{"0 MORE (" + std::to_string(shown + 1) + "/" + std::to_string(pages) + ")",
                     false});
    }
    // A band too short to print the whole page would otherwise drop rows in
    // silence, which is the exact failure the paging replaced. It cannot happen
    // at any resolution this game runs at -- a test pins the capacity at
    // 320x180 and at 640x360 -- and if it ever did, the MORE row survives so
    // the list still says out loud that there is more of it.
    if (capacity >= 1 && static_cast<int>(rows.size()) > capacity) {
        rows.resize(static_cast<std::size_t>(capacity));
        rows.back().label =
            "0 MORE (" + std::to_string(shown + 1) + "/" + std::to_string(pages) + ")";
        rows.back().picked = false;
    }
    return rows;
}

void drawDialogue(Framebuffer& target, const DialogueViewState& state) {
    if (!state.open) {
        return;
    }
    // TASK #83, AND INNOVATION SPRINT ITEM #1 MAKES IT TRUE. This comment
    // used to say "the box itself eases" while `fade` only ever multiplied a
    // fillRect/drawText alpha argument -- the panel's own rect never moved.
    // It really does now: `fade` still drives every alpha below exactly as
    // it always did, AND drives topOffset/bottomOffset further down, which
    // slide the top band in from the top edge and the bottom band in from
    // the bottom edge as the SAME value rises from 0 to 1. See
    // DialogueViewState::openAmount -- 1 is "fully open and not animating",
    // which is what every hand-built state already meant, so a caller that
    // never heard of this still gets topOffset == bottomOffset == 0 and
    // draws bit-for-bit what it always drew.
    const float fade = std::clamp(state.openAmount, 0.0F, 1.0F);
    const int scale = std::max(1, target.height() / 180);
    const int margin = 5 * scale;
    const int rowStep = 8 * scale;
    const int glyphAdvance = 5 * scale;
    const CentreRect centre = hudCentreRect(target.width(), target.height());

    // ---- the top band: who, and what they said ----------------------------
    //
    // Height is computed from the rows it will actually draw and then CLAMPED
    // to the top of the exclusion rectangle. If a line ever wants more room
    // than the band has, the line loses; the play space does not.
    // Wrapped clear of the top-right corner, which is the clock's and the
    // purse's and stays theirs: a spoken line running under a two-digit hour is
    // unreadable and looks like a bug.
    //
    // ASKED, NOT GUESSED. This was `34 * scale`, a number that had to be
    // re-checked by hand every time a row was added to that corner or its size
    // changed -- and polish-1 changed both. The HUD knows how wide its own
    // stack is.
    const int reservedRight = hudTopRightReserve(target.height());
    const std::size_t columns = static_cast<std::size_t>(
        std::max(8, (target.width() - 2 * margin - reservedRight) / glyphAdvance));
    std::vector<std::string> speech = wrapText(state.line, columns);
    // The alert takes a row of the top band when there is one, so the speech
    // gives one up rather than the alert being silently dropped off the bottom
    // of a band that was sized before anybody asked for it. A warning outranks
    // the fourth line of a greeting.
    const int alertRows = state.alert.empty() ? 0 : 1;
    const int maxSpeechRows =
        std::max(0, (centre.y0 - margin - rowStep * 2) / rowStep - alertRows);
    if (static_cast<int>(speech.size()) > maxSpeechRows) {
        speech.resize(static_cast<std::size_t>(std::max(0, maxSpeechRows)));
    }
    // ---- the detail line: the picked row, spelled out ----------------------
    //
    // THE COLUMN IS EIGHTEEN GLYPHS AND SOME LABELS ARE NOT. S6's own shipped
    // frame printed "7 SIGN ON: THE." -- clipped on a word boundary to a row
    // that names nothing at all -- and two contracts as "8 TAKE 3 SCALPS." and
    // "9 TAKE 4 SCALPS.". No amount of cleverness in clipLabel fixes a string
    // longer than the space it has, so the row the cursor is on gets a line of
    // its own, the whole width of the frame, with nothing taken off it.
    //
    // IT LIVES IN THE TOP BAND AND NOT UNDER THE GRID, and that is a
    // measurement rather than a preference: at 1280x720 the bottom band is
    // 154 pixels between the exclusion rectangle and the frame edge, which is
    // four rows of the topic grid and thirty spare pixels -- two short of a
    // fifth row. A detail line drawn there would be clipped off the bottom of
    // the frame, which is the same class of bug as the one it exists to fix.
    // The top band is sized from what it draws, so it simply grows by a row.
    // AND ONLY WHEN THE COLUMN ACTUALLY CUT IT. This row exists because an
    // eighteen-glyph column printed "7 SIGN ON: THE." and two contracts as
    // "8 TAKE 3 SCALPS." -- labels that name nothing. It was then drawn for
    // EVERY row, including the great majority that fit their column whole, so
    // a conversation with "1 THEIR BUSINESS" on the cursor spent a full row of
    // the top band printing "> THEIR BUSINESS" directly above the identical
    // words already highlighted in the grid. A row that repeats the row under
    // it is the cheapest real estate in the game to buy back.
    const int columnWidth = (target.width() - 2 * margin) / kTopicColumns;
    const std::size_t columnRoom =
        static_cast<std::size_t>(std::max(1, columnWidth / glyphAdvance - 2));
    std::string detail = dialogueDetailLine(state);
    if (!detail.empty()) {
        // The key printed beside the label is part of what has to fit, exactly
        // as topicRowsFor composes it.
        const std::string numbered =
            std::to_string(state.cursor - topicPageOf(state.cursor) * kTopicPageSize + 1) + " " +
            detail;
        if (numbered.size() <= columnRoom) {
            detail.clear();
        }
    }
    const int detailRows = detail.empty() ? 0 : 1;
    // TASK #82. THE CASEBOOK'S OWN DATELINE, under the detail row and after
    // it in the priority order -- see DialogueViewState::caseRef's own
    // comment on why this is a fourth row rather than folded into `line`.
    const int caseRefRows = state.caseRef.empty() ? 0 : 1;
    const int topHeight =
        std::min(centre.y0 - scale, margin + rowStep * (1 + static_cast<int>(speech.size()) +
                                                        alertRows + detailRows + caseRefRows));
    // INNOVATION SPRINT ITEM #1. THE ANIMATED RECT, COMPUTED ONCE. The top
    // band already hugs the top edge (this file's own header), so it slides
    // in from off the top of the frame as `fade` rises: at fade == 0 this is
    // -topHeight, which puts the whole band above y == 0 and out of sight;
    // at fade == 1 (every hand-built state, and every settled frame) it is
    // exactly 0, which is the layout this panel has always drawn -- so the
    // OPEN geometry is untouched and only the path to it moves. `topHeight`
    // itself -- the CAPACITY math above, and every "does this row still fit
    // above topHeight" check below -- stays in the band's own unshifted
    // coordinates; only where the content is actually PAINTED gets this
    // offset added, at each drawText/fillRect call site, so a row that fits
    // at fade == 1 still fits mid-transition and nothing about how many rows
    // this band can hold changes because it is moving.
    const int topOffset = -static_cast<int>(static_cast<float>(topHeight) * (1.0F - fade));
    target.fillRect(0, topOffset, target.width(), topHeight, kPanel, 0.82F * fade);
    target.fillRect(0, topHeight + topOffset, target.width(), scale, kEdge, 0.55F * fade);

    // ---- the nameplate: who is talking, on a plate of their own -----------
    //
    // Flat text straight on the panel background read as one more line of
    // menu furniture -- indistinguishable, at a glance, from the topic grid
    // underneath it. A plate under the row, and an accent in the EXACT colour
    // attitudeInk already uses to carry standing, makes the row read as a
    // SPEAKER rather than a caption -- which is the one job a first-person
    // conversation's top band has that no other row in this game does.
    //
    // SIZED FROM WHAT IT DRAWS, and never past the clock's own reserve, for
    // the identical reason the speech column stops there: a plate wide enough
    // to run under the hour would look like it was reaching for something
    // that is not its business.
    {
        int nameplateWidth = textWidth(state.speaker, scale);
        if (!state.epithet.empty()) {
            nameplateWidth += glyphAdvance + textWidth(state.epithet, scale);
        }
        if (!state.attitude.empty()) {
            nameplateWidth += glyphAdvance + textWidth("(" + state.attitude + ")", scale);
        }
        const int plateCeiling = std::max(glyphAdvance * 4, target.width() - reservedRight);
        const int plateWidth = std::min(plateCeiling, margin + nameplateWidth + glyphAdvance);
        const int plateHeight = rowStep;
        // BRIGHT ENOUGH TO READ AS A SHAPE, not just a rounding error on the
        // panel underneath it. The first version of this used the panel's own
        // near-black at a low alpha and the plate vanished into it in every
        // capture -- correct arithmetic, invisible result, which is worse than
        // not drawing it: a plate you cannot see is not a nameplate treatment,
        // it is a wasted fillRect. Warm bronze, closer to kEdge than to kPanel,
        // so the row reads as raised the instant the panel appears.
        target.fillRect(0, topOffset, plateWidth, plateHeight, Rgb{0.22F, 0.19F, 0.15F},
                        0.62F * fade);
        target.fillRect(0, plateHeight - scale + topOffset, plateWidth, scale, kEdge, 0.45F * fade);
        // The accent: standing, in colour, before a single word of it is read.
        // Neutral brass when nobody has an opinion yet -- the keys page, the
        // options page and the casebook all borrow this same widget and none
        // of them is anybody's attitude.
        const Rgb accent = state.attitude.empty() ? kEdge : attitudeInk(state.attitude);
        target.fillRect(0, topOffset, scale, plateHeight, accent, 0.92F * fade);
    }

    int cursorX = margin;
    cursorX += drawText(target, cursorX, margin + topOffset, state.speaker, kSpeakerInk, 0.98F, scale);
    if (!state.epithet.empty()) {
        cursorX += glyphAdvance;
        cursorX +=
            drawText(target, cursorX, margin + topOffset, state.epithet, kEpithetInk, 0.80F, scale);
    }
    if (!state.attitude.empty()) {
        // Beside the name, not against the right edge: the right edge is the
        // clock's. Colour carries it -- red for hostile, green for warm and up
        // -- so standing is legible without reading.
        cursorX += glyphAdvance;
        drawText(target, cursorX, margin + topOffset, "(" + state.attitude + ")",
                 attitudeInk(state.attitude), 0.95F, scale);
    }
    // speechRevealChars < 0 means "draw all of it", which is what every
    // hand-built state gets and what a caller who never heard of the effect
    // gets for free. Session narrows the budget for a short window after a
    // line changes; see the field's own doc comment for why a cut, when there
    // is one, always lands on a word.
    std::size_t speechBudget = state.speechRevealChars < 0
                                   ? std::numeric_limits<std::size_t>::max()
                                   : static_cast<std::size_t>(state.speechRevealChars);
    for (std::size_t i = 0; i < speech.size() && speechBudget > 0; ++i) {
        const std::string& fullLine = speech[i];
        const int y = margin + rowStep * static_cast<int>(i + 1);
        if (fullLine.size() <= speechBudget) {
            drawText(target, margin, y + topOffset, fullLine, kSpeechInk, 0.94F, scale);
            speechBudget -= fullLine.size();
        } else {
            // Pulled back to the last whole word within the budget, the same
            // rule clipLabel uses and for the identical reason: a fragment cut
            // mid-word reads as the truncation bug this codebase has shipped
            // and fixed more than once, not as an animation in progress. The
            // three dots say "still arriving" out loud, the way clipLabel's
            // own mark says "cut here" out loud.
            const std::size_t cut = speechBudget;
            const std::size_t space = fullLine.rfind(' ', cut - 1);
            const std::string shown = (space != std::string::npos && space > 0)
                                          ? fullLine.substr(0, space)
                                          : fullLine.substr(0, cut);
            drawText(target, margin, y + topOffset, shown + "...", kSpeechInk, 0.94F, scale);
            speechBudget = 0;
        }
    }
    if (alertRows > 0) {
        // The bouncer's own colour, so it reads as somebody shouting across the
        // room rather than as another thing the person in front of you said.
        // `y` (and the fits-check against `topHeight`) stay in the band's own
        // unshifted coordinates -- see topOffset's own header -- only the
        // draw call itself is offset.
        const int y = margin + rowStep * (static_cast<int>(speech.size()) + 1);
        if (y + 6 * scale <= topHeight) {
            drawText(target, margin, y + topOffset,
                     clipToWidth(state.alert, target.width() - 2 * margin, scale),
                     Rgb{0.90F, 0.62F, 0.30F}, 0.95F, scale);
        }
    }
    if (detailRows > 0) {
        const int y = margin + rowStep * (static_cast<int>(speech.size()) + alertRows + 1);
        if (y + 6 * scale <= topHeight) {
            const std::size_t width = static_cast<std::size_t>(
                std::max(8, (target.width() - 2 * margin) / glyphAdvance));
            drawText(target, margin, y + topOffset, "> " + clipLabel(detail, width), kTopicPicked,
                     0.92F, scale);
        }
    }
    if (caseRefRows > 0) {
        // TASK #82. Last in the priority order and drawn like `alert` is:
        // clipped and marked rather than wrapped and silently shortened, and
        // dropped outright -- never garbled -- on the rare frame that has no
        // room left for it, exactly as `alert` and `detail` already are.
        const int y = margin +
                      rowStep * (static_cast<int>(speech.size()) + alertRows + detailRows + 1);
        if (y + 6 * scale <= topHeight) {
            drawText(target, margin, y + topOffset,
                     clipToWidth(state.caseRef, target.width() - 2 * margin, scale), kCaseRefInk,
                     0.86F, scale);
        }
    }

    // ---- the bottom band: what you can say --------------------------------
    //
    // SIZED FROM WHAT IT DRAWS, which is the rule the top band has always had
    // and this one never did. The band claimed the deepest it could legally go
    // whether it had twelve topics in it or two, so a doorman with three things
    // to say still took a fifth of the frame -- and even at four full rows it
    // took the remainder of a division nobody was spending.
    //
    // The haggle counter and the workbench keep the old floor: both draw four
    // rows AND a foot line pinned to the bottom margin, and shrinking the band
    // under them would put the foot through the last row.
    const int deepest = std::max(centre.y1 + scale, target.height() - margin - rowStep * 6);
    const int maxRows = std::clamp((target.height() - deepest - 2 * scale) / rowStep, 1, kTopicRows);
    // PAGED, and every printed row carries the key that picks it. The rows
    // themselves are built by topicRowsFor, which is what a test can drive --
    // see the header on the S4 mutation that shipped green.
    const std::vector<TopicRow> printed =
        topicRowsFor(state.topics, state.page, state.cursor, maxRows * kTopicColumns);
    const int rows = std::clamp((static_cast<int>(printed.size()) + kTopicColumns - 1) /
                                    kTopicColumns,
                                1, maxRows);
    const int bottomTop = (state.haggling || state.forging || state.letter)
                              ? deepest
                              : target.height() - 2 * scale - rows * rowStep;
    // INNOVATION SPRINT ITEM #1. THE SAME TREATMENT, FROM THE OTHER EDGE. The
    // bottom band hugs the bottom edge, so it slides in from off the bottom
    // of the frame as `fade` rises: at fade == 0 this is
    // `target.height() - bottomTop`, which pushes the whole band's top edge
    // down to exactly `target.height()` -- out of sight below the frame; at
    // fade == 1 it is 0, the untouched, always-shipped layout. Same rule as
    // topOffset: `bottomTop`, `rows`, `deepest` and every capacity/fits check
    // below stay in unshifted coordinates, and only the actual paint calls
    // add this in.
    const int bottomOffset =
        static_cast<int>(static_cast<float>(target.height() - bottomTop) * (1.0F - fade));
    // TASK #82. THE PARCHMENT SWITCH. A letter gets the warm palette in place
    // of the ordinary dark one; every other page -- including the casebook
    // list a letter is reached FROM -- keeps the panel it always had.
    const Rgb bandPanel = state.letter ? kParchmentPanel : kPanel;
    const Rgb bandEdge = state.letter ? kParchmentEdge : kEdge;
    target.fillRect(0, bottomTop + bottomOffset, target.width(), target.height() - bottomTop,
                    bandPanel, 0.86F * fade);
    target.fillRect(0, bottomTop - scale + bottomOffset, target.width(), scale, bandEdge,
                    0.65F * fade);

    if (state.haggling) {
        // The counter, not the topic list: what they want, what you are about
        // to say, and how much more of this they will take.
        const std::string head = state.goods + " - THEY ASK " + std::to_string(state.asking) + "C";
        drawText(target, margin, bottomTop + scale + bottomOffset, head, kSpeechInk, 0.95F, scale);
        const std::string mine = "YOUR OFFER: " + std::to_string(state.offer) + "C";
        drawText(target, margin, bottomTop + scale + rowStep + bottomOffset, mine, kHaggleInk,
                 0.98F, scale);
        std::string patience = "PATIENCE ";
        for (int i = 0; i < std::max(0, state.patience); ++i) {
            patience += '*';
        }
        drawText(target, margin, bottomTop + scale + rowStep * 2 + bottomOffset, patience,
                 kTopicInk, 0.85F, scale);
        drawText(target, margin, bottomTop + scale + rowStep * 3 + bottomOffset,
                 "LEFT/RIGHT NAME A NUMBER   ENTER OFFER   T TAKE IT   ESC WALK", kTopicInk, 0.80F,
                 scale);
        return;
    }

    if (state.forging) {
        // The workbench. Canon's own four questions and the link, one per row,
        // with the price and the verdict under them -- so a player can see a
        // composition get dearer as they turn it up, and see it refused before
        // they ask for it.
        const std::string head = "COMPOSE - COST " + std::to_string(state.forgeDifficulty) +
                                 " OF " + std::to_string(state.forgeCeiling);
        drawText(target, margin, bottomTop + scale + bottomOffset, head, kSpeechInk, 0.95F, scale);
        for (std::size_t i = 0; i < state.forgeFields.size(); ++i) {
            const int index = static_cast<int>(i);
            const int column = index / 3;
            const int row = index % 3;
            const int x = margin + column * columnWidth;
            const int y = bottomTop + scale + rowStep * (row + 1);
            if (column >= kTopicColumns || y + 6 * scale > target.height()) {
                continue;
            }
            const bool picked = index == state.forgeCursor;
            std::string label = state.forgeFields[i];
            const std::size_t room =
                static_cast<std::size_t>(std::max(1, columnWidth / glyphAdvance));
            if (label.size() > room) {
                label.resize(room);
            }
            if (picked) {
                drawPickHighlight(target, x, y + bottomOffset, rowStep, glyphAdvance, scale,
                                  textWidth(label, scale), static_cast<int>(room) * glyphAdvance,
                                  state.phase);
                drawText(target, x - glyphAdvance / 2, y + bottomOffset, ">", kTopicPicked, 0.95F,
                         scale);
            }
            drawText(target, x + glyphAdvance / 2, y + bottomOffset, label,
                     picked ? kTopicPicked : kTopicInk, picked ? 0.98F : 0.82F, scale);
        }
        const std::string foot =
            state.forgeProblem.empty()
                ? std::string("UP/DOWN FIELD   LEFT/RIGHT VALUE   ENTER MAKE   ESC STOP")
                : state.forgeProblem;
        drawText(target, margin, target.height() - margin - 2 * scale + bottomOffset, foot,
                 state.forgeProblem.empty() ? kTopicInk : kHaggleInk, 0.88F, scale);
        return;
    }

    if (state.letter) {
        // WRAP EVERY PARAGRAPH TO THE ROW'S ACTUAL WIDTH, and PAGE across
        // screens when the whole letter does not fit on one -- see
        // DialogueViewState::letterLines' own header on why this happens
        // here and not in Session. A blank entry in `letterLines` is a
        // paragraph break, not an empty row to skip: `wrapText` on an empty
        // string returns nothing, which would otherwise silently swallow
        // the gap between salutation and body.
        const std::size_t columns = static_cast<std::size_t>(
            std::max(8, (target.width() - 2 * margin) / glyphAdvance));
        std::vector<std::string> allLines;
        for (const std::string& paragraph : state.letterLines) {
            if (paragraph.empty()) {
                allLines.emplace_back();
                continue;
            }
            for (std::string& row : wrapText(paragraph, columns)) {
                allLines.push_back(std::move(row));
            }
        }
        // ONE FOOT ROW RESERVED, ALWAYS, so the page indicator (or the close
        // hint, on a letter short enough to need none) never has to fight a
        // body row for the same pixels. UNSHIFTED, like every other capacity
        // number in this function -- see topOffset's own header -- so paging
        // never recomputes mid-transition; bottomOffset is added only where
        // this is actually painted, below.
        const int footY = target.height() - margin - 2 * scale;
        const int rowsAvail =
            std::max(1, (footY - (bottomTop + scale) - scale) / rowStep);
        const int pages =
            std::max(1, (static_cast<int>(allLines.size()) + rowsAvail - 1) / rowsAvail);
        const int page = std::clamp(state.page, 0, pages - 1);
        const std::size_t first =
            static_cast<std::size_t>(page) * static_cast<std::size_t>(rowsAvail);
        for (std::size_t i = first;
             i < allLines.size() && i < first + static_cast<std::size_t>(rowsAvail); ++i) {
            const int y = bottomTop + scale + rowStep * static_cast<int>(i - first);
            drawText(target, margin, y + bottomOffset, allLines[i], kParchmentInk, 0.92F, scale);
        }
        const std::string foot =
            pages > 1 ? "0 MORE (" + std::to_string(page + 1) + "/" + std::to_string(pages) +
                            ")   ESC BACK   L PUTS IT DOWN"
                      : std::string("ESC BACK   L PUTS IT DOWN");
        drawText(target, margin, footY + bottomOffset, foot, kParchmentInk, 0.62F, scale);
        return;
    }

    // The layout is column-major over the rows the band was sized for. The
    // first version used a fixed six and broke out of the loop the moment a row
    // ran past the bottom edge -- which silently dropped the entire second
    // column, so a speaker with seven things to say showed four of them.
    for (std::size_t i = 0; i < printed.size(); ++i) {
        const int column = static_cast<int>(i) / rows;
        const int row = static_cast<int>(i) % rows;
        const int x = margin + column * columnWidth;
        const int y = bottomTop + scale + row * rowStep;
        if (column >= kTopicColumns || y + 6 * scale > target.height()) {
            continue;
        }
        // Cut to the column rather than allowed to run into the next one -- AND
        // a two-glyph gutter, which S4 did not have. Its own headline frame
        // shows topic 5 reading "ASK TO BE MADE SHE" with the next column's "9"
        // jammed against the E: the row is drawn at x + half a glyph and the
        // next column's cursor arrow at x - half a glyph, so a label sized to
        // the whole column overruns it by one glyph and collides with the arrow
        // of the one after. Two back.
        //
        // ON A WORD BOUNDARY since S6 -- see clipLabel, and the two labels in
        // S5's own shipped frame that made it necessary.
        const std::size_t room =
            static_cast<std::size_t>(std::max(1, columnWidth / glyphAdvance - 2));
        const std::string label = clipLabel(printed[i].label, room);
        if (printed[i].picked) {
            // THE HIGHLIGHT FIRST, so the arrow and the label print on top of
            // it rather than under it. Kept to the same room clipLabel already
            // cut the text to, so it can never reach the next column's key any
            // more than the label already could.
            drawPickHighlight(target, x, y + bottomOffset, rowStep, glyphAdvance, scale,
                              textWidth(label, scale), static_cast<int>(room) * glyphAdvance,
                              state.phase);
            drawText(target, x - glyphAdvance / 2, y + bottomOffset, ">", kTopicPicked, 0.95F,
                     scale);
        }
        drawText(target, x + glyphAdvance / 2, y + bottomOffset, label,
                 printed[i].picked ? kTopicPicked : kTopicInk, printed[i].picked ? 0.98F : 0.82F,
                 scale);
    }

}

}  // namespace granadad::render
