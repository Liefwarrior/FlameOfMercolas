#pragma once

// THE HEARING -- the Flame's bench, drawn as a page in the terminal register.
//
// THE RULING (Eli, 2026-09-02, verbatim, binding): "If the player is tagged
// as a criminal it should be like Daggerfall where you can go to court and
// you can face jail or execution (game over)." JUSTICE-SPEC sections 5 and 6
// are this file and its Session half: the court is A PAGE, not a room -- no
// map edit, no interior scene, no new art. The officer's line, the charge off
// the ledger, the three rows, the priest's weighing in the reference's own
// four-line check block, the judgment as an inverted-fill badge, the sentence
// in numbers, the priest's words. It draws into the Framebuffer exactly as
// the casebook and the controls page do, so the 3D overlay composites it
// unchanged.
//
// ---------------------------------------------------------------------------
// THE SHAPE: THE STACKED FRAME WITH A MASTER/DETAIL BODY
// ---------------------------------------------------------------------------
// UI-REFERENCE-TERMINAL.md's informed-choice layout, the shape the chargen quiz
// already draws ("answer left, consequence right"):
//
//   ◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
//   !THE MISSION -- A HEARING                                    DAY 4  23:40 |
//   ◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
//   !WATCHMAN CULL LAYS THE PAPER ON THE TABLE.                               |
//   |THE WARD SAYS YOU PUT CANNIC DOWN IN THE GILDED GULL. THREE SAW IT.      !
//   !THE PAPER ASKS FOR THE ROPE.                                             |
//   ◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
//   ![1 - I DID IT.]        !A CONFESSION IS WEIGHED AS IT IS GIVEN. THE      !
//   | 2 - I DID NOT.        |FLAME'S ANSWER IS FIXED BEFORE YOU SPEAK IT.     |
//   ! 3 - HEAR THE PAPER    !                                                 !
//   ◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
//   !ESC                                                                      |
//   ◆~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-◆
//
// The tab row is the breadcrumb (UI-EA-SPEC sec. 5, one header line), with
// the clock as the persistent readout. THE READING BAND under it is this
// page's one departure from the pause family's two-row header: the charge is
// three sentences and they are the whole reason the page exists, so the band
// holds four rows whether or not the charge wraps -- the panes hold their
// height, nothing jumps as the cursor moves. The body is the master/detail
// split; the detail pane swaps with the cursor (the consequence of the
// hovered plea), swaps to THE PAPER on row 3 (the "View X's stats" idiom --
// inspect before committing), and after the plea it becomes THE CHECK BLOCK:
//
//   [THE PRIEST WEIGHS]                               <- badge, inverted
//   24 THE FLAME + 6 TONGUE + 10 THE DOOR - 2 HEAT = 38
//   + 6 CONFESSED = 44                                <- the plea's own term
//   THE LINES: 55 SPARED  38 FINED  14 HELD           <- the threshold(s)
//   [FINED]                                           <- verdict, inverted
//   17 ROYALS.                                        <- the sentence, numbers
//   "I have read what you gave at this door..."       <- the priest
//
// stolen outright from the reference's Preaching Check: the check's name, its
// arithmetic, the threshold and the verdict, every input to the outcome, and
// nothing hidden behind a black box. Mechanics first, then consequence in
// prose. SPARED is the longer badge on the larger margin, like CRITICAL
// SUCCESS; the verdict's fill takes the judgment's own accent.
//
// ---------------------------------------------------------------------------
// THE GRAMMAR EXCEPTION, STATED (JUSTICE-SPEC 6.3)
// ---------------------------------------------------------------------------
// UI-EA law: the key that opened a page closes it; ESC backs out one layer.
// THE HEARING HAS NO OPENER AND NO BACK. ESC closes THE PAPER back to the rows
// and disarms an armed plea, and does nothing else; the ten world verbs'
// dismissOverlays() does not touch it; PAUSE still opens over it (a player can
// always quit the game) with WAIT refused ("THE PRIEST IS WAITING."). This is
// the build's first un-backable modal and it is declared here rather than
// discovered. The nav band says so: at rest it prints the one key it honours.
//
// Rows print their digits and digits pick what they print (chooseVisibleTopic's
// shape); ENTER/A confirms; the plea row is ARMED on the first press and
// CONFIRMED on the second (the QUIT pattern), so a leaned-on ENTER cannot
// plead. Every prompt is device-aware through the state's key fields, which
// Session fills off promptConfirmKey / promptBackKey.
//
// ---------------------------------------------------------------------------
// PURE RENDER
// ---------------------------------------------------------------------------
// This file holds no state, knows nothing about Session and never touches the
// simulation. Session builds a HearingPageState off the hashed HearingState on
// the crime ledger (sim/justice.hpp) and the bark tables, and hands it over --
// exactly as it does for the casebook page and the ward map. Drawing this page
// cannot plead: nothing here calls Tavern::plead().

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/render/framebuffer.hpp"
#include "granadad/render/panel.hpp"

