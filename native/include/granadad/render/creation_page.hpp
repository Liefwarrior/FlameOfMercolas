#pragma once

// THE CHARACTER-CREATION SCREEN, COMPOSED -- the seven steps of the flow drawn
// as ONE INSTRUMENT rather than seven variations on a topic grid.
//
// WHAT THIS REPLACES, AND WHY. Every creation step used to build a
// DialogueViewState and hand it to drawDialogue(): a top band, a bottom grid of
// eighteen-glyph columns, and a large untouched middle. That reuse was right
// when the flow was two screens; by the time it was seven it had three defects
// the owner hit directly:
//
//   1. EVERY ANSWER CLIPPED AT EIGHTEEN GLYPHS INCLUDING ITS ROW NUMBER. A quiz
//      answer is a sentence. `1 YOU TOLD THE.` is not a choice anybody can
//      make, and the renderer compensated by printing the hovered answer THREE
//      TIMES per frame (a detail row, a centre line, and the grid row) --
//      which is the real-estate complaint and the readability complaint in one
//      place.
//   2. THE COLUMN COUNT WAS FIXED AT TWO whatever the window was, so at
//      1280x720 half the frame was unused above a vast empty middle holding one
//      line of text.
//   3. NOTHING SHOWED WHAT AN ANSWER BOUGHT. The owner spent real choices --
//      skills, coin, faction standing, a named actor's disposition -- with no
//      idea what any of them did.
//
// docs/design/UI-REFERENCE-TERMINAL.md answers all three by name and prescribes
// the layout for this exact screen: *"The chargen quiz. Answers as the left
// list, the consequence of the highlighted answer in the right pane. The owner
// spent his choices blind; this is the fix, and it is already drawn for us."*
// So this file is that, built out of render/panel.hpp's vocabulary and adding
// nothing local that the vocabulary could have carried.
//
// ---------------------------------------------------------------------------
// A MODEL, NOT A WIDGET -- WHICH IS WHAT MAKES THE MOUSE HONEST
// ---------------------------------------------------------------------------
// CreationPage is plain data: the crumbs, the rows, the detail pane's facts and
// effects, the commit verb. CreationFlow builds one; drawCreationPage() draws
// it; creationPageHitTest() answers "what is under this pointer" off THE SAME
// composition function the drawing uses. That last part is the whole reason the
// model exists as a separate thing: a hit-test derived from a second, parallel
// description of the layout is a hit-test that will eventually point at the
// wrong row, and the only defence is that both come from one compose() call.
//
// It also means a test can assert what a step SAYS -- that a biography answer's
// detail pane names the skill it moves and by how much -- with no framebuffer
// at all.
//
// FIXED LAYOUTS, NOT MODULAR. Ruled on, verbatim: "Alright well then dont make
// it modular" -- "but clean". One considered composition per step. It responds
// to the WINDOW (an option list re-columns, prose rewraps, the detail pane
// collapses when it cannot be read) and never to a player dragging an edge,
// because there is no such edge.

#include <cstdint>
#include <string>
#include <vector>

#include "granadad/render/panel.hpp"

namespace granadad::render {

class Framebuffer;

/// One row of the master list.
struct CreationPageRow {
    /// The printed hotkey. Empty prints nothing -- see CreationPage::shape on
    /// when a list earns numbers and when numbering it would be a lie.
    std::string key;
    std::string label;
    /// The COMMON VALUE COLUMN: a designation, a cost, a state label. Empty
    /// draws nothing and costs nothing, which is how a row that has been taken
    /// drops its price without the column moving for everybody else.
    std::string value;
    Rgb accent = panelInk().accent;
    bool selectable = true;
    bool labelTakesAccent = false;
    /// THE WIDTH THIS ROW'S VALUE VOTES AT MEASURE TIME, in cells, when it is
    /// wider than the value it currently holds; 0 (the default) means the
    /// value's own length, which is right for every row whose value only
    /// changes when the composition would legitimately change anyway.
    ///
    /// The exception this exists for is the sheet's NAME row: its value is
    /// the LIVE TYPED NAME, so a measure that reads the value's own length
    /// wiggles the frame in 2-cell steps while the player types -- the ship
    /// note's "worst screen in the game" tell. The NAME row sets this to
    /// kMaxNameLength and the measure votes as if the name were already at
    /// the cap, so the frame is sized once for the widest legal name and
    /// holds still under every keystroke. Only the MEASURE reads it; the
    /// draw still lays out the real value, and the spare cells are ordinary
    /// pane emptiness.
    int measureValueCells = 0;
};

/// How the master list lays its rows out. Both are the same grammar -- numbered,
/// direct-select, inverted-fill selection -- at two different scales.
enum class CreationListShape : std::uint8_t {
    /// Rows are NAMES. Short entries in as many columns as the pane will hold.
    Columns,
    /// Rows are SENTENCES. One wrapped block each, the fill spanning the whole
    /// block. The quiz and the biography, whose answers run past a hundred
    /// glyphs and cannot be a column of names without becoming unreadable.
    Blocks,
    /// Rows are KEYS. A tight character grid at a constant advance, read
    /// ACROSS -- the on-screen keyboard, and nothing else so far. Same grammar
    /// again: framed pane, header, inverted-fill selection, commit verb at the
    /// foot. What differs is one layout rule, and panel.hpp's KeyGridStyle
    /// header says why a keyboard cannot borrow the list's spread.
    Keys,
};

/// Everything one creation step puts on screen. Built by CreationFlow::page().
struct CreationPage {
    // --- the header -------------------------------------------------------
    /// The nav path -- "NEW GAME / ANSWER FOR YOURSELF / QUESTION 5". Leaf in
    /// the step's own accent, ancestors dim. EVERY PANEL CARRIES ONE.
    std::vector<std::string> crumbs;
    /// The task, stated as an instruction on its own row underneath.
    std::string instruction;
    /// Left of the tab row.
    std::string title;
    /// WHERE YOU ARE IN THE FLOW. These carry no key because they are not views
    /// you may switch to -- see the note in creation.cpp on why that is still
    /// the reference's tab row and not a decoration.
    std::vector<PanelTab> tabs;
    int currentTab = -1;
    /// The persistent resource readout, right-aligned: the running tally while
    /// the quiz is being answered, the slot counts and the point pool on the
    /// sheet, what the past has cost so far on the biography. The number you are
    /// spending is always on screen while you choose.
    std::string readout;

