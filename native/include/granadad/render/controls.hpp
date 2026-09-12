#pragma once

// THE KEYS, AND EVERY PREFERENCE ABOUT HOW THE GAME IS DRIVEN.
//
// WHY THIS IS A LIBRARY AND NOT A SWITCH STATEMENT IN main.cpp
//
// It used to be a switch statement in main.cpp, and src/client/main.cpp said so
// out loud:
//
//     // VERIFICATION GAP (S3): NOTHING TESTS THIS SWITCH. Every branch below
//     // calls a Session method the suite drives directly, so the behaviour is
//     // covered and the BINDING is not -- a key wired to the wrong verb, or a
//     // conversation that fails to capture the keyboard, would ship green.
//
// Three sprints of controls work happened over that comment. This file closes
// it: the binding table, the hold-or-toggle modifiers, the stick deadzones and
// the settings file are all ordinary testable objects in granadad-render, and
// the client is left holding SDL and nothing else. What main.cpp keeps is the
// translation from SDL_Scancode to Key and the calls that follow -- which is
// still untested, but it is now twenty lines of table instead of three hundred
// lines of game.
//
// NO SDL IN HERE. Not one include, deliberately. granadad-render draws frames
// with no window at all (that is how --screenshot and two hundred cases work),
// and the moment this file knows what SDL_SCANCODE_W is, none of it can be
// built or tested without a windowing library.
//
// NO FLOATS EITHER, even though this is render and floats are legal here.
// Everything below feeds MoveInput, which feeds the simulation, and integers
// all the way to the boundary means a settings file cannot make two machines
// disagree about where a body ended up.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/sim/angle.hpp"

namespace granadad::render {

// ---------------------------------------------------------------------------
// what the player can ask for
// ---------------------------------------------------------------------------

/// Every verb a key can be bound to.
///
/// NINE AND THE STICKS. This enum was rebuilt from scratch a SECOND time, and
/// that is the second deliberate exception to the insert-only rule every
/// earlier version of this comment stated -- stated once, here, exactly as
/// #85 stated the first (see docs/design/DECISIONS.md, the controls row, and
/// controls.cpp's fromText() for the migration a saved file gets instead of
/// silently corrupting). THE RULE HELD BECAUSE OF SAVED SETTINGS FILES, and
/// there are still no shipped players: `granadad-controls.cfg` lives beside an
/// executable nobody outside this repo has run.
///
/// THE SHAPE IS THE OWNER'S RULING, VERBATIM: "I want you to work on
/// simplifying the controls and minimizing the number of inputs necessary.
/// Even a game like Morrowind worked on the console with just a few buttons.
/// When I press LMB I want to enter fighting mode and hit whoever is in
/// front of me." NINE world verbs below are marked CORE, plus MAP as the one
/// keyboard-only direct shortcut the owner asked for by name ("a map that
/// they can press M to see") -- down from the thirteen #85 shipped. On a pad
/// the map is a page of NOTES (LB/RB step through your papers and the ward
/// map is one of them, Oblivion's own tabbed menu), so it spends no button.
///
/// WHAT WAS CUT, AND WHERE IT WENT:
///   QuickWheel (Q hold / R3)   -> cut. The quick bar steps on the wheel and
///                                  on the pad's D-pad left/right (QuickNext/
///                                  QuickPrev carry both halves now); the
///                                  digits stay; the Grimoire is a NOTES page.
///   PagePrev / PageNext        -> cut as ACTIONS. `[` `]` and LB/RB are page
///                                  grammar the router reads raw (pageStep),
///                                  the way it already read arrows and TAB.
///   KeysPage / OptionsPage     -> cut. The pause menu's CONTROLS and SETTINGS
///                                  rows are the door on both devices.
///   the walk-toggle (tap RUN)  -> cut. RUN is a plain hold; SNEAK is the
///                                  quiet stance every theft resolves off.
///   Map's pad half (SELECT)    -> folded into NOTES; SELECT is WAIT now,
///                                  Oblivion's own spend of that button.
///   Attack on X, Cast on RT    -> RT swings, LT guards, RB casts -- the
///                                  primary hand on the right trigger, the
///                                  way the mouse's primary is the swing.
///
/// ORDER IS STILL THE SETTINGS FILE'S ORDER AND THE KEYS PAGE'S ORDER, and
/// FROM THIS POINT ON new actions go on the END again -- the insert-only
/// rule resumes the moment this list ships. The settings file resolves by
/// NAME (actionKey), so reordering the survivors cost nobody a binding.
enum class Action : std::uint8_t {
    // --- movement axes: always separate sticks/keys, never one of the nine ---
    Forward = 0,
    Back,
    StrafeLeft,
    StrafeRight,
    /// Arrow-key turning. An ACCESSIBILITY FALLBACK for playing without a
    /// mouse; sim::kTurnRate says at length why it is not allowed to define
    /// the feel of anything. Not counted among the core buttons: nobody maps
    /// this on a pad, which already turns with the right stick.
    TurnLeft,
    TurnRight,

