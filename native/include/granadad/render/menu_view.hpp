#pragma once

// THE TILED MENU -- Morrowind's own inventory screen, scaled to this game's
// four read-your-own-state pages, DRAWN IN THE TERMINAL-PANEL REGISTER.
//
// Eli, having just looked at a real Morrowind screenshot: "I like how we've
// got a UI that's nice and chunky like from the 90s, BUT even Daggerfall knew
// when to scale it back to fit more words on the screen. Fix the multi-page
// menu thing, just show them like Morrowind does." Morrowind's own layout:
// three roughly-square panels across the top (Stats, Map, Magic) plus one
// full-width panel along the bottom (Inventory), all visible SIMULTANEOUSLY,
// no paging between them. That is what this file draws: Character top-left,
// Map top-centre, Letters top-right, Journal full-width along the bottom.
//
// ON THE GRAMMAR NOW, NOT BESIDE IT. This was the last surface in the build
// still drawing hairline rectangles -- four solid one-pixel boxes, a private
// palette, a title register of its own -- while every page a bumper away from
// it (the casebook page, the map, the keys page, creation) had been converted
// to docs/design/UI-REFERENCE-TERMINAL.md's vocabulary. It is converted here,
// and it is ONE frame rather than four: the reference's own "one full-screen
// frame, divided into stacked panels by horizontal rules" -- a single
// `+~-~-` bordered composition with `◆` junctions, ONE interior rule cutting
// the Journal band off the bottom, TWO interior dividers cutting the top band
// into three tiles, and the `|`/`!` alternation running through the outer
// edges and both dividers alike, because the texture is the frame and not a
// decoration on each box.
//
// ONE METRIC, NOT TWO -- panel.hpp's own stated narrowing of what this file
// used to do ("two glyph sizes on one surface means two cell grids, and two
// cell grids is how a column stops lining up with the rule above it"). A
// tile's title takes its emphasis from INVERSION instead of from hudScale: the
// speaker's name knocked out of an accent fill, the reference's own idiom,
// brightest on the tile that holds the keyboard. The rest of Eli's "even
// Daggerfall knew when to scale it back" brief is kept where it always was:
// everything is at hudMinorScale, which is what panelMetric() hands out.
//
// THIS DOES NOT HUG THE HUD'S CENTRE-CLEAR RULE, AND THAT IS DELIBERATE.
// dialogue_view.hpp's own header states the rule this file's sibling
// (drawDialogue) exists to prove: "the HUD hugs all four edges and leaves the
// centre completely clear", because a first-person conversation happens with
// the person you are talking to still on screen. A tiled overview of your own
// character sheet, map, letters and casebook is not a conversation with
// anybody standing in front of you -- there is nobody TO look at while it is
// up, the same way there is nobody to look at while Oblivion's or Skyrim's own
// full-screen inventory is open. So this is the one surface in the renderer
// that is EXEMPT from kHudEdgeFraction, and only while the tiled Menu
// specifically is open -- a live conversation (drawDialogue) is not exempt
// and never will be.
//
// FOCUS, NOT PAGES. #85's Menu used to be six pages a bumper flipped between
// one at a time. With four tiles drawn every frame there is no "page" left to
// flip -- so PagePrev/PageNext are repurposed (session.hpp's own note) to
// step which TILE has input focus instead: arrow keys, the printed numbers
// and ENTER all act on the focused tile, and the other three keep showing
// whatever they last did, unread but not reset -- the same way Morrowind's
// own four panes each hold their own scroll position while only one has the
// keyboard. The focused tile's badge is the bright inverted one and its
// cursor row wears the full accent fill; an unfocused tile keeps a DIM fill
// on the row its cursor sat on, so nothing is greyed out and nothing is
// dropped -- state moves the whole row together.
//
// A TILE'S PAGE IS ITS PANE. The old drawing capped every tile's list at
// dialogue_view.hpp's nine-topic page whatever the pane's height -- ten rows
// and a "0 MORE (1/2)" under twenty rows of black, which the ship note
// called the worst screen in the game. menuTilePageFor() below derives the
// page from the rows the pane actually holds: a list that fits is shown
// WHOLE, and a list that does not paginates by the pane instead of clipping.
// The keys 1-9 keep meaning exactly what Session's own direct-select
// arithmetic says they mean (page * kTopicPageSize + slot), so a printed
// number is always the number that picks that row -- rows outside the
// current nine-key window are simply shown unnumbered, reachable by the
// cursor, the same honesty casebook_page.cpp's kDirectSelectRows keeps.