    // --- the master list --------------------------------------------------
    CreationListShape shape = CreationListShape::Columns;
    std::vector<CreationPageRow> rows;
    /// Index into `rows`. -1 selects nothing.
    int cursor = -1;
    /// The master's share of the body out of 100.
    int masterShare = 50;
    /// A ceiling on the column count for a Columns list. The actual count comes
    /// from the content against the window, as it must.
    ///
    /// For a Keys grid it is EXACT rather than a ceiling -- the cursor walks a
    /// fixed rectangle, so the drawn grid has to be the shape that cursor
    /// believes in. See KeyGridStyle.
    int maxColumns = 2;
    /// THE BODY BAND'S FLOOR, IN GRID ROWS, AND THE REASON THE FRAME CAN SIZE
    /// TO ITS CONTENT WITHOUT TWITCHING.
    ///
    /// The frame ends after the taller of its two panes rather than padding out
    /// to the window -- the reference's own rule, which is about cursor
    /// movement and not about panels in general: hold height where moving the
    /// cursor SWAPS the content, size to content where the content is static.
    /// The master list is static as the cursor moves within it. The DETAIL pane
    /// is not, so it may not be allowed to set the height on its own, or the
    /// frame would grow and shrink a row at a time as you arrow.
    ///
    /// This is the step's answer: one number, chosen once against the tallest
    /// thing any row of that step can put in the detail pane. Same kind of
    /// per-step composition decision `masterShare` already is. The detail's
    /// real height still enters the maximum, so a floor set too low costs a
    /// small jump and never a clipped line.
    int bodyHoldRows = 0;

    // --- the detail pane --------------------------------------------------
    bool hasDetail = true;
    /// The subject, inverted in `accent` -- the reference's own "this is what
    /// you are looking at".
    std::string detailBadge;
    /// Right-aligned opposite the badge: the subject's state or family.
    std::string detailStatus;
    Rgb accent = panelInk().accent;
    /// Labels left, values at one shared column. Facts, not paragraphs.
    std::vector<PanelFact> facts;
    /// Flavour, then named effect, then number -- colour-sorted.
    std::vector<PanelLine> lines;
    /// Shape and figure together. The quiz axes; the sheet's attributes.
    std::vector<PanelBar> bars;
    int barCells = 12;
    /// The commit action at the FOOT of the detail pane, restating its cost.
    /// STATE CHANGES THE VERB -- there is no greyed-out disabled button here.
    std::string commitVerb;
    std::string commitCost;
    /// Texture the empty tail of the detail pane. A judgement per screen, not a
    /// rule -- see drawStipple.
    bool stipple = true;

    /// Global nav, below its own rule. Entry 0 is the way back, and is the one
    /// entry a pointer may click.
    std::vector<PanelOption> nav;
    float alpha = 1.0F;
};

/// The resolved geometry of one page at one window size. Both the drawing and
/// the hit-test read this and nothing else, so they cannot disagree.
struct CreationLayout {
    PanelMetric metric;
    PanelRect bounds;
    PanelRect interior;
    PanelRect headerBand;
    PanelRect bodyBand;
    PanelRect navBand;
    MasterDetail body;
    /// The master pane, and the detail pane. When `body.split` is false the
    /// list takes the whole body and there is no detail pane -- the honest
    /// answer at a small window, rather than two panes too thin to read.
    PanelRect listRect;
    PanelRect detailRect;
    /// The key grid, for a Keys page and nothing else -- `usable` false on
    /// every other shape, and on a Keys page whose pane could not take the
    /// grid's shape (the drawing and the hit-test both fall back to the column
    /// list on that one, together, because they read this same field).
    KeyGridPlan grid;
    int tabRow = 0;
    int headerRow = 0;
    int bodyRow = 0;
    int bodyRows = 0;
    int navRow = 0;
    std::vector<int> ruleRows;
    bool usable = false;
};

[[nodiscard]] CreationLayout creationLayout(const CreationPage& page, int frameWidth,
                                            int frameHeight);

void drawCreationPage(Framebuffer& target, const CreationPage& page);

/// What a pointer is over. The mouse is a FIRST-CLASS input on this screen, not
/// a bolt-on: hover moves the same cursor the keyboard and the pad move, and a
/// click on a row is the same call ENTER makes on it.
struct CreationHit {
    enum class Zone : std::uint8_t {
        None,
        /// `index` is a row of the master list.
        Row,
        /// The commit verb at the foot of the detail pane.
        Commit,
        /// The way back, in the global nav row.
        Back,
    };
    Zone zone = Zone::None;
    int index = -1;

    [[nodiscard]] bool operator==(const CreationHit& other) const = default;
};

[[nodiscard]] CreationHit creationPageHitTest(const CreationPage& page, int frameWidth,
                                              int frameHeight, int px, int py);

}  // namespace granadad::render