    // --- THE NINE. Every one carries a keyboard/mouse half and a pad half. ---

    /// CORE 1, SWING. The owner's "press LMB and hit whoever is in front of
    /// me": one press from hands down RAISES the hands and swings, the same
    /// press charges, so tap = raise and swing, hold = raise and hard swing
    /// (sim::Tavern::playerHandsUp is the fighting-mode bit the room reads).
    /// A HELD button: the down edge starts the hold clock and the release
    /// edge resolves the swing, hard iff the hold reached kHardSwingHoldSteps
    /// == HoldToggle::kTapSteps. MOUSE1 and the RIGHT TRIGGER -- the pad's
    /// primary hand, where the mouse's primary is. See Session::attackDown()
    /// / attackUp().
    Attack,
    /// CORE 2, GUARD. HELD, never latched -- a latched guard is a footgun in
    /// a brawl. From hands down it raises the hands WITHOUT a blow, the free
    /// way into fighting mode. MOUSE2 and the LEFT TRIGGER, opposite SWING,
    /// the Oblivion pair. Sim::Tavern::tickBrawl reads the held state.
    Block,
    /// CORE 3, CAST. Casts whatever the Grimoire has readied; every refusal
    /// is spoken (nothing equipped, out of reach, cooling). C and the RIGHT
    /// BUMPER -- Oblivion's own cast button.
    Cast,
    /// CORE 4, USE. ONE button for everything in reach, resolved by stance
    /// and by what is faced -- render::Session::interact() is the whole rule
    /// and interactPrompt() is the same rule read for the reticle, so the
    /// label on screen can never say something the key would not do. Its
    /// last slot before LOOK is LOWER HANDS: hands up, nothing in reach, and
    /// the fists come down. E and A.
    Interact,
    /// CORE 5, SNEAK. The stance every theft resolves off (stealth.hpp). A
    /// HoldToggle: tap latches, hold holds. LCTRL and B -- and B is BACK on
    /// every page (pageBackRemap), the one contextual reuse the grammar keeps.
    Crouch,
    /// CORE 6, JUMP. Jump, mantle or drop, resolved by what is ahead or below
    /// -- render::Session::vertical(). SPACE and Y.
    Vertical,
    /// CORE 7, RUN. A plain HOLD now: the tap-to-walk toggle is cut (a pad
    /// picks its gait off the stick's magnitude and SNEAK is the quiet
    /// stance). LSHIFT, and the left stick click as the pad's optional
    /// second half -- the stick alone already sprints at full push.
    Sprint,
    /// CORE 8, NOTES. Your papers on one screen: the sheet, the chart, the
    /// letters, the casebook -- and, one bumper past them, the WARD MAP and
    /// the GRIMOIRE. LB/RB and `[` `]` page through all six (pageStep); the
    /// key that opened it closes it. J and D-PAD UP.
    Menu,
    /// CORE 9, PAUSE. RESUME / WAIT / CONTROLS / SETTINGS / QUIT, and the
    /// universal back: ESC (and B on a page) backs out of whatever is open.
    /// ESC and START.
    Pause,

