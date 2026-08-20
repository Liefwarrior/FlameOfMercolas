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
//
// ---------------------------------------------------------------------------
// THE CROSSHAIR PASS: THE ONE ELEMENT THAT IS ALLOWED IN THE MIDDLE, AND WHY.
//
// The owner played the build and said, verbatim: "The 'E' button shouldn't have
// that label text be at the bottom of the screen. It needs to be improved to be
// properly contextual and when shown hover a bit to the top-right of the center
// crosshair."
//
// That is a direct instruction to break the rule at the top of this file, in
// one place, deliberately. So it is broken in exactly one place, it is named
// (hudAimRect below), it is CLAMPED to that rectangle rather than trusted to
// stay in it, and the old guarantee still holds for every other row: with the
// aim fields empty, drawHud() puts nothing at all inside hudCentreRect, and
// test_render.cpp still proves it with all sixteen other rows lit at once.
//
// Two things go in there and they are one element:
//
//   1. THE RETICLE. There was not one. The owner's sentence assumes a "center
//      crosshair" and the prompt has to hang off something, so the aim point
//      is now marked -- four short ticks around an open centre, so the pixel
//      you are actually aiming at stays visible. It takes the subject's own
//      accent when something is in reach and sits dim when nothing is, which
//      is the reference frame's "the spatial view HIGHLIGHTS the current
//      interaction target" (docs/design/UI-REFERENCE-TERMINAL.md) in the
//      cheapest form this view can carry.
//
//   2. THE AIM PROMPT, up and to the right of it. NOT a framed pane -- the
//      spec's own "what NOT to carry" closes with "full-screen takeover for
//      things that should be glanceable... the crosshair prompt and the alert
//      row must stay light", so there is no border motif on this, no plate and
//      no panel grid. Two rows of the ordinary 4x6 font:
//
//          GERTA SALTCOTTE  BARTENDER      <- what you are looking at
//          E  TALK                         <- what the key will do to it
//
//      The VERB ROW IS THE ANCHOR and never moves; the subject row appears
//      above it. That is the reference's "panes hold their height" applied to
//      the smallest surface in the game: sweeping the crosshair across a
//      doorway must not make the verb jump a row.

