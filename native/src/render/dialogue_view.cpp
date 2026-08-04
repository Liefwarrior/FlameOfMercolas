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
    const std::size_t columns =
        static_cast<std::size_t>(std::max(8, (target.width() - 2 * margin) / glyphAdvance));
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
        const int width = textWidth(state.attitude, scale);
        drawText(target, target.width() - margin - width, margin, state.attitude,
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

    const int columnWidth = (target.width() - 2 * margin) / kTopicColumns;
    const std::size_t shown = std::min(state.topics.size(), static_cast<std::size_t>(kTopicSlots));
    for (std::size_t i = 0; i < shown; ++i) {
        const int column = static_cast<int>(i) / kTopicRows;
        const int row = static_cast<int>(i) % kTopicRows;
        const int x = margin + column * columnWidth;
        const int y = bottomTop + scale + row * rowStep;
        if (y + 6 * scale > target.height()) {
            break;
        }
        const bool picked = static_cast<int>(i) == state.cursor;
        std::string label = (i < 9 ? std::to_string(i + 1) : std::string(".")) + " " +
                            state.topics[i];
        // Truncated to the column rather than allowed to run into the next one.
        const std::size_t room =
            static_cast<std::size_t>(std::max(1, (columnWidth - glyphAdvance) / glyphAdvance));
        if (label.size() > room) {
            label.resize(room);
        }
        if (picked) {
            drawText(target, x - glyphAdvance / 2, y, ">", kTopicPicked, 0.95F, scale);
        }
        drawText(target, x + glyphAdvance / 2, y, label, picked ? kTopicPicked : kTopicInk,
                 picked ? 0.98F : 0.82F, scale);
    }
}

}  // namespace granadad::render
