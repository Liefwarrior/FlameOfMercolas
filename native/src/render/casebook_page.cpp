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
            return "OPEN";
    }
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

/// THE COMPOSITION. Seven bands, three of which are rules, and a master/detail
/// split in the body -- keys_page.cpp's shape, because it is the shape the
/// reference's master/detail frame has and this page is the second surface the
/// spec names for it by name.
struct Composition {
    PanelMetric metric;
    PanelRect bounds;
    PanelRect interior;
    int tabRow = 0;
    int headerRow = 0;
    int headerRows = 0;
    int bodyRow = 0;
    int bodyRows = 0;
    int navRow = 0;
    std::vector<int> ruleRows;
    MasterDetail body;
    PanelRect headerBand;
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
    const Rgb accent = panelInk().accent;
    return {
        PanelOption{"UP DOWN", "NEXT LEAD", "", accent, InkRole::Dim, false},
        PanelOption{"LEFT RIGHT", state.tab == CasebookTab::Leads ? "THE CASE" : "THE LEADS", "",
                    accent, InkRole::Dim, false},
        PanelOption{"ENTER", "GO TO IT", "", accent, InkRole::Dim, false},
        PanelOption{state.closeKey.empty() ? std::string("TAB") : state.closeKey, "CLOSE", "",
                    accent, InkRole::Dim, false},
    };
}

[[nodiscard]] OptionListStyle navStyleOf() {
    OptionListStyle style;
    style.showKeys = true;
    style.maxColumns = 4;
    style.gutterCells = 2;
    style.minRows = 1;
    return style;
}