#include <array>
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
    /// FATIGUE BUILD: the wind, in points, drawn as a slimmer second bar
    /// tucked in the margin DIRECTLY BELOW the health bar -- beside the
    /// number it reads with, and moving no other element to get there. A
    /// non-positive fatigueMax draws nothing, so every hand-built HudState
    /// that predates the bar is pixel-identical. Continuous gradient off its
    /// own ramp (amber wind cooling to a spent grey-blue -- see
    /// fatigueColor()'s note in hud.cpp), same healthColor discipline: the
    /// fill fraction drives colour and segments off the identical clamp.
    int fatigue = -1;
    int fatigueMax = -1;
    /// The bar's own ease, driven by a Session-side EasedToggle mirroring
    /// the health bar's visibility rule. 1 by default: a caller that never
    /// heard of it draws at full strength, the alertFade contract.
    float fatigueFade = 1.0F;
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
    /// FIRST-PERSON COMBAT (S13): the crafting the next press of Cast will
    /// spend -- "CAST  STING", with "(12S)" riding the same line while the
    /// link cools. Top-right under the sack, one line on the edge, because a
    /// readied spell is exactly the element that creeps into being a hotbar
    /// panel. Empty draws nothing: the grimoire is empty at spawn and the
    /// row appears the moment there is a crafting to ready.
    std::string_view spellLabel;
    /// HELD-EFFECTS BUILD: every crafting currently HELD on the player --
    /// "STEADY THE HAND 842S", name and seconds left, counting down
    /// continuously. Top-right stack under the CAST row, one row per live
    /// hold, at most four: a fifth simultaneous hold is not composable off
    /// the authored shelf, and an edge stack is not becoming a buff panel.
    /// Empty rows draw nothing, which is the usual state of all four --
    /// every hand-built HudState that predates these fields is
    /// pixel-identical.
    std::array<std::string_view, 4> effectLabels{};
    /// One fade per row, the alertFade contract: defaults keep a caller that
    /// never heard of them at full strength.
    std::array<float, 4> effectFades{1.0F, 1.0F, 1.0F, 1.0F};
    /// FIRST-PERSON COMBAT (S13): "GUARD UP" exactly while the room's own
    /// held-block state is true. Bottom band, centred, right behind the lock
    /// in priority -- a held guard is read every second of a fight, and it
    /// is the ROOM's fact (what tickBrawl actually reads), never the key's.
    std::string_view blockLabel;
    /// SPELLS BUILD: the quick bar strip -- BOTTOM-CENTRE, which is this
    /// file's own header giving Barony's hotbar its place ("hotbar
    /// bottom-centre"). Ten cells out of the bottom band's slot grid, never
    /// the play space: a filled slot's digit is bright, an empty one dim, the
    /// slot holding the EQUIPPED crafting is inverted (the one source of
    /// truth is Tavern's equipped id, the same id the CAST row reads), the
    /// selected slot is framed, and the selected slot's own name rides the
    /// same row. TRANSIENT, not furniture: quickBarFade is a Session-side
    /// EasedToggle that raises it while the wheel or the number row is in
    /// use and puts it down a couple of seconds after.
    std::array<std::string_view, 10> quickSlots{};
    int quickSelected = -1;
    int quickEquipped = -1;
    float quickBarFade = 0.0F;
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
    /// THE CROSSHAIR PASS. THE AIM PROMPT, and it is FOUR fields rather than
    /// one composed string because the three of them are coloured by three
    /// different roles and the fourth is not text at all.
    ///
    /// #85 shipped this as a single "E  TALK" row centred along the bottom
    /// edge. It was honest and it was in the wrong place -- the owner's own
    /// sentence is at the top of this file. What replaced it hangs off the
    /// reticle (hudAimRect) and NAMES ITS OBJECT:
    ///
    ///     aimSubject   "GERTA SALTCOTTE"   what is in reach, in its own accent
    ///     aimNote      "BARTENDER"         the qualifier, dim, beside it
    ///     aimVerb      "TALK"              what the key runs, in the key colour
    ///     aimKey       "E"                 the binding, drawn as a key cap
    ///
    /// aimVerb empty draws NOTHING AT ALL, reticle included -- see
    /// Session::interactTarget() for when that is (a page already owns the
    /// keyboard). aimSubject empty draws the verb row alone, which is the
    /// honest floor: LOOK at ground with nothing authored on it names nothing,
    /// because inventing a name for it would be the machine talking.
    std::string_view aimKey;
    std::string_view aimVerb;
    std::string_view aimSubject;
    std::string_view aimNote;
    /// Which accent the subject and the reticle take. See AimKind.
    int aimKind = 0;
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
    /// render::EasedToggle, not a shared one, because the aim prompt and
    /// stealthLabel (for instance) appear and disappear on completely
    /// unrelated triggers. Each defaults to 1, the same no-op reasoning as
    /// alertFade: a HudState nobody eased draws exactly as it always has.
    /// THE AIM PROMPT'S OWN EasedToggle, as it always was -- the field kept
    /// its name through the crosshair pass because the toggle behind it
    /// (Session::interactAnim_) is the same one, driven off the same verb.
    float interactFade = 1.0F;
    float lockFade = 1.0F;
    float caseFade = 1.0F;
    float roomFade = 1.0F;
    float rivalFade = 1.0F;
    float guildFade = 1.0F;
    float objectiveFade = 1.0F;
    float stealthFade = 1.0F;
    /// PLANNING SPRINT (item #2, the sweep). THE SAME PATTERN, FOR THE THREE
    /// ROWS THAT WERE STILL MISSING IT. standingLabel/heatLabel/stashLabel
    /// used to snap on hud.hpp:73-105's own `conversing` bool at full
    /// strength while stealthFade (right above) already eased its neighbour
    /// -- a real sweep for the class of bug the prior two sprints fixed found
    /// them. Same no-op-by-default reasoning as every *Fade field above.
    float standingFade = 1.0F;
    float heatFade = 1.0F;
    float stashFade = 1.0F;
    /// FIRST-PERSON COMBAT (S13). The same pattern for the two rows the
    /// Cast/Block task added, each on its own Session-side EasedToggle.
    float spellFade = 1.0F;
    float blockFade = 1.0F;
    /// INNOVATION SPRINT ITEM #3. 1 the instant a NEW bouncer's warning
    /// arrives, easing down to 0 over a handful of frames -- see
    /// render::ImpactPulse's own header and Session::alertPulse_'s. Unlike
    /// every *Fade field above (a level, held at whatever the row's own
    /// EasedToggle currently sits at) this is an EVENT: it grows the alert
    /// row's own legibility plate (drawTextPlate, this same sprint) a few
    /// pixels past its settled size for an instant and lets it ease back
    /// down, so the warning reads as having LANDED rather than merely having
    /// appeared. Defaults to 0, which is the plate's ordinary settled size --
    /// what every caller before this field existed drew, and what this one
    /// draws again a few frames after any warning.
    float alertPulse = 0.0F;
    /// DISTRICT PHASE D: THE THRESHOLD MOMENT. The name of the place the
    /// player has JUST CROSSED INTO -- "SALTGATE RISE" the step they walk out
    /// of the Quayward's east gate -- announced once, briefly, on a plate
    /// centred over the compass ribbon, and then gone.
    ///
    /// IT IS NOT A SECOND locationLabel AND IT MUST NEVER BECOME ONE.
    /// locationLabel (above) is REFERENCE: the sub-label of the compass, up
    /// every frame, dim, small, read when you want it. This is an EVENT --
    /// crossing a boundary -- and the whole of what it adds is that the
    /// crossing is legible AT THE MOMENT IT HAPPENS instead of only by
    /// noticing that a dim row two sizes down has quietly changed its words.
    /// Both are drawn from the same fact (sim::docks::placeNameAt); neither
    /// is derived from the other, because one is a state and the other is an
    /// edge, exactly the distinction anim.hpp draws between EasedToggle and
    /// ImpactPulse.
    ///
    /// TOP BAND, NOT THE MIDDLE, and that is this file's own rule and not a
    /// preference -- see the header. A place-name announcement is the single
    /// most obvious candidate in this game for a big centred title card in
    /// the play space, which is exactly why it gets a plate on the edge under
    /// the ribbon that already says where you are. It is DROPPED outright
    /// rather than drawn if the frame is too short to hold it clear of the
    /// exclusion rectangle -- BottomBand::take()'s own rule, applied at the
    /// other edge.
    ///
    /// Empty draws nothing, and so does a zero fade: every hand-built
    /// HudState that predates these three fields is pixel-identical.
    std::string_view placePlate;
    /// 0 (gone) .. 1 (full strength). Session's own EasedToggle per the
    /// settled convention (DECISIONS.md UI rule 1), NOT shared with any row:
    /// crossing a boundary has nothing to do with a purse changing.
    ///
    /// DEFAULTS TO 0, NOT 1. Every *Fade field above defaults to 1 because
    /// its row's ordinary state is "on screen"; this one's ordinary state is
    /// "not on screen at all", the same reasoning quickBarFade already
    /// defaults to 0 for.
    float placePlateFade = 0.0F;
    /// WHERE THE PLATE IS IN ITS OWN RISE, as a signed fraction of one lift:
    /// -1 is fully below its settled row (the instant of the crossing), 0 is
    /// settled, +1 is fully above it (the end of the fade). The plate RISES
    /// THROUGH its resting place rather than sliding in and back out the way
    /// it came -- a notice that lifts off the compass and is gone, which is
    /// the one motion that reads as an announcement rather than as a row
    /// appearing.
    ///
    /// A SIGNED CONTINUOUS DRIFT AND NOT A DIRECTION FLAG, per DECISIONS.md
    /// UI rule 4: the same continuous EasedToggle value drives both the alpha
    /// and the offset, so the plate can never be caught bright and mid-slide
    /// or settled and half-faded. Session derives the sign from the toggle's
    /// own target(); nothing here has to know which way it is going.
    /// Defaults to 0 (settled), which is what a caller that only sets
    /// placePlate/placePlateFade means.
    float placePlateDrift = 0.0F;
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

