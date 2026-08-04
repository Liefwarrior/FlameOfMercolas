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
    /// S10: where the bloodletter trail stands and where it wants you next --
    /// "CASE 2/6 > THE DROWNED HOLD". Bottom-left, ONE row, immediately over
    /// the HP label. Empty only when casebook.json is missing.
    ///
    /// AN INVESTIGATION READOUT IS THE ELEMENT MOST LIKELY TO BECOME A PANEL IN
    /// THE MIDDLE OF THE SCREEN. It gets one row on an edge, the same deal the
    /// sack and the stealth line got, and the casebook proper opens in the
    /// conversation's own two bands where the centre is already proven clear.
    ///
    /// IT SITS AT y - 16*scale AND THAT IS A MEASUREMENT. The exclusion
    /// rectangle's lower edge is at 0.78 * height; the bottom-left stack's rows
    /// at y - 8 and y - 16 are below it at every resolution the game runs at,
    /// and the rows above that are not -- which drawRoom's own comment has said
    /// since S8 about the objective row. The one row S10 adds takes a slot that
    /// is provably outside the rectangle rather than adding a third violation
    /// to a stack that already had two.
    ///
    /// VERIFICATION GAP (S10): the guild row (y - 24) and the objective row
    /// (y - 32) still cross the rectangle at 320x180 and at 640x360 whenever
    /// they are non-empty. Pre-existing since S4, already documented at
    /// drawRoom, and NOT fixed here: the honest fix is a bottom band that spans
    /// the frame the way Barony's does, which is a layout change and not a
    /// sprint's tail end. It is why the case row went below them and not above.
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
};

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

/// Width in pixels a string would occupy at a scale.
[[nodiscard]] int textWidth(std::string_view text, int scale) noexcept;

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
