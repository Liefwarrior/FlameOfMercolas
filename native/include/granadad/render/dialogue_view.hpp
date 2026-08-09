#pragma once

// The conversation surface, drawn where a conversation is allowed to be.
//
// THE HUD RULE APPLIES HERE AND IT IS THE HARD PART. "The HUD hugs all four
// edges and leaves the centre completely clear" is not a preference
// (COMBAT-FEEL-REFERENCE section 3, task #66) and it is the exact rule the
// Java build's first-person view broke: its inspector sheet and craftings bar
// ate the right half of the screen.
//
// A dialogue surface is the obvious thing to break it with, because the obvious
// design is a big box in the middle. So this one is not:
//
//     TOP BAND     who is talking, what they think of you, and what they just
//                  said. Wrapped to three lines.
//     BOTTOM BAND  the topics, in two columns, with a cursor.
//     THE MIDDLE   the person you are talking to. Untouched.
//
// That is better design as well as compliance: you look at their face while
// they talk, which is the entire reason the game is first person.
//
// dialogueCentreIsClear() exists so a test can PROVE it rather than trust it,
// and the test drives it with the longest speaker name, the longest authored
// line and a full twelve-topic list at once.

#include <cstdint>
#include <string>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"

namespace granadad::render {

/// Everything the surface draws. Nothing here is authoritative: the simulation
/// owns all of it and this only puts it on the screen.
struct DialogueViewState {
    bool open = false;
    /// "MASTER VENN".
    std::string speaker;
    /// "LANDLORD OF THE GILDED GULL".
    std::string epithet;
    /// "WARM", "HOSTILE", ... straight off the ledger.
    std::string attitude;
    /// What they just said. Wrapped by the view, never by the caller.
    std::string line;
    /// Something the player must not miss that was NOT said by the person in
    /// front of them -- a bouncer's warning shouted across the room while a
    /// conversation is open.
    ///
    /// IT IS HERE AND NOT ON THE HUD, AND THAT IS A DEFECT REPORT. The HUD drew
    /// the alert at `height - margin - 23*scale`, which is inside the bottom
    /// band this file claims at `height - margin - rowStep*6`, and Session draws
    /// the panel first and the HUD over it -- so the warning overprinted row two
    /// of the topic grid across all three columns. docs/frames/s7-skyrun.png
    /// shipped as PROOF of a fix while showing exactly that:
    /// "2KLEDCTARBECK@GYOU HAVE HAD/THESKONLY WORD0YOURGET.1/THE DOOR."
    ///
    /// The bottom band is the topic list's and nothing else may draw in it. The
    /// top band is sized from what it draws, so the alert simply takes a row of
    /// it -- above the detail line, because a warning outranks a menu label.
    std::string alert;
    /// The topic labels, in the order the simulation built them. ALL of them,
    /// never a slice: paging is the view's job and a caller that pre-sliced
    /// would be a caller that can drop one.
    std::vector<std::string> topics;
    /// Which one the cursor is on. An index into `topics`, not into the page.
    int cursor = 0;
    /// Which page of the list is showing.
    int page = 0;

    // --- polish-2: a courtesy for whoever is watching it live ---------------
    //
    // Neither field changes what a test sees by default. A negative
    // speechRevealChars and a zero phase are exactly what a hand-built state
    // already had before this pass, because "the line arrived instantly and
    // the panel does not breathe" is what every caller that never heard of
    // this got for free before it existed.

    /// How many characters of `line` the top band has been told to show, or a
    /// negative number for all of it. Session narrows this for a short window
    /// after a fresh line arrives, so a reply visibly types in rather than
    /// snapping onto the screen -- see the drawing code for why the cut, when
    /// there is one, always lands on a word and never mid-one: a frame caught
    /// mid-reveal must read as "still arriving", not repeat the class of bug
    /// clipLabel exists to rule out.
    int speechRevealChars = -1;
    /// A seconds-ish animation clock for the panel's own small motion -- the
    /// picked topic's highlight breathes with it. The same role
    /// body_->stepCount()/60 already plays for the lamp flicker in
    /// Session::drawFrame: a pure function of simulated steps, so a scripted
    /// capture still draws the same frame every time it is asked to.
    float phase = 0.0F;

