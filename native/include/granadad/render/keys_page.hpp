#pragma once

// THE CONTROLS PAGE, DRAWN IN THE TERMINAL-PANEL REGISTER -- and the proof that
// panel.hpp's vocabulary actually composes a real screen at every window size.
//
// WHY THIS SCREEN AND NOT ANOTHER. The panes pass had to convert ONE existing
// surface rather than ship a library nothing had ever drawn with, and this one
// exercises more of the grammar than any other page in the build: a tab row
// with a sibling view and a right-aligned readout, an instruction header, a
// master/detail split, a numbered option list that must re-column, a common
// value column (the bound key), wrapped prose, aligned key/value facts, and a
// contextual verb at the foot of the detail pane. It is also the one full-page
// surface no other phase of this programme is contracted to redraw, so the
// foundation gets proved without two phases fighting over the same file.
//
// WHAT IT REPLACES, AND WHY THAT WAS WORTH DOING. The page was
// DialogueViewState fed to drawDialogue: a two-band conversation surface with a
// 3x4 topic grid at the bottom. Twenty-nine bindings through a nine-slot grid is
// FOUR PAGES, and docs/frames/panes/before-keys-960.png is what that looked
// like -- nine rows visible, "0 MORE (1/4)" in the corner, and a strip of
// street between the two bands doing nothing for anybody. A player looking for
// the map key had to turn pages to find out this game has one.
//
// The composition here shows every binding it can fit, in as many columns as
// the window affords, with the highlighted one explained in full beside the
// list. At 960x540 that is all twenty-nine on one screen with no page turn at
// all.
//
// NOT MODULAR. There is no drag, no resize and no saved arrangement -- see
// panel.hpp's header and the owner's own ruling. What varies here is the
// WINDOW: the same composition resolves against 320x180 and against 1920x1080,
// and the only thing that changes is how many columns the list takes and
// whether the detail pane fits beside it or the list takes the whole body.
//
// THIS IS A PURE RENDER SURFACE. It holds no state and knows nothing about
// Session; render::Session builds a KeysPageState out of its live
// ControlSettings and hands it over, exactly the way it already builds a
// DialogueViewState. Nothing here touches the simulation.

#include <string>
#include <vector>

#include "granadad/render/framebuffer.hpp"

namespace granadad::render {

/// One row of the page. A binding, or one of the handful of rows that are notes
/// about a mode rather than a key you can change.
struct KeysPageRow {
    /// What is bound, as the player would say it: "W", "MOUSE1", "MOUSE".
    std::string binding;
    /// What it does: "FORWARD", "USE", "QUICK BAR".
    std::string verb;
    /// The second binding, when there is one. Empty draws nothing.
    std::string alternate;
    /// The detail pane's prose for this row. Empty is legal and draws nothing,
    /// which is what a note row gets.
    std::string help;
    /// True when ENTER on this row would open the rebinder. A note about what
    /// W does WHILE A WIRE IS IN A LOCK is not a binding and cannot be changed,
    /// and the commit verb at the foot of the detail pane says so instead of
    /// offering a rebind that would do nothing.
    bool bindable = true;
    /// What the LEGACY one-line list prints for this row, when `binding` and
    /// `verb` do not simply concatenate into it.
    ///
    /// Session::keyRows() -- the flat vector<string> the old topic-grid page
    /// and two cases still read -- is DERIVED from these rows rather than
    /// written twice, so the two lists cannot come out different lengths and
    /// leave the cursor able to select a row the page cannot draw. Three rows
    /// need the override because their one-line wording is not their two-column
    /// wording: contextual traversal has no key at all ("WALK AT A LEDGE"),
    /// and the lockpicking notes carry a "LOCK:" prefix in the flat list that
    /// the two-column layout says once, in the family label, instead of on
    /// every row.
    std::string listRow;
    /// Which family the row belongs to, and therefore WHAT COLOUR IT IS. The
    /// reference gives every entity its own accent and fills the selected row
    /// in that accent rather than in one global highlight hue, so a page is
    /// scannable by colour before a word of it is read. See kGroup* below.
    int group = 0;
};

/// The families, and the order they are drawn in. Movement first because that
/// is what a player checks first, then the verbs they will actually press, then
/// the screens, then the quick bar, and the mode notes last because they are
/// not bindings at all.
inline constexpr int kKeysGroupMove = 0;
inline constexpr int kKeysGroupAct = 1;
inline constexpr int kKeysGroupScreen = 2;
inline constexpr int kKeysGroupQuick = 3;
inline constexpr int kKeysGroupNote = 4;
inline constexpr int kKeysGroupCount = 5;

/// Everything the page draws. A closed page draws nothing at all.
struct KeysPageState {
    bool open = false;
    /// The caller's own ease -- Session::panelAnim_, the same EasedToggle every
    /// overlay in this build shares one of. 0 draws nothing.
    float openAmount = 1.0F;

    /// Left of the tab row.
    std::string title = "CONTROLS";
    /// Right-aligned in the tab row, and it is the build: the version used to
    /// be burnt into the top-left corner of every captured frame, and this is
    /// where a player goes looking for it.
    std::string readout;
    /// The instruction header. Wraps to the frame.
    std::string instruction;

    std::vector<KeysPageRow> rows;
    /// Index into `rows`. The page scrolls by whole pane-fuls to keep it
    /// visible; it is never clamped here, because the cursor belongs to the
    /// caller.
    int cursor = 0;

    /// A bouncer's warning, or anything else that outranks a menu. Empty draws
    /// nothing.
    std::string alert;
};

/// Draws the whole page over a rendered frame.
void drawKeysPage(Framebuffer& target, const KeysPageState& state);

/// How many rows one screenful holds at this frame size, and which screenful
/// the cursor is on. Exposed because it is the page's own answer to "is this
/// list longer than the window", and a case can pin it -- and because at
/// 960x540 the answer is THIRTY-SIX, which is the number that makes the four
/// pages this surface used to have go away.
struct KeysPageScroll {
    int perScreen = 1;
    int screen = 0;
    int screens = 1;
    int firstRow = 0;
};
[[nodiscard]] KeysPageScroll keysPageScroll(const KeysPageState& state, int frameWidth,
                                            int frameHeight);

/// Which row of the bindings list a pixel lands on -- an index into
/// state.rows, or -1. THE POINTER PASS: the exact inverse of what
/// drawKeysPage drew, built out of the same composition, the same
/// whole-list plan and the same scroll (casebookLeadAtPixel's own shape),
/// through panel.hpp's optionListAt.
[[nodiscard]] int keysRowAtPixel(const KeysPageState& state, int frameWidth, int frameHeight,
                                 int px, int py);

}  // namespace granadad::render
