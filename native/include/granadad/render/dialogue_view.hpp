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
//     TOP BAND     a bordered pane hugging the top edge: the header, in the
//                  reference's `subject > status` shape -- the speaker's name
//                  knocked out of an inverted fill IN THEIR OWN ATTITUDE
//                  COLOUR, then their standing, with the epithet as the
//                  right-aligned readout -- then what they just said, then an
//                  alert if one was shouted, then a dateline if there is one.
//                  Sized from what it draws and clamped to the exclusion rect.
//     BOTTOM BAND  a bordered pane hugging the bottom edge: the topics as a
//                  numbered direct-select list whose COLUMN COUNT COMES FROM
//                  ITS OWN LONGEST LABEL, selection an INVERTED FILL in the
//                  speaker's accent, and an instruction header riding the
//                  pane's top rule so it costs no row at all.
//     THE MIDDLE   the person you are talking to. Untouched.
//
// That is better design as well as compliance: you look at their face while
// they talk, which is the entire reason the game is first person.
//
// BUILT ON panel.hpp, WHICH IS THE POINT. Every rule, edge, junction, header,
// list and fill above is a call into the shared terminal vocabulary, so this
// surface is the same register as the creation flow, the map, the casebook and
// the controls page rather than a fourth dialect of it. What it looked like
// before that conversion is on record: no border motif, no header, the
// selection drawn as a `>` arrow the spec forbids by name, and every topic cut
// to an eighteen-glyph column -- "4 PICK THEIR POCK.", "8 SELL WHAT YOU.",
// "2 THE VANISHED." -- on the busiest surface in the build.
//
// The centre-clear guarantee is PROVEN rather than trusted, by cases in
// test_render.cpp and test_tavern_render.cpp that drive it with the longest
// speaker name, the longest authored line and a full twelve-topic list at
// once, at every size the game runs at.

#include <cstdint>
#include <string>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/hud.hpp"
#include "granadad/render/panel.hpp"

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

    // --- TASK #82: the journal's dateline, and the letter -------------------

    /// ONE LINE: a casebook entry's dateline and cross-reference -- "DAY 2
    /// 20:14  FROM THE BODY  OPENED THE OUTFALL, THE LEDGER". Empty draws
    /// nothing, which is every page but the casebook and every casebook
    /// screen but a picked entry.
    ///
    /// A SEPARATE ROW, DELIBERATELY, RATHER THAN FOLDED INTO `line`. `line`
    /// is wrapped and, when it asks for more room than the top band has, the
    /// TAIL IS SILENTLY DROPPED -- see the comment over `speech.resize` in
    /// dialogue_view.cpp, an accepted tradeoff for a widget reused by six
    /// different pages. A lead's own found/detail text already runs close to
    /// that ceiling on its own (the Drowned Hold's paragraph is the longest
    /// sentence in the game), so a dateline appended to it would be the
    /// first thing silently cut -- data quietly missing, which is exactly
    /// the class of bug the "no shitty English anywhere" bar exists to
    /// catch. This row is sized and clipped the way `alert` and the topic
    /// detail line already are: it MARKS a cut, and never drops one in
    /// silence.
    std::string caseRef;

    /// WHAT THE BLANK ROWS UNDER THE LIST ARE WAITING FOR.
    ///
    /// UI-REFERENCE-TERMINAL.md rules on this by name: "Empty states are
    /// worded, not blank -- `no trinket`, `Luck 0`. Absence is stated so the
    /// reader knows it was considered." Three of the tiled Menu's four panels
    /// ship a stranger a header over a void -- THE LETTERS holds nothing at
    /// all until a lead has been stood over, THE CHART holds three rows, the
    /// casebook tile holds one -- and a quarter-screen panel with nothing in
    /// it reads as a page that failed rather than as a case nobody has worked
    /// yet.
    ///
    /// NOT `line`, and the distinction is the point. `line` says WHAT THE
    /// PANEL IS, is authored once per panel, and the three top tiles
    /// deliberately suppress it (see drawMenuTiles' own note on spending a
    /// quarter of the frame on rows rather than on a sentence already read).
    /// This says WHAT WOULD BE IN IT AND HOW THE PLAYER PUTS IT THERE, and it
    /// is drawn ONLY into room the rows did not want -- so it is gone the
    /// moment the list has earned the space, and it never competes with
    /// content for a row.
    ///
    /// The wording is per-state and belongs to the caller: a tile that already
    /// holds one letter must not still say nobody has written to you.
    std::string emptyLine;

    /// TASK #82. A LETTER IS A DOCUMENT, NOT A MENU: an authored page in
    /// somebody else's hand, addressed and signed, read in full -- not a
    /// wrapped sentence competing with the top band's own two-row ceiling.
    /// When true, the bottom band stops being a topic grid (same switch
    /// `haggling`/`forging` already make) and becomes a parchment-toned
    /// panel instead of the ordinary dark one -- the "parchment-style
    /// panel" out of this engine's own pixel-ink vocabulary, since nothing
    /// in this renderer has a texture to borrow one from.
    bool letter = false;
    /// ONE ENTRY A PARAGRAPH, RAW. Unlike a topic label (short, authored to
    /// fit its column -- casebook.hpp's own `brief` field states that rule
    /// outright) a letter's body is PROSE, of a length nobody chose with a
    /// pixel budget in mind, so it is wrapped and PAGED at draw time instead
    /// -- the same wrapText this widget already runs the top band's speech
    /// through, reapplied here because Session has no window size to wrap
    /// against and a caller that pre-decided where every sentence breaks at
    /// every resolution would be the "picks badly" case casebook.hpp warns
    /// against, aimed at prose instead of a proper noun. `cursor`/`page`
    /// mean something different while this is true: `page` selects which
    /// screen of the WRAPPED, FLATTENED body is showing rather than a page
    /// of `topics`, which a letter does not use for anything but the title
    /// list a caller reads before one is picked.
    std::vector<std::string> letterLines;

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

    // --- ship note move 3: the prompts name the device holding them --------
    //
    // The keys this view prints, in the vocabulary of whichever device last
    // spoke -- assembled by Session off controls.hpp's prompt lookup. THE
    // DEFAULTS ARE THE EXACT LITERALS THIS VIEW ALWAYS PRINTED, so a
    // hand-built state draws byte-identical frames.
    /// "ENTER", or "A". The haggle's OFFER IT key.
    std::string confirmKey = "ENTER";
    /// "ESC", or "B". The haggle's WALK AWAY key and the letter foot's BACK.
    std::string backKey = "ESC";
    /// The haggle's take-their-price key: "T", or "RB" (main.cpp routes
    /// Action::PageNext -- and now the raw T -- to takeAskingPrice).
    std::string takeKey = "T";
    /// The letter foot's closing clause. Keyboard: "L PUTS IT DOWN". Empty
    /// drops the clause -- the pad has no L, and its B BACK already names
    /// the way out.
    std::string letterDownLine = "L PUTS IT DOWN";
    /// Whether the topic rows print their digits ("6 PICK THEIR POCKET"). A
    /// keyboard's digits pick a row outright; a pad has none, walks the list
    /// on the D-pad and confirms on A, so its rows print the words alone.
    bool showDigits = true;
};

