#pragma once

// The HUD, which hugs the edges and leaves the middle alone.
//
// THE RULE, and it is not a preference (COMBAT-FEEL-REFERENCE.md section 3,
// task #66): the HUD hugs all four edges and leaves the centre completely
// clear. Barony pins HP bottom-left, hotbar bottom-centre, XP top-centre,
// minimap bottom-right, and NOTHING floats in the play space. The Java build's
// first-person view failed exactly here — its inspector sheet and craftings bar
// ate the right half of the screen — so this file carries an explicit
// centre-clear rectangle and hudCentreIsClear() exists so a test can prove the
// rule rather than trusting it.
//
// S1 shipped two elements: health bottom-left, compass top-centre. S2 adds
// three, all of them ON an edge and none of them anywhere near the middle:
// the clock and the purse top-right, the room's own line bottom-right, and a
// bouncer's warning across the very bottom. The exclusion zone is what stops
// them creeping inwards, and the test that checks it goes red if they do.
//
// ---------------------------------------------------------------------------
// polish-1: THE SAME HUD, IN LESS OF THE FRAME.
//
// Ten sprints added ten rows and every one of them was drawn at the same size
// as the health bar, so a rooftop frame at 960x540 carried four rows of sky
// eaten in the top right, a two-row block in the top centre and a build banner
// in the top left. The look is not the problem and is not being changed: same
// 4x6 font, same colours, same corners, same rule about the middle. Three
// things are:
//
//   1. TWO SIZES, NOT ONE. hudScale() is the register the player reads at a
//      glance -- the compass, the hour, the health bar, a shout. Everything
//      else is reference material they look at deliberately, and it is drawn
//      at hudMinorScale(), one step down. That is a hierarchy rather than a
//      shrink: the frame says what to read first.
//
//   2. ABSENCE COSTS NOTHING. A row that says nothing happened does not get a
//      row. "NOBODY IN PARTICULAR" is the ward having no opinion of you, and
//      it held 20 characters of the top right in every frame of the game.
//
//   3. ROWS ARE ALLOCATED, NOT NUMBERED. Every element used to carry its own
//      hand-computed offset in scale units, which is how S9 shipped the lock
//      row printed through the guild row and S10 shipped a clue printed
//      through the case row. The bottom band hands out SLOTS in priority
//      order now, and the top-right stack drops its least important row rather
//      than crossing the exclusion rectangle. Two things can no longer be
//      given the same pixel row by arithmetic that nobody re-checked.
//
// Measured, not estimated. `granadad --nohud` draws the same scene with no
// interface on it at all, so every pixel that differs between the two captures
// is interface -- see docs/HUD-REAL-ESTATE.md for the commands and the script.
// At 960x540, interface ink as a fraction of the frame:
//
//     street        5.80%  ->  2.91%
//     rooftops      6.89%  ->  4.11%
//     conversation 42.96%  -> 36.11%
//
// and the area those rows CLAIM -- glyphs closed up into the rows that own
// them, which is the space actually lost -- 8.19% -> 5.13% on the street.

#include <cstdint>
#include <string>
#include <string_view>

#include "granadad/render/framebuffer.hpp"

