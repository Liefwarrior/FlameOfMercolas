#include "granadad/render/casebook_page.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "granadad/render/panel.hpp"

namespace granadad::render {

namespace {

/// ONE COLOUR PER STATE, and the state is the only thing on this list worth
/// colouring. The reference fills a selected row in the ENTITY'S own accent so
/// a dense page is scannable by hue before a word of it is read; here the
/// entity is a lead and what a reader wants at a glance is not which building
/// it is but whether it is still waiting for them.
///
/// Amber for OPEN because amber is this build's "press me". Grey for a dead
/// end, which is the one row that wants nothing. Green for a lead that paid
/// off, the same ink every number in this vocabulary takes. And the trail's
/// last lead gets its own violet -- the crosshair prompt already spends that
/// hue on a lead and on nothing else, so the answer to the case reads as a
/// different kind of thing from the four walks that failed.
[[nodiscard]] Rgb stateAccent(CasebookLeadState state, bool close) noexcept {
    // THE VIOLET IS FOR AN ANSWER, NOT FOR A DESTINATION. A trail-closing lead
    // that is still Open is exactly as actionable as any other unwalked lead
    // and takes the amber; it only becomes the answer once it has been stood
    // over, which is when the hue changes.
    if (close && state != CasebookLeadState::Open) {
        return Rgb{0.74F, 0.62F, 0.94F};
    }
    switch (state) {
        case CasebookLeadState::Cold:
            return Rgb{0.62F, 0.60F, 0.56F};
        case CasebookLeadState::Followed:
            return Rgb{0.52F, 0.82F, 0.48F};
        case CasebookLeadState::Open:
        default:
            return Rgb{0.98F, 0.86F, 0.42F};
    }
}

/// The word at the right of the row, and the word at the right of the badge.
/// STATE CHANGES THE ROW: an Open lead prices itself in the only currency this
/// game charges, which is a walk across the ward, so its value column carries
/// the PLACE. A lead that has been stood over has nothing left to pay and the
/// place drops out for a state label -- the reference's own `1 - Earth 200*`
/// becoming `3 - Blood`.
[[nodiscard]] std::string stateWord(const CasebookLeadRow& row) {
    if (row.close && row.state != CasebookLeadState::Open) {
        return "THE END OF IT";
    }
    switch (row.state) {
        case CasebookLeadState::Cold:
            return "DEAD END";
        case CasebookLeadState::Followed:
            return "FOLLOWED";
        case CasebookLeadState::Open:
        default:
            // THE PULL PACK: the lead the compass is carrying says so on the
            // BADGE, in its own words -- not FOLLOWING, which shares a stem
            // and a column with the book's own FOLLOWED and read as the same
            // state twice.
            return row.followed ? "ON THE COMPASS" : "OPEN";
    }
}

/// THE COMPASS MARK on the list row: the arrowhead motif ahead of the place,
/// so the row keeps its place word (the walk is still the cost) and still
/// says, without a word, that the compass points at it.
[[nodiscard]] std::string compassMark(const std::string& place) {
    return std::string(kGlyphRight) + " " + place;
}

/// A place name in a narrow column drops its leading article, exactly as
/// map_view.cpp's own label rule does -- so the two surfaces shorten a name the
/// same way and a player reading THE WEIGHHOUSE on the plan finds WEIGHHOUSE in
/// the book rather than a third spelling of it. The article is the ONLY word
/// ever removed.
[[nodiscard]] std::string shortPlace(const std::string& place) {
    if (place.size() > 4 && place.compare(0, 4, "THE ") == 0) {
        return place.substr(4);
    }
    return place;
}

/// THE COMPOSITION. Five bands, two of which are rules, and a master/detail
/// split in the body -- keys_page.cpp's shape, because it is the shape the
/// reference's master/detail frame has and this page is the second surface the
/// spec names for it by name.
struct Composition {
    PanelMetric metric;
    PanelRect bounds;
    PanelRect interior;
    int tabRow = 0;
    int bodyRow = 0;
    int bodyRows = 0;
    int navRow = 0;
    std::vector<int> ruleRows;
    MasterDetail body;
    PanelRect bodyBand;
    PanelRect navBand;
    bool usable = false;
};

/// The foot of the master pane, held open whether or not there is a page to
/// turn -- so the rule under the body never moves.
inline constexpr int kIndicatorRows = 1;

/// The master's share of the body, out of 100.
///
/// A FIXED SHARE, AND FIXED ON PURPOSE -- which is a deliberate difference from
/// keys_page.cpp's walk over three candidates. That walk exists to fit a
/// twenty-nine-row list into as many columns as the window affords; this list is
/// twelve rows at its longest and fits one column at every size, so there is
/// nothing for a wider master to buy. What the share HAS to do is hold the row
/// whole at the resolution with the FEWEST cells across, which is the biggest
/// window: hudMinorScale steps up with height, so 1920x1080 has 74 interior
/// cells where 960x540 has 94. Half of 74 is 37 cells, and the widest row this
/// list can build -- a one-cell key, a twelve-cell short name and a twenty-cell
/// place -- wants 36. One cell of headroom, and a case pins it at all five
/// capture sizes so a longer authored `short` goes red here instead of arriving
/// clipped on somebody's screen. It is also the number the DETAIL pane needs:
/// half leaves it 34 cells at 1920x1080, which is what a nine-cell label column
/// and a twenty-three-cell value (THE NIGHT-SOUP DISCIPLE) want.
inline constexpr int kMasterShare = 50;
inline constexpr int kMinMasterCells = 22;
/// Thirty cells is a wrapped paragraph that still reads. Below it the split
/// collapses and the list takes the whole body, which is the honest answer at
/// 320x180 rather than two panes too thin for either job.
inline constexpr int kMinDetailCells = 30;

/// Nine digits, and the list can run to twelve. Rows past this print no number.
/// See the header: a printed `10` that no key press reaches is a lie, and the
/// cursor reaches every row regardless.
inline constexpr int kDirectSelectRows = 9;

/// THE PAGING CAP (UI-EA-SPEC 1.5 #27): the lead list pages at eight rows,
/// `+N` riding the rule under the body saying what waits below the fold. The
/// walked-out book was a fifty-word wall; eight leads is a screenful a reader
/// actually reads.
inline constexpr int kLeadPageRows = 8;

/// THE NAV BAND ASKS ITS OWN LIST HOW MANY ROWS IT NEEDS, which is the pattern
/// the map pass handed on and the reason this composition takes a row count
/// rather than assuming one.
///
/// It is not a nicety. The nav list's widest key is `LEFT RIGHT` (ten cells)
/// and drawOptionList sizes every column off the widest entry, so four columns
/// want eighty-six cells -- which 960x540 has (94) and 1920x1080 does not (74,
/// because hudMinorScale steps up with height and the biggest window is the
/// narrowest in cells). A one-row band at 1920 dropped the fourth entry
/// silently, and the fourth entry is the key that CLOSES THE BOOK. A footer
/// that quietly stops naming the way out is worse than a footer that spends a
/// second row.
[[nodiscard]] std::vector<PanelOption> navOptionsFor(const CasebookPageState& state) {
    // THE RAISED (TUTOR) FORM -- the band is PLANNED against this so its
    // geometry holds still while the verb words fade (UI-EA-SPEC sec. 2); at
    // rest the drawing blanks the labels and bare keycaps remain. The cursor
    // keys are the keycap motifs (sec. 5): `UP DOWN` and `LEFT RIGHT` retire
    // for arrowheads, worth ten cells a row to the narrowest window.
    const Rgb accent = panelInk().accent;
    // THE PULL PACK: five entries now, the same five on every view so the
    // band's row count -- and with it the frame's foot -- holds still across
    // the tab row (test_casebook_page's "the views swap the detail pane and
    // nothing else"). The second entry names the NEXT view; the third is the
    // commit in the view's own words (READ IT fronts a case off the shelf);
    // the fourth is FOLLOW, live on every view (a lead here, a case's own
    // next lead on the shelf).
    const char* next = state.tab == CasebookTab::Leads  ? "CASE"
                       : state.tab == CasebookTab::Case ? "CASES"
                                                        : "LEADS";
    const char* commit = state.tab == CasebookTab::Cases ? "READ IT" : "GO TO IT";
    // THE ONE VERB IS ITS OWN UNDO, and the band says which half it is: on
    // the lead the compass already carries, F reads LET GO.
    const int count = static_cast<int>(state.rows.size());
    const bool onFollowed = state.tab != CasebookTab::Cases && count > 0 &&
                            state.rows[static_cast<std::size_t>(std::clamp(state.cursor, 0, count - 1))]
                                .followed;
    return {
        PanelOption{std::string(kGlyphUpDown), state.tab == CasebookTab::Cases ? "CASE" : "LEAD",
                    "", accent, InkRole::Dim, false},
        PanelOption{std::string(kGlyphLeft) + std::string(kGlyphRight), next, "", accent,
                    InkRole::Dim, false},
        // SHIP NOTE SEAM 3: the confirm is the state's device-worded key, not
        // a hardcoded ENTER -- a pad reads A GO TO IT here, live.
        PanelOption{state.commitKey.empty() ? std::string(kGlyphReturn) : state.commitKey, commit,
                    "", accent, InkRole::Dim, false},
        PanelOption{state.followKey.empty() ? std::string("F") : state.followKey,
                    onFollowed ? "LET GO" : "FOLLOW", "", accent, InkRole::Dim, false},
        PanelOption{state.closeKey.empty() ? std::string("J") : state.closeKey, "CLOSE", "",
                    accent, InkRole::Dim, false},
    };
}

/// The three views, in tab-row order. ONE list, three call sites (the
/// measure, the hit-test, the drawing), so a fourth view is one line.
[[nodiscard]] std::vector<PanelTab> tabsFor() {
    return {PanelTab{"", "LEADS"}, PanelTab{"", "THE CASE"}, PanelTab{"", "CASES"}};
}

[[nodiscard]] OptionListStyle navStyleOf() {
    OptionListStyle style;
    style.showKeys = true;
    style.maxColumns = 4;
    style.gutterCells = 2;
    style.minRows = 1;
    return style;
}

/// THE TWO HALVES OF THIS BODY ARE NOT THE SAME KIND OF THING, and the
/// reference's rule sorts them: hold height where moving the cursor SWAPS the
/// content, size to content where the content is static.
///
///   * THE LEAD LIST is static under the cursor. It grows -- one lead on a new
///     game, twelve by the end -- but it grows when the WORLD opens a lead, not
///     when you press down, so it may set the height. On a new game it was one
///     row in a forty-one-row pane at 640x360, which is the second screen a
///     stranger touches and the second-largest "unfinished" tell in the build.
///
///   * THE DETAIL PANE is exactly what the cursor swaps, and two leads' clues
///     do not wrap to the same number of lines. If it set the height the frame
///     would grow and shrink a row at a time as you arrowed the list, which is
///     the defect the rule exists to prevent.
///
/// So the detail half is HELD at a floor and the master half is free to push
/// past it. The floor is the tallest thing EITHER VIEW can reach -- the case
/// tab's badge, hook, two fact blocks, nerve and commit verb, which is the
/// deeper of the two -- and it is ONE number rather than one per tab on
/// purpose: LEFT/RIGHT is a cursor over views, so a per-tab floor would move
/// the frame's foot every time you crossed the tab row. test_casebook_page's
/// "the two views swap the detail pane and nothing else" is the guard, and it
/// caught exactly that on the first run of this change.

inline constexpr int kDetailHoldRows = 20;

/// Below this a body is not two panes, it is two slots.
inline constexpr int kMinBodyRows = 8;

[[nodiscard]] Composition compose(int frameWidth, int frameHeight, int navRows,
                                  int gridRowsOverride = -1, int gridCellsOverride = -1) {
    Composition out;
    out.metric = panelMetric(frameHeight);
    const int cells = gridCellsOverride > 0 ? gridCellsOverride : out.metric.cellsIn(frameWidth);
    const int full = out.metric.rowsIn(frameHeight);
    const int rows = gridRowsOverride > 0 ? gridRowsOverride : full;
    if (cells < 8 || rows < 10) {
        return out;
    }
    // A FRAME THAT ENDED EARLY IS SEATED, NOT PINNED -- on BOTH axes now. It
    // used to take the top the full-height grid would have taken, which left
    // the book as a 199px panel with 159px of black under it; panelSeatY()
    // splits that remainder. And it used to take the whole window's width
    // whatever the book held, which left it a full-frame letterbox;
    // measuredCells() (see composeFor) now hands in the width its own widest
    // row earns and panelSeatX() splits THAT remainder, 45/55, the same
    // slightly-leading-edge judgement -- the same two rules creation_page.cpp
    // seats on, so the two first surfaces of the game agree.
    //
    // Walking LEADS to THE CASE still cannot make the breadcrumb jump, and that
    // is now load-bearing rather than incidental: the detail half is HELD at
    // kDetailHoldRows, the measure reads BOTH views' content wherever the
    // cursor is, and the list is the same list in both views, so both tabs
    // compose to the same width and height and therefore to the same seat.
    // test_casebook_page's "the two views swap the detail pane and nothing
    // else" is the guard.
    out.bounds = PanelRect{panelSeatX(frameWidth, out.metric.widthOf(cells)),
                           panelSeatY(frameHeight, out.metric.heightOf(rows)),
                           out.metric.widthOf(cells), out.metric.heightOf(rows)};
    out.interior = PanelRect{out.bounds.x + out.metric.cellW(), out.bounds.y + out.metric.cellH(),
                             out.metric.widthOf(cells - 2), out.metric.heightOf(rows - 2)};

    // ONE HEADER LINE (UI-EA-SPEC sec. 5, breadcrumb law): the tab row --
    // title, tabs, resource readout -- IS the breadcrumb. The old two-row
    // instruction band and its rule are gone; a bouncer's warning overdraws
    // the tab row while it lasts, and the selection is named once, on the
    // detail pane's badge.
    const std::vector<PanelRect> bands = splitRows(out.interior, out.metric,
                                                   {
                                                       spanCells(1),   // the tab row
                                                       spanCells(1),   // rule
                                                       spanWeight(1),  // the body
                                                       spanCells(1),        // rule
                                                       spanCells(navRows),  // global nav
                                                   });
    const auto rowOf = [&out](const PanelRect& band) {
        return (band.y - out.interior.y) / out.metric.cellH();
    };
    out.tabRow = rowOf(bands[0]);
    out.bodyBand = bands[2];
    out.bodyRow = rowOf(bands[2]);
    out.bodyRows = out.metric.rowsIn(bands[2].h);
    out.navBand = bands[4];
    out.navRow = rowOf(bands[4]);
    out.ruleRows = {rowOf(bands[1]), rowOf(bands[3])};
    out.body =
        splitMasterDetail(out.bodyBand, out.metric, kMasterShare, kMinMasterCells, kMinDetailCells);
    out.usable = out.bodyRows > kIndicatorRows;
    return out;
}

/// The master list, as options. See stateWord() on the value column.
[[nodiscard]] std::vector<PanelOption> optionsFor(const std::vector<CasebookLeadRow>& rows) {
    std::vector<PanelOption> out;
    out.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const CasebookLeadRow& row = rows[i];
        PanelOption option;
        option.key = i < static_cast<std::size_t>(kDirectSelectRows)
                         ? std::to_string(static_cast<int>(i) + 1)
                         : std::string();
        option.label = row.brief.empty() ? row.place : row.brief;
        // AND A ROW WHOSE NAME IS ALREADY ITS PLACE SAYS IT ONCE. Five of the
        // twelve leads are authored with a `short` that is the building --
        // HARL'S YARD, KENNEL ROW, WRACKHOUSE -- and printing "HARL'S YARD
        // HARL'S YARD" spends twelve cells of a narrow column on a repeat. The
        // colour still carries the state, which is the reference's own `3 -
        // Blood` with no number beside it.
        const std::string place = shortPlace(row.place);
        // THE PULL PACK: the row the compass carries KEEPS its place and
        // wears the arrowhead ahead of it -- a row that said the place once
        // (its name IS its place) wears the arrowhead alone.
        option.value = row.state != CasebookLeadState::Open ? stateWord(row)
                       : row.followed ? (place == shortPlace(option.label)
                                             ? std::string(kGlyphRight)
                                             : compassMark(place))
                       : place == shortPlace(option.label) ? std::string()
                                                           : place;
        option.accent = stateAccent(row.state, row.close);
        // The value takes the row's own ink rather than the vocabulary's green:
        // on this list the value IS the state, so colouring it anything else
        // would put two different answers to "how is this lead doing" on one
        // row.
        option.valueInk = row.state == CasebookLeadState::Open ? InkRole::Prose : InkRole::Dim;
        option.labelTakesAccent = true;
        option.selectable = true;
        out.push_back(std::move(option));
    }
    return out;
}

[[nodiscard]] OptionListStyle listStyle() {
    OptionListStyle style;
    style.showKeys = true;
    // ONE COLUMN, ALWAYS, and it is a ceiling rather than a layout. Twelve
    // leads in two columns would put the trail's second half beside its first,
    // which reads as two lists; a case's leads are one sequence and the eye
    // should run down them. The pane is sized so one column fits, so the
    // content never asks for a second.
    style.maxColumns = 1;
    style.gutterCells = 2;
    // NO minRows. The composition fixed the rect; padding the list would only
    // stop the columns balancing.
    style.minRows = 0;
    style.alignValues = true;
    return style;
}

[[nodiscard]] PanelRect listRectOf(const Composition& comp) {
    const int listRows = std::max(0, comp.bodyRows - kIndicatorRows);
    return PanelRect{comp.body.master.x, comp.body.master.y, comp.body.master.w,
                     comp.metric.heightOf(listRows)};
}

/// THE LEAD VIEW'S FACT BLOCK, built in one place so the drawing and the width
/// measure walk the SAME list -- see panel.hpp's optionListAt on why the
/// alternative (a measurement that re-describes a layout) is a thing that
/// drifts.
///
/// WHAT IS A FACT AND WHAT IS A SENTENCE, decided by measuring rather than by
/// taste. The first capture of this pane put `what` ("WHAT THE SEA SPAT UP,
/// AND WHO SOLD IT", 37 glyphs) and `openedBy` (four names for the Drowned
/// Hold, 47) in this block and drawFacts clipped both -- "THE BODY, AND
/// WHOEVER FOU..". Those are sentences and they are in the prose block below,
/// wrapped. What is left is five short answers, and the witness takes two
/// rows rather than one because "BONDSMAN GRIEVE, OF THE KING'S BOND" is
/// thirty-five glyphs against a twenty-three-cell value column.
///
/// EVERY EMPTY STATE IS WORDED. The Outfall has no witness at all; a blank
/// row there reads as a bug and "NOBODY -- IT IS A PLACE" reads as an answer.
/// That is the reference's own `no trinket`.
[[nodiscard]] std::vector<PanelFact> leadFactsFor(const CasebookLeadRow& row) {
    // THE DIET (UI-EA-SPEC 1.5): three facts, one word of label each. WHO
    // carries the trade after a comma instead of spending a THEY ARE row; the
    // bearing left this block for the commit verb's own restatement, where the
    // walk is the cost; `--` costs zero words and still says "considered".
    // Three rows for EVERY row of the book, so nothing below them moves as the
    // cursor walks it.
    std::string who = row.who.empty() ? std::string("A PLACE") : row.who;
    if (!row.who.empty() && !row.whoWhat.empty()) {
        who += ", " + row.whoWhat;
    }
    return {
        PanelFact{"AT", row.place, InkRole::Prose},
        PanelFact{"WHO", who, row.who.empty() ? InkRole::Dim : InkRole::Prose},
        PanelFact{"HEARD", row.heard.empty() ? std::string("--") : row.heard, InkRole::Number},
    };
}

/// THE CASE VIEW'S FACT BLOCK, shared for the identical reason.
///
/// THE COUNT, WORDED HONESTLY. "READ 4 OF 9 IN THE BOOK" and not "4 OF 12":
/// twelve is how many leads casebook.json holds and the player has no way to
/// know that number, so printing it would tell them how much they have not
/// found -- which is the one thing an investigation must not hand over.
[[nodiscard]] std::vector<PanelFact> caseFactsFor(const CasebookPageState& state) {
    // Tallies as tallies (UI-EA-SPEC 1.5 #29): `4/9` is the census's own
    // one-word form, and 9 is still `known`, never the file's total -- the
    // count stays worded honestly, just shorter. THEY CALL YOU keeps its
    // clause: it is the ward talking, not the machine.
    return {
        PanelFact{"READ", std::to_string(state.read) + "/" + std::to_string(state.known),
                  InkRole::Number},
        PanelFact{"WAITING", std::to_string(std::max(0, state.known - state.read)),
                  InkRole::Number},
        PanelFact{"DEAD ENDS", std::to_string(state.cold), InkRole::Dim},
        PanelFact{"THEY CALL YOU", state.calledYou, InkRole::Prose},
    };
}

/// THE SHELF'S FACT BLOCK for the highlighted case: what it is waiting on.
/// Three rows for every row of the shelf, so nothing under them moves as the
/// cursor walks it -- leadFactsFor's own rule.
[[nodiscard]] std::vector<PanelFact> shelfFactsFor(const CasebookShelfRow& row) {
    return {
        PanelFact{row.book ? "READ" : "STAGE", row.tally.empty() ? std::string("--") : row.tally,
                  InkRole::Number},
        PanelFact{"NEXT", row.next.empty() ? std::string("--") : row.next,
                  row.next.empty() ? InkRole::Dim : InkRole::Prose},
        PanelFact{"COMPASS", row.followed ? std::string("ON THIS") : std::string("--"),
                  row.followed ? InkRole::Prose : InkRole::Dim},
    };
}

/// A fact block's natural width: the longest label, the two-cell gutter
/// factValueColumn spends after it, the longest value.
[[nodiscard]] int factsNaturalCells(const std::vector<PanelFact>& facts) {
    int label = 0;
    int value = 0;
    for (const PanelFact& fact : facts) {
        label = std::max(label, static_cast<int>(fact.label.size()));
        value = std::max(value, static_cast<int>(fact.value.size()));
    }
    return label + 2 + value;
}

/// THE WIDTH MEASURE -- the widest row EITHER VIEW of this book will draw for
/// ANY lead, plus the border, through panelMeasureCells. The mirror of the
/// body-height sizing in composeFor, and cursor-proof the same way
/// kDetailHoldRows is: everything here is a maximum over the whole book and
/// over both tabs, so arrowing the list, crossing the tab row or walking the
/// ward (a bearing is never the longest value on its row) moves no border.
/// What CAN move it is the world putting a new lead in the book -- which is
/// the same event that is already allowed to move the height.
[[nodiscard]] int measuredCells(const CasebookPageState& state, const PanelMetric& metric,
                                int frameWidth) {
    // THE MASTER at its natural width: the one-column plan's own content
    // cells, planned against a deliberately roomy rect so the answer is the
    // content's and not the pane's. Floored at the split's own master floor.
    int master = kMinMasterCells;
    if (!state.rows.empty()) {
        const PanelRect roomy{0, 0, metric.widthOf(200), metric.heightOf(80)};
        const OptionListPlan plan =
            planOptionList(optionsFor(state.rows), roomy, metric, listStyle());
        master = std::max(master, optionListNaturalCells(plan, listStyle().gutterCells));
    }

    // THE DETAIL at the widest thing it can be asked to hold -- badge rows,
    // fact blocks, the commit verb's widest fixed wording -- across every
    // lead AND the case view. Prose gets no vote (it wraps; the four-row hook
    // pane is the one wrap whose row count is pinned by the composition, so
    // it votes at a quarter of its length). Floored at the split's own detail
    // floor AND at the width kDetailHoldRows was judged against
    // (panelHeldDetailCells), so the held height keeps its promise at any
    // width the measure chooses.
    int detail = std::max(kMinDetailCells, panelHeldDetailCells(kMasterShare, kMinMasterCells));
    for (const CasebookLeadRow& row : state.rows) {
        const std::string label = row.brief.empty() ? row.place : row.brief;
        // THE WIDEST STATE WORD THIS ROW CAN WEAR, followed or not -- so a
        // FOLLOW press (a state change, but a player's one) moves no border.
        const int stateCells =
            std::max(static_cast<int>(stateWord(row).size()),
                     row.state == CasebookLeadState::Open
                         ? static_cast<int>(std::string_view("ON THE COMPASS").size())
                         : 0);
        detail = std::max(detail, static_cast<int>(label.size()) + 2 + 2 + stateCells + 1);
        detail = std::max(detail, factsNaturalCells(leadFactsFor(row)));
    }
    detail = std::max(detail, static_cast<int>(state.caseTitle.size()) + 2 + 2 +
                                  static_cast<int>(std::string_view("CLOSED").size()) + 1);
    detail = std::max(detail, factsNaturalCells(caseFactsFor(state)));
    // THE CASES SHELF votes too: a title, the two-cell gutter, its state --
    // so crossing onto the shelf moves no border either.
    for (const CasebookShelfRow& row : state.shelf) {
        detail = std::max(detail, static_cast<int>(row.title.size()) + 2 +
                                      static_cast<int>(std::string_view("READING").size()) + 1);
        detail = std::max(detail, static_cast<int>(row.title.size()) + 2 +
                                      static_cast<int>(row.state.size()) + 1);
        detail = std::max(detail, factsNaturalCells(shelfFactsFor(row)));
    }
    detail = std::max(detail, (static_cast<int>(state.hook.size()) + 3) / 4);
    // The commit line's widest FIXED variant -- "ENTER - LOOK AT IT" plus the
    // restated look key -- rather than the per-lead bearing variants, which
    // are both shorter and would put a walking body's changing bearing into
    // the frame's width. "ENTER" LITERALLY, not state.commitKey: the pad's
    // "A" is shorter, and a measure that followed the device would resize
    // the card mid-frame on a live switch. Widest variant, held.
    const std::string look = "ENTER - LOOK AT IT (" + state.lookKey + ")";
    detail = std::max(detail, static_cast<int>(look.size()));

    int want = masterDetailCellsFor(kMasterShare, kMinMasterCells, master, detail);

    // The one single row that must shed nothing: the tab row, which is the
    // page's whole header now (the breadcrumb line is gone -- UI-EA-SPEC
    // sec. 5's one-header-line law). The nav band gets no vote -- unlike
    // creation's one-row band it already knows how to take a second row.
    want = std::max(want, tabRowCells(state.title, tabsFor(), state.readout));

    return panelMeasureCells(metric.cellsIn(frameWidth), want + 2);
}

/// One row if the whole nav list fits in one, two if it does not. Still a FIXED
/// composition: it responds to the window and to the length of its own list,
/// never to a player.
[[nodiscard]] Composition composeFor(const CasebookPageState& state, int frameWidth,
                                     int frameHeight) {
    Composition out = compose(frameWidth, frameHeight, 1);
    if (!out.usable) {
        return out;
    }

    // THE MEASURE, WIDTH FIRST -- so the nav-row check and the body-height
    // sizing below both run against the panes the page will actually draw,
    // which is the wrap feedback: a narrower frame takes its nav in two rows
    // and its prose taller, and both are counted rather than discovered.
    int gridCells = measuredCells(state, out.metric, frameWidth);
    if (gridCells < out.metric.cellsIn(frameWidth)) {
        const Composition sized = compose(frameWidth, frameHeight, 1, -1, gridCells);
        if (sized.usable) {
            out = sized;
        } else {
            gridCells = -1;
        }
    } else {
        gridCells = -1;
    }

    const std::vector<PanelOption> nav = navOptionsFor(state);
    int navRows = 1;
    if (planOptionList(nav, out.navBand, out.metric, navStyleOf()).overflowed) {
        Composition taller = compose(frameWidth, frameHeight, 2, -1, gridCells);
        if (taller.usable) {
            out = taller;
            navRows = 2;
        }
    }

    // SIZE THE BODY TO WHAT IS IN IT. The list is measured against the FULL
    // pane on purpose: a planner asked against a pane too short to hold its
    // content answers with the pane, and the answer wanted here is the content
    // -- CAPPED at the paging rule (UI-EA-SPEC 1.5 #27): past eight leads the
    // list pages rather than growing the body, and `+N` on the rule says so.
    const int listNeed =
        state.rows.empty()
            ? 1
            : std::min(kLeadPageRows,
                       planOptionList(optionsFor(state.rows), listRectOf(out), out.metric,
                                      listStyle())
                           .rows);
    const int hold = kDetailHoldRows;
    // One blank row of breathing space under the taller half -- the same the
    // panes already leave between their content and their stippled field.
    const int want =
        std::clamp(std::max(listNeed + kIndicatorRows + 1, hold), kMinBodyRows, out.bodyRows);
    if (want >= out.bodyRows) {
        return out;
    }
    // The body is the only spanWeight in the stack, so a grid this many rows
    // shorter is a BODY that many rows shorter and nothing above it moves.
    const int gridRows = out.metric.rowsIn(frameHeight) - (out.bodyRows - want);
    const Composition sized = compose(frameWidth, frameHeight, navRows, gridRows, gridCells);
    return sized.usable ? sized : out;
}

/// THE CONSEQUENCE BLOCK -- what told you to come here, and what this lead
/// bought. Lifted out of the drawing so casebookPageMetrics() can measure the
/// SAME list the pane will pin, rather than a second description of it.
[[nodiscard]] std::vector<PanelLine> effectLinesFor(const CasebookLeadRow& row,
                                                    const Rgb& accent) {
    // TERSE BULLETS (UI-EA-SPEC 1.5 #28): one-word names -- FROM, FOR, OPENED
    // -- with the world words whole after them. The lead with no opener says
    // nothing about it: the case badge is one pane over.
    std::vector<PanelLine> out;
    if (!row.from.empty()) {
        // WHAT TOLD YOU TO COME HERE is what makes a trail a trail rather than a
        // list of addresses, and it wraps because the Drowned Hold is named by
        // FOUR separate leads -- which is what corroboration is.
        out.push_back(PanelLine{Bullet::Dot, "FROM", row.from, InkRole::Prose, accent});
    }
    if (row.state == CasebookLeadState::Open) {
        out.push_back(PanelLine{Bullet::Dot, "FOR", row.what, InkRole::Prose, accent});
        return out;
    }
    for (const std::string& opened : row.opened) {
        // THE ROW THE OWNER NEVER SAW. Three of these under the ledger.
        out.push_back(PanelLine{Bullet::Dot, "OPENED", opened, InkRole::Number, accent});
    }
    if (row.opened.empty()) {
        // Kept for the block's own invariant -- a consequence block is never
        // empty (test_casebook_page pins it) -- and because a walked lead that
        // paid nothing is a real answer, not scaffolding.
        out.push_back(PanelLine{Bullet::Dot, "", "IT OPENED NOTHING.", InkRole::Dim, accent});
    }
    return out;
}

/// THE FLAVOUR BLOCK -- the clue and the paragraph behind it. An OPEN lead has
/// no clue yet and says nothing at all (the FOR bullet below already carries
/// the errand) -- except on a FRESH book, where the pane's one job is to hand
/// a stranger the case: the hook prints here, the spec's twenty-word case
/// brief where fifty words of scaffolding stood.
[[nodiscard]] std::vector<PanelLine> flavourLinesFor(const CasebookLeadRow& row,
                                                     const CasebookPageState& state,
                                                     const Rgb& accent) {
    if (row.state == CasebookLeadState::Open) {
        if (state.read == 0 && !state.hook.empty()) {
            return {PanelLine{Bullet::None, "", state.hook, InkRole::Prose, accent}};
        }
        return {};
    }
    // FLAVOUR -> NAMED EFFECT -> NUMBER, colour-sorted: the clue is what you
    // got, the paragraph is what it means, and the bulleted names in the block
    // below are the mechanical consequence -- which on this surface is literally
    // what went into the book.
    std::vector<PanelLine> out{PanelLine{Bullet::None, "", row.found, InkRole::Prose, accent}};
    if (!row.detail.empty()) {
        out.push_back(PanelLine{Bullet::None, "", row.detail, InkRole::Prose, accent});
    }
    return out;
}

/// THE LEADS VIEW: everything about the lead the cursor is on.
void drawLeadDetail(Framebuffer& target, const PanelRect& detail, const PanelMetric& metric,
                    const CasebookLeadRow& row, const CasebookPageState& state, float alpha) {
    const PanelInk& ink = panelInk();
    const Rgb accent = stateAccent(row.state, row.close);
    const std::string label = row.brief.empty() ? row.place : row.brief;

    const std::vector<PanelRect> panes = splitRows(detail, metric,
                                                   {
                                                       spanCells(1),   // the subject badge
                                                       spanCells(1),   // air
                                                       spanCells(3),   // the facts
                                                       spanCells(1),   // air
                                                       spanWeight(1),  // prose, then the verb
                                                   });

    // THE SUBJECT, INVERTED, with its state right-aligned opposite -- the
    // reference's "subject then status" header shape, and the identical fill
    // the selected row in the list wears, so the two read as one object seen
    // twice.
    const int badge = static_cast<int>(label.size()) + 2;
    drawInvertedFill(target, panes[0], metric, 0, 0, badge, accent, alpha);
    drawCellTextKnockout(target, panes[0], metric, 1, 0, label, ink.knockout, alpha);
    // A CELL OF AIR OFF THE FRAME'S OWN EDGE. The map pass paid a capture to
    // learn this: a right-aligned value flush against the border reads as
    // punctuated by the `|`/`!` flicker -- "WASTREL!".
    drawCellTextRight(target, panes[0], metric, 1, 0, stateWord(row), ink.dim, alpha);

    // FACTS, NOT PARAGRAPHS: labels left, values at one shared column. Built
    // by leadFactsFor -- see its header for what is a fact and what is a
    // sentence, and note the width measure walks the same list, which is what
    // keeps "BONDSMAN GRIEVE, OF THE KING'S BOND" whole at every width the
    // measure can choose.
    drawFacts(target, panes[2], metric, leadFactsFor(row), -1, alpha);

    // THE PROSE, IN TWO BLOCKS, AND THE CONSEQUENCE IS THE ONE THAT IS PINNED.
    //
    // A CAPTURE FOUND THIS. At 1920x1080 the detail pane holds fewer ROWS than
    // at 960x540 (hudMinorScale steps up with height), the clue and its
    // paragraph wrapped to eight lines, and drawProse simply stopped at the
    // bottom of the rect -- so `• OPENED THE LEDGER` was not drawn at all. That
    // is this whole pass's own defect reintroduced one pane deeper: a lead that
    // opened three things silently reporting two.
    //
    // So the CONSEQUENCE block is measured first and pinned to the foot of the
    // prose area, immediately above the commit verb it justifies -- the
    // reference's own shape, where `• Unlocks rituals:` sits directly over
    // `e - Establish` -- and the FLAVOUR takes whatever is left above it. What
    // gets cut when the pane is short is the tail of a paragraph, which is a
    // paragraph the player can read at any other window size, rather than the
    // one line that says what the case gained.
    const int proseRows = std::max(0, metric.rowsIn(panes[4].h) - 2);
    const PanelRect prose{panes[4].x, panes[4].y, panes[4].w, metric.heightOf(proseRows)};

    const std::vector<PanelLine> flavour = flavourLinesFor(row, state, accent);
    const std::vector<PanelLine> effects = effectLinesFor(row, accent);
    const int effectRows = std::min(measureProse(prose, metric, effects), proseRows);
    const int flavourRoom = std::max(0, proseRows - effectRows);
    PanelRect flavourRect{prose.x, prose.y, prose.w, metric.heightOf(flavourRoom)};
    // AND A CUT PARAGRAPH SAYS IT WAS CUT. At 1920x1080 the pane holds fewer
    // rows than at 960x540 and the clue's second paragraph runs past the room
    // the consequence block left it; stopping mid-sentence reads as a rendering
    // fault rather than as a paragraph continuing. One row is spent on the mark,
    // which is menu_view.cpp's own convention for the same situation.
    const bool cut = measureProse(flavourRect, metric, flavour) > flavourRoom;
    if (cut && flavourRoom > 1) {
        flavourRect.h = metric.heightOf(flavourRoom - 1);
    }
    const int wrote = drawProse(target, flavourRect, metric, flavour, alpha);
    const int used = cut && flavourRoom > 1 ? flavourRoom : wrote;
    if (cut && flavourRoom > 1) {
        drawCellText(target, prose, metric, 0, flavourRoom - 1, "...", ink.dim, alpha);
    }
    const PanelRect effectRect{prose.x, prose.y + metric.heightOf(flavourRoom), prose.w,
                               metric.heightOf(effectRows)};
    drawProse(target, effectRect, metric, effects, alpha);

    // DELIBERATE EMPTINESS, TEXTURED -- the reference's own faint field in the
    // gap the flavour did not need, rather than a black hole or something
    // crammed in to fill it.
    const int spare = flavourRoom - used;
    if (spare >= 3) {
        const PanelRect rest{prose.x, prose.y + metric.heightOf(used + 1), prose.w,
                             metric.heightOf(spare - 1)};
        drawStipple(target, rest, metric, ink.rule, kPaneStippleAlpha * alpha);
    }

    // STATE CHANGES THE VERB. Three states, three verbs, no greyed-out button:
    //
    //   standing in it, still open   LOOK AT IT -- the one thing worth doing
    //   anywhere else                SHOW ME ON THE MAP, with the bearing
    //   no such place on the plan    said out loud, rather than a dead key
    // THE VERB NAMES THE LIVE CONFIRM KEY (state.commitKey -- "ENTER" on a
    // keyboard, "A" on a pad), the last of this page's keyboard literals.
    // The WIDTH MEASURE above still votes with the widest fixed "ENTER"
    // variant, deliberately, so the card does not resize on a live device
    // switch -- the stable-geometry rule, one axis over.
    const std::string commit =
        state.commitKey.empty() ? std::string(kGlyphReturn) : state.commitKey;
    std::string verb;
    std::string cost;
    if (row.here && row.state == CasebookLeadState::Open) {
        // The look key restated bare -- "(E)" -- the canon KEY - VERB (COST)
        // grammar with the explainer clause retired (UI-EA-SPEC 1.5 #28,
        // commit 8 -> 4).
        verb = commit + " - LOOK AT IT";
        cost = "(" + state.lookKey + ")";
    } else if (row.routable) {
        // THE RESTATEMENT IS THE WALK, not the name -- and standing on it
        // there is nothing to restate: HERE, the map page's own state label.
        verb = commit + " - SHOW IT";
        cost = row.here ? std::string("(HERE)") : "(" + row.bearing + ")";
    } else {
        const int lastRow = metric.rowsIn(detail.h) - 1;
        drawCellText(target, detail, metric, 0, lastRow, "NO PLACE FOR THIS", ink.dim, alpha);
        return;
    }
    drawCommitVerb(target, detail, metric, verb, cost, ink.key, alpha);
    // The commit beat (contract b): armed by the routing at the press.
    drawCommitPulse(target, detail, metric, verb, cost, accent, alpha, state.commitPulse);
}

/// THE CASE VIEW: the same pane, the case instead of the lead. The master list
/// is drawn identically under both, which is the stable-geometry rule taken
/// seriously -- a tab that also moved the list would be two compositions
/// wearing one frame.
void drawCaseDetail(Framebuffer& target, const PanelRect& detail, const PanelMetric& metric,
                    const CasebookPageState& state, float alpha) {
    const PanelInk& ink = panelInk();
    const Rgb accent = Rgb{0.90F, 0.46F, 0.40F};

    const std::vector<PanelRect> panes = splitRows(detail, metric,
                                                   {
                                                       spanCells(1),   // the case badge
                                                       spanCells(1),   // air
                                                       spanCells(4),   // the hook
                                                       spanCells(1),   // air
                                                       spanCells(4),   // the facts
                                                       spanCells(1),   // air
                                                       spanWeight(1),  // the nerve, and the verb
                                                   });

    const int badge = static_cast<int>(state.caseTitle.size()) + 2;
    drawInvertedFill(target, panes[0], metric, 0, 0, badge, accent, alpha);
    drawCellTextKnockout(target, panes[0], metric, 1, 0, state.caseTitle, ink.knockout, alpha);
    drawCellTextRight(target, panes[0], metric, 1, 0, state.closed ? "CLOSED" : "OPEN", ink.dim,
                      alpha);

    drawProse(target, panes[2], metric,
              {PanelLine{Bullet::None, "", state.hook, InkRole::Prose, accent}}, alpha);

    // The count and the name the ward has for you, via caseFactsFor -- one
    // list, walked by this drawing and by the width measure both. Its header
    // carries the honesty note about "OF 9" versus "OF 12".
    drawFacts(target, panes[4], metric, caseFactsFor(state), -1, alpha);

    // THE WARD'S NERVE AS A BAR, because the reference's own answer to "show
    // the consequence" is shape and figure together rather than a line of text
    // reporting a number.
    const int barCells = std::max(6, metric.cellsIn(panes[6].w) / 3);
    const std::vector<PanelBar> bars{
        PanelBar{"DREAD", std::to_string(state.dread), state.dread, 100, accent}};
    drawBars(target, panes[6], metric, bars, barCells, alpha);
    const PanelRect under{panes[6].x, panes[6].y + metric.heightOf(2), panes[6].w,
                          std::max(0, panes[6].h - metric.heightOf(2))};
    std::vector<PanelLine> mood{
        PanelLine{Bullet::None, "", state.dreadBand, InkRole::Prose, accent}};
    if (state.closed && !state.closeLine.empty()) {
        mood.push_back(PanelLine{Bullet::Dot, "", state.closeLine, InkRole::Number, accent});
    }
    const int used = drawProse(target, under, metric, mood, alpha);
    const int spare = std::max(0, metric.rowsIn(under.h) - 2) - used;
    if (spare >= 3) {
        const PanelRect rest{under.x, under.y + metric.heightOf(used + 1), under.w,
                             metric.heightOf(spare - 1)};
        drawStipple(target, rest, metric, ink.rule, kPaneStippleAlpha * alpha);
    }
    drawCommitVerb(target, detail, metric, std::string(kGlyphLeft) + " - LEADS", "", ink.key,
                   alpha);
}

/// The shelf, as options -- one list, two drawings (the pane and the
/// collapsed body), so the two cannot drift.
[[nodiscard]] std::vector<PanelOption> shelfOptionsFor(const CasebookPageState& state) {
    const PanelInk& ink = panelInk();
    const Rgb accent = Rgb{0.90F, 0.46F, 0.40F};
    std::vector<PanelOption> options;
    options.reserve(state.shelf.size());
    for (const CasebookShelfRow& row : state.shelf) {
        PanelOption option;
        option.label = row.title;
        // THE VALUE IS THE STATE, the lead list's own rule: the fronted book
        // says so where the others say where they stand.
        option.value = row.fronted ? "READING" : row.state;
        option.accent = row.book ? accent : ink.dim;
        option.valueInk = row.book ? InkRole::Prose : InkRole::Dim;
        option.labelTakesAccent = row.book;
        option.selectable = true;
        options.push_back(std::move(option));
    }
    return options;
}

[[nodiscard]] OptionListStyle shelfStyle() {
    OptionListStyle style;
    style.showKeys = false;
    style.maxColumns = 1;
    style.gutterCells = 2;
    style.minRows = 0;
    style.alignValues = true;
    return style;
}

/// THE CASES VIEW (THE PULL PACK): the shelf in the same pane. Every book the
/// player has been handed and every questline they have started, one row
/// each, where each stands in the tallies' own one-word forms; the
/// highlighted case's facts under the list; the commit that fronts it for
/// the page. The master list is drawn identically under it -- the
/// stable-geometry rule -- so fronting a different book is the ONE thing
/// that changes the list, and it changes on the press, not on the tab.
void drawShelfDetail(Framebuffer& target, const PanelRect& detail, const PanelMetric& metric,
                     const CasebookPageState& state, float alpha) {
    const PanelInk& ink = panelInk();
    const Rgb accent = Rgb{0.90F, 0.46F, 0.40F};
    const int count = static_cast<int>(state.shelf.size());
    const int listRows = std::max(1, std::min(count, 6));
    const std::vector<PanelRect> panes = splitRows(detail, metric,
                                                   {
                                                       spanCells(1),         // the badge
                                                       spanCells(1),         // air
                                                       spanCells(listRows),  // the shelf
                                                       spanCells(1),         // air
                                                       spanCells(3),         // the facts
                                                       spanWeight(1),        // air, then the verb
                                                   });
    const std::string badge = "CASES";
    drawInvertedFill(target, panes[0], metric, 0, 0, static_cast<int>(badge.size()) + 2, accent,
                     alpha);
    drawCellTextKnockout(target, panes[0], metric, 1, 0, badge, ink.knockout, alpha);
    // The count, right-aligned: books in hand over the files that ship --
    // the one number here the player CAN know. The ones not in hand are
    // counted and never named.
    int inHand = 0;
    for (const CasebookShelfRow& row : state.shelf) {
        inHand += row.book ? 1 : 0;
    }
    drawCellTextRight(target, panes[0], metric, 1, 0,
                      std::to_string(inHand) + "/" + std::to_string(std::max(inHand, state.shelfBookTotal)) +
                          " IN HAND",
                      ink.dim, alpha);

    if (count == 0) {
        drawCellText(target, panes[2], metric, 0, 0, "NOTHING ON THE SHELF YET", ink.dim, alpha);
        return;
    }
    const OptionListPlan plan = planOptionList(shelfOptionsFor(state), panes[2], metric, shelfStyle());
    const int at = std::clamp(state.shelfCursor, 0, count - 1);
    drawOptionListPlanned(target, panes[2], metric, shelfOptionsFor(state), at, plan, alpha);

    const CasebookShelfRow& row = state.shelf[static_cast<std::size_t>(at)];
    drawFacts(target, panes[4], metric, shelfFactsFor(row), -1, alpha);

    // STATE CHANGES THE VERB: a book you can read, the book you ARE reading,
    // a questline (read in the Journal tile).
    const std::string commit =
        state.commitKey.empty() ? std::string(kGlyphReturn) : state.commitKey;
    const int lastRow = metric.rowsIn(detail.h) - 1;
    if (!row.book) {
        drawCellText(target, detail, metric, 0, lastRow, "A LINE, NOT A BOOK. THE JOURNAL HAS IT.",
                     ink.dim, alpha);
        return;
    }
    const std::string verb = commit + " - READ IT";
    const std::string cost = row.fronted ? std::string("(OPEN)") : std::string();
    drawCommitVerb(target, detail, metric, verb, cost, ink.key, alpha);
    drawCommitPulse(target, detail, metric, verb, cost, accent, alpha, state.commitPulse);
}

/// THE SHELF WHERE THE BODY IS ONE PANE (320x180): the split collapsed and
/// the list took the whole body, so the shelf takes the whole body too --
/// the same rows, the same highlight, the facts of the highlighted case
/// under them and the commit at the foot. A tab that showed nothing at the
/// smallest window would be a tab that lied there.
void drawShelfList(Framebuffer& target, const PanelRect& body, const PanelMetric& metric,
                   const CasebookPageState& state, float alpha) {
    const PanelInk& ink = panelInk();
    const Rgb accent = Rgb{0.90F, 0.46F, 0.40F};
    const int count = static_cast<int>(state.shelf.size());
    if (count == 0) {
        drawCellText(target, body, metric, 0, 0, "NOTHING ON THE SHELF YET", ink.dim, alpha);
        return;
    }
    const int listRows = std::max(1, std::min(count, 6));
    const std::vector<PanelRect> panes = splitRows(body, metric,
                                                   {
                                                       spanCells(listRows),  // the shelf
                                                       spanCells(1),         // air
                                                       spanCells(3),         // the facts
                                                       spanWeight(1),        // air, then the verb
                                                   });
    const OptionListPlan plan = planOptionList(shelfOptionsFor(state), panes[0], metric, shelfStyle());
    const int at = std::clamp(state.shelfCursor, 0, count - 1);
    drawOptionListPlanned(target, panes[0], metric, shelfOptionsFor(state), at, plan, alpha);
    const CasebookShelfRow& row = state.shelf[static_cast<std::size_t>(at)];
    drawFacts(target, panes[2], metric, shelfFactsFor(row), -1, alpha);
    const std::string commit =
        state.commitKey.empty() ? std::string(kGlyphReturn) : state.commitKey;
    if (row.book) {
        const std::string verb = commit + " - READ IT";
        const std::string cost = row.fronted ? std::string("(OPEN)") : std::string();
        drawCommitVerb(target, body, metric, verb, cost, ink.key, alpha);
        drawCommitPulse(target, body, metric, verb, cost, accent, alpha, state.commitPulse);
    }
}

}  // namespace