namespace granadad::render {

/// Which pane the detail is showing. Three, and the cursor never moves the
/// frame between them: the panes hold their height.
enum class HearingView : std::uint8_t {
    /// Before the plea: the hovered row's consequence, in prose.
    Plea = 0,
    /// HEAR THE PAPER: the charge sheet as phrases, never numbers, and
    /// 0 - BACK to the rows.
    Paper = 1,
    /// After the plea: the check block, the verdict, the sentence, the priest.
    Judged = 2,
};

/// One row of the master list.
struct HearingRow {
    /// The printed hotkey. "1", "2", "3", "0".
    std::string key;
    /// "I DID IT." / "I DID NOT." / "HEAR THE PAPER" / "BACK" / "I HAVE
    /// NOTHING TO SAY." / the confirm row after the judgment.
    std::string label;
    Rgb accent = panelInk().accent;
};

/// Everything the page draws. A closed page draws nothing at all.
struct HearingPageState {
    bool open = false;
    /// The caller's own ease -- Session::panelAnim_, shared with every overlay.
    float openAmount = 1.0F;

    /// Left of the tab row: the breadcrumb.
    std::string title = "THE MISSION -- A HEARING";
    /// Right-aligned in the tab row: the day and the live clock.
    std::string readout;
    /// A bouncer's warning, or anything else that outranks a menu -- it takes
    /// the tab row while it lasts, as on every page.
    std::string alert;

    // --- the reading band -------------------------------------------------
    /// "WATCHMAN CULL LAYS THE PAPER ON THE TABLE."
    std::string laid;
    /// "THE WARD SAYS YOU PUT CANNIC DOWN IN THE GILDED GULL. THREE SAW IT." /
    /// "THE WARD HAS YOU FOR TWO LIFTS AND A CRACKED BOX."
    std::string charge;
    /// "THE PAPER ASKS FOR A CELL." / "THE HAND." / "THE ROPE."
    std::string asks;
    /// THE OFFICER WHO WALKED YOU IN, and what he says as he lays the paper
    /// (court.taken, out of the bark tables) -- drawn under the rows in the
    /// master pane's spare room, his name in the subject's accent and the
    /// line in dim ink: the Watch stands by the wall while the priest
    /// speaks. Empty on a record with no officer, and the pane keeps its
    /// stipple.
    std::string officerName;
    std::string officerSays;

    // --- the master list --------------------------------------------------
    HearingView view = HearingView::Plea;
    std::vector<HearingRow> rows;
    /// Index into `rows`. Never clamped here: the cursor belongs to the caller.
    int cursor = 0;
    /// The armed plea row (the QUIT pattern): its label already carries the
    /// "-- SURE? <key>" tail; this flags which one so the fill can show it.
    int armed = -1;