    // --- the tenth, keyboard only: the owner's own direct shortcut ----------

    /// MAP. "A map that they can press M to see." The full-screen ward plan.
    /// M on a keyboard; on a pad it is a page of NOTES and spends no button
    /// (SELECT, which used to open it, is WAIT). A file that still writes
    /// "bind map M PAD_BACK" is migrated -- see fromText().
    Map,

    // --- kept, but NOT one of the nine: bonus shortcuts, Oblivion PC's own
    // plurality of input. Each has a door elsewhere that a new player finds
    // without learning the key. --------------------------------------------

    /// WAIT. The hour-select page. The pause menu's WAIT row is the door on
    /// both devices; T (Oblivion's own wait key) and SELECT are the direct
    /// shortcuts. NOT core: nobody has to learn it.
    Wait,
    /// The number row readies a slot with nothing open. Keyboard only, the
    /// way Oblivion PC's hotkeys are; the pad steps the bar instead.
    QuickSlot1,
    QuickSlot2,
    QuickSlot3,
    QuickSlot4,
    QuickSlot5,
    QuickSlot6,
    QuickSlot7,
    QuickSlot8,
    QuickSlot9,
    QuickSlot0,
    /// Steps the quick bar: the mouse wheel, and the pad's D-pad right/left
    /// (the QuickWheel's hold-and-step folded into two plain presses).
    QuickNext,
    QuickPrev,

    /// NOT A GAMEPLAY CONTROL. A dev/capture utility, always F12, excluded
    /// from the count the way the owner's brief asked -- "not part of the
    /// Steam Input action set."
    Screenshot,
    Count
};

inline constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);

/// The stable name an action is written under in the settings file. Never
/// translated, never prettified: this is a file format.
[[nodiscard]] std::string_view actionKey(Action action) noexcept;

/// What the keys page calls it. Short, because it shares a row with a key name.
[[nodiscard]] std::string_view actionLabel(Action action) noexcept;

/// One sentence on what the key actually does, for the controls page's detail
/// pane (render/keys_page.hpp).
///
/// THE LABEL IS NOT ENOUGH ANY MORE AND HAS NOT BEEN SINCE #85. "USE" is
/// Interact + Examine + Steal + Lift + Rest + the lockpick verb, resolved by
/// stance and by what is faced; "JUMP" is Jump + Traverse + DropDown. A page
/// that prints six characters beside a key cannot say that, and the old page
/// printed exactly six characters beside a key. This is what the master/detail
/// layout put a pane there for.
[[nodiscard]] std::string_view actionHelp(Action action) noexcept;

/// Parses `actionKey`. Action::Count for anything unrecognised, so an old
/// settings file with a dropped action is ignored rather than fatal.
[[nodiscard]] Action actionFromKey(std::string_view name) noexcept;

// ---------------------------------------------------------------------------
// what a key is
// ---------------------------------------------------------------------------

/// A physical key or mouse control, in this game's own vocabulary.
///
/// NOT AN SDL SCANCODE. The client translates; see the file header. The numbers
/// are arbitrary but STABLE -- they are what a saved binding file resolves
/// through by NAME, so reordering is harmless and renaming is not.
enum class Key : std::int32_t {
    None = 0,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Up, Down, Left, Right,
    Space, Enter, Escape, Tab, Backspace,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    Minus, Equals, Comma, Period, Slash, Semicolon, Apostrophe,
    LeftBracket, RightBracket, Backslash, Grave,

    /// The mouse, which is a perfectly good place to bind a verb and which no
    /// build before this one would let you use for one.
    MouseLeft, MouseRight, MouseMiddle, MouseX1, MouseX2,
    WheelUp, WheelDown,

