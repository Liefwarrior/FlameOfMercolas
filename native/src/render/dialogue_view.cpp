#include "granadad/render/dialogue_view.hpp"

#include <algorithm>
#include <string>

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
    const int reservedRight = 34 * scale;
    const std::size_t columns = static_cast<std::size_t>(
        std::max(8, (target.width() - 2 * margin - reservedRight) / glyphAdvance));
    std::vector<std::string> speech = wrapText(state.line, columns);
    const int maxSpeechRows = std::max(0, (centre.y0 - margin - rowStep * 2) / rowStep);
    if (static_cast<int>(speech.size()) > maxSpeechRows) {
        speech.resize(static_cast<std::size_t>(std::max(0, maxSpeechRows)));
    }
    const int topHeight =
        std::min(centre.y0 - scale, margin + rowStep * (1 + static_cast<int>(speech.size())));
    target.fillRect(0, 0, target.width(), topHeight, kPanel, 0.82F);
    target.fillRect(0, topHeight, target.width(), scale, kEdge, 0.55F);

    int cursorX = margin;
    cursorX += drawText(target, cursorX, margin, state.speaker, kSpeakerInk, 0.98F, scale);
    if (!state.epithet.empty()) {
        cursorX += glyphAdvance;
        cursorX += drawText(target, cursorX, margin, state.epithet, kEpithetInk, 0.80F, scale);
    }
    if (!state.attitude.empty()) {
        // Beside the name, not against the right edge: the right edge is the
        // clock's. Colour carries it -- red for hostile, green for warm and up
        // -- so standing is legible without reading.
        cursorX += glyphAdvance;
        drawText(target, cursorX, margin, "(" + state.attitude + ")",
                 attitudeInk(state.attitude), 0.95F, scale);
    }
    for (std::size_t i = 0; i < speech.size(); ++i) {
        drawText(target, margin, margin + rowStep * static_cast<int>(i + 1), speech[i], kSpeechInk,
                 0.94F, scale);
    }

    // ---- the bottom band: what you can say --------------------------------
    const int bottomTop = std::max(centre.y1 + scale, target.height() - margin - rowStep * 6);
    target.fillRect(0, bottomTop, target.width(), target.height() - bottomTop, kPanel, 0.82F);
    target.fillRect(0, bottomTop - scale, target.width(), scale, kEdge, 0.55F);

    if (state.haggling) {
        // The counter, not the topic list: what they want, what you are about
        // to say, and how much more of this they will take.
        const std::string head = state.goods + " - THEY ASK " + std::to_string(state.asking) + "C";
        drawText(target, margin, bottomTop + scale, head, kSpeechInk, 0.95F, scale);
        const std::string mine = "YOUR OFFER: " + std::to_string(state.offer) + "C";
        drawText(target, margin, bottomTop + scale + rowStep, mine, kHaggleInk, 0.98F, scale);
        std::string patience = "PATIENCE ";
        for (int i = 0; i < std::max(0, state.patience); ++i) {
            patience += '*';
        }
        drawText(target, margin, bottomTop + scale + rowStep * 2, patience, kTopicInk, 0.85F,
                 scale);
        drawText(target, margin, bottomTop + scale + rowStep * 3,
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
        drawText(target, margin, bottomTop + scale, head, kSpeechInk, 0.95F, scale);
        const int columnWidth = (target.width() - 2 * margin) / kTopicColumns;
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
            if (picked) {
                drawText(target, x - glyphAdvance / 2, y, ">", kTopicPicked, 0.95F, scale);
            }
            std::string label = state.forgeFields[i];
            const std::size_t room =
                static_cast<std::size_t>(std::max(1, columnWidth / glyphAdvance));
            if (label.size() > room) {
                label.resize(room);
            }
            drawText(target, x + glyphAdvance / 2, y, label, picked ? kTopicPicked : kTopicInk,
                     picked ? 0.98F : 0.82F, scale);
        }
        const std::string foot =
            state.forgeProblem.empty()
                ? std::string("UP/DOWN FIELD   LEFT/RIGHT VALUE   ENTER MAKE   ESC STOP")
                : state.forgeProblem;
        drawText(target, margin, target.height() - margin - 2 * scale, foot,
                 state.forgeProblem.empty() ? kTopicInk : kHaggleInk, 0.88F, scale);
        return;
    }

    // How many rows the band ACTUALLY has, computed rather than assumed. The
    // first version used a fixed six and broke out of the loop the moment a row
    // ran past the bottom edge -- which silently dropped the entire second
    // column, so a speaker with seven things to say showed four of them.
    const int columnWidth = (target.width() - 2 * margin) / kTopicColumns;
    const int rows =
        std::clamp((target.height() - bottomTop - 2 * scale) / rowStep, 1, kTopicRows);

    // PAGED, and every printed row carries the key that picks it. The rows
    // themselves are built by topicRowsFor, which is what a test can drive --
    // see the header on the S4 mutation that shipped green.
    const int capacity = rows * kTopicColumns;
    const std::vector<TopicRow> printed =
        topicRowsFor(state.topics, state.page, state.cursor, capacity);

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
            drawText(target, x - glyphAdvance / 2, y, ">", kTopicPicked, 0.95F, scale);
        }
        drawText(target, x + glyphAdvance / 2, y, label,
                 printed[i].picked ? kTopicPicked : kTopicInk, printed[i].picked ? 0.98F : 0.82F,
                 scale);
    }

    // ---- the detail line: the picked row, spelled out ----------------------
    //
    // THE COLUMN IS EIGHTEEN GLYPHS AND SOME LABELS ARE NOT. S6's own shipped
    // frame printed "7 SIGN ON: THE." -- clipped on a word boundary to a row
    // that names nothing at all -- and two contracts as "8 TAKE 3 SCALPS." and
    // "9 TAKE 4 SCALPS.". No amount of cleverness in clipLabel fixes a string
    // that is longer than the space it has.
    //
    // This is the space it has. One row, under the grid, the whole width of the
    // frame, showing the label the cursor is on with nothing taken off it. The
    // band already had the room: the grid is four rows in a six-row band.
    const std::string detail = dialogueDetailLine(state);
    if (!detail.empty()) {
        const int y = bottomTop + scale + rows * rowStep;
        // Never past the bottom edge, and never into the play space. Both
        // clauses matter: the band's height is computed, not fixed, and a
        // resolution that leaves no room simply does not get the line.
        const std::size_t width =
            static_cast<std::size_t>(std::max(1, (target.width() - 2 * margin) / glyphAdvance));
        if (y + 6 * scale <= target.height() && y > centre.y1) {
            drawText(target, margin, y, clipLabel(detail, width), kTopicPicked, 0.90F, scale);
        }
    }
}

}  // namespace granadad::render