/// What the crosshair is on, and therefore what colour the reticle and the
/// subject line take. The reference's colour discipline is one colour per
/// ROLE, not per element (docs/design/UI-REFERENCE-TERMINAL.md), and these are
/// the roles a first-person crosshair can land on in this ward.
enum class AimKind : int {
    /// Nothing named in reach. The floor: LOOK, dim, no subject row.
    Nothing = 0,
    /// A body you can talk to or take from.
    Person = 1,
    /// A door or a building the sign table names.
    Place = 2,
    /// A thing with a lid, a lock or a price -- a strongbox, your own bed.
    Thing = 3,
    /// SOMETHING THE CASE IS ABOUT. Its own colour, because the whole game is
    /// this one and a player who cannot tell it from a doorway is the player
    /// who thought the Bloodletter trail ended at the Weighhouse.
    Clue = 4,
};

/// THE ONE REGION OF THE PLAY SPACE THE HUD MAY ENTER, and it is a clamp
/// rather than a description: drawHud() intersects every pixel of the reticle
/// and the aim prompt with this rectangle, so the prompt cannot creep however
/// long a name the ward hands it.
///
/// Anchored on the exact frame centre -- the reticle sits astride it, and the
/// prompt runs up and to the right of it, per the owner's own sentence. The
/// right edge is the HUD's ordinary margin, so a very long name is CLIPPED
/// rather than allowed to run off the frame the way the S6 alert did.
[[nodiscard]] CentreRect hudAimRect(int width, int height) noexcept;

/// The band the VERB row of the aim prompt occupies, and it is the whole
/// reason the prompt is composed from the bottom up.
///
/// A first-person crosshair sweeps continuously and the subject under it
/// appears and vanishes several times a second. If that moved the verb row,
/// the one line a player reads every second of play would jitter under their
/// eye -- the reference's "a list whose layout jumps as you arrow through it
/// feels broken", on the smallest surface in this game. So the subject grows
/// UPWARD off a verb row whose y is a function of the frame alone, and this is
/// public so a case can prove those pixels do not move rather than a comment
/// claiming it.
[[nodiscard]] CentreRect hudAimVerbRow(int width, int height) noexcept;

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