    /// A gamepad, in the face-button names everyone actually says.
    PadSouth, PadEast, PadWest, PadNorth,
    PadLeftBumper, PadRightBumper,
    PadLeftTrigger, PadRightTrigger,
    PadLeftStick, PadRightStick,
    PadStart, PadBack,
    PadUp, PadDown, PadLeft, PadRight,

    Count
};

/// The name a key is written under, and read back by. "W", "LSHIFT", "WHEELUP".
[[nodiscard]] std::string_view keyName(Key key) noexcept;
/// Parses `keyName`, case-insensitively. Key::None for anything unrecognised.
[[nodiscard]] Key keyFromName(std::string_view name) noexcept;

// ---------------------------------------------------------------------------
// which device is holding the prompt
// ---------------------------------------------------------------------------

/// The two vocabularies a prompt can speak. MOUSE AND KEYBOARD ARE ONE
/// DEVICE: nobody puts the mouse down to press E, and a prompt that flapped
/// between "E - TALK" and "MOUSE1 - ATTACK" wordings as the hand moved
/// between them would be churn, not information. The pad is the other one.
///
/// This is CLIENT state -- which hand last spoke is a fact about the person
/// at the desk, never about the simulation, so nothing here may ever feed a
/// hash or a MoveInput. Session holds the current value (see
/// Session::noteInputDevice) and every prompt reads it at draw time.
enum class InputDevice : std::uint8_t { KeyboardMouse = 0, Pad };

// ---------------------------------------------------------------------------
// UI-EA-SPEC sec. 5: the motif sentinels
// ---------------------------------------------------------------------------
//
// SIX GLYPHS THE 4x6 FONT DOES NOT HAVE, ENTERING THROUGH THE SANCTIONED
// CHANNEL ONLY: a prompt string may carry the sentinel bytes below, and the
// panel text drawer maps each one to a drawn 4x6-cell motif (panel.cpp's
// motif table -- same cell, same advance, same shadow, NO font change). The
// bytes are 0x01-0x06, which collide with no printable character and no
// existing string in the build.
//
// WHY BYTES INSIDE STRINGS rather than a parallel markup: every keycap and
// nav label in the game already flows through the promptKey choke points
// below (promptKeyName / promptConfirmKey / promptMoveKeys), and a sentinel
// that travels INSIDE the string travels wherever those strings already go
// -- composed feet, echoed commit verbs, device-swapped labels -- with no
// second channel to keep in step. Device-awareness survives by construction:
// a pad still prints "A", "B", "X"; only the keyboard's ENTER and the arrow
// vocabularies (and the pad's D-pad wordings) become motifs.
//
// CROSS-LANE CONTRACT (a): FLOW emits these from the choke points; PAGES
// renders them in drawPanelText. A drawer that has not learned them yet
// advances the cell and draws nothing -- a blank, not garbage -- which is
// the same degradation an unknown character always had.

/// Carriage-return arrow -- the ENTER keycap.
inline constexpr char kMotifReturn = '\x01';
/// Solid triangles: up, down, left, right -- the arrow-key keycaps.
inline constexpr char kMotifUp = '\x02';
inline constexpr char kMotifDown = '\x03';
inline constexpr char kMotifLeft = '\x04';
inline constexpr char kMotifRight = '\x05';
/// The d-pad cross. "D-PAD UP" is the cross followed by the up triangle.
inline constexpr char kMotifDPad = '\x06';

/// True exactly for the six sentinel bytes above -- the one predicate a text
/// drawer or a copy sweep needs to tell "motif to draw" from "character the
/// font is missing".
[[nodiscard]] constexpr bool isMotifSentinel(char c) noexcept {
    return c >= kMotifReturn && c <= kMotifDPad;
}

/// True for the sixteen pad keys and nothing else.
[[nodiscard]] bool keyIsPad(Key key) noexcept;

/// The device a key belongs to. Pad keys answer Pad; every keyboard and
/// mouse key -- and Key::None, which belongs to nobody -- answers
/// KeyboardMouse, the shipped default vocabulary.
[[nodiscard]] InputDevice deviceOfKey(Key key) noexcept;