#include <string>
#include <vector>

#include "granadad/render/dialogue_view.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/panel.hpp"

namespace granadad::render {

/// Which of the four tiles a field of MenuTileState is, and which one
/// currently has input focus. Fixed order, matching the on-screen layout:
/// Character (top-left), Map (top-centre), Letters (top-right), Journal
/// (bottom, full width) -- the order PagePrev/PageNext cycle through, and the
/// same order #85's old six-page Menu opened Journal/Character/Map/Letters
/// in, just without Keys/Options tacked on the end (see session.hpp's own
/// note on where those two went).
inline constexpr int kMenuFocusCharacter = 0;
inline constexpr int kMenuFocusMap = 1;
inline constexpr int kMenuFocusLetters = 2;
inline constexpr int kMenuFocusJournal = 3;
inline constexpr int kMenuFocusCount = 4;

/// Everything the tiled Menu draws: four panels' worth of content, reusing
/// the SAME DialogueViewState a single conversation panel already uses
/// (speaker/epithet/line/topics/cursor/page/caseRef/letter/letterLines all
/// mean exactly what they mean there -- see that struct's own header), which
/// tile currently has focus, and the shared open/close ease every overlay in
/// this build already shares one of (Session::panelAnim_).
struct MenuTileState {
    bool open = false;
    DialogueViewState character;
    DialogueViewState map;
    DialogueViewState letters;
    DialogueViewState journal;
    /// kMenuFocusCharacter/Map/Letters/Journal.
    int focus = kMenuFocusJournal;
    /// 0 (closed) .. 1 (open) -- see DialogueViewState::openAmount's own
    /// header; the identical contract, the identical default.
    float openAmount = 1.0F;
    /// The panel's own small motion clock, kept for contract compatibility
    /// with every caller that already fills it (Session hands it the same
    /// stepCount/60 every overlay gets). The converted tiles draw a settled
    /// frame off it -- a scripted capture still draws the same frame every
    /// time it is asked to.
    float phase = 0.0F;

    // --- nine and the sticks: the foot, in the device's own keycaps ------
    //
    // The hub had no foot at all, so the one thing a pad player had to learn
    // to leave it -- the bumpers page on to the ward map and the grimoire --
    // was printed nowhere. Four slots, the casebook page's own shape: the
    // cursor, the page ring, the pick, the close. THE DEFAULTS ARE THE
    // KEYBOARD'S LITERALS, so a hand-built state draws a keyboard foot.
    /// "\x02\x03" (the up/down triangles) or the d-pad cross.
    std::string navMoveKeys = "\x02\x03";
    /// "< >" or "LB RB" -- the ring (controls.hpp's promptPageKeys).
    std::string navPageKeys = "< >";
    /// The return motif, or "A".
    std::string confirmKey = "\x01";
    /// "J" (the live NOTES binding), or "B".
    std::string closeKey = "J";