/// The topic grid, AS IT WAS AUTHORED AND AS IT IS NO LONGER DRAWN.
///
/// Four rows of three columns was the fixed layout this surface used at every
/// resolution, and three columns of an eighteen-glyph budget is exactly why it
/// shipped "4 PICK THEIR POCK.". UI-REFERENCE-TERMINAL.md rules against it in
/// as many words -- "Column count follows content, not a fixed setting ... Pick
/// the count from the longest entry against the available width" -- so
/// drawDialogue now asks panel.hpp's planOptionList, which does exactly that,
/// and NOTHING IN THE RENDERER READS THESE THREE ANY MORE.
///
/// They survive as the SIZE OF A FULL PAGE, which several cases measure
/// against, and as the number a capacity argument is given when a caller wants
/// "as many rows as this list can have". Twelve is Master Venn's list, which is
/// the longest in the game: his own business, three authored micro-histories,
/// the ward, the vanished clerk, his trade, buying a bed, arguing about the
/// price of one, standing him a drink, a hand in his purse, and leaving.
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

/// The full, UNCUT label of the row the cursor is on.
///
/// ITS JOB HAS SHRUNK, AND THAT IS THE FIX WORKING. This existed because a
/// topic column was eighteen glyphs at every resolution and some labels are
/// longer than that however they are worded, so the picked row needed a line of
/// its own to be readable at all. The list now sizes its columns to its own
/// longest label, so at 640x360 and above nothing is cut and the restatement is
/// not needed -- the bottom pane's header carries the instruction instead.
/// drawDialogue still falls back to this, on the rule above the list, when a
/// window is narrow enough that a label genuinely does not fit its column
/// (320x180 is a smoke size, not a play size). A row nobody can read outranks a
/// header saying what the panel is for.
///
/// The original argument, kept because it is what the fallback is for:
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

/// Cuts a label to the room its column has, ON A WORD BOUNDARY, and says out
/// loud that it cut.
///
/// THE CONVERSATION SURFACE NO LONGER CALLS THIS, which is the right fix rather
/// than a cleverer cut: eighteen glyphs was never going to hold "PICK THEIR
/// POCKET" and no rule can make it. drawDialogue's topic list picks its column
/// count from the longest label it actually holds. This is still menu_view.cpp's
/// (the tiled Menu's four smaller panels) and creation.cpp's, and a case in
/// test_tavern_render.cpp pins every one of its cuts.
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
                                                 int cursor, int capacity,
                                                 bool showDigits = true);

