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
#include <vector>

#include "granadad/render/anim.hpp"
#include "granadad/render/framebuffer.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// UI-EA: THE DISCLOSURE DURATIONS live in anim.hpp (included above), named
// once and shared per UI-EA-SPEC sec. 3 -- kPlateHoldSteps (150, the EVENT
// tier's ~2.5s hold), kTutorHoldSteps (180, the TUTOR tier's ~3s),
// kIdleWakeSteps (300, hesitation-as-a-request-for-help). Both lanes landed
// the same numbers in two homes; the integrator kept anim.hpp's and this
// header defers to it. kPageEaseSteps is already EasedToggle's default 8.
// ---------------------------------------------------------------------------

/// THE TUTOR BAND: the quickBar countdown-plus-toggle pattern, generalized --
/// UI-EA-SPEC's cross-lane contract (c). A band of instructional text is
/// raised in full on an event (page open, device change, an unrecognized
/// press, idle hesitation), holds for its countdown, and eases back down to
/// whatever its at-rest form is (keycaps, or nothing).
///
/// OWNERSHIP SPLIT, per the contract: LANE HUD lands this shape; LANE PAGES
/// instantiates one per band (map band, casebook foot, keys foot, creation
/// feet, pause legend) and draws raised/rest forms off value(); LANE FLOW
/// calls raise() on the wake events it routes. Steps-based, render-side,
/// unhashed, exactly like every EasedToggle in the game.
///
/// THE CALLING CONVENTION IS THE QUICK BAR'S, deliberately: raise() wherever
/// the event lands (any number of times per step -- a raise extends a live
/// hold and never shortens one), sync(suppressed) wherever the owner's
/// syncPanelAnim-equivalent runs (also any number of times per step), and
/// advance() EXACTLY once per step -- a countdown spent per call would make
/// the hold depend on how many keys were pressed during it, the defect
/// Session::step()'s own countdown comment names.
struct TutorBand {
    EasedToggle anim;
    int showSteps = 0;

