#include "granadad/render/dialogue_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "granadad/render/panel.hpp"

namespace granadad::render {

namespace {

constexpr Rgb kPanel{0.04F, 0.04F, 0.05F};
constexpr Rgb kEdge{0.44F, 0.40F, 0.31F};
constexpr Rgb kEpithetInk{0.60F, 0.58F, 0.50F};
constexpr Rgb kSpeechInk{0.88F, 0.86F, 0.80F};
constexpr Rgb kTopicPicked{0.98F, 0.86F, 0.42F};
constexpr Rgb kHaggleInk{0.90F, 0.76F, 0.42F};
constexpr Rgb kAlertInk{0.90F, 0.62F, 0.30F};
/// TASK #82. THE ONE ROW THAT SAYS "THIS IS A CASEBOOK ENTRY, NOT A LEAD ON
/// THE LIST" -- muted, the same register the epithet already reads at,
/// because a dateline is reference material read deliberately and not the
/// thing the eye should land on first.
constexpr Rgb kCaseRefInk{0.58F, 0.56F, 0.48F};
/// TASK #82. THE PARCHMENT PALETTE. Warm and dim rather than inverted to a
/// bright page -- this build's whole HUD is dark panels and light ink read
/// by what reads as lamplight, and a letter is a page held up to that same
/// lamp, not a sheet lit from behind.
constexpr Rgb kParchmentPanel{0.16F, 0.11F, 0.06F};
constexpr Rgb kParchmentEdge{0.62F, 0.46F, 0.22F};
constexpr Rgb kParchmentInk{0.86F, 0.74F, 0.52F};

/// Attitude colouring. The one place standing is a COLOUR and not a word: you
/// should be able to tell across a room, without reading, that the man behind
/// the bar has stopped liking you.
///
/// IT IS ALSO THE SELECTION FILL NOW. UI-REFERENCE-TERMINAL.md's rule is that
/// the inverted highlight takes "the ENTITY'S own accent" -- Blood's row fills
/// bright green, not one global highlight hue -- and on a conversation surface
/// the entity is the person you are talking to. So a hostile topic list lights
/// up red and a friend's lights up green, and standing is legible from the
/// shape of the cursor before a word of the header is read.
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

/// One glyph is one cell, and every string this surface prints is ASCII by the
/// time it gets here (foldToAscii runs in the raws loader).
[[nodiscard]] int cellsOf(const std::string& text) noexcept {
    return static_cast<int>(text.size());
}

/// A printed topic row -- "6 PICK THEIR POCKET", "0 MORE (2/3)" -- split back
/// into the key the player presses and the label it names.
///
/// topicRowsFor STAYS THE DRAWING PATH, which is the whole point of it being a
/// separate function: the S4 review reinstated a paging bug in the draw code
/// and the gate stayed green, so the rows a test can drive have to be the rows
/// that are actually printed. This composes PanelOptions out of exactly what it
/// returns rather than re-deriving the page here, so the mutation that empties
/// that vector still empties this list.
[[nodiscard]] PanelOption topicOption(const std::string& row, const Rgb& accent) {
    PanelOption option;
    option.accent = accent;
    option.valueInk = InkRole::Number;
    const std::size_t space = row.find(' ');
    if (space == std::string::npos) {
        option.label = row;
        return option;
    }
    option.key = row.substr(0, space);
    option.label = row.substr(space + 1);
    return option;
}

/// What rides the rule at the top of the bottom band.
///
/// UI-REFERENCE-TERMINAL.md, on the actor sheet: "Text can ride the horizontal
/// rule ... A cheap way to place a persistent fact without spending a line on
/// it." It is cheap here for a reason that is a measurement rather than a
/// preference -- the bottom band may not begin above the exclusion rectangle,
/// which at 320x180 leaves it FIVE rows in total. A header row and an interior
/// rule would have spent two of the three that are not border, and a two-row
/// topic list is not a topic list. So the header rides the rule the frame was
/// going to draw anyway, and every row of the band stays the list's.
struct BandHeader {
    /// The selection named in prose -- the reference's own "Selected tile:
    /// (28,13) The Poisoned Dusts". Takes the subject's accent.
    std::string subject;
    /// The persistent readout, right-aligned: the page you are on, the price
    /// being argued over, what a composition costs. Dim.
    std::string readout;
};

/// How much room the bottom band has, in the grid it draws on.
///
/// IT MAY NOT BEGIN ABOVE THE EXCLUSION RECTANGLE, which is the whole
/// constraint on this surface and is not negotiable (COMBAT-FEEL-REFERENCE
/// section 3). Derived ONCE, here, because both drawDialogue and
/// dialogueTopicLayout need the same answer and two copies of this arithmetic
/// is how a planned layout and a drawn one drift apart.
struct BandGeometry {
    int cells = 0;
    int rows = 0;
    bool usable = false;
};

[[nodiscard]] BandGeometry bandGeometryFor(const PanelMetric& metric, int width, int height) {
    const CentreRect centre = hudCentreRect(width, height);
    BandGeometry geometry;
    geometry.rows = metric.rowsIn(height - (centre.y1 + metric.scale));
    geometry.cells = metric.cellsIn(width);
    // Three rows is a border and one content row. Four cells is two edges and
    // two of content. Below either there is no pane to draw.
    geometry.usable = geometry.rows >= 3 && geometry.cells >= 4;
    return geometry;
}

/// A CEILING, NOT A SETTING -- planOptionList picks the actual count from the
/// longest entry against the width available, which is the rule
/// UI-REFERENCE-TERMINAL.md states outright ("Column count follows content, not
/// a fixed setting") and the rule this file used to break by authoring
/// kTopicColumns == 3 at every resolution.
[[nodiscard]] OptionListStyle topicListStyle() {
    OptionListStyle style;
    style.maxColumns = 4;
    style.gutterCells = 2;
    style.alignValues = false;
    return style;
}

/// Rules and edges out of the shared vocabulary, at this surface's palette.
[[nodiscard]] FrameStyle bandStyle(float fade, bool parchment) {
    FrameStyle style;
    // `◆` at every corner and junction, consistently, across the whole
    // surface -- the same choice keys_page.cpp, casebook_page.cpp and
    // creation_page.cpp already made, so the four converted screens are one
    // register and not four dialects of it.
    style.junction = Motif::Diamond;
    style.alpha = fade;
    // A BAND, NOT A PAGE. The first-person view is behind these panels and is
    // allowed to show through -- you are meant to still see who is talking.
    // kBandGroundAlpha rather than kPageGroundAlpha, and the two are named so
    // the difference reads as a decision instead of as more drift.
    style.groundAlpha = kBandGroundAlpha;
    style.rule = parchment ? kParchmentEdge : kEdge;
    style.ground = parchment ? kParchmentPanel : kPanel;
    return style;
}

}  // namespace

// THE PICKED ROW'S HIGHLIGHT: a soft band under the label and a bright
// hairline where the cursor arrow sits.
//
// NO LONGER USED BY drawDialogue, AND THAT IS THIS PASS'S SECOND FINDING.
// UI-REFERENCE-TERMINAL.md forbids the arrow by name -- "Selection is an
// inverted highlight ... Not an arrow, not a bracket" -- and the conversation
// panel's verifier photographed exactly that: a `>` printed beside the picked
// topic. The topic list now selects with drawInvertedFill in the speaker's own
// attitude accent, which is the spec's idiom, so this shape survives only for
// menu_view.cpp's four tiled panels, which are a different surface and were
// not in this pass's scope. See the header on the .hpp declaration.
//
// BREATHES GENTLY WITH `phase`, and 0 draws it at rest.
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
    // is where the column ends, and the mark is what makes that legible.
    //
    // THE CONVERSATION SURFACE NO LONGER CALLS THIS AT ALL, and that is the
    // right fix rather than a cleverer cut: eighteen glyphs was never enough
    // for "PICK THEIR POCKET" and no rule can make it be. See drawDialogue --
    // the topic list picks its column count from the longest label it actually
    // holds, so nothing is cut. menu_view.cpp's tiled panels still use this,
    // and so does creation.cpp; a case in test_tavern_render.cpp pins it.
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
    // silence, which is the exact failure the paging replaced. If it ever
    // happens the MORE row survives, so the list still says out loud that
    // there is more of it.
    if (capacity >= 1 && static_cast<int>(rows.size()) > capacity) {
        rows.resize(static_cast<std::size_t>(capacity));
        rows.back().label =
            "0 MORE (" + std::to_string(shown + 1) + "/" + std::to_string(pages) + ")";
        rows.back().picked = false;
    }
    return rows;
}

