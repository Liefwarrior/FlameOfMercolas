#pragma once

// THE TILED MENU -- Morrowind's own inventory screen, scaled to this game's
// four read-your-own-state pages.
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
// THIS DOES NOT HUG THE HUD'S CENTRE-CLEAR RULE, AND THAT IS DELIBERATE.
// dialogue_view.hpp's own header states the rule this file's sibling
// (drawDialogue) exists to prove: "the HUD hugs all four edges and leaves the
// centre completely clear", because a first-person conversation happens with
// the person you are talking to still on screen. A tiled overview of your own
// character sheet, map, letters and casebook is not a conversation with
// anybody standing in front of you -- there is nobody TO look at while it is
// up, the same way there is nobody to look at while Oblivion's or Skyrim's own
// full-screen inventory is open. Morrowind's own four panes cover most of the
// screen for exactly this reason, and Eli's brief was to draw the equivalent
// here, not to draw four small boxes squeezed into the old centre-clear
// budget four different pages already proved too small for real content (see
// the panel-density note below). So this is the one surface in the renderer
// that is EXEMPT from kHudEdgeFraction, and only while the tiled Menu
// specifically is open -- a live conversation (drawDialogue) is not exempt
// and never will be.
//
// TWO REGISTERS, NOT ONE -- Eli's own "even Daggerfall knew when to scale it
// back" half of the brief. Each panel's TITLE is drawn at hudScale(): the
// chunky, read-at-a-glance size this game's whole HUD uses, its own 90s
// identity, which Eli explicitly said he likes and wants kept. Every panel's
// BODY -- the rows, the prose, the picked-row detail -- is drawn at
// hudMinorScale(): the tighter, "reference material read deliberately" size
// hud.hpp's own polish-1 round introduced. A quarter of the screen is not
// enough room for a Character sheet or a Letters list at the primary register
// dialogue_view.cpp's single wide panel always used; hudMinorScale is what
// lets real information fit in it.
//
// FOCUS, NOT PAGES. #85's Menu used to be six pages a bumper flipped between
// one at a time. With four tiles drawn every frame there is no "page" left to
// flip -- so PagePrev/PageNext are repurposed (session.hpp's own note) to
// step which TILE has input focus instead: arrow keys, the printed numbers
// and ENTER all act on the focused tile, and the other three keep showing
// whatever they last did, unread but not reset -- the same way Morrowind's
// own four panes each hold their own scroll position while only one has the
// keyboard. drawFocusRing() below is the one shape this file uses to say "you
// are here": a bright border on the focused tile's own frame, reusing the
// identical accent colour dialogue_view.cpp's drawPickHighlight already means
// "this one is selected" with, so a player who has read one page of this game
// already knows what that colour means on this one.

#include "granadad/render/dialogue_view.hpp"
#include "granadad/render/framebuffer.hpp"

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
    /// The panel's own small motion, so the focused tile's row highlight
    /// breathes exactly the way a conversation's own picked topic does.
    float phase = 0.0F;

    // --- INNOVATION SPRINT (item #2): the focus swap eases, not snaps -----
    //
    // drawPanelFrame() used to pick its border colour and thickness off a
    // bare `focused` bool -- an instant cut the moment PagePrev/PageNext
    // moved focus from one tile to another. Each tile now carries its OWN
    // 0 (not focused) .. 1 (focused) amount instead of that bool, eased by
    // Session with a SHORT, SNAPPY render::EasedToggle -- quick enough to
    // feel responsive rather than sluggish, but never an instant swap. See
    // Session's own focus-anim fields for why each tile gets its own
    // instance rather than one shared value: all four can be mid-transition
    // at once during a fast double-tap of the bumper, and a shared value
    // would make the tile losing focus and the tile gaining it animate in
    // lockstep instead of independently.
    //
    // DEFAULTS MATCH `focus` ABOVE'S OWN DEFAULT (Journal), so a hand-built
    // state that never heard of this draws the identical hard-edged border
    // every pre-existing caller and test already expects.
    float characterFocus = 0.0F;
    float mapFocus = 0.0F;
    float lettersFocus = 0.0F;
    float journalFocus = 1.0F;
};

/// Draws all four tiles at once: Character top-left, Map top-centre, Letters
/// top-right, Journal full-width along the bottom. A closed Menu
/// (state.open == false) draws nothing, the same contract drawDialogue()
/// has.
void drawMenuTiles(Framebuffer& target, const MenuTileState& state);

}  // namespace granadad::render