/// What a PROMPT calls a key. keyName()'s vocabulary for keyboard and mouse
/// keys ("E", "TAB", "MOUSE1"), and the spoken names for pad keys -- "A",
/// "START", "SELECT", "LB" -- the OSK's own manners ("B BACK / A TAKE /
/// START DONE") applied everywhere. Deliberate exceptions, all of them cases
/// where the word was longer than the key: the brackets print "<" and ">"
/// (hud.cpp's 4x6 font has no glyph for '[' or ']', and the keys page
/// already labels the pair "PAGE <"/"PAGE >"); ENTER prints the return
/// motif (kMotifReturn); the four arrow keys print their triangle motifs;
/// and the pad's four D-pad keys print the cross-plus-triangle pair
/// (kMotifDPad then the direction) where they used to spell "D-PAD UP".
/// UI-EA-SPEC sec. 5 -- the words ENTER/UP/DOWN/D-PAD were the single
/// largest chrome spend in every foot in the census. NEVER a file format:
/// toText()/fromText() still speak keyName(), and nothing parses this
/// vocabulary back.
[[nodiscard]] std::string_view promptKeyName(Key key) noexcept;

struct ControlSettings;  // declared below, with the rest of the binding table

/// The half of an action's two bindings that belongs to `device` -- the
/// binding table already knows both keys per Action, and this is the
/// draw-time read of it. Primary is preferred over secondary within a
/// device, the same order actionFor() resolves in. When the action has no
/// key on the asked-for device the OTHER device's key is returned rather
/// than Key::None: a prompt that hides a reachable verb because it is bound
/// on the other hand is lying by omission.
[[nodiscard]] Key promptKey(const ControlSettings& settings, Action action,
                            InputDevice device) noexcept;

/// promptKeyName(promptKey(...)), or "--" when the action holds no key at
/// all -- keyName()'s own unbound answer, so the two surfaces agree.
[[nodiscard]] std::string_view promptLabel(const ControlSettings& settings, Action action,
                                           InputDevice device) noexcept;

/// The page grammar the client's router hard-codes rather than binds --
/// main.cpp's route_menu_key: ENTER confirms and A confirms (PadSouth via
/// Action::Interact), ESC backs out and B backs out (PadEast is remapped to
/// Escape while any page is open -- the parity pass), arrows move a list
/// and so does the D-pad, raw, ahead of any binding. Said ONCE, here, so a
/// nav band and the router cannot drift apart one wording at a time.
///
/// UI-EA-SPEC sec. 5: the keyboard's halves speak motifs now -- confirm is
/// the return sentinel, move is the up/down triangle pair -- and the pad's
/// move is the bare d-pad cross. Back stays "ESC"/"B": short and iconic.
[[nodiscard]] std::string_view promptConfirmKey(InputDevice device) noexcept;  // return motif / "A"
[[nodiscard]] std::string_view promptBackKey(InputDevice device) noexcept;     // "ESC" / "B"
[[nodiscard]] std::string_view promptMoveKeys(InputDevice device) noexcept;    // triangles / cross