[[nodiscard]] Composition compose(int frameWidth, int frameHeight, int navRows) {
    Composition out;
    out.metric = panelMetric(frameHeight);
    const int cells = out.metric.cellsIn(frameWidth);
    const int rows = out.metric.rowsIn(frameHeight);
    if (cells < 8 || rows < 10) {
        return out;
    }
    out.bounds = PanelRect{(frameWidth - out.metric.widthOf(cells)) / 2,
                           (frameHeight - out.metric.heightOf(rows)) / 2,
                           out.metric.widthOf(cells), out.metric.heightOf(rows)};
    out.interior = PanelRect{out.bounds.x + out.metric.cellW(), out.bounds.y + out.metric.cellH(),
                             out.metric.widthOf(cells - 2), out.metric.heightOf(rows - 2)};

    const std::vector<PanelRect> bands = splitRows(out.interior, out.metric,
                                                   {
                                                       spanCells(1),   // the tab row
                                                       spanCells(1),   // rule
                                                       spanCells(2),   // the instruction
                                                       spanCells(1),   // rule
                                                       spanWeight(1),  // the body
                                                       spanCells(1),        // rule
                                                       spanCells(navRows),  // global nav
                                                   });
    const auto rowOf = [&out](const PanelRect& band) {
        return (band.y - out.interior.y) / out.metric.cellH();
    };
    out.tabRow = rowOf(bands[0]);
    out.headerBand = bands[2];
    out.headerRow = rowOf(bands[2]);
    out.headerRows = out.metric.rowsIn(bands[2].h);
    out.bodyBand = bands[4];
    out.bodyRow = rowOf(bands[4]);
    out.bodyRows = out.metric.rowsIn(bands[4].h);
    out.navBand = bands[6];
    out.navRow = rowOf(bands[6]);
    out.ruleRows = {rowOf(bands[1]), rowOf(bands[3]), rowOf(bands[5])};
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
        option.value = row.state != CasebookLeadState::Open ? stateWord(row)
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

/// One row if the whole nav list fits in one, two if it does not. Still a FIXED
/// composition: it responds to the window and to the length of its own list,
/// never to a player.
[[nodiscard]] Composition composeFor(const CasebookPageState& state, int frameWidth,
                                     int frameHeight) {
    Composition out = compose(frameWidth, frameHeight, 1);
    if (!out.usable) {
        return out;
    }
    const std::vector<PanelOption> nav = navOptionsFor(state);
    if (planOptionList(nav, out.navBand, out.metric, navStyleOf()).overflowed) {
        Composition taller = compose(frameWidth, frameHeight, 2);
        if (taller.usable) {
            return taller;
        }
    }
    return out;
}

[[nodiscard]] PanelRect listRectOf(const Composition& comp) {
    const int listRows = std::max(0, comp.bodyRows - kIndicatorRows);
    return PanelRect{comp.body.master.x, comp.body.master.y, comp.body.master.w,
                     comp.metric.heightOf(listRows)};
}

/// THE CONSEQUENCE BLOCK -- what told you to come here, and what this lead
/// bought. Lifted out of the drawing so casebookPageMetrics() can measure the
/// SAME list the pane will pin, rather than a second description of it.
[[nodiscard]] std::vector<PanelLine> effectLinesFor(const CasebookLeadRow& row,
                                                    const Rgb& accent) {
    std::vector<PanelLine> out;
    if (row.from.empty()) {
        // THE ONE LEAD WITH NO OPENER. Said as a whole sentence rather than as
        // "TOLD YOU BY THE CASE OPENED HERE", which is what a named-effect
        // bullet makes of it and which reads as two half-sentences collided.
        out.push_back(PanelLine{Bullet::Dot, "", "THE CASE OPENED HERE.", InkRole::Dim, accent});
    } else {
        // WHAT TOLD YOU TO COME HERE is what makes a trail a trail rather than a
        // list of addresses, and it wraps because the Drowned Hold is named by
        // FOUR separate leads -- which is what corroboration is, and a pane that
        // showed only the first of the four would hide the shape of the case.
        out.push_back(PanelLine{Bullet::Dot, "TOLD YOU BY", row.from, InkRole::Prose, accent});
    }
    if (row.state == CasebookLeadState::Open) {
        out.push_back(PanelLine{Bullet::Dot, "GO FOR", row.what, InkRole::Prose, accent});
        return out;
    }
    for (const std::string& opened : row.opened) {
        // THE ROW THE OWNER NEVER SAW. Three of these under the ledger.
        out.push_back(PanelLine{Bullet::Dot, "OPENED", opened, InkRole::Number, accent});
    }
    if (row.opened.empty()) {
        out.push_back(row.close
                          ? PanelLine{Bullet::Dot, "", "THE TRAIL ENDS HERE.", InkRole::Number,
                                      accent}
                          : PanelLine{Bullet::Dot, "", "IT OPENED NOTHING.", InkRole::Dim,
                                      accent});
    }
    return out;
}

/// THE FLAVOUR BLOCK -- the clue and the paragraph behind it, or the sentence
/// that says nobody has stood over this yet.
[[nodiscard]] std::vector<PanelLine> flavourLinesFor(const CasebookLeadRow& row,
                                                     const Rgb& accent) {
    if (row.state == CasebookLeadState::Open) {
        // NOT YET LOOKED AT, said out loud. The alternative is an empty half of
        // a pane, which reads as the page failing rather than as the lead
        // waiting.
        return {PanelLine{Bullet::None, "", "NOBODY HAS STOOD OVER THIS YET.", InkRole::Dim,
                          accent}};
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
                                                       spanCells(5),   // the facts
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

    // FACTS, NOT PARAGRAPHS: labels left, values at one shared column.
    //
    // WHAT IS A FACT AND WHAT IS A SENTENCE, decided by measuring rather than by
    // taste. The first capture of this pane put `what` ("WHAT THE SEA SPAT UP,
    // AND WHO SOLD IT", 37 glyphs) and `openedBy` (four names for the Drowned
    // Hold, 47) in this block and drawFacts clipped both -- "THE BODY, AND
    // WHOEVER FOU..". Those are sentences and they are in the prose block below,
    // wrapped. What is left is five short answers, and the witness takes two
    // rows rather than one because "BONDSMAN GRIEVE, OF THE KING'S BOND" is
    // thirty-five glyphs against a twenty-three-cell value column.
    //
    // EVERY EMPTY STATE IS WORDED. The Outfall has no witness at all; a blank
    // row there reads as a bug and "NOBODY -- IT IS A PLACE" reads as an answer.
    // That is the reference's own `no trinket`.
    const std::vector<PanelFact> facts{
        PanelFact{"WHERE", row.place, InkRole::Prose},
        PanelFact{"WHO", row.who.empty() ? std::string("NOBODY -- IT IS A PLACE") : row.who,
                  row.who.empty() ? InkRole::Dim : InkRole::Prose},
        PanelFact{"THEY ARE", row.whoWhat.empty() ? std::string("--") : row.whoWhat,
                  row.whoWhat.empty() ? InkRole::Dim : InkRole::Prose},
        PanelFact{"HEARD", row.heard.empty() ? std::string("--") : row.heard, InkRole::Number},
        PanelFact{"FROM YOU", row.here ? std::string("YOU ARE STANDING IN IT") : row.bearing,
                  row.here ? InkRole::Number : InkRole::Prose},
    };
    drawFacts(target, panes[2], metric, facts, -1, alpha);

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

    const std::vector<PanelLine> flavour = flavourLinesFor(row, accent);
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
        drawStipple(target, rest, metric, ink.rule, 0.22F * alpha);
    }

    // STATE CHANGES THE VERB. Three states, three verbs, no greyed-out button:
    //
    //   standing in it, still open   LOOK AT IT -- the one thing worth doing
    //   anywhere else                SHOW ME ON THE MAP, with the bearing
    //   no such place on the plan    said out loud, rather than a dead key
    if (row.here && row.state == CasebookLeadState::Open) {
        drawCommitVerb(target, detail, metric, "ENTER - LOOK AT IT",
                       "(" + state.lookKey + " DOES IT OUT THERE)", ink.key, alpha);
    } else if (row.routable) {
        // THE RESTATEMENT IS THE WALK, not the name. The place is already on the
        // WHERE row four lines above and in the breadcrumb at the top of the
        // frame; what pressing this actually costs is crossing the ward, and
        // that is the number worth putting next to the verb.
        // NO RESTATEMENT WHEN THERE IS NOTHING TO RESTATE. The FROM YOU row
        // four lines above already reads YOU ARE STANDING IN IT; a commit line
        // that says it a second time is the clutter the composition rules say
        // to cut rather than shrink.
        drawCommitVerb(target, detail, metric, "ENTER - SHOW ME WHERE",
                       row.here ? std::string() : "(" + row.bearing + ")", ink.key, alpha);
    } else {
        const int lastRow = metric.rowsIn(detail.h) - 1;
        drawCellText(target, detail, metric, 0, lastRow, "NO PLACE ON THE PLAN FOR THIS", ink.dim,
                     alpha);
    }
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

    // THE COUNT, WORDED HONESTLY. "READ 4 OF 9 IN THE BOOK" and not "4 OF 12":
    // twelve is how many leads casebook.json holds and the player has no way to
    // know that number, so printing it would tell them how much they have not
    // found -- which is the one thing an investigation must not hand over.
    const std::vector<PanelFact> facts{
        PanelFact{"LEADS READ", std::to_string(state.read) + " OF " + std::to_string(state.known) +
                                    " IN THE BOOK",
                  InkRole::Number},
        PanelFact{"STILL WAITING", std::to_string(std::max(0, state.known - state.read)),
                  InkRole::Number},
        PanelFact{"DEAD ENDS", std::to_string(state.cold), InkRole::Dim},
        PanelFact{"THEY CALL YOU", state.calledYou, InkRole::Prose},
    };
    drawFacts(target, panes[4], metric, facts, -1, alpha);

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
        drawStipple(target, rest, metric, ink.rule, 0.22F * alpha);
    }
    drawCommitVerb(target, detail, metric, "LEFT - BACK TO THE LEADS", "", ink.key, alpha);
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
    out.perScreen = std::max(1, plan.columns * plan.rows);
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
                      {spanCells(1), spanCells(1), spanCells(5), spanCells(1), spanWeight(1)});
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
    // A FULL TAKEOVER, so the ground is dense. The map page paid a capture to
    // learn this: at the vocabulary's default the ward's own hanging signage
    // reads straight through a column of text.
    style.groundAlpha = 0.99F;

    PanelFrame frame(target, comp.bounds, metric, style);
    for (const int r : comp.ruleRows) {
        frame.addRule(r);
    }
    if (comp.body.split) {
        frame.addDivider(comp.body.dividerCell, comp.bodyRow, comp.bodyRows);
    }
    frame.draw();

    // --- the tab row -------------------------------------------------------
    // NO PRINTED KEYS ON THE TABS. See the header: the digits are the leads'
    // and TAB is the key that closes the book. LEFT/RIGHT step the views and
    // the nav band says so, rather than a tab advertising a hotkey that does
    // nothing -- the same call creation_page.cpp made for its stage tabs.
    const std::vector<PanelTab> tabs{PanelTab{"", "LEADS"}, PanelTab{"", "THE CASE"}};
    drawTabRow(target, frame.band(comp.tabRow, 1), metric, state.title, tabs,
               static_cast<int>(state.tab), state.readout, ink.accent, alpha);

    // --- the breadcrumb / instruction header -------------------------------
    const int count = static_cast<int>(state.rows.size());
    const int at = count > 0 ? std::clamp(state.cursor, 0, count - 1) : -1;
    std::vector<std::string> crumbs{state.title};
    if (!state.caseTitle.empty()) {
        crumbs.push_back(state.caseTitle);
    }
    Rgb leafInk = ink.prose;
    if (state.tab == CasebookTab::Leads && at >= 0) {
        const CasebookLeadRow& row = state.rows[static_cast<std::size_t>(at)];
        crumbs.push_back(row.brief.empty() ? row.place : row.brief);
        leafInk = stateAccent(row.state, row.close);
    } else if (state.tab == CasebookTab::Case) {
        crumbs.push_back("THE CASE");
    }
    drawBreadcrumb(target, comp.headerBand, metric, crumbs, leafInk, alpha);
    if (comp.headerRows > 1) {
        const PanelRect second{comp.headerBand.x, comp.headerBand.y + metric.cellH(),
                               comp.headerBand.w, metric.cellH()};
        if (!state.alert.empty()) {
            // A warning outranks a menu, and it lands on the header's second
            // row -- which the band holds open whether or not there is one, so
            // nothing below moves when a bouncer starts talking.
            drawCellText(target, second, metric, 0, 0, state.alert, Rgb{0.90F, 0.52F, 0.30F},
                         alpha);
        } else if (!state.instruction.empty()) {
            drawCellText(target, second, metric, 0, 0, state.instruction, ink.dim, alpha);
        }
    }

    // --- the master list ---------------------------------------------------
    const PanelRect listRect = listRectOf(comp);
    const std::vector<PanelOption> options = optionsFor(state.rows);
    const OptionListPlan plan = planOptionList(options, listRect, metric, listStyle());
    const CasebookPageScroll scroll = casebookPageScroll(state, target.width(), target.height());
    if (count > 0) {
        const int first = std::clamp(scroll.firstRow, 0, count);
        const int last = std::min(count, first + scroll.perScreen);
        const std::vector<PanelOption> page(options.begin() + first, options.begin() + last);
        drawOptionListPlanned(target, listRect, metric, page, at - first, plan, alpha);
    } else {
        // AN EMPTY BOOK IS WORDED. It happens for exactly one frame of one
        // session -- a casebook.json that failed to load -- and a blank pane
        // there would read as the page being broken rather than the file being
        // absent.
        drawCellText(target, listRect, metric, 0, 0, "NOTHING IN THE BOOK YET", ink.dim, alpha);
    }

    // DELIBERATE EMPTINESS, TEXTURED -- the same call the detail pane makes. A
    // twelve-row book in a thirty-row pane is composition, and the reference
    // leaves exactly this kind of area blank; but blank and BLACK are different
    // things, and the faint `.`/`'` field is what says the space was left rather
    // than forgotten.
    if (count > 0) {
        const int drawnRows = std::min(count, scroll.perScreen);
        const int spare = metric.rowsIn(listRect.h) - drawnRows;
        if (spare >= 3) {
            const PanelRect rest{listRect.x, listRect.y + metric.heightOf(drawnRows + 1),
                                 listRect.w, metric.heightOf(spare - 1)};
            drawStipple(target, rest, metric, ink.rule, 0.22F * alpha);
        }
    }

    const PanelRect indicator{comp.body.master.x,
                              comp.body.master.y + metric.heightOf(comp.bodyRows - kIndicatorRows),
                              comp.body.master.w, metric.cellH()};
    if (scroll.screens > 1) {
        drawCellText(target, indicator, metric, 0, 0,
                     "MORE  " + std::to_string(scroll.screen + 1) + "/" +
                         std::to_string(scroll.screens),
                     ink.dim, alpha);
    }

    // --- the detail pane ---------------------------------------------------
    if (comp.body.split) {
        if (state.tab == CasebookTab::Case || at < 0) {
            drawCaseDetail(target, comp.body.detail, metric, state, alpha);
        } else {
            drawLeadDetail(target, comp.body.detail, metric,
                           state.rows[static_cast<std::size_t>(at)], state, alpha);
        }
    }

    // --- global nav, below its own rule ------------------------------------
    const std::vector<PanelOption> nav = navOptionsFor(state);
    // A CELL OF AIR OFF THE RIGHT EDGE. drawOptionList SPREADS its columns so
    // the last one's content ends at the pane's right edge, which is right for a
    // pane and wrong for a band that ends at the frame's own `|`/`!` flicker --
    // "PUT IT DOWN|" reads as punctuated. Narrowed here rather than in the
    // shared primitive, because the spread is correct everywhere it is not
    // butted against a border.
    const PanelRect navRect{comp.navBand.x, comp.navBand.y,
                            std::max(0, comp.navBand.w - metric.cellW()), comp.navBand.h};
    drawOptionList(target, navRect, metric, nav, -1, navStyleOf(), alpha);
}

}  // namespace granadad::render
