#pragma once

// THE CASEBOOK, DRAWN AS THE MASTER/DETAIL FRAME -- and the fix for the one
// defect the owner's own playthrough found in the CONTENT rather than in the
// chrome.
//
// ---------------------------------------------------------------------------
// THE DEFECT, AND WHY IT IS A UI DEFECT AND NOT A CONTENT ONE
// ---------------------------------------------------------------------------
// He followed the Bloodletter case to Crell at the Weighhouse and it "seemed to
// stop there." It did not stop. content/raws/quests/casebook.json gives
// `weighhouse-ledger` an `opens` list three long -- Harl's Yard, the King's
// Bond, Brann's gray ledger -- and sim/casebook.cpp marks it Followed and puts
// all three in the book, correctly, every time.
//
// What the game did about that is in docs/frames/casebook/before-weighhouse-960.png:
// a dim grey row in the bottom-left corner changed from CASE 4/6 to CASE 4/9.
// Nothing else. No announcement, and the casebook itself was a strip along the
// bottom of a four-tile Menu behind a key, showing seven of twelve rows, with
// nothing about the highlighted lead beyond its own short name. He saw two of
// twelve leads and concluded the content had run out.
//
// So this page answers three things and nothing else. It does not change one
// lead, one word of authored text, one dread number or one `opens` list:
//
//   1. THE MOMENT IS ANNOUNCED. hud.hpp's casePlate -- the threshold plate's
//      own idiom, a notice that rises through its resting place and is gone --
//      fires on the RISING EDGE of a look that opened something. Session owns
//      it; see Session::casePlateWanted().
//   2. THE BOOK IS A MASTER/DETAIL FRAME. Leads as the numbered left list with
//      their state carried by colour, glyph AND the value column; the
//      highlighted lead's place, holder, dateline, opener and clue in the pane
//      beside it; the commit verb at the foot of that pane.
//   3. A LEAD ROUTES TO WHERE IT IS. The commit verb hands the selection to the
//      WARD MAP's own cursor (Session::showLeadOnMap) rather than building a
//      second navigation path -- the map pass already made a place the unit of
//      selection, so the casebook's answer to "where is Harl's Yard" is the
//      map's answer, arrived at from the book.
//
// ---------------------------------------------------------------------------
// STATE CHANGES THE ROW, THE LABEL AND THE VERB -- ALL THREE, TOGETHER
// ---------------------------------------------------------------------------
// UI-REFERENCE-TERMINAL.md pins this down off the Theology frame's owned and
// unowned states, and a casebook is exactly the surface it was written for:
//
//   OPEN      the row's value column carries THE PLACE -- what you would do
//             about it -- in the accent that means "act on me". The detail pane
//             says NOT YET LOOKED AT and the verb offers the map.
//   COLD      the value column drops the place and reads DEAD END. The pane
//             carries the clue that closed it. The verb still offers the map,
//             because walking back to a dead end is a legitimate thing to want.
//   FOLLOWED  the value reads FOLLOWED and the pane lists what it OPENED, by
//             name, with a `•` each -- which is the row the owner never saw.
//   THE CLOSE reads THE END OF IT rather than DEAD END. The sim marks a lead
//             with an empty `opens` Cold either way (casebook.cpp's own rule);
//             the raws know which one is the end of the trail and this page
//             does not print "DEAD END" over the answer to the case.
//
// There is no greyed-out row anywhere on this page, deliberately.
//
// ---------------------------------------------------------------------------
// TWO VIEWS, ONE GEOMETRY
// ---------------------------------------------------------------------------
// The tab row carries LEADS and THE CASE, and the tab swaps only the DETAIL
// pane: the master list, the rules, the header and the nav band are drawn
// identically in both. That is the stable-geometry rule taken seriously -- a
// tab that also moved the list would be two compositions wearing one frame.
//
// THE TABS CARRY NO PRINTED KEY, and that is a conflict written down rather
// than fudged. The reference prints `d - Dominions` because `d` is a key. Here
// the digits are spent on the leads (direct-select, which is what a list of
// twelve places wants) and TAB is the key that OPENS AND CLOSES this page --
// Action::Menu, the rule every page in this build keeps. So the views are
// stepped with LEFT and RIGHT, the nav band says so, and no tab advertises a
// hotkey that does nothing.
//
// NINE DIGITS, TWELVE LEADS. Rows past the ninth print no number. The same call
// keys_page.cpp made for twenty-nine bindings, at a smaller scale: a printed
// `10` that no single key press can reach is a lie, and the cursor reaches
// every row regardless.
//
// ---------------------------------------------------------------------------
// PURE RENDER
// ---------------------------------------------------------------------------
// This file holds no state, knows nothing about Session and never touches the
// simulation. Session builds a CasebookPageState out of the live Casebook and
// the authored raws and hands it over, exactly as it already does for the keys
// page and the ward map. Reading this page cannot read a lead: nothing here
// calls Casebook::look().

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/panel.hpp"