/// NINE AND THE STICKS: the page grammar's two sideways steps, RAW, ahead of
/// any binding -- the way arrows, the D-pad and TAB already were -- so the
/// bumpers can cast in the world (RB is CAST) and still turn the page on
/// every surface, Oblivion's own tabbed menu: LB/RB step the TABS along the
/// top (the six pages of NOTES: sheet, chart, letters, casebook, ward map,
/// grimoire), LT/RT step the SUB-TABS inside one page (the ward map's four
/// views, the casebook's LEADS / THE CASE, KEYS / OPTIONS). The keyboard's
/// halves: `[` `]` for pages, TAB for the next sub-tab (as it always was).
///
/// -1, 0 or +1. Said ONCE, here, so the router and every nav band read the
/// same keys -- a band that advertised a step the router did not take is the
/// exact drift this file exists to kill.
[[nodiscard]] int pageStep(Key key) noexcept;  // `[`/LB = -1, `]`/RB = +1
[[nodiscard]] int tabStep(Key key) noexcept;   // LT = -1, TAB/RT = +1
/// The keycaps the two steps print, in the device's vocabulary: "< >" / "LB
/// RB" for pages, "TAB" / "LT RT" for sub-tabs.
[[nodiscard]] std::string_view promptPageKeys(InputDevice device) noexcept;
[[nodiscard]] std::string_view promptTabKeys(InputDevice device) noexcept;
/// The second commit a page can carry (the ward map's TRAVEL, the haggle's
/// TAKE THEIR PRICE), on the one face button the nine leave free: X. "T" on
/// a keyboard, the page's own raw key, exactly as it has always been.
[[nodiscard]] std::string_view promptAltCommitKey(InputDevice device) noexcept;  // "T" / "X"
[[nodiscard]] bool isAltCommitKey(Key key) noexcept;

/// The page grammar's ONE remap, stated as a function so it is testable and
/// so the client's router and its fall-through press cannot apply it
/// differently: while a page owns the input (`pageOpen`), the pad's East
/// button IS Escape -- the universal back promptBackKey() already advertises
/// as "B" -- and every other key, and East with no page open, passes through
/// unchanged.
///
/// WHY IT MUST HAPPEN ONCE, AT THE EVENT EDGE, before the router AND the
/// press that runs when the router declines: PadEast's world binding is
/// Action::Crouch. The first parity pass remapped it privately inside the
/// router, judged the Escape, deliberately fell through so "back out of
/// whatever is open" could run -- and then the caller replayed the press as
/// the RAW PadEast, which reached Crouch. One press, two handlers: the
/// casebook closed AND the street carried a CROUCHED banner nobody asked
/// for (the ship note's seam #1). Remap once, and the press only ever means
/// one thing.
///
/// `pageOpen` is the caller's fact (main.cpp's pointer_page_open, minus a
/// live key-rebinding capture, which must see the real PadEast); this
/// function owns only the rule.
[[nodiscard]] Key pageBackRemap(Key key, bool pageOpen) noexcept;

// ---------------------------------------------------------------------------
// hold AND toggle, which is two features and one control
// ---------------------------------------------------------------------------

/// A modifier that is BOTH a hold and a toggle, because players disagree about
/// which one sprint and crouch should be and the argument has no winner.
///
/// TAP IT and it latches on; tap it again and it latches off. HOLD IT and it is
/// on for as long as you hold it and off the moment you let go. The two do not
/// fight: a tap is a press shorter than kTapSteps, and anything longer is a
/// hold, so the same key does both without a setting to choose between them.
///
/// It counts MOVEMENT STEPS and not milliseconds, so it behaves identically at
/// 30 and 300 frames a second -- which is the same reason StepPump exists.
class HoldToggle {
public:
    /// Longest press still counted as a tap. 15 steps is a quarter of a second:
    /// comfortably longer than any deliberate tap and comfortably shorter than
    /// the shortest press anybody makes when they mean to hold something.
    static constexpr std::int64_t kTapSteps = 15;

    void press(std::int64_t stepNow) noexcept;
    void release(std::int64_t stepNow) noexcept;
    /// Held down, or latched on. See activeNow in the .cpp for the one clause
    /// that is not obvious: a press that turned a latch OFF is not "on" while
    /// you keep holding it.
    [[nodiscard]] bool active() const noexcept { return activeNow(); }
    [[nodiscard]] bool latched() const noexcept { return latched_; }
    /// Drops both. For a mode change -- a conversation opening, a menu -- where
    /// leaving a key latched from before would be a surprise afterwards.
    void clear() noexcept;

private:
    [[nodiscard]] bool activeNow() const noexcept;

    bool held_ = false;
    bool latched_ = false;
    /// True when the press that is currently down began by cancelling a latch,
    /// so releasing it must not immediately latch again.
    bool cancelledLatch_ = false;
    std::int64_t pressedAt_ = 0;
};