/// WHAT THE BOTTOM BAND WILL ACTUALLY DRAW, at this frame size, with no
/// framebuffer involved.
///
/// PUBLIC AND PURE, AND THAT IS THE POINT. The defect this pass closed --
/// "4 PICK THEIR POCK." on the busiest surface in the build -- was a LAYOUT
/// defect, and the only evidence against it up to now was a screenshot. A
/// screenshot proves one resolution on one day. This lets a case assert, at
/// every size the game runs at, that the page holds every row it was given and
/// that no label on it was cut -- which is the claim, stated as a claim.
///
/// It is also the shape of the lesson from the S4 review: the paging fix was
/// tested through arithmetic that the drawing code did not call, the review
/// then reinstated the bug INSIDE drawDialogue, and the whole gate stayed
/// green. drawDialogue calls this and draws what it returns, so a case over
/// this is a case over the drawing path.
struct TopicLayout {
    /// The rows, exactly as they will be printed -- key, label and accent.
    /// Empty when the band has no room at all.
    std::vector<PanelOption> options;
    /// The column count, row count and label column planOptionList settled on.
    OptionListPlan plan;
    /// Index into `options` of the picked row, or -1.
    int selected = -1;
    /// True when what will be printed is NOT the whole label -- which is the
    /// one case the picked row still gets spelled out in full above the list.
    /// False at every size the game is actually played at.
    bool abbreviated = false;
};

[[nodiscard]] TopicLayout dialogueTopicLayout(const DialogueViewState& state, int width,
                                              int height);

// ---------------------------------------------------------------------------
// THE POINTER PASS: the bottom band's topic list, invertible
// ---------------------------------------------------------------------------
//
// This one widget IS five of the ship note's silent pages -- the pause menu,
// the options page, the grimoire, the wait page and a live conversation all
// draw their list through drawDialogue's bottom band -- so ONE hit-test here
// is the mouse for all five, exactly the way mapPlaceAtPixel was the mouse
// for the ward map. Both functions below are pure and settled (openAmount is
// taken as 1: a click mid-slide answers for where the band is landing, which
// is where the very next frame draws it).

/// WHERE THE TOPIC LIST ACTUALLY IS, at this frame size: the rect
/// drawDialogue hands drawOptionListPlanned (the band frame's interior, rows
/// 0..plan.rows), the plan it draws with, and how many rows are on the page.
/// Not usable for the haggle, the workbench or an open letter -- those band
/// bodies are not the topic list, and the honest answer for a pointer there
/// is "the page is modal".
struct TopicListGeometry {
    bool usable = false;
    PanelMetric metric;
    /// The list's rect in framebuffer pixels -- what optionListAt inverts.
    PanelRect list;
    OptionListPlan plan;
    /// Rows actually printed on this page, the MORE row included.
    int count = 0;
};
[[nodiscard]] TopicListGeometry dialogueTopicListGeometry(const DialogueViewState& state,
                                                          int width, int height);

/// What a pixel of the band means, in the vocabulary the router speaks.
struct DialogueTopicHit {
    /// Index into the drawn page's rows, or -1 for "not on a row".
    int row = -1;
    /// The topic's index into state.topics, or -1 (off-list, or the MORE row).
    int index = -1;
    /// The nine-key slot the row answers to on this page -- what
    /// chooseVisibleTopic and its per-page siblings take. -1 with `index`.
    int slot = -1;
    /// True when the pixel is on the MORE row that turns the page.
    bool more = false;
};

/// Which row of the drawn topic list a pixel lands on. The inverse of what
/// drawDialogue drew, built out of the same walk -- dialogueTopicLayout for
/// the rows and the plan, the band arithmetic for the rect, optionListAt for
/// the row -- see panel.hpp's optionListAt on why the inverse of a layout
/// lives beside the layout.
[[nodiscard]] DialogueTopicHit dialogueTopicAtPixel(const DialogueViewState& state, int width,
                                                    int height, int px, int py);

/// The picked row's highlight: a soft band under the label and a bright
/// hairline where the cursor arrow sits.
///
/// NOT USED BY drawDialogue ANY MORE. UI-REFERENCE-TERMINAL.md forbids the
/// arrow by name -- "Selection is an inverted highlight ... Not an arrow, not a
/// bracket. The fill is the affordance" -- and the conversation panel's own
/// verifier photographed `>1 TELL ME ABOUT...`. The topic list selects with
/// panel.hpp's drawInvertedFill in the speaker's own attitude accent now. This
/// shape survives only for menu_view.cpp's four tiled panels, which were not in
/// that pass's scope and are the next surface to convert.
///
/// EXPOSED (moved out of dialogue_view.cpp's own anonymous namespace) so the
/// tiled Menu (menu_view.cpp) can draw the identical "this one is selected"
/// shape inside
/// each of its four smaller panels instead of re-implementing it -- see that
/// file's own header. Parameterised by `scale` rather than tied to the one
/// register drawDialogue() itself draws at, so a caller drawing at
/// hudMinorScale (a smaller panel, denser text) gets a highlight sized to
/// match.
void drawPickHighlight(Framebuffer& target, int x, int y, int rowStep, int glyphAdvance, int scale,
                       int labelWidth, int roomWidth, float phase);

}  // namespace granadad::render