namespace granadad::render {

/// WHAT THE BLANK ROWS OF THE LEAD LIST ARE WAITING FOR, in one sentence.
///
/// UI-REFERENCE-TERMINAL.md, by name: "Empty states are worded, not blank --
/// `no trinket`, `Luck 0`. Absence is stated so the reader knows it was
/// considered." On a new game the book holds ONE lead against a pane held at
/// kDetailHoldRows, and that pane is the second surface a stranger ever meets
/// -- the world opens straight onto it with no input at all. A stippled field
/// under a single row reads as a list that failed to load; this reads as a
/// case nobody has worked yet, which is what it is.
///
/// ONE STRING, TWO SURFACES. The full-screen casebook PAGE draws it under its
/// master list and the tiled Menu's casebook TILE draws it under the same
/// list; wording the same absence two different ways in two places that show
/// the same twelve leads is exactly the drift the shared vocabulary exists to
/// stop. Both wrap it to their own pane rather than authoring line breaks.
///
/// It names the act, not the key: LOOK is the verb the commit line at the foot
/// of the detail pane already offers ("ENTER - LOOK AT IT"), and the key that
/// does it out there is printed beside that verb rather than guessed at here.
inline constexpr std::string_view kBookWaitingLine =
    "THE REST OF THE BOOK IS WAITING ON YOU. STAND OVER A LEAD AND LOOK.";

/// Which view is over the selection. Two, and both earn their space: the lead
/// you are pointing at, and the case the leads belong to.
enum class CasebookTab : std::uint8_t {
    /// The highlighted lead: where, who holds it, what was found, what it
    /// opened, and the verb that takes you there.
    Leads = 0,
    /// The case itself: the hook, the ward's nerve, and the count.
    Case = 1,
    /// THE PULL PACK: the CASES shelf -- every book the player has been
    /// handed and every questline they have started, with where each stands,
    /// and the commit that fronts one for the page. Oblivion's one
    /// presentation contribution the reference doc itself endorses (the
    /// Current/Completed split), and the switcher the code flagged as
    /// missing twice.
    Cases = 2,
};
inline constexpr int kCasebookTabCount = 3;

/// Where a lead stands, for drawing. Mirrors sim::LeadState minus Unheard,
/// which never reaches this page: an unheard lead is not in the book.
enum class CasebookLeadState : std::uint8_t { Open = 0, Cold = 1, Followed = 2 };

/// One row of the master list, and everything the detail pane says about it.
/// Every field is authored text or a formatted fact -- nothing here is computed
/// prose.
struct CasebookLeadRow {
    /// The authored short name: "THE LEDGER". A casebook row, not a signpost.
    std::string brief;
    /// The place, in the words the sign uses: "THE WEIGHHOUSE".
    std::string place;
    /// What you are going there for: "THE HARBORMASTER'S LEDGER".
    std::string what;
    /// Who holds it, and what they are. Both empty for a lead that is a thing
    /// rather than a person -- and the pane says so out loud rather than
    /// leaving the row blank.
    std::string who;
    std::string whoWhat;
    /// The clue, and the paragraph behind it. Empty while the lead is Open,
    /// because nobody has stood over it yet.
    std::string found;
    std::string detail;
    /// "DAY 1 21:40" -- when this went in the book.
    std::string heard;
    /// What told you to come here, by short name, comma-separated. Empty for
    /// the lead the case opened on, which the pane words rather than blanks.
    std::string from;
    /// What looking at this one put in the book, by short name. Only ever
    /// populated for a Followed lead -- this is the sentence the owner's run
    /// never showed him.
    std::vector<std::string> opened;
    CasebookLeadState state = CasebookLeadState::Open;
    /// The trail's own last lead. See this file's header on why it is not a
    /// dead end even though the simulation marks it Cold.
    bool close = false;
    /// "NE 40 PACES" from where the body is standing to the lead's own site.
    std::string bearing;
    /// THE BODY COULD LOOK AT THIS FROM WHERE IT IS STANDING -- sim::
    /// kLookRangeTiles of the site, on its band, which is exactly the reach
    /// Casebook::look() answers to. NOT "inside the footprint": the verb this
    /// flag changes is LOOK, so the flag has to mean what LOOK means or the
    /// page offers a key that would do nothing.
    bool here = false;
    /// The ward map has a named place for this lead, so the commit verb can
    /// route to it. False makes the verb say so instead of offering a key that
    /// would do nothing.
    bool routable = false;
    /// THE PULL PACK: this is the lead the compass carries -- the player's
    /// own pick, or the newest-heard default standing in for one. The row
    /// KEEPS its place word and wears the arrowhead motif before it; the
    /// badge reads ON THE COMPASS (its own words, not FOLLOWED's stem: that
    /// one is the book's past tense, this is the street's present); and the
    /// nav band's FOLLOW entry reads LET GO on it, because the one verb is
    /// its own undo.
    bool followed = false;
};

/// THE PULL PACK: one row of the CASES shelf. Authored titles and formatted
/// facts, nothing computed. ONLY WHAT IS IN HAND IS LISTED: a book the
/// player has not been handed is counted in the badge ("1/3 IN HAND") and
/// never named -- Oblivion's journal never lists an unstarted quest by
/// name, and neither does this shelf.
struct CasebookShelfRow {
    /// "THE BLOODLETTER", "THE DISCIPLE'S OATH".
    std::string title;
    /// Where it stands, for the list's value column: "READ 4/9", "CLOSED",
    /// "STAGE 2/6", "DONE".
    std::string state;
    /// The tally alone, for the fact block: "4/9", "ALL", "2/6", "6/6".
    std::string tally;
    /// The next thing, worded: an open lead's place, a stage's own label, or
    /// the close line. Empty is worded by the drawing, never blank.
    std::string next;
    /// A book (frontable, followable) rather than a questline (read-only).
    bool book = false;
    /// Which book, as a render::CaseBookId ordinal, or -1 for a questline --
    /// the commit and the FOLLOW verb address the book by this, never by the
    /// row's position (a shelf that hides unhanded books is not indexed by
    /// them).
    int bookId = -1;
    /// The book the page is reading.
    bool fronted = false;
    /// Holds the lead the compass carries.
    bool followed = false;
};

/// Everything the page draws. A closed page draws nothing at all.
struct CasebookPageState {
    bool open = false;
    /// The caller's own ease -- Session::panelAnim_, shared with every overlay.
    float openAmount = 1.0F;