CasebookPageScroll casebookPageScroll(const CasebookPageState& state, int frameWidth,
                                      int frameHeight) {
    CasebookPageScroll out;
    const Composition comp = composeFor(state, frameWidth, frameHeight);
    if (!comp.usable || state.rows.empty()) {
        return out;
    }
    // PLANNED AGAINST THE WHOLE LIST, never against one screenful -- so the
    // value column is sized for the longest place that exists and does not
    // shuffle when a page turns.
    const OptionListPlan plan =
        planOptionList(optionsFor(state.rows), listRectOf(comp), comp.metric, listStyle());
    // The paging cap, same number composeFor sized the body against.
    out.perScreen = std::max(1, std::min(kLeadPageRows, plan.columns * plan.rows));
    const int count = static_cast<int>(state.rows.size());
    out.screens = std::max(1, (count + out.perScreen - 1) / out.perScreen);
    out.screen = std::clamp(std::max(0, state.cursor) / out.perScreen, 0, out.screens - 1);
    out.firstRow = out.screen * out.perScreen;
    return out;
}

CasebookPageMetrics casebookPageMetrics(const CasebookPageState& state, int frameWidth,
                                        int frameHeight) {
    CasebookPageMetrics out;
    const Composition comp = composeFor(state, frameWidth, frameHeight);
    out.metric = comp.metric;
    if (!comp.usable) {
        return out;
    }
    out.usable = true;
    out.split = comp.body.split;
    out.master = listRectOf(comp);
    out.detail = comp.body.detail;
    out.masterCells = comp.metric.cellsIn(comp.body.master.w);
    out.detailCells = comp.metric.cellsIn(comp.body.detail.w);
    const std::vector<PanelOption> nav = navOptionsFor(state);
    const OptionListPlan navPlan = planOptionList(nav, comp.navBand, comp.metric, navStyleOf());
    out.navEntries = static_cast<int>(nav.size());
    out.navShown = std::min(out.navEntries, navPlan.columns * navPlan.rows);
    out.navRows = comp.metric.rowsIn(comp.navBand.h);
    // THE CONSEQUENCE BLOCK, measured against the room the pane actually pins
    // for it. A capture found the defect this reports: at 1920x1080 the pane
    // holds fewer rows than at 960x540, the clue wrapped to eight lines, and
    // `• OPENED THE LEDGER` was silently not drawn.
    if (comp.body.split && !state.rows.empty()) {
        const int at =
            std::clamp(state.cursor, 0, static_cast<int>(state.rows.size()) - 1);
        const CasebookLeadRow& row = state.rows[static_cast<std::size_t>(at)];
        const std::vector<PanelRect> panes =
            splitRows(comp.body.detail, comp.metric,
                      {spanCells(1), spanCells(1), spanCells(3), spanCells(1), spanWeight(1)});
        const int proseRows = std::max(0, comp.metric.rowsIn(panes[4].h) - 2);
        const PanelRect prose{panes[4].x, panes[4].y, panes[4].w,
                              comp.metric.heightOf(proseRows)};
        const std::vector<PanelLine> effects =
            effectLinesFor(row, stateAccent(row.state, row.close));
        out.effectRowsWanted = measureProse(prose, comp.metric, effects);
        out.effectRows = std::min(out.effectRowsWanted, proseRows);
    }
    if (state.rows.empty()) {
        return out;
    }
    const OptionListPlan plan =
        planOptionList(optionsFor(state.rows), out.master, comp.metric, listStyle());
    out.keyCells = plan.keyCells;
    out.labelCells = plan.labelCells;
    out.valueCells = plan.valueCells;
    out.columns = plan.columns;
    out.listRows = plan.rows;
    return out;
}