    // --- INNOVATION SPRINT (item #2): the focus swap eases, not snaps -----
    //
    // Each tile carries its OWN 0 (not focused) .. 1 (focused) amount, eased
    // by Session with a SHORT, SNAPPY render::EasedToggle -- quick enough to
    // feel responsive rather than sluggish, but never an instant swap. In the
    // converted register the amount drives the tile's BADGE: its fill grows
    // brighter as focus arrives and dims as it leaves, the same job the old
    // border thickness lerp did on the hairline frames. See Session's own
    // focus-anim fields for why each tile gets its own instance rather than
    // one shared value: all four can be mid-transition at once during a fast
    // double-tap of the bumper.
    //
    // DEFAULTS MATCH `focus` ABOVE'S OWN DEFAULT (Journal), so a hand-built
    // state that never heard of this draws the identical settled emphasis
    // every pre-existing caller and test already expects.
    float characterFocus = 0.0F;
    float mapFocus = 0.0F;
    float lettersFocus = 0.0F;
    float journalFocus = 1.0F;
};

/// WHERE THE FOUR TILES SIT, at this window size, with no framebuffer
/// involved -- the same public-and-pure contract dialogueTopicLayout and
/// casebookPageMetrics keep, and for the same reason: the defect this
/// conversion closes was a LAYOUT defect, and a case must be able to state
/// the fix as a claim at every size the game runs at rather than trusting one
/// screenshot of one day.
struct MenuTileLayout {
    PanelMetric metric;
    /// The one bordered frame all four tiles share.
    PanelRect bounds;
    /// The four content panes, in kMenuFocus* order by name. Pixel rects
    /// inside the frame's interior; each starts at its own row 0.
    PanelRect character;
    PanelRect map;
    PanelRect letters;
    PanelRect journal;
    /// Interior rows the top band (the three tiles) holds.
    int topRows = 0;
    /// The interior row the horizontal rule between the bands sits on.
    int ruleRow = 0;
    /// NINE AND THE STICKS: the foot -- the interior row of the rule under
    /// the Journal, and the one-row nav band under that.
    int footRuleRow = 0;
    PanelRect nav;
    /// The interior cell columns the two vertical dividers sit on.
    int dividerCellA = 0;
    int dividerCellB = 0;
    /// False when the window is too small to compose four legible panes --
    /// the ground still darkens, nothing else draws.
    bool usable = false;
};

[[nodiscard]] MenuTileLayout menuTileLayout(int width, int height);

/// One printed row of a tile's list: the direct-select digit (empty for a row
/// outside the current nine-key window), the label as authored, and whether
/// the cursor is on it.
struct MenuTileRow {
    std::string key;
    std::string label;
    bool picked = false;
};

/// A TILE'S PAGE, derived from the pane rather than from a constant.
struct MenuTilePage {
    /// The rows actually shown, top to bottom.
    std::vector<MenuTileRow> rows;
    /// Index into `rows` of the picked one, or -1.
    int selected = -1;
    /// Which pane-sized screen is showing, and how many there are. One
    /// screen when the whole list fits.
    int screen = 0;
    int screens = 1;
    /// True when a MORE foot row is wanted under the rows -- the list did not
    /// fit, and one of `capacity`'s rows was spent saying so.
    bool more = false;
};

/// THE PANE-SIZED PAGINATOR. `capacity` is the rows the tile's list area
/// actually holds; a list of `capacity` or fewer topics is returned WHOLE
/// (no MORE row, one screen), and a longer one is windowed into screens of
/// `capacity - 1` rows anchored so the cursor is always on the screen shown.
///
/// THE PRINTED DIGITS STAY TRUTHFUL TO SESSION. Direct-select resolves
/// `page * kTopicPageSize + slot` (session.cpp's pickCursorIfVisible), and
/// `page` tracks the cursor (`page == cursor / kTopicPageSize` by
/// wrapCursorAndPage/advancePage), so the nine rows of THAT window -- and
/// only those -- carry digits. The cursor's own row is always inside both the
/// window and the screen, so at least one printed number is always on show.
///
/// PURE, same contract as planOptionList: no framebuffer, so a case can pin
/// "twelve topics in a twenty-seven-row pane is twelve rows and no MORE" and
/// "twelve topics in a six-row pane is three screens" without rendering a
/// pixel.
[[nodiscard]] MenuTilePage menuTilePageFor(const std::vector<std::string>& topics, int page,
                                           int cursor, int capacity);

/// Draws all four tiles at once: Character top-left, Map top-centre, Letters
/// top-right, Journal full-width along the bottom. A closed Menu
/// (state.open == false) draws nothing, the same contract drawDialogue()
/// has.
void drawMenuTiles(Framebuffer& target, const MenuTileState& state);

/// THE POINTER PASS: what a pixel of the tiled Menu is pointing at.
struct MenuTileHit {
    /// kMenuFocus* of the pane the pixel is inside, or -1 outside all four.
    int tile = -1;
    /// The list row under the pixel as an ABSOLUTE index into that tile's
    /// topics (the same index Session's cursor holds), or -1 -- in the pane
    /// but not on a row, an open letter, a tile with nothing listed.
    int row = -1;
    /// True when the pixel is on the tile's unkeyed MORE foot.
    bool more = false;
};

/// The inverse of drawTile's list, through the SAME walk: menuTileLayout for
/// the panes, the badge/epithet/journal-prose row spend for where the list
/// starts, menuTilePageFor for which screenful is showing, and panel.hpp's
/// optionListAt against the identical whole-list plan the drawing uses. See
/// optionListAt on why the inverse of a layout lives beside the layout.
[[nodiscard]] MenuTileHit menuTileHitAtPixel(const MenuTileState& state, int width, int height,
                                             int px, int py);

}  // namespace granadad::render