// WHAT THE TOPIC LIST WILL ACTUALLY DRAW, worked out with no framebuffer in
// sight. See the header on the declaration for why it is public.
TopicLayout dialogueTopicLayout(const DialogueViewState& state, int width, int height) {
    TopicLayout layout;
    const PanelMetric metric = panelMetric(height);
    const BandGeometry geometry = bandGeometryFor(metric, width, height);
    if (!geometry.usable) {
        return layout;
    }
    const int listCells = geometry.cells - 2;
    const int listRowsAvail = geometry.rows - 2;
    const OptionListStyle style = topicListStyle();
    const Rgb accent = state.attitude.empty() ? kTopicPicked : attitudeInk(state.attitude);

    // PLANNED AGAINST THE WHOLE PAGE FIRST, then drawn against that one plan,
    // which is what panel.hpp's own header asks for: planning per drawn subset
    // makes the column count and the value column jump every time a row
    // appears or goes, and "nothing jumps as the cursor moves" is the rule
    // this vocabulary exists to keep.
    std::vector<TopicRow> printed =
        topicRowsFor(state.topics, state.page, state.cursor, kTopicPageSize + 1);
    for (const TopicRow& topicRow : printed) {
        layout.options.push_back(topicOption(topicRow.label, accent));
    }
    const PanelRect probe{0, 0, metric.widthOf(listCells), metric.heightOf(listRowsAvail)};
    layout.plan = planOptionList(layout.options, probe, metric, style);
    // ---- the narrow-window fallback, and the order of its two losses ---
    //
    // At 640x360 and above the page fits at full width and neither of
    // these runs. Below that -- 320x180 is a smoke size, not a play size,
    // and the bottom band gets FIVE rows there because it may not begin
    // above the exclusion rectangle -- the whole page does not fit, and
    // something has to give. It gives in this order, which is a judgement
    // and worth stating:
    //
    //   1. LABEL WIDTH FIRST. Enough columns for every row of the page,
    //      with the labels cut by clipLabel -- which cuts on a word
    //      boundary and MARKS the cut, so the row says it was shortened
    //      instead of pretending it ended there -- and the picked row
    //      spelled out in full on the rule above (see below). A page whose
    //      labels are abbreviated is legible. A page missing three of its
    //      topics is not, and a `0 MORE` that turns to a page that does not
    //      exist is worse than either.
    //   2. ROWS ONLY IF THAT IS STILL NOT ENOUGH, back through
    //      topicRowsFor, which forces its last surviving row to be the
    //      MORE row rather than dropping topics in silence.
    const int fits = layout.plan.columns * layout.plan.rows;
    if (fits < static_cast<int>(printed.size()) && listRowsAvail > 0) {
        const int need =
            std::min(style.maxColumns,
                     (static_cast<int>(printed.size()) + listRowsAvail - 1) / listRowsAvail);
        const int share = (listCells - (need - 1) * style.gutterCells) / std::max(1, need);
        const int room = share - layout.plan.keyCells;
        if (need > layout.plan.columns && room >= 4) {
            for (PanelOption& option : layout.options) {
                option.label = clipLabel(option.label, static_cast<std::size_t>(room));
            }
            const PanelRect narrowed{0, 0, metric.widthOf(listCells),
                                     metric.heightOf(listRowsAvail)};
            layout.plan = planOptionList(layout.options, narrowed, metric, style);
        }
    }
    const int capacity = layout.plan.columns * layout.plan.rows;
    if (capacity >= 1 && capacity < static_cast<int>(printed.size())) {
        printed = topicRowsFor(state.topics, state.page, state.cursor, capacity);
        layout.options.clear();
        for (const TopicRow& topicRow : printed) {
            PanelOption option = topicOption(topicRow.label, accent);
            if (layout.plan.labelCells > 0 && cellsOf(option.label) > layout.plan.labelCells) {
                option.label =
                    clipLabel(option.label, static_cast<std::size_t>(layout.plan.labelCells));
            }
            layout.options.push_back(option);
        }
    }
    for (std::size_t i = 0; i < printed.size(); ++i) {
        if (printed[i].picked) {
            layout.selected = static_cast<int>(i);
        }
    }
    // THE ONE CASE THE RESTATEMENT IS STILL FOR. Whenever the row on screen
    // is not the whole label -- which at 640x360 and above is never -- the
    // picked row is spelled out in full on the rule, which is the
    // reference's "the selection is named in prose underneath", and the
    // instruction gives way to it: a row nobody can read whole outranks a
    // header saying what the panel is for.
    if (layout.selected >= 0) {
        const std::string full = dialogueDetailLine(state);
        const std::string& shown =
            layout.options[static_cast<std::size_t>(layout.selected)].label;
        if (!full.empty() && shown != full) {
            layout.abbreviated = true;
        }
    }
    return layout;
}