    /// Left of the tab row.
    std::string title = "THE CASEBOOK";
    /// The case's own title, the breadcrumb's second crumb.
    std::string caseTitle;
    /// Right-aligned in the tab row and up the whole time: how far the case has
    /// got and what the ward's nerve is at. The reference's persistent resource
    /// readout, and here the resource is the investigation.
    std::string readout;
    /// The instruction header.
    std::string instruction;
    /// A bouncer's warning, or anything else that outranks a menu.
    std::string alert;

    CasebookTab tab = CasebookTab::Leads;
    std::vector<CasebookLeadRow> rows;
    /// Index into `rows`. Never clamped here: the cursor belongs to the caller.
    int cursor = 0;

    // --- the CASES shelf (THE PULL PACK) -----------------------------------
    std::vector<CasebookShelfRow> shelf;
    /// Index into `shelf`, the caller's own, never clamped here.
    int shelfCursor = 0;
    /// How many case files ship, for the badge's "1/3 IN HAND" -- the one
    /// number here the player CAN know without the shelf naming what they
    /// have not been handed.
    int shelfBookTotal = 0;
    /// The FOLLOW verb's key in the live device's vocabulary: "F" on a
    /// keyboard (a raw page key, the map's own T precedent), the Attack
    /// half's button on a pad (X). Empty prints no FOLLOW entry.
    std::string followKey = "F";

    // --- the CASE view -----------------------------------------------------
    std::string hook;
    /// The authored dread band label for the current score.
    std::string dreadBand;
    /// The authored closing line. Only shown once the trail is closed.
    std::string closeLine;
    /// What the ward calls the player for the work so far.
    std::string calledYou;
    bool closed = false;
    std::int32_t dread = 0;
    std::int32_t read = 0;
    std::int32_t cold = 0;
    /// Leads in the book, and leads the file holds. The second is deliberately
    /// NOT shown to the player as a target -- see casebook_page.cpp -- but the
    /// page needs both to word the count honestly.
    std::int32_t known = 0;
    std::int32_t total = 0;

    /// What the nav band prints for "put the book down": the real bound key for
    /// Action::Menu, so the page never invents one.
    std::string closeKey = "J";
    /// And the real bound key for the look verb, which the commit line names
    /// when the body is already standing on the lead.
    std::string lookKey = "E";
    /// The page grammar's confirm key in the live device's vocabulary --
    /// promptConfirmKey's "ENTER" or "A" -- for the commit verb ("... - SHOW
    /// ME WHERE" / "... - LOOK AT IT") and the nav band's GO TO IT row. The
    /// last of this page's keyboard literals, moved onto a state field the
    /// way closeKey already flows; the WIDTH MEASURE still votes with the
    /// widest fixed variant (ENTER), so the frame does not resize when the
    /// other hand speaks mid-frame. (Both the input and pointer lanes built
    /// this field independently; the input lane's name and fixed measure won
    /// at the merge.)
    std::string commitKey = "ENTER";