// ---------------------------------------------------------------------------
// the mouse
// ---------------------------------------------------------------------------

/// THE PRIMARY AIM PATH, and the two knobs a player expects to find for it.
struct MouseSettings {
    /// BAM per mouse count. 14 is roughly 0.077 degrees a count, which puts a
    /// 400 CPI mouse at about 31 cm for a full turn -- a middling sensitivity
    /// that most people will move.
    std::int32_t sensitivity = 14;
    /// Down is up. A real preference held by real people and free to support.
    bool invertY = false;
    /// NO SMOOTHING AND NO ACCELERATION, and there is deliberately no setting
    /// for either. Raw relative deltas straight onto the yaw is the correct
    /// default and the only one this build offers; a filter here would be the
    /// archaic feel arriving by a different door.
    static constexpr bool kRawAlways = true;

    /// Applies sensitivity and invert to one frame's motion. The ONE place
    /// either preference is read.
    [[nodiscard]] sim::Angle yawFor(std::int32_t countsX) const noexcept;
    [[nodiscard]] sim::Angle pitchFor(std::int32_t countsY) const noexcept;
};

/// Bounds the sensitivity slider. 1 is unusably slow and 200 is unusably fast;
/// both are reachable, because somebody's hand is not yours.
inline constexpr std::int32_t kMinSensitivity = 1;
inline constexpr std::int32_t kMaxSensitivity = 200;
inline constexpr std::int32_t kSensitivityStep = 2;

/// Bounds the field of view, in horizontal degrees. 90 is the default; 65 is
/// cinematic and 130 is a fishbowl, and both are legitimate things to want.
inline constexpr std::int32_t kMinFov = 60;
inline constexpr std::int32_t kMaxFov = 130;
inline constexpr std::int32_t kFovStep = 5;

// ---------------------------------------------------------------------------
// the gamepad
// ---------------------------------------------------------------------------

/// A stick, in the raw signed range every gamepad API reports.
struct Stick {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

inline constexpr std::int32_t kStickMax = 32767;

/// REAL DEADZONES, which means RADIAL ones.
///
/// The cheap version is per-axis -- ignore x under a threshold, ignore y under a
/// threshold -- and it is why so many games feel like the stick snaps to the
/// compass: near the centre one axis clears the bar while the other does not, so
/// every small push comes out as pure north or pure east. A radial deadzone
/// measures the LENGTH of the stick vector and either takes the whole thing or
/// none of it, so a gentle push in any direction is a gentle push in that
/// direction.
///
/// And the outer edge is rescaled. Sticks do not physically reach 32767 in the
/// diagonals; without saturation, full deflection north-east is slower than full
/// deflection north, which reads as the pad being broken.
struct PadSettings {
    /// Percent of full deflection ignored at the centre.
    std::int32_t deadzonePercent = 18;
    /// Percent of full deflection treated as maximum.
    std::int32_t saturationPercent = 95;
    /// How fast the look stick turns at full deflection, BAM per second. 40000
    /// is about 220 degrees a second, which is where a pad shooter sits.
    std::int32_t lookBamPerSecond = 40000;
    /// Trigger travel ignored before a trigger counts as pressed, percent.
    std::int32_t triggerDeadzonePercent = 12;
};

/// The stick with its deadzone removed and its outer edge rescaled, still in
/// -kStickMax..kStickMax. A stick inside the deadzone comes back exactly zero.
[[nodiscard]] Stick applyDeadzone(Stick raw, const PadSettings& pad) noexcept;

/// How far the stick is pushed, 0..kStickMax. The RADIAL length, which is the
/// number a gait threshold has to be read off -- a diagonal push is a full push.
[[nodiscard]] std::int32_t stickMagnitude(Stick stick) noexcept;

/// One axis of a processed stick as a movement intent in {-1, 0, +1}.
///
/// TILE-STEPPED INTENT, and that is a real limitation stated rather than hidden:
/// sim::MoveInput carries a direction and a gait, not an analogue magnitude, so
/// a pad cannot currently walk at three-quarter speed. What it CAN do is pick a
/// gait -- push past kAnalogueWalkPercent for a jog, past kAnalogueSprintPercent
/// for a sprint -- which is where most of the value of an analogue stick
/// actually is.
[[nodiscard]] std::int32_t stickIntent(std::int32_t axis) noexcept;

/// Percent of full deflection at which a stick stops being a walk and becomes a
/// jog, and at which it becomes a sprint.
inline constexpr std::int32_t kAnalogueWalkPercent = 55;
inline constexpr std::int32_t kAnalogueSprintPercent = 92;

/// How far a look stick turns the head in one frame, BAM.
///
/// CUBED, and that is the response curve every pad shooter uses: a small push
/// gives a very small turn (which is what aiming needs) and a full push gives
/// the whole rate (which is what turning round needs). A linear stick can do one
/// or the other and never both.
[[nodiscard]] sim::Angle padLook(std::int32_t axis, std::int32_t bamPerSecond,
                                 std::int32_t steps) noexcept;

// ---------------------------------------------------------------------------
// the whole of it, on disk
// ---------------------------------------------------------------------------

/// Every binding and every preference, and it survives the process.
struct ControlSettings {
    /// Two keys per action, because everybody wants arrows AND WASD, or a
    /// keyboard binding AND a pad button. Key::None means unbound.
    Key primary[kActionCount] = {};
    Key secondary[kActionCount] = {};