    /// The event: raise the band in full for `holdSteps`.
    void raise(int holdSteps = kTutorHoldSteps) noexcept {
        if (holdSteps > showSteps) {
            showSteps = holdSteps;
        }
    }
    /// Put it down now -- a page closing takes its bands with it.
    void cancel() noexcept { showSteps = 0; }
    /// Re-assert the target: up while the countdown lives and nothing owns
    /// the frame over it. Safe to call many times per step.
    void sync(bool suppressed) noexcept { anim.setTarget(!suppressed && showSteps > 0); }
    /// Once per step, never from a const draw path.
    void advance() noexcept {
        if (showSteps > 0) {
            --showSteps;
        }
        anim.advance();
    }
    /// 0 (at rest) .. 1 (fully raised): what the band's raised form draws at.
    [[nodiscard]] float value() const noexcept { return anim.value(); }
    /// True while the raise is still wanted -- the target a test asserts on.
    [[nodiscard]] bool wanted() const noexcept { return showSteps > 0; }
};

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
    /// UI-EA (LANE HUD): THE STREET SUB-LABEL IS GONE. locationLabel used to
    /// sit here -- the place name under the ribbon, up every frame. The word
    /// diet's row table deletes it outright: the threshold plate already
    /// announces every crossing at the moment it happens, and a reference row
    /// restating it sixty times a second was a word on screen because it was
    /// true, not because it changed. The map and the dialogue epithet still
    /// print Session::placeLabel(); the street does not.
    /// Seconds since midnight. Drawn top-right as HH:MM. Negative draws nothing.
    int timeOfDaySeconds = -1;
    /// UI-EA (LANE HUD): THE CLOCK IS EARNED TEXT NOW. Session's own
    /// EasedToggle (clockAnim_) raises it on an hour tick, on any time charge
    /// (travel, a wait pick, a sleep) and while the wait page is pricing
    /// hours, and puts it down ~2.5s later. Defaults to 1: every hand-built
    /// HudState that predates the diet draws exactly as it always has.
    float clockFade = 1.0F;
    /// The purse, top-right under the clock. Negative draws nothing.
    int coin = -1;
    /// UI-EA (LANE HUD): THE PURSE WAKES ON A COIN DELTA and sleeps ~2.5s
    /// later -- money is on screen when it moves, which is when it matters.
    /// Same default-1 contract as clockFade.
    float purseFade = 1.0F;
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
    /// ACTION-COMBAT BUILD (section 5, channel 2): "HELD HARD -- CUDGEL 14-18"
    /// while a swing is charged past the hard threshold. Bottom band, adjacent
    /// to the guard row, in the hot charge register -- a swing and a guard are
    /// mutually exclusive (the guard drops the instant the hand leaves Idle),
    /// so the two never contend for the band. Empty draws nothing, the usual
    /// state, so every hand-built HudState that predates it is pixel-identical.
    std::string_view chargeLabel;
    /// STANCE & ROOM BUILD: "FISTS UP" / "CUDGEL UP" / "STEEL UP" exactly
    /// while the room's own fighting-mode bit (Tavern::playerHandsUp) is true
    /// -- the state the owner could not see. Bottom band, centred, right
    /// behind the guard row: the two CAN share the band (a guard raises the
    /// hands), and both read as one fact -- what the hands are doing. Empty
    /// draws nothing, the usual state, so every hand-built HudState that
    /// predates it is pixel-identical.
    std::string_view handsLabel;
    /// KIT BUILD (defence v1): "COAT TURNS 2" for kPlateHoldSteps after a
    /// landed blow the worn kit softened -- the piece with the most DR on
    /// the body and what the blow lost to it. An EVENT row (the Law of
    /// Earned Text: said when it happens, never furniture), centred right
    /// behind the hands row in the blocked-blow wash's own steel-cool ink,
    /// so the row and the wash read as one fact. Its own row rather than
    /// the alert, because a bouncer's warning outranks the alert for as
    /// long as the house minds you and would eat every turn in a brawl.
    /// Empty draws nothing, the usual state.
    std::string_view turnLabel;
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
    /// UI-EA (LANE HUD): THE QUICK BAR'S TUTOR TOAST -- "WHEEL - STEP" on a
    /// keyboard, the D-pad's left and right on a pad (the keys through
    /// promptLabel, so a rebind re-words it). Nine and the sticks cut the
    /// QuickWheel hold, so the STEP is what is taught: Session raises it the
    /// first two times the quick bar comes up, riding the strip's own
    /// countdown, and never again. TUTOR tier: it
    /// takes a bottom-band slot directly after the strip, in the quiet
    /// reference ink, and both empty-and-zero defaults draw nothing at all.
    std::string_view wheelHint;
    float wheelHintFade = 0.0F;
    /// The ladder the player is highest on, and the rung: "FLAME - DISCIPLE".
    /// Bottom-left, stacked over the health bar, because that is where a
    /// character's own state lives and the centre stays empty. Drawn only when
    /// the player is on a rung at all.
    /// UI-EA (LANE HUD): earned text -- wakes for ~2.5s when the rung
    /// changes, sleeps otherwise. A title held for a week is on the sheet.
    std::string_view guildLabel;
    /// What the current questline wants next, in the journal's own words.
    /// Bottom-left under the guild, and clipped to a single line.
    /// UI-EA (LANE HUD): earned text -- wakes on change, sleeps otherwise.
    std::string_view objectiveLabel;
    /// S8: the man who has put the player on the floor most often and how
    /// often -- "BRAM MARROW x2". Bottom-left under the objective, still on
    /// the edge. Empty draws nothing, which is the usual case: nobody has
    /// beaten you yet.
    ///
    /// UI-EA (LANE HUD): DIETED 6 -> 3 (the spec's "rank 6->3"). The "RIVAL "
    /// prefix and the title were reference material -- the row's own corner
    /// and colour already say what he is, and his title is on his sheet. The
    /// HUNTING word leaves the label too; the fact rides rivalHunts below and
    /// the row's red ink, which is how the row has always been read at a
    /// glance ("a hunted man should not have to read the line to notice it").
    /// The count (x2) is a value and values never get vaguer.
    std::string_view rivalLabel;
    /// UI-EA (LANE HUD): whether the rival is currently hunting the player.
    /// Colours the row red. The word itself is off the label (above); a
    /// hand-built label that still says "HUNTING" is honoured too, so every
    /// pre-diet HudState draws in the colour it always did.
    bool rivalHunts = false;
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
    /// ACTION-COMBAT BUILD (section 5, channel 1). THE RETICLE IS THE WEAPON --
    /// the charge readout, DECOUPLED from the interact prompt so it draws the
    /// reticle during a swing hold even with nothing in reach (aimVerb empty),
    /// which the interact-only early-return could not do.
    ///   aimChargeFrac    0..1, the hold's progress toward the hard threshold;
    ///                    the four ticks retract toward centre across it.
    ///   aimChargeHard    the hold reached the hard tier; the ticks take the
    ///                    warm accent.
    ///   aimChargeOnLine  a body sits on the look-ray right now; the ticks
    ///                    brighten, and a swing thrown will land.
    /// All three default to the at-rest values, so every hand-built HudState
    /// that predates them draws its reticle exactly as it always has.
    float aimChargeFrac = 0.0F;
    bool aimChargeHard = false;
    bool aimChargeOnLine = false;
    /// S10: where the bloodletter trail stands and where it wants you next --
    /// "CASE 2/6 > THE DROWNED HOLD". Bottom-left, ONE row.
    ///
    /// UI-EA (LANE HUD): EARNED TEXT. The row used to be up every frame; now
    /// Session raises it for ~2.5s when a beat or a lead moves and when the
    /// casebook closes (the recap a player putting the book down actually
    /// wants), and it sleeps the rest of the time -- the J page is where the
    /// case lives, and a corner row nobody is looking at cannot orient
    /// anybody by being permanent.
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
    /// UI-EA (LANE HUD): earned text -- wakes on room entry and on the room's
    /// loudness turning over, sleeps ~2.5s later. A head-count drifting by
    /// one is not an event and does not wake it.
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
    /// ACTION-COMBAT BUILD. The HELD HARD charge row's own ease, its own
    /// Session-side EasedToggle, the same no-op-by-default reasoning as every
    /// *Fade above: a HudState nobody eased draws exactly as it always has.
    float chargeFade = 1.0F;
    /// STANCE & ROOM BUILD. The fighting-mode row's own ease, its own
    /// Session-side EasedToggle, the same no-op-by-default reasoning.
    float handsFade = 1.0F;
    /// KIT BUILD. The turn row's own ease, its own Session-side EasedToggle,
    /// the same no-op-by-default reasoning.
    float turnFade = 1.0F;
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
    /// IT IS NOT A REFERENCE ROW AND IT MUST NEVER BECOME ONE. The old
    /// locationLabel sub-label -- the same fact up every frame, dim, small --
    /// is exactly what the word diet deleted; this is an EVENT -- crossing a
    /// boundary -- and the whole of what it adds is that the crossing is
    /// legible AT THE MOMENT IT HAPPENS. With the sub-label gone the plate is
    /// the one thing on the street that names ground, which is the diet's own
    /// argument: text prints when it changes, never merely because it is
    /// true. Drawn from sim::docks::placeNameAt, an edge and not a state --
    /// exactly the distinction anim.hpp draws between EasedToggle and
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

    /// THE CASEBOOK PASS: THE MOMENT LEADS OPEN.
    ///
    /// "3 NEW LEADS  J YOUR CASEBOOK", said once, at the instant it becomes
    /// true, and then gone. The owner followed the Bloodletter to Crell at the
    /// Weighhouse, three leads opened, and the only thing on the frame that
    /// said so was a dim grey corner row changing from CASE 4/6 to CASE 4/9 --
    /// docs/frames/casebook/before-weighhouse-960.png. He concluded the content
    /// had run out.
    ///
    /// THE SAME THREE FIELDS AS placePlate, THE SAME DRAWING, THE SAME SLOT,
    /// AND IT OUTRANKS IT. Both are announcements of an edge; both rise through
    /// one resting place under the compass ribbon and leave; and two plates
    /// stacked in one band is two notices fighting. So drawHud draws THIS one
    /// when it has anything to say and the place plate otherwise -- Session
    /// already guarantees only one is non-empty at a time, and hud.cpp enforces
    /// it anyway rather than trusting a caller.
    ///
    /// IT IS NOT A SECOND caseLabel. caseLabel is the bottom-left row that
    /// says where the trail stands -- and under the word diet even THAT row
    /// sleeps at rest, waking only when a beat moves or the book closes. This
    /// is an EVENT plate, and while it is up the case row is deliberately
    /// kept down (the spec's "the notice IS the case news"): one piece of
    /// news, said once, in one place.
    ///
    /// Empty draws nothing, and so does a zero fade: every hand-built HudState
    /// that predates these three fields is pixel-identical.
    std::string_view casePlate;
    /// 0 (gone) .. 1 (full strength). Session's own EasedToggle. Defaults to 0
    /// for placePlateFade's reason: this row's ordinary state is "not there".
    float casePlateFade = 0.0F;
    /// Where the plate is in its own rise, signed. See placePlateDrift: the
    /// identical contract, driven off the identical toggle shape.
    float casePlateDrift = 0.0F;

    // --- THE PULL PACK (render/pull.hpp) --------------------------------------
    //
    // THE STREET SAYS WHERE NEXT. Three additions to the top band, every one
    // of them defaulting to "draw nothing" so every hand-built HudState that
    // predates them is pixel-identical.

    /// STATE tier: "NE 40  THE WEIGHHOUSE" -- the ONE lead the player chose to
    /// FOLLOW (or the authored next lead standing in for a choice), on the
    /// ribbon's own sub-label row, from the same bearing/paces arithmetic the
    /// casebook page prints. Up every frame while a lead is followed; that is
    /// the one line the UI-EA rest budget grew by (spec 1.2 #11). Empty draws
    /// nothing. Never a person, never a clue -- the marker doctrine.
    std::string_view pullLabel;
    /// The followed lead's own bearing, BAM 0..65535, or -1: drawn as the
    /// amber tick on the ribbon, under the strip, so the eye can line the
    /// fixed mark up on it without reading a word.
    std::int32_t pullTickBam = -1;
    /// Bearings of DISCOVERED NAMED PLACES (a heard lead names them, or the
    /// body has stood in them), BAM 0..65535 each: the bone ticks on the
    /// ribbon. Only the ones inside the strip's 180-degree window draw.
    std::vector<std::int32_t> placeTickBams;

    /// EVENT tier: the skill-up toast, top-left -- "SKYRUNNING RISES TO 12" --
    /// Oblivion's own beat in this HUD's own register. Rises through its
    /// resting row and fades, on Session's own EasedToggle, in situ (mid-fight,
    /// mid-climb) and never pausing anything. Empty or zero fade draws nothing.
    std::string_view skillToast;
    float skillToastFade = 0.0F;
    /// Signed drift, the plates' own contract: -1 rising in, +1 drifting out.
    float skillToastDrift = 0.0F;
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
    /// KIT BUILD. SOMEBODY'S THING: taking it is theft, and the crosshair
    /// says so before the press -- the reference's red hand. Its own
    /// accent, and the THEIRS note beside the name.
    Owned = 5,
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

/// WHERE THE AIM PROMPT WILL ACTUALLY LAND, given what is under the reticle
/// this frame -- the union of the two rows and their scrim air, not the whole
/// fence above. Empty (all zero) when nothing would be drawn.
///
/// hudAimRect is a CLAMP and covers the entire right half of the centre band;
/// keeping a whole band of the street clear of a prompt that occupies a
/// fraction of it would cost signs that were never in its way. This is the
/// fraction, and it is measured by the same pure layout drawAim draws from
/// rather than by a second copy of that arithmetic living in the caller.
///
/// THE COLLISION IT EXISTS FOR: the world's hanging signage is drawn before
/// the HUD and knows nothing about it, so "TARWALK" and "E - TALK" landed on
/// the same pixels on the street and the prompt won by painting over a sign
/// mid-word. drawSignage takes this as an exclusion and skips a label that
/// would collide, which is the rule it already applies between one sign and
/// another.
[[nodiscard]] CentreRect hudAimPromptRect(const HudState& state, int width, int height);

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