// ---------------------------------------------------------------------------
// THE SURFACE
// ---------------------------------------------------------------------------
//
// REBUILT ON THE SHARED TERMINAL VOCABULARY (panel.hpp), and the reason is a
// defect report. The last UI pass converted creation, the map and the casebook
// and never touched this file -- which is where the game is actually played --
// so the busiest surface in the build was still drawing flat bands with no
// border motif, no header, a `>` arrow the spec forbids by name, and topic
// labels cut to an eighteen-glyph column that printed "4 PICK THEIR POCK.".
//
// What changed, in the spec's own terms:
//
//   * BOTH BANDS ARE PanelFrames. `+~-~-` rules with `◆` junctions, vertical
//     edges alternating `|`/`!` per row. The register, not a filled rectangle.
//   * THE TOP BAND CARRIES THE HEADER, in the actor sheet's `subject > status`
//     shape: the speaker's name knocked out of an inverted fill IN THEIR OWN
//     ATTITUDE COLOUR, then `>`, then the standing. The epithet is the
//     right-aligned readout beside it.
//   * SELECTION IS AN INVERTED FILL in that same accent. The arrow is gone.
//   * THE TOPIC LIST PICKS ITS COLUMN COUNT FROM ITS OWN LONGEST LABEL,
//     through planOptionList, instead of being authored at three columns of
//     eighteen glyphs. Nothing is cut, at any of the five capture sizes.
//   * The bottom band's header RIDES the rule it was going to draw anyway.
//
// NOT MASTER/DETAIL, AND THAT IS A DECISION RATHER THAN AN OMISSION. The split
// is right for the casebook and the quiz because their entries have a body to
// show -- a lead has a place, a finding and a verb; an answer has a
// consequence. sim::Topic has a label and nothing else (dialogue.hpp: kind,
// label, barkKey, payload, arg), so a detail pane over a topic could only
// restate the label bigger or invent a preview the simulation does not have.
// And the room is not there either: the bottom band may not begin above the
// exclusion rectangle, which caps it at eleven rows at 640x360 and five at
// 320x180 -- a detail pane half that width and four rows deep holds less than
// the top band already gives the speaker's own words.
//
// The surface IS master/detail at the frame's own scale, which is the shape
// UI-REFERENCE-TERMINAL.md's gameplay frame actually describes: "the question
// is asked where the narration lives; the answer is given where the verbs
// live". The top band is the detail pane -- who this is, what they think of
// you, what they just said, and the highlighted row named in full on the rule
// between them. The bottom band is the master list. Between them sits the
// person's face, which is the entire reason this game is first person.
//
// THE HUD RULE STILL BINDS. Both bands are sized from what they draw and
// clamped to the exclusion rectangle; the centre of the screen is untouched at
// every resolution, which test_render.cpp and test_tavern_render.cpp both
// prove with the longest name, the longest line and a full topic list at once.
void drawDialogue(Framebuffer& target, const DialogueViewState& state) {
    if (!state.open) {
        return;
    }
    // TASK #83 / INNOVATION SPRINT ITEM #1. `fade` drives every alpha AND the
    // two slide offsets, so the panel eases in from the edges rather than
    // popping. 1 is "fully open and not animating", which is what every
    // hand-built state already meant -- a caller that never heard of this gets
    // topOffset == bottomOffset == 0 and the settled layout.
    const float fade = std::clamp(state.openAmount, 0.0F, 1.0F);
    if (fade <= 0.0F) {
        return;
    }
    const PanelMetric metric = panelMetric(target.height());
    const CentreRect centre = hudCentreRect(target.width(), target.height());
    const Rgb accent = state.attitude.empty() ? kTopicPicked : attitudeInk(state.attitude);

    // =====================================================================
    // the top band -- who, what they think of you, and what they just said
    // =====================================================================
    //
    // WRAPPED CLEAR OF THE TOP-RIGHT CORNER, which is the clock's and the
    // purse's and stays theirs: a spoken line running under a two-digit hour
    // is unreadable and looks like a bug. ASKED, NOT GUESSED -- the HUD knows
    // how wide its own stack is.
    const int reservedRight = hudTopRightReserve(target.height());
    const int topCells = std::max(4, metric.cellsIn(target.width() - reservedRight));
    const int topTextCells = std::max(1, topCells - 2);
    const int topRowsAvail = metric.rowsIn(centre.y0);
    const int topContentAvail = topRowsAvail - 2;
    if (topContentAvail >= 1) {
        const int alertRows = state.alert.empty() ? 0 : 1;
        const int caseRefRows = state.caseRef.empty() ? 0 : 1;
        // The interior rule under the header costs a row, so it is spent only
        // where there are rows to spend. At 320x180 the whole band is three
        // content rows and the header plus two lines of speech is the better
        // use of them.
        int ruleRows = topContentAvail >= 4 ? 1 : 0;
        std::vector<std::string> speech =
            wrapText(state.line, static_cast<std::size_t>(topTextCells));
        // The alert takes a row of the top band when there is one, so the
        // speech gives one up rather than the alert being silently dropped: a
        // warning outranks the fourth line of a greeting.
        const int maxSpeechRows =
            std::max(0, topContentAvail - 1 - ruleRows - alertRows - caseRefRows);
        if (static_cast<int>(speech.size()) > maxSpeechRows) {
            speech.resize(static_cast<std::size_t>(maxSpeechRows));
        }
        if (speech.empty() && alertRows == 0 && caseRefRows == 0) {
            ruleRows = 0;
        }
        const int contentRows =
            1 + ruleRows + static_cast<int>(speech.size()) + alertRows + caseRefRows;
        const int topHeight = metric.heightOf(contentRows + 2);
        // The band hugs the top edge, so it slides in from off the top as
        // `fade` rises. At fade == 1 this is exactly 0 -- the settled layout.
        // Every capacity number above stays in unshifted coordinates; only the
        // bounds handed to the frame carry the offset, and everything inside
        // is drawn relative to the frame, so a row that fits at rest still
        // fits mid-transition.
        const int topOffset = -static_cast<int>(static_cast<float>(topHeight) * (1.0F - fade));
        const PanelRect bounds{0, topOffset, metric.widthOf(topCells), topHeight};
        PanelFrame frame(target, bounds, metric, bandStyle(fade, false));
        if (ruleRows > 0) {
            frame.addRule(1);
        }
        frame.draw();
        const PanelRect body = frame.interior();

        // ---- the header: subject then status -----------------------------
        //
        // UI-REFERENCE-TERMINAL.md, off the actor sheet: "The header is
        // subject then status -- `Jeff > Idle`, the state in its own colour."
        // The name is knocked out of an inverted fill in that same colour,
        // which is this vocabulary's one way of emphasising a title: two glyph
        // sizes on one surface means two cell grids, and two cell grids is how
        // a column stops lining up with the rule above it.
        {
            const int nameCells = std::min(cellsOf(state.speaker) + 2, topTextCells);
            if (!state.speaker.empty() && nameCells > 0) {
                drawInvertedFill(target, body, metric, 0, 0, nameCells, accent, 0.95F * fade);
                drawCellTextKnockout(target, body, metric, 1, 0, state.speaker,
                                     panelInk().knockout, fade);
            }
            int cell = std::max(0, nameCells) + 1;
            if (!state.attitude.empty() && cell + 2 < topTextCells) {
                cell += drawCellText(target, body, metric, cell, 0, "> ", panelInk().dim, fade);
                cell += drawCellText(target, body, metric, cell, 0, state.attitude, accent,
                                     0.98F * fade);
            }
            // The epithet is the right-aligned readout -- and on every page
            // this widget is reused for (the pause menu, the controls page,
            // the grimoire, the casebook) it is the instruction line, which is
            // exactly where the reference puts a persistent readout. Clipped
            // to what is left rather than allowed to run back into the name.
            if (!state.epithet.empty()) {
                const int room = topTextCells - cell - 2;
                if (room > 0) {
                    const std::string cut =
                        clipToWidth(state.epithet, metric.widthOf(room), metric.scale);
                    drawCellTextRight(target, body, metric, 1, 0, cut, kEpithetInk, 0.82F * fade);
                }
            }
        }

        // ---- what they said ----------------------------------------------
        //
        // speechRevealChars < 0 means "draw all of it", which is what every
        // hand-built state gets. Session narrows the budget for a short window
        // after a line changes, and the cut always lands on a word: a frame
        // caught mid-reveal must read as "still arriving" and not as the
        // truncation bug this codebase has shipped and fixed more than once.
        const int firstSpeechRow = 1 + ruleRows;
        std::size_t speechBudget = state.speechRevealChars < 0
                                       ? std::numeric_limits<std::size_t>::max()
                                       : static_cast<std::size_t>(state.speechRevealChars);
        for (std::size_t i = 0; i < speech.size() && speechBudget > 0; ++i) {
            const std::string& fullLine = speech[i];
            const int row = firstSpeechRow + static_cast<int>(i);
            if (fullLine.size() <= speechBudget) {
                drawCellText(target, body, metric, 0, row, fullLine, kSpeechInk, 0.96F * fade);
                speechBudget -= fullLine.size();
            } else {
                const std::size_t cut = speechBudget;
                const std::size_t space = fullLine.rfind(' ', cut - 1);
                const std::string shown = (space != std::string::npos && space > 0)
                                              ? fullLine.substr(0, space)
                                              : fullLine.substr(0, cut);
                drawCellText(target, body, metric, 0, row, shown + "...", kSpeechInk, 0.96F * fade);
                speechBudget = 0;
            }
        }
        int row = firstSpeechRow + static_cast<int>(speech.size());
        if (alertRows > 0) {
            // The bouncer's own colour, so it reads as somebody shouting
            // across the room rather than as another thing the person in front
            // of you said. It lives HERE and not on the HUD because the HUD
            // drew it straight through the topic grid -- see
            // DialogueViewState::alert's own header, and the frame that
            // shipped as proof of a fix while showing the collision.
            drawCellText(target, body, metric, 0, row, state.alert, kAlertInk, 0.96F * fade);
            ++row;
        }
        if (caseRefRows > 0) {
            drawCellText(target, body, metric, 0, row, state.caseRef, kCaseRefInk, 0.88F * fade);
        }
    }

    // =====================================================================
    // the bottom band -- what you can say
    // =====================================================================
    //
    // IT MAY NOT BEGIN ABOVE THE EXCLUSION RECTANGLE. That is the whole
    // constraint on this surface and it is not negotiable (COMBAT-FEEL-
    // REFERENCE section 3): the Java build's first-person view ate the right
    // half of the screen with an inspector sheet and this is the element most
    // likely to do it again.
    const BandGeometry geometry = bandGeometryFor(metric, target.width(), target.height());
    if (!geometry.usable) {
        return;
    }
    const int bandRowsAvail = geometry.rows;
    const int bandCells = geometry.cells;

    BandHeader header;
    std::vector<PanelOption> options;
    OptionListStyle style = topicListStyle();

    if (state.haggling) {
        header.subject = "HAGGLING OVER " + state.goods;
        header.readout = "THEY ASK " + std::to_string(state.asking) + " C";
    } else if (state.forging) {
        header.subject = "COMPOSING";
        header.readout = "REACH " + std::to_string(state.forgeCeiling);
    } else if (state.letter) {
        header.subject = "A LETTER";
    } else {
        // AN INSTRUCTION HEADER, NAMING THE TASK AND ITS SUBJECT -- the
        // reference's own "Select a tile of the Pearl Lands to preach the word
        // of Xaleon to", which is what a header does on a panel that is a task
        // rather than a place.
        //
        // AND NOT THE PICKED ROW SPELLED OUT, WHICH IS WHAT THE FIRST VERSION
        // OF THIS PUT HERE. It photographed as "TELL ME ABOUT..." printed on
        // the rule with the identical words highlighted in the row directly
        // under it -- the same duplicate row polish-1 already bought back once
        // (see clipLabel's header). It was worth a row then and it is worth a
        // rule now: the list no longer cuts anything, so there is nothing left
        // for a restatement to rescue.
        //
        // Only for a real conversation. On the pause menu, the controls page,
        // the grimoire and the casebook -- the other pages this one widget is
        // -- `epithet` is already the instruction and it is already on screen
        // in the top band, so this side stays empty rather than saying the
        // same thing twice at the other end of the frame.
        if (!state.attitude.empty() && !state.speaker.empty()) {
            header.subject = "WHAT YOU SAY TO " + state.speaker;
        }
        const int pages = topicPageCount(state.topics.size());
        if (pages > 1) {
            header.readout = "PAGE " + std::to_string(std::clamp(state.page, 0, pages - 1) + 1) +
                             "/" + std::to_string(pages);
        }
    }

    // ---- the list, and the height it asks for ---------------------------
    TopicLayout layout;
    OptionListPlan plan;
    int selected = -1;
    if (!state.haggling && !state.forging && !state.letter) {
        // THE LAYOUT IS DECIDED SOMEWHERE A TEST CAN DRIVE IT. Every column
        // count, every fallback and every cut this list makes is in
        // dialogueTopicLayout, which is pure and takes a width and a height --
        // so "no topic label is ever cut at a size the game runs at" is a case
        // over a function rather than a claim about a screenshot. That is the
        // lesson of the S4 review, which reinstated a paging bug in the draw
        // code and watched the whole gate stay green.
        layout = dialogueTopicLayout(state, target.width(), target.height());
        options = layout.options;
        plan = layout.plan;
        selected = layout.selected;
        if (layout.abbreviated) {
            // THE ONE CASE THE RESTATEMENT IS STILL FOR. Whenever the row on
            // screen is not the whole label -- which at 640x360 and above is
            // never -- the picked row is spelled out in full on the rule,
            // which is the reference's "the selection is named in prose
            // underneath", and the instruction gives way to it: a row nobody
            // can read whole outranks a header saying what the panel is for.
            const std::string full = dialogueDetailLine(state);
            if (!full.empty()) {
                header.subject = full;
            }
        }
    } else if (state.forging) {
        // The workbench. Canon's own four questions and the link, one per row,
        // as a numbered-list-shaped block whose SELECTION IS A FILL -- this is
        // the second `>` arrow the spec forbade and this pass removed.
        style.showKeys = false;
        style.alignValues = true;
        for (const std::string& field : state.forgeFields) {
            PanelOption option;
            option.accent = accent;
            option.valueInk = InkRole::Number;
            const std::size_t colon = field.find(':');
            if (colon == std::string::npos) {
                option.label = field;
            } else {
                option.label = field.substr(0, colon);
                std::size_t at = colon + 1;
                while (at < field.size() && field[at] == ' ') {
                    ++at;
                }
                option.value = field.substr(at);
            }
            options.push_back(option);
        }
        selected = state.forgeCursor;
    } else if (state.haggling) {
        // NAV VERBS IN THE SAME LIST AS CONTENT OPTIONS, keyed, in aligned
        // columns -- the reference's command palette, in place of the one
        // crammed sixty-glyph line this used to print.
        // SHIP NOTE MOVE 3: the keys arrive on the state, worded for
        // whichever device last spoke; defaults are the old literals.
        options.push_back(PanelOption{"LEFT RIGHT", "NAME A NUMBER", "", accent,
                                      InkRole::Number, true, false});
        options.push_back(
            PanelOption{state.confirmKey, "OFFER IT", "", accent, InkRole::Number, true, false});
        options.push_back(PanelOption{state.takeKey, "TAKE THEIR PRICE", "", accent,
                                      InkRole::Number, true, false});
        options.push_back(
            PanelOption{state.backKey, "WALK AWAY", "", accent, InkRole::Number, true, false});
    }

    // The band is as deep as what it holds and no deeper -- the rule the top
    // band has kept since S3 and this one did not until polish-1. A doorman
    // with three things to say does not take the same slice of the frame as
    // Master Venn with twelve.
    int bandRows = bandRowsAvail;
    if (!state.haggling && !state.forging && !state.letter) {
        bandRows = std::clamp(plan.rows + 2, 3, bandRowsAvail);
    }
    const int bandHeight = metric.heightOf(bandRows);
    const int bandTop = target.height() - bandHeight;
    // The same treatment from the other edge: the band hugs the bottom, so it
    // slides in from off the bottom of the frame as `fade` rises.
    const int bottomOffset =
        static_cast<int>(static_cast<float>(target.height() - bandTop) * (1.0F - fade));
    const PanelRect bandBounds{0, bandTop + bottomOffset, metric.widthOf(bandCells), bandHeight};
    PanelFrame band(target, bandBounds, metric, bandStyle(fade, state.letter));
    band.draw();
    const PanelRect body = band.interior();

    // ---- the header, riding the rule ------------------------------------
    if (!header.subject.empty() || !header.readout.empty()) {
        const PanelRect rule{body.x, bandBounds.y, body.w, metric.cellH()};
        const Rgb ground = state.letter ? kParchmentPanel : kPanel;
        const Rgb subjectInk = state.letter ? kParchmentInk : accent;
        int used = 0;
        if (!header.subject.empty()) {
            const int wide = std::min(cellsOf(header.subject) + 2, metric.cellsIn(rule.w));
            // The rule glyphs under the text are knocked out, so the header
            // reads as text SET INTO the rule rather than printed over it.
            target.fillRect(rule.x, rule.y, metric.widthOf(wide), metric.cellH(), ground,
                            0.95F * fade);
            used = 1 + drawCellText(target, rule, metric, 1, 0, header.subject, subjectInk,
                                    0.96F * fade);
        }
        if (!header.readout.empty()) {
            const int wide = cellsOf(header.readout) + 2;
            const int start = metric.cellsIn(rule.w) - wide;
            if (start > used) {
                target.fillRect(rule.x + metric.widthOf(start), rule.y, metric.widthOf(wide),
                                metric.cellH(), ground, 0.95F * fade);
                drawCellTextRight(target, rule, metric, 1, 0, header.readout, panelInk().dim,
                                  0.90F * fade);
            }
        }
    }

    if (state.letter) {
        // WRAP EVERY PARAGRAPH TO THE ROW'S ACTUAL WIDTH, and PAGE across
        // screens when the whole letter does not fit on one. A blank entry in
        // `letterLines` is a paragraph break, not an empty row to skip.
        const std::size_t columns = static_cast<std::size_t>(std::max(1, metric.cellsIn(body.w)));
        std::vector<std::string> allLines;
        for (const std::string& paragraph : state.letterLines) {
            if (paragraph.empty()) {
                allLines.emplace_back();
                continue;
            }
            for (std::string& wrapped : wrapText(paragraph, columns)) {
                allLines.push_back(std::move(wrapped));
            }
        }
        // ONE FOOT ROW RESERVED, ALWAYS, so the page indicator never has to
        // fight a body row for the same pixels.
        const int rowsAvail = std::max(1, band.rowCount() - 1);
        const int pages =
            std::max(1, (static_cast<int>(allLines.size()) + rowsAvail - 1) / rowsAvail);
        const int page = std::clamp(state.page, 0, pages - 1);
        const std::size_t first =
            static_cast<std::size_t>(page) * static_cast<std::size_t>(rowsAvail);
        for (std::size_t i = first;
             i < allLines.size() && i < first + static_cast<std::size_t>(rowsAvail); ++i) {
            drawCellText(target, body, metric, 0, static_cast<int>(i - first), allLines[i],
                         kParchmentInk, 0.94F * fade);
        }
        // SHIP NOTE MOVE 3: the foot names the device's own keys. With the
        // state's defaults this is character-for-character the old string.
        std::string foot;
        if (pages > 1) {
            foot = "0 MORE (" + std::to_string(page + 1) + "/" + std::to_string(pages) + ")   ";
        }
        foot += state.backKey + " BACK";
        if (!state.letterDownLine.empty()) {
            foot += "   " + state.letterDownLine;
        }
        drawCellText(target, body, metric, 0, band.rowCount() - 1, foot, kParchmentInk,
                     0.68F * fade);
        return;
    }

    if (state.haggling) {
        // ALIGNED KEY/VALUE ROWS -- labels left, values at a common column.
        // Facts, not paragraphs, which is what a counter-offer is.
        std::string patience;
        for (int i = 0; i < std::max(0, state.patience); ++i) {
            patience += '*';
        }
        if (patience.empty()) {
            // Absence is worded, never blank -- the reference's own `no
            // trinket`. A patience of nothing is the last thing a player
            // should have to infer from an empty row.
            patience = "GONE";
        }
        const std::vector<PanelFact> facts{
            PanelFact{"THEY ASK", std::to_string(state.asking) + " C", InkRole::Number},
            PanelFact{"YOUR OFFER", std::to_string(state.offer) + " C", InkRole::Accent},
            PanelFact{"PATIENCE", patience, InkRole::Key},
        };
        const int factRows = std::min(3, band.rowCount());
        drawFacts(target, band.band(0, factRows), metric, facts, -1, fade);
        if (band.rowCount() > factRows + 1) {
            drawOptionList(target, band.band(factRows + 1, band.rowCount() - factRows - 1), metric,
                           options, -1, style, 0.92F * fade);
        }
        return;
    }

    if (state.forging) {
        // THE COMMIT VERB AT THE FOOT, RESTATING ITS COST, and STATE CHANGES
        // THE VERB rather than grey it out: a composition the bench refuses
        // says why, where the verb would have been. Both are
        // UI-REFERENCE-TERMINAL.md verbatim.
        const int footRows = 1;
        const int fieldRows = std::max(1, band.rowCount() - footRows);
        drawOptionList(target, band.band(0, fieldRows), metric, options, selected, style, fade);
        const PanelRect foot = band.band(band.rowCount() - footRows, footRows);
        if (state.forgeProblem.empty()) {
            drawCommitVerb(target, foot, metric, "ENTER - MAKE IT",
                           "COST " + std::to_string(state.forgeDifficulty) + " OF " +
                               std::to_string(state.forgeCeiling),
                           accent, fade);
        } else {
            drawCellText(target, foot, metric, 0, 0, state.forgeProblem, kHaggleInk, 0.96F * fade);
        }
        return;
    }

    // THE TOPIC LIST. Numbered, direct-select, column-major so the keys read
    // DOWN each column and the numbering runs continuously across the layout;
    // the selection is an inverted fill in the speaker's own accent, and the
    // nav verbs (LEAVE, and the MORE row that turns the page) live in the same
    // list as the content options rather than in a separate controls area.
    drawOptionListPlanned(target, band.band(0, plan.rows), metric, options, selected, plan, fade);
}

}  // namespace granadad::render