    // --- UI-EA-SPEC sec. 2: the Law of Earned Text -------------------------
    /// TUTOR tier, 0 (rest) .. 1 (raised). At rest the nav band prints bare
    /// keycaps; raised, the verb words ride beside them at this strength.
    /// The countdown helper is LANE HUD's, the wake signals LANE FLOW's; this
    /// page only renders the value.
    float tutor = 0.0F;
    /// Contract (b): the commit beat, armed by the routing's ImpactPulse at
    /// GO TO IT / LOOK AT IT. Default 0 draws nothing.
    float commitPulse = 0.0F;
};

/// Draws the whole page over a rendered frame.
void drawCasebookPage(Framebuffer& target, const CasebookPageState& state);

/// How many rows one screenful of the master list holds at this frame size, and
/// which screenful the cursor is on. Same shape and same reason as
/// KeysPageScroll: it is the page's own answer to "is this list longer than the
/// window", and a case can pin it without a framebuffer.
struct CasebookPageScroll {
    int perScreen = 1;
    int screen = 0;
    int screens = 1;
    int firstRow = 0;
};
[[nodiscard]] CasebookPageScroll casebookPageScroll(const CasebookPageState& state, int frameWidth,
                                                    int frameHeight);

/// THE COMPOSITION, RESOLVED, WITHOUT DRAWING ANYTHING.
///
/// Same contract and same reason as panel.hpp's planOptionList: the geometry a
/// screen is drawn at is a pure function of the state and the window, so a case
/// can pin it at 320x180 and at 1920x1080 without rendering a pixel -- and the
/// one thing that can quietly go wrong on this page (a lead's authored short
/// name outgrowing the master pane, or a place name outgrowing the value
/// column) becomes a red build instead of a clipped word on somebody's screen.
struct CasebookPageMetrics {
    bool usable = false;
    /// False when the window was too narrow to hold both panes and the list
    /// took the whole body. The honest answer at 320x180.
    bool split = false;
    PanelMetric metric;
    /// Where the list is drawn, and where the detail pane is. Exposed so a case
    /// can find a row's own pixel and ask the hit-test about it.
    PanelRect master;
    PanelRect detail;
    int masterCells = 0;
    int detailCells = 0;
    /// The list's own plan: what the key column, the label column and the
    /// shared value column actually came out as.
    int keyCells = 0;
    int labelCells = 0;
    int valueCells = 0;
    int columns = 0;
    int listRows = 0;
    /// The global nav band: how many entries it HOLDS against how many it has.
    /// Exposed because a footer that silently stops naming the key that closes
    /// the book is a defect a screenshot found once and a case should catch
    /// from then on -- see casebook_page.cpp's navOptionsFor.
    int navEntries = 0;
    int navShown = 0;
    int navRows = 0;
    /// The detail pane's CONSEQUENCE block -- what told you to come here and
    /// what the lead opened -- as rows it wants against rows the pane pins for
    /// it. They must be equal: a lead that opened three things and reported two
    /// is this whole pass's own defect, one pane deeper.
    int effectRows = 0;
    int effectRowsWanted = 0;
};
[[nodiscard]] CasebookPageMetrics casebookPageMetrics(const CasebookPageState& state,
                                                      int frameWidth, int frameHeight);

/// Which lead a pixel of the master list lands on, or -1. The inverse of what
/// was drawn, built out of the same walk -- see panel.hpp's optionListAt on why
/// the inverse of a layout lives beside the layout rather than being re-derived
/// by every screen that wants a mouse.
[[nodiscard]] int casebookLeadAtPixel(const CasebookPageState& state, int frameWidth,
                                      int frameHeight, int px, int py);

/// Which of the two view tabs (LEADS / THE CASE, as CasebookTab values) a
/// pixel of the tab row lands on, or -1. The sibling casebookLeadAtPixel was
/// always meant to have -- the ship note names the tab row by name: keyboard
/// LEFT/RIGHT steps the views and clicking them did nothing. Built on
/// panel.hpp's tabRowTabAt against the SAME band, title, tabs and readout the
/// drawing hands drawTabRow, through the same composition.
[[nodiscard]] int casebookTabAtPixel(const CasebookPageState& state, int frameWidth,
                                     int frameHeight, int px, int py);

}  // namespace granadad::render