    // --- task #83: the panel eases open and closed instead of popping -------
    //
    // DEFAULTS TO 1, WHICH IS "FULLY OPEN AND NOT ANIMATING" -- exactly what
    // every hand-built state already meant before this field existed. A test
    // that constructs a DialogueViewState directly and never heard of this
    // draws bit-for-bit what it always drew.

    /// 0 (closed) .. 1 (open). Session eases this with render::EasedToggle
    /// rather than snapping it the step `open` flips, so pressing E, J, F1,
    /// F2, ESC or C -- this widget is dialogue, the casebook, the keys page,
    /// options, the pause menu and the character sheet at once, see the
    /// struct's own header -- grows the panel in and shrinks it away rather
    /// than switching it like a light. Multiplies the alpha of the panel's
    /// own background and its edge line, top band and bottom band alike: the
    /// box itself materialising and dissolving is what reads as a transition
    /// rather than a flip. The content drawn over it is not separately faded.
    ///
    /// THE PANEL KEEPS DRAWING DURING ITS CLOSING TAIL. `open` alone used to
    /// be both "is there a panel" and "has the player closed it", and a close
    /// animation needs those to be two different questions for a few frames:
    /// Session sets `open` true for as long as openAmount is above zero, even
    /// after the flag that opened it has gone false, so the fade has
    /// something left to draw while it finishes.
    float openAmount = 1.0F;

    // --- haggling -----------------------------------------------------------
    bool haggling = false;
    /// What they are asking, and what the player is about to offer.
    int asking = 0;
    int offer = 0;
    int patience = 0;
    std::string goods;