    MouseSettings mouse{};
    PadSettings pad{};
    /// Horizontal field of view, degrees.
    std::int32_t fovDegrees = 90;

    /// The shipped bindings. WASD, Shift, Ctrl, Space, E, Tab, Escape, and the
    /// number row on the quick slots -- which is to say, the layout somebody who
    /// has played any first-person game in the last twenty years already knows.
    [[nodiscard]] static ControlSettings defaults() noexcept;

    /// The action this key drives, or Action::Count. First match wins, primary
    /// before secondary, which is also the order the keys page prints them.
    [[nodiscard]] Action actionFor(Key key) const noexcept;
    /// True when this key drives that action either way round.
    [[nodiscard]] bool bound(Action action, Key key) const noexcept;

    /// Binds a key, taking it off whatever else had it.
    ///
    /// STEALING IS THE POINT. A rebinding screen that lets two verbs share a key
    /// produces a game where one of them silently stops working, and the player
    /// has no way to find out which. Binding W to Jump un-binds W from Forward,
    /// visibly, and the keys page shows Forward as unbound until it is given
    /// something.
    void bind(Action action, Key key, bool asSecondary = false) noexcept;

    /// Clamps every slider into range. Called after loading, so a hand-edited
    /// file cannot produce an unplayable game.
    void sanitise() noexcept;

    /// The settings file, as text. One `action key key` line per action and one
    /// `set name value` line per preference -- readable, diffable, and editable
    /// by hand, which is a feature and not an accident.
    [[nodiscard]] std::string toText() const;
    /// Reads what toText wrote. UNKNOWN LINES ARE SKIPPED, not fatal: a file
    /// written by a later build has to leave an earlier one playable.
    static ControlSettings fromText(std::string_view text);
};

/// Where the settings live. Beside the executable, because this game has no
/// installer, no launcher and no user-profile directory yet, and a file the
/// player can see and delete is better than one they cannot find.
inline constexpr std::string_view kControlsFileName = "granadad-controls.cfg";

/// Reads the file, or the defaults if it is missing or unreadable. NEVER
/// throws: unplayable controls because a config file went bad is the worst
/// possible failure mode for a config file.
[[nodiscard]] ControlSettings loadControls(const std::filesystem::path& file);
/// Writes it. False on any failure, and the caller carries on -- losing a
/// rebinding is annoying and crashing is worse.
bool saveControls(const ControlSettings& settings, const std::filesystem::path& file);

}  // namespace granadad::render