int casebookTabAtPixel(const CasebookPageState& state, int frameWidth, int frameHeight, int px,
                       int py) {
    const Composition comp = composeFor(state, frameWidth, frameHeight);
    if (!comp.usable) {
        return -1;
    }
    // The exact band the drawing hands drawTabRow -- frame.band(comp.tabRow, 1)
    // over the same interior the composition worked out -- and the exact
    // title, tabs, current and readout, so tabRowTabAt is answering for the
    // pixels the row actually printed on.
    const PanelRect band{comp.interior.x, comp.interior.y + comp.metric.heightOf(comp.tabRow),
                         comp.interior.w, comp.metric.cellH()};
    return tabRowTabAt(band, comp.metric, state.title, tabsFor(), static_cast<int>(state.tab),
                       state.readout, px, py);
}

int casebookLeadAtPixel(const CasebookPageState& state, int frameWidth, int frameHeight, int px,
                        int py) {
    const Composition comp = composeFor(state, frameWidth, frameHeight);
    if (!comp.usable || state.rows.empty()) {
        return -1;
    }
    const OptionListPlan plan =
        planOptionList(optionsFor(state.rows), listRectOf(comp), comp.metric, listStyle());
    const CasebookPageScroll scroll = casebookPageScroll(state, frameWidth, frameHeight);
    const int count = static_cast<int>(state.rows.size());
    const int first = std::clamp(scroll.firstRow, 0, count);
    const int drawn = std::min(count, first + scroll.perScreen) - first;
    const int at = optionListAt(listRectOf(comp), comp.metric, plan, drawn, px, py);
    return at < 0 ? -1 : first + at;
}

