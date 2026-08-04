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
    /// One line about the room the player is standing in. Bottom-right.
    std::string_view roomLabel;
    /// Something said to the player that they need to have heard -- a
    /// bouncer's warning. Bottom edge, centred horizontally but well below the
    /// exclusion zone.
    std::string_view alert;
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

}  // namespace granadad::render