    // --- the workbench ------------------------------------------------------
    bool forging = false;
    /// The bench's five fields, already worded by the simulation. The view
    /// prints them and owns none of them.
    std::vector<std::string> forgeFields;
    int forgeCursor = 0;
    /// What the composition would cost to open, and the deepest the student can
    /// reach. Both integers out of the cost model.
    int forgeDifficulty = 0;
    int forgeCeiling = 0;
    /// Empty when the composition is legal; the refusal in short words when it
    /// is not, so the bench says WHY before the priest has to.
    std::string forgeProblem;
};

/// The topic grid. Four rows is what the bottom band can hold at 640x360
/// without crossing into the exclusion rectangle -- it is a measurement, not a
/// preference -- and three columns is what it takes to show all twelve of
/// Master Venn's, who has the longest list in the game: his own business,
/// three authored micro-histories, the ward, the vanished clerk, his trade,
/// buying a bed, arguing about the price of one, standing him a drink, a hand
/// in his purse, and leaving.
inline constexpr int kTopicRows = 4;
inline constexpr int kTopicColumns = 3;
inline constexpr int kTopicSlots = kTopicRows * kTopicColumns;

/// How many topics one page shows, and it is NINE for a reason that is not
/// aesthetic: those are the keys 1..9, and every visible topic must be
/// reachable by the number printed beside it.
///
/// S3 shipped twelve slots numbered `1`-`9` and then three rows whose number
/// was a full stop, reachable only by arrow keys with nothing on screen saying
/// so -- and Master Venn already filled all twelve, so the next topic added to
/// him would have vanished with no ellipsis and no warning. The S3 review found
/// both. The grid still holds twelve; nine of them are the page, the tenth is
/// "0 MORE (2/3)" and 0 is bound to it, and a list of any length is therefore
/// completely addressable from the keyboard with no topic ever dropped.
inline constexpr int kTopicPageSize = 9;

/// How many pages a list of this length needs. Always at least one.
[[nodiscard]] int topicPageCount(std::size_t topics) noexcept;
/// Which page a topic index falls on.
[[nodiscard]] int topicPageOf(int index) noexcept;
/// True when the list is long enough to need the MORE row at all.
[[nodiscard]] bool topicsPaginate(std::size_t topics) noexcept;

/// Draws the whole surface over a rendered frame. A closed conversation draws
/// nothing at all.
void drawDialogue(Framebuffer& target, const DialogueViewState& state);

/// Breaks a line at spaces to fit `columns` characters. Never splits a word
/// unless the word alone is longer than the column.
[[nodiscard]] std::vector<std::string> wrapText(const std::string& text, std::size_t columns);

/// The full, UNCUT label of the row the cursor is on -- what the detail line
/// under the grid prints.
///
/// WHY THIS EXISTS. A topic column is eighteen glyphs at every resolution this
/// game runs at, and some labels are longer than that no matter how they are
/// worded: "SIGN ON: THE SKYRUNNERS" is twenty-three, and S6's own shipped
/// frame printed it as "7 SIGN ON: THE." -- a row that names nothing. More
/// clipping logic cannot fix a label that does not fit; a second line can.
///
/// IT GOES IN THE TOP BAND, UNDER WHAT THEY SAID, and that is a measurement
/// and not a preference. At 1280x720 the bottom band is 154 pixels between the
/// exclusion rectangle and the frame edge: four rows of the topic grid and
/// thirty spare pixels, which is two short of a fifth row. A detail line drawn
/// under the grid would be clipped off the bottom of the frame -- the same
/// class of bug it exists to fix. The top band is sized from what it draws, so
/// it grows by a row and nothing else moves.
///
/// So the grid stays terse and scannable, and the ONE row the player has the
/// cursor on is spelled out in full across the whole width underneath it. An
/// empty list, or a cursor on the MORE row, gives an empty string and the line
/// is not drawn.
[[nodiscard]] std::string dialogueDetailLine(const DialogueViewState& state);

/// Cuts a topic label to the room its column has, ON A WORD BOUNDARY, and says
/// out loud that it cut.
///
/// S5 shipped `label.resize(room)`, which cuts mid-word, and its own headline
/// frame docs/frames/s5-skyrunner-line.png has "6 THE VANISHED CLE" and "7 ASK
/// TO BE MADE R" printed on it. A label that stops in the middle of a word
/// reads as a rendering bug rather than as a list that is longer than the
/// column, which is what it actually is.
///
/// The rule: keep whole words while they fit, drop the first one that does not,
/// and mark the cut with a full stop. A single word longer than the column has
/// nowhere to break, so it is still cut -- but it is cut one short and marked,
/// so even that case says "there is more of this word" instead of pretending
/// the label ended there.
[[nodiscard]] std::string clipLabel(const std::string& label, std::size_t room);

/// One printed row of the topic list: the number the player presses, a space,
/// and the label -- exactly the string drawText is handed.
struct TopicRow {
    std::string label;
    bool picked = false;
};

/// THE ROWS THE TOPIC LIST ACTUALLY DRAWS, for a page of a list.
///
/// EXTRACTED IN S5, AND THE REASON IS A FINDING. S4 closed "topics past the
/// ninth were unreachable" with a case over topicPageCount / topicPageOf, which
/// is pure arithmetic and never touches the drawing code. The S4 review
/// reinstated the original bug in drawDialogue -- `std::min(total,
/// kTopicPageSize)` instead of `std::min(total, first + kTopicPageSize)`, which
/// makes page two and page three draw ZERO topic rows -- and the whole 311-case
/// gate stayed green, because the neighbouring render case only asserted that
/// SOME ink was on screen and an empty page still has a speaker header on it.
///
/// drawDialogue calls this and prints what it returns. A case over this
/// function is therefore a case over the drawing path, and the mutation that
/// shipped green in S4 empties the vector it returns.
[[nodiscard]] std::vector<TopicRow> topicRowsFor(const std::vector<std::string>& topics, int page,
                                                 int cursor, int capacity);

}  // namespace granadad::render