void drawCasebookPage(Framebuffer& target, const CasebookPageState& state) {
    if (!state.open || state.openAmount <= 0.0F) {
        return;
    }
    const Composition comp = composeFor(state, target.width(), target.height());
    if (!comp.usable) {
        return;
    }
    const float alpha = std::clamp(state.openAmount, 0.0F, 1.0F);
    const PanelMetric metric = comp.metric;
    const PanelInk& ink = panelInk();

    FrameStyle style;
    // `◆` at every corner and junction, consistently, across this whole screen.
    style.junction = Motif::Diamond;
    style.alpha = alpha;
    style.stipple = false;
    // A FULL TAKEOVER, so the ground is dense -- kPageGroundAlpha, the one
    // number every full-screen page now uses. See its own header: this file
    // used to say 0.99 for the same reason the map said 0.995 and the controls
    // page said 0.97, which is three spellings of one rule.
    style.groundAlpha = kPageGroundAlpha;

    // A FULL TAKEOVER IS STILL A FULL TAKEOVER WHEN THE FRAME ENDS EARLY.
    //
    // The book now sizes its body to what is in it (see composeFor), which is
    // right -- a one-lead book in a forty-one-row pane was the second-largest
    // "unfinished" tell in the build. But this page is opened FROM THE WORLD,
    // and the first capture of the sized frame had the panel across the top of
    // the screen and the Tarwalk at full daylight brightness across the bottom
    // half of it, with the HUD already stood down: a bright, busy band under a
    // near-black page. The takeover is a property of the PAGE, not of how many
    // rows its content happens to spend, so the ground takes the whole frame
    // and the border takes only what it has to enclose.
    target.fillRect(0, 0, target.width(), target.height(), style.ground,
                    style.groundAlpha * alpha);

    PanelFrame frame(target, comp.bounds, metric, style);
    for (const int r : comp.ruleRows) {
        frame.addRule(r);
    }
    if (comp.body.split) {
        frame.addDivider(comp.body.dividerCell, comp.bodyRow, comp.bodyRows);
    }
    frame.draw();

    // --- THE ONE HEADER LINE ------------------------------------------------
    // The tab row IS the breadcrumb (UI-EA-SPEC sec. 5): title, the two views,
    // the case's own readout. NO PRINTED KEYS ON THE TABS -- the digits are
    // the leads' and TAB is the key that closes the book. A bouncer's warning
    // outranks the book and takes the row while it lasts; the old two-row
    // instruction band is gone, and the selection is named once, on the badge.
    const int count = static_cast<int>(state.rows.size());
    const int at = count > 0 ? std::clamp(state.cursor, 0, count - 1) : -1;
    if (!state.alert.empty()) {
        drawCellText(target, frame.band(comp.tabRow, 1), metric, 0, 0, state.alert,
                     Rgb{0.90F, 0.52F, 0.30F}, alpha);
    } else {
        drawTabRow(target, frame.band(comp.tabRow, 1), metric, state.title, tabsFor(),
                   static_cast<int>(state.tab), state.readout, ink.accent, alpha);
    }

    // --- the master list ---------------------------------------------------
    const PanelRect listRect = listRectOf(comp);
    const std::vector<PanelOption> options = optionsFor(state.rows);
    const OptionListPlan plan = planOptionList(options, listRect, metric, listStyle());
    const CasebookPageScroll scroll = casebookPageScroll(state, target.width(), target.height());
    // THE PULL PACK: on the CASES view the shelf's own highlight is the ONE
    // armed cursor -- the lead list still draws (the stable-geometry rule)
    // but with no row filled, so two lists cannot both look chosen. Where
    // the body is one pane the shelf takes it outright.
    const bool shelfUp = state.tab == CasebookTab::Cases;
    if (shelfUp && !comp.body.split) {
        drawShelfList(target, listRect, metric, state, alpha);
    } else if (count > 0) {
        const int first = std::clamp(scroll.firstRow, 0, count);
        const int last = std::min(count, first + scroll.perScreen);
        const std::vector<PanelOption> page(options.begin() + first, options.begin() + last);
        drawOptionListPlanned(target, listRect, metric, page, shelfUp ? -1 : at - first, plan,
                              alpha);
    } else {
        // AN EMPTY BOOK IS WORDED. It happens for exactly one frame of one
        // session -- a casebook.json that failed to load -- and a blank pane
        // there would read as the page being broken rather than the file being
        // absent.
        drawCellText(target, listRect, metric, 0, 0, "NOTHING IN THE BOOK YET", ink.dim, alpha);
    }

    // DELIBERATE EMPTINESS, TEXTURED. The fourteen-word waiting sentence is
    // retired (UI-EA-SPEC 1.5, prose 14->0): the Law of Earned Text says the
    // book's remaining room is not news, and the fresh book's detail pane now
    // hands a stranger the case's own hook instead of scaffolding. The faint
    // field still says "left on purpose".
    if (count > 0 && !(shelfUp && !comp.body.split)) {
        const int listRows = metric.rowsIn(listRect.h);
        const int used = std::min(count, scroll.perScreen);
        const int spare = listRows - used;
        if (spare >= 3) {
            const PanelRect rest{listRect.x, listRect.y + metric.heightOf(used + 1), listRect.w,
                                 metric.heightOf(spare - 1)};
            drawStipple(target, rest, metric, ink.rule, kPaneStippleAlpha * alpha);
        }
    }

    // `+N` RIDES THE RULE (UI-EA-SPEC 1.5): what waits below the fold,
    // right-aligned over the master pane in the rule under the body -- the
    // reference's own text-on-the-divider trick, no row spent, and the digits
    // key (`0` = MORE) pages it.
    const int below = count - std::min(count, scroll.firstRow + scroll.perScreen);
    if (below > 0 && comp.ruleRows.size() >= 2 && !(shelfUp && !comp.body.split)) {
        const PanelRect ruleBand{comp.body.master.x,
                                 comp.interior.y + metric.heightOf(comp.ruleRows[1]),
                                 comp.body.master.w, metric.cellH()};
        drawCellTextRight(target, ruleBand, metric, 0, 0, "+" + std::to_string(below), ink.dim,
                          alpha);
    }

    // --- the detail pane ---------------------------------------------------
    if (comp.body.split) {
        if (state.tab == CasebookTab::Cases) {
            drawShelfDetail(target, comp.body.detail, metric, state, alpha);
        } else if (state.tab == CasebookTab::Case || at < 0) {
            drawCaseDetail(target, comp.body.detail, metric, state, alpha);
        } else {
            drawLeadDetail(target, comp.body.detail, metric,
                           state.rows[static_cast<std::size_t>(at)], state, alpha);
        }
    }

    // --- global nav, below its own rule ------------------------------------
    // Planned ONCE against the raised form so nothing moves as the tutor words
    // fade; drawn at rest as bare keycaps (UI-EA-SPEC sec. 2). A CELL OF AIR
    // OFF THE RIGHT EDGE -- "PUT IT DOWN|" reads as punctuated.
    const std::vector<PanelOption> nav = navOptionsFor(state);
    const PanelRect navRect{comp.navBand.x, comp.navBand.y,
                            std::max(0, comp.navBand.w - metric.cellW()), comp.navBand.h};
    const OptionListPlan navPlan = planOptionList(nav, navRect, metric, navStyleOf());
    std::vector<PanelOption> navCaps = nav;
    for (PanelOption& option : navCaps) {
        option.label.clear();
    }
    drawOptionListPlanned(target, navRect, metric, navCaps, -1, navPlan, alpha);
    if (state.tutor > 0.0F) {
        drawOptionListPlanned(target, navRect, metric, nav, -1, navPlan,
                              alpha * std::min(1.0F, state.tutor));
    }
}

}  // namespace granadad::render