namespace granadad::render {

/// What the HUD is told about the player. Nothing here is authoritative — the
/// simulation owns all of it and the HUD only draws it.
struct HudState {
    int health = 100;
    int healthMax = 100;
    /// Off while a conversation is open. The bottom band is a topic list then,
    /// and two things fighting over the same forty pixels is how the centre-
    /// clear rule gets broken by accident. Punching closes the conversation, so
    /// the bar is back before it can ever matter.
    bool showHealth = true;
    /// Same rule at the other edge: the top band belongs to whoever is talking
    /// to you, so the compass ribbon and the place name stand down. The clock
    /// and the purse stay, because a price is being discussed and the hour is
    /// why the doors are open.
    bool showCompass = true;
    /// BAM facing, straight off the body.
    std::int32_t yawBam = 0;
    /// Shown under the compass. Empty draws nothing.
    std::string_view locationLabel;
    /// Seconds since midnight. Drawn top-right as HH:MM. Negative draws nothing.
    int timeOfDaySeconds = -1;
    /// The purse, top-right under the clock. Negative draws nothing.
    int coin = -1;
    /// What the ward as a whole thinks of the player, top-right under the
    /// purse. Reputation READABLE rather than hidden. Empty draws nothing.
    std::string_view standingLabel;
    /// S5: what the Watch has heard, what is in your coat, and whether you are
    /// carrying somebody else's bale. Top-right under the standing, still
    /// hugging the edge. Empty draws nothing, which is the usual case.
    std::string_view heatLabel;
    /// S6: what is in the sack and what it weighs -- "3 FLOWER  24DR".
    /// Top-right under the heat, still hugging the edge. Empty draws nothing.
    ///
    /// It is on the EDGE and not in a sheet on purpose. The Java build's
    /// first-person view failed exactly here: its inspector ate the right half
    /// of the screen. An inventory in this game is one line in a corner until
    /// it has earned more.
    std::string_view stashLabel;
    /// The ladder the player is highest on, and the rung: "FLAME - DISCIPLE".
    /// Bottom-left, stacked over the health bar, because that is where a
    /// character's own state lives and the centre stays empty. Drawn only when
    /// the player is on a rung at all.
    std::string_view guildLabel;
    /// What the current questline wants next, in the journal's own words.
    /// Bottom-left under the guild, and clipped to a single line.
    std::string_view objectiveLabel;
    /// S8: the man who has put the player on the floor most often, what he
    /// answers to now, and whether he is looking for them -- "RIVAL BRAM
    /// MARROW - CRAFTLORD x2  HUNTING". Bottom-left under the objective, still
    /// on the edge. Empty draws nothing, which is the usual case: nobody has
    /// beaten you yet.
    std::string_view rivalLabel;
    /// S9: whether the room can currently make the player out, how much light
    /// is falling on them and how much noise they are making -- "HIDDEN CROUCH
    /// DARK 12  QUIET". Top-right under the sack, still hugging the edge.
    ///
    /// It is a LINE ON AN EDGE and not a meter in the middle for the same
    /// reason the sack is one line: the centre stays empty, and a stealth
    /// indicator is exactly the kind of element that creeps inwards.
    std::string_view stealthLabel;
    /// S9: the lock under the wire -- pins set, where the pick is being held,
    /// strain on it and how many picks are left. Bottom edge, above the alert
    /// row, and empty whenever no lock is open.
    std::string_view lockLabel;
    /// #85. THE RESOLVED VERB Interact is about to run -- "[E] TALK" changing
    /// to "[E] PICKPOCKET" the instant the player crouches facing somebody.
    /// Bottom edge, above the alert row: the element the whole consolidation
    /// exists to make honest, so it sits where the alert (the loudest thing
    /// on this edge besides a bouncer's warning) already trains the eye to
    /// look. Empty draws nothing -- see Session::interactPrompt() for when
    /// that is (a page already owns the keyboard, or nothing at all resolves
    /// within reach, which cannot happen: LOOK is always the floor).
    std::string_view interactLabel;
    /// S10: where the bloodletter trail stands and where it wants you next --
    /// "CASE 2/6 > THE DROWNED HOLD". Bottom-left, ONE row. Empty only when
    /// casebook.json is missing.
    ///
    /// AN INVESTIGATION READOUT IS THE ELEMENT MOST LIKELY TO BECOME A PANEL IN
    /// THE MIDDLE OF THE SCREEN. It gets one row on an edge, the same deal the
    /// sack and the stealth line got, and the casebook proper opens in the
    /// conversation's own two bands where the centre is already proven clear.
    ///
    /// polish-1 CLOSED THE S10 GAP THIS COMMENT USED TO CARRY. The guild and
    /// objective rows were drawn at hand-written offsets of y - 24 and y - 32
    /// scale units and crossed the exclusion rectangle at 320x180 and 640x360
    /// whenever they were non-empty -- a defect nothing tested, because no case
    /// ever populated them. They are allocated out of the bottom band's slot
    /// grid now, every slot in it is proven below the rectangle before anything
    /// is drawn in it, and a row that has no legal slot is DROPPED rather than
    /// drawn in the play space. The test that proves it lights every field in
    /// this struct at once at three resolutions.
    std::string_view caseLabel;
    /// One line about the room the player is standing in. Bottom-right.
    std::string_view roomLabel;
    /// Something said to the player that they need to have heard -- a
    /// bouncer's warning. Bottom edge, centred horizontally but well below the
    /// exclusion zone.
    std::string_view alert;
    /// Off while a conversation is open, for exactly the reason showHealth is.
    ///
    /// S7 SHIPPED THIS AS A DEFECT AND CALLED THE FRAME PROOF. The alert is
    /// drawn at `height - margin - 23*scale`; the dialogue's bottom band starts
    /// at `height - margin - rowStep*6` and its rows are drawn from there; the
    /// HUD is drawn AFTER the panel, so the warning won and row two of the
    /// topic grid became unreadable sludge in docs/frames/s7-skyrun.png.
    ///
    /// The warning is not dropped when this is false -- it moves into the
    /// conversation's own top band (DialogueViewState::alert), which is sized
    /// from what it draws. Nothing is lost and nothing overlaps.
    bool showAlert = true;
    /// TASK #83. 0 (gone) .. 1 (full strength). Session eases this with
    /// render::EasedToggle instead of the alert popping onto the frame at full
    /// brightness the instant there is one and popping off the instant
    /// messageSteps_ hits zero -- an interaction prompt that appears and
    /// dismisses smoothly rather than one that flicks on and off. Multiplies
    /// only the alert row's own alpha; every other HUD row is unaffected.
    /// Defaults to 1, which is what every hand-built HudState already meant
    /// before this field existed, so `* alertFade` is a no-op for a caller
    /// that never heard of it.
    float alertFade = 1.0F;
    /// HARDENING PASS. THE SAME alertFade PATTERN, ONE PER ROW. #83 eased the
    /// alert row in and out and left every other row on this HUD popping --
    /// see this file's own header on the rule these rows draw under and
    /// Session::syncPanelAnim() for how each of these is driven by its OWN
    /// render::EasedToggle, not a shared one, because interactLabel and
    /// stealthLabel (for instance) appear and disappear on completely
    /// unrelated triggers. Each defaults to 1, the same no-op reasoning as
    /// alertFade: a HudState nobody eased draws exactly as it always has.
    float interactFade = 1.0F;
    float lockFade = 1.0F;
    float caseFade = 1.0F;
    float roomFade = 1.0F;
    float rivalFade = 1.0F;
    float guildFade = 1.0F;
    float objectiveFade = 1.0F;
    float stealthFade = 1.0F;
};

/// The size the HUD's own register is drawn at: the compass, the hour, the
/// health bar, a shout. Scaled off the frame HEIGHT so the chunky 4x6 font
/// keeps the same apparent size at every resolution.
[[nodiscard]] int hudScale(int height) noexcept;

/// One step down, and the size everything the player looks at DELIBERATELY is
/// drawn at: the place name, the purse, standing, heat, the sack, the stealth
/// line, the case, the rung, the objective, the rival, the room.
///
/// It is a hierarchy and not a shrink. Ten sprints put ten rows on this HUD and
/// drew every one of them at the size of the health bar, so the frame had no
/// way to say what to read first and the rows that said the least were as loud
/// as the ones that said the most. At 960x540 this is an 8x12 glyph, which is
/// the size the whole HUD is at 640x360 -- a resolution the game already calls
/// legible -- and at 320x180 it is the same as hudScale(), because there is
/// nothing below a 4x6 font to step down to.
[[nodiscard]] int hudMinorScale(int height) noexcept;

/// How wide the top-right stack is at its widest, so the conversation surface
/// can wrap clear of it instead of guessing. A spoken line running under the
/// hour is unreadable and looks like a bug.
[[nodiscard]] int hudTopRightReserve(int height) noexcept;

/// The fraction of the screen, on each axis, that the HUD may occupy from an
/// edge. Anything between these on both axes is play space and must stay clear.
inline constexpr float kHudEdgeFraction = 0.22F;

/// The centre rectangle the HUD is forbidden from touching, in pixels.
struct CentreRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

[[nodiscard]] CentreRect hudCentreRect(int width, int height) noexcept;

/// Draws the whole HUD over a rendered frame.
void drawHud(Framebuffer& target, const HudState& state);

/// Draws a string in the 4x6 pixel font, top-left anchored. Returns the width
/// drawn. Public because the debug overlay and the capture stamp use it.
int drawText(Framebuffer& target, int x, int y, std::string_view text, const Rgb& colour,
             float alpha, int scale);

/// HARDENING PASS. THE S8 "NES POP-UP" TREATMENT (docs/design/PLAYTEST-LOG.md's
/// S8 entry), CARRIED OVER FROM client-observer -- retired whole in f712579
/// ("the observer's old Java client and its CLI runner, gone") the day before
/// this pass, so the treatment is real and shipped once but the class that
/// drew it (PlaceSignArt.java) no longer exists in this tree. Its exact
/// constants (pure #000000 field, a flat bone border, no gradient, no fade)
/// are pulled from that file's own git history rather than re-guessed, and
/// reused here as this engine's kPlateBlack/kPlateBone.
///
/// A hard-edged solid field plus a flat border of `border` px, sized to
/// exactly the rectangle the caller hands it -- draw this FIRST, then the
/// text over it, the identical fill-then-border-then-text order the retired
/// renderer used so the border is never eaten by the fill. `alpha` is the
/// caller's own fade (signage recedes by distance, the alert row eases with
/// HudState::alertFade); the original popup never needed one because it
/// snapped on and off outright rather than easing, but both of this pass's
/// callers do.
void drawTextPlate(Framebuffer& target, int x0, int y0, int x1, int y1, int border,
                    float alpha);

/// Width in pixels a string would occupy at a scale.
[[nodiscard]] int textWidth(std::string_view text, int scale) noexcept;

/// True when the 4x6 font has a glyph for this character. Lowercase is drawn
/// as uppercase, so it answers for both.
///
/// A CHARACTER WITH NO GLYPH IS A HOLE, NOT AN ERROR. drawText advances the
/// cursor for it and draws nothing, so text with one in it comes out looking
/// like a rendering glitch rather than like the copy defect it is, and nothing
/// goes red. The font carries exactly the characters the authored barks use --
/// see the table in hud.cpp -- which does NOT include '[', ']' or '_', and
/// every one of those had found its way into a line the player reads: seven
/// bracketed receipts in dialogue.cpp, and the spell workbench printing the
/// raws' own OVER_TIME and WHILE_ACTIVE keys straight onto the panel.
///
/// Public so that test_copy.cpp can walk every player-facing surface in the
/// game and assert this of all of it.
[[nodiscard]] bool isDrawableGlyph(char c) noexcept;

/// Cuts a string to what fits in `pixels` at this scale, and marks the cut.
///
/// EVERY HUD LINE THAT IS NOT ANCHORED TO AN EDGE GOES THROUGH THIS. A line
/// anchored right or left is clamped by its anchor; a CENTRED line is not, and
/// the alert is the only centred line the HUD has. S6 shipped a 68-character
/// bouncer warning into it at 1280x720 -- roughly 1,380 pixels of text in a
/// 1,280-pixel frame -- and the last two words were drawn off the edge, cut
/// mid-glyph. docs/frames/s6-skyrun-quiet.png is the evidence.
///
/// Callers used to clip to a column count they guessed (Session::say's 56), and
/// a second caller that did not know about the guess is exactly how the bug
/// got in. The frame knows its own width; this takes it.
[[nodiscard]] std::string clipToWidth(std::string_view text, int pixels, int scale);

}  // namespace granadad::render