    // --- the detail pane, by view -----------------------------------------
    /// Plea view: the hovered row's consequence, wrapped to the pane.
    std::string consequence;
    /// Paper view: the sheet as phrases. Facts, not paragraphs.
    std::vector<std::string> paper;
    /// Judged view: the check block.
    std::string weighsBadge = "THE PRIEST WEIGHS";
    /// "24 THE FLAME + 6 TONGUE + 10 THE DOOR - 2 HEAT = 38"
    std::string arithmetic;
    /// "+ 6 CONFESSED = 44" / "+ 4 THE PRIEST IS A MAN = 42" -- empty on a
    /// hearing with no plea (mercy given once: nothing is weighed).
    std::string pleaTerm;
    /// "THE LINES: 55 SPARED  38 FINED  14 HELD" / "THE LINE: 24 MERCY"
    std::string lines;
    /// "SPARED" .. "THE ROPE". The verdict badge, in `verdictAccent`.
    std::string verdict;
    Rgb verdictAccent = panelInk().accent;
    /// "TWO NIGHTS. 17 ROYALS." / "BONDSWORN 5 DAYS." / "THE ROPE."
    std::string sentence;
    /// The priest's own words, out of the bark tables.
    std::string priest;

    // --- the nav band -----------------------------------------------------
    /// The page grammar's keys in the live device's vocabulary --
    /// promptConfirmKey's return motif or "A", promptBackKey's "ESC" or "B".
    std::string confirmKey;
    std::string backKey = "ESC";
    /// The one verb the back key does here, worded for the tutor tier: "THE
    /// ROWS" while the paper is open, "DISARM" while a plea is armed, empty
    /// (no back) otherwise -- the grammar exception, printed honestly.
    std::string backVerb;

    // --- UI-EA-SPEC sec. 2: the Law of Earned Text -------------------------
    float tutor = 0.0F;
    /// Contract (b): the commit beat, armed by the routing's ImpactPulse at
    /// the plea and at the sentence.
    float commitPulse = 0.0F;
};

/// Draws the whole page over a rendered frame.
void drawHearingPage(Framebuffer& target, const HearingPageState& state);

/// THE COMPOSITION, RESOLVED, WITHOUT DRAWING ANYTHING -- casebookPageMetrics'
/// own contract: the geometry is a pure function of the state and the window,
/// so a case can pin it at 320x180 and at 1920x1080 without rendering a pixel.
struct HearingPageMetrics {
    bool usable = false;
    /// False when the window was too narrow to hold both panes and the list
    /// took the whole body.
    bool split = false;
    PanelMetric metric;
    PanelRect bounds;
    PanelRect reading;
    PanelRect master;
    PanelRect detail;
    PanelRect nav;
    int masterCells = 0;
    int detailCells = 0;
    /// The reading band's rows, and the rows the three sentences want at this
    /// width. Wanted must never exceed held, or a charge arrives clipped.
    int readingRows = 0;
    int readingRowsWanted = 0;
    /// The body's held rows, and the rows the check block wants in the detail
    /// pane at this width -- the same must-fit claim for the priest's block.
    int bodyRows = 0;
    int detailRowsWanted = 0;
};
[[nodiscard]] HearingPageMetrics hearingPageMetrics(const HearingPageState& state,
                                                    int frameWidth, int frameHeight);

/// Which row of the master list a pixel lands on, or -1. The inverse of what
/// was drawn, built out of the same walk -- see panel.hpp's optionListAt.
[[nodiscard]] int hearingRowAtPixel(const HearingPageState& state, int frameWidth,
                                    int frameHeight, int px, int py);

/// The rows the body holds, whatever the view. ONE number for the page,
/// chosen once against the tallest thing any view can put in the detail pane
/// (the check block with a wrapped arithmetic line and a wrapped priest's
/// line), so that pleading does not move the frame's foot. Fits the 25-row
/// grid the smallest window (320x180) affords with the reading band and the
/// nav row around it.
inline constexpr int kHearingBodyRows = 14;
/// The reading band: three sentences, and the charge is allowed a second
/// line at a narrow pane.
inline constexpr int kHearingReadingRows = 4;

}  // namespace granadad::render
