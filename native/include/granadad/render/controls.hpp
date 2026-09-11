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
/// #85 REBUILT THIS ENUM FROM SCRATCH, and that is a deliberate, one-time
/// exception to the insert-only rule every earlier version of this comment
/// stated. THE RULE HELD BECAUSE OF SAVED SETTINGS FILES, and there are none
/// to protect: this game is unreleased, `granadad-controls.cfg` lives beside
/// an executable nobody outside this repo has run, and the old 38-action
/// table is not worth preserving byte-for-byte just to avoid a diff. See
/// controls.cpp's own header on the migration stance this took instead of
/// silently corrupting an old file: a clean break, stated once, here.
///
/// THE SHAPE OF IT IS OBLIVION'S OWN, PER ELI'S BRIEF, VERBATIM: "a button to
/// swing, a button to 'interact (pickpocket if sneaking)'... only 10-12
/// buttons that need mapped for all the controls." THIRTEEN Actions below are
/// marked CORE -- the ones a player actually has to map, movement axes and
/// the accessibility turn keys excluded, Screenshot excluded (it is a
/// dev/capture utility, not a Steam-Input-style gameplay action). Thirteen is
/// ONE OVER the top of Eli's own 10-12 range, and that bend is his own
/// doing, stated plainly rather than fudged: the first-person combat task
/// spent the last two slots on Cast and Block, and then the owner asked for
/// a map key directly -- "It's too difficult to locate places like the
/// mission, let's give the player a map that they can press M to see", "and
/// select on controller" -- which is the ceiling's own author bending it.
/// Map is #13; the NEXT core verb somebody wants has to consolidate into an
/// existing one, the way Interact and Vertical already did. Six verbs
/// that used to be six keys (Interact, Examine, Steal, Lift, Rest, and the
/// lockpick verb) now resolve out of ONE Interact press, by stance and by
/// what is faced -- see render::Session::interact()'s own header, which is
/// the actual resolution rule this consolidation exists to state. Six more
/// (Jump, Traverse, DropDown) fold into Vertical the same way. Six MENU
/// pages (Journal, Keys, Character, Map, Letters, Options) fold into ONE
/// Menu action with PagePrev/PageNext flipping between them -- Oblivion and
/// Skyrim's own tabbed inventory screen, not a new pattern.
///
/// ORDER IS STILL THE SETTINGS FILE'S ORDER AND THE KEYS PAGE'S ORDER, and
/// FROM THIS POINT ON new actions go on the END again -- the insert-only
/// rule resumes the moment this list ships, because THEN it will be
/// protecting something.
enum class Action : std::uint8_t {
    // --- movement axes: always separate sticks/keys, never one of the 12 ---
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

    // --- the 12 core gameplay buttons (#85; Cast and Block sit appended at
    // the enum's END per the insert-only rule, but count among these) --------

    /// CORE. Was Punch. One button swings whatever is in the hand -- a fist or
    /// a weapon. It is a HELD button, not a one-shot: the down edge starts a
    /// hold clock and the release edge resolves the swing, hard iff the hold
    /// reached the tap/hold boundary (kHardSwingHoldSteps == HoldToggle::
    /// kTapSteps). The client arms a self-guard on the world press so a
    /// page-consumed press fires no swing on its release. See
    /// render::Session::attackDown() / attackUp() (punch() is the legacy tap).
    Attack,
    /// CORE. Was Interact + Examine + Steal + Lift + Rest, and the lockpick
    /// verb Steal used to reach contextually. ONE button, resolved by stance
    /// and by what is faced -- render::Session::interact() is the whole rule,
    /// and render::Session::interactPrompt() is the same rule read for the
    /// HUD instead of acted on, so the label on screen can never say
    /// something this key would not actually do.
    Interact,
    /// CORE. The stance every other resolution keys off -- see stealth.hpp.
    /// Unchanged: still a render::HoldToggle, tap latches, hold holds.
    Crouch,
    /// CORE. Was Jump + Traverse + DropDown. Resolved by what is directly
    /// ahead or below -- render::Session::vertical() is the rule, and see its
    /// header on why the mantle half of Traverse almost never fires from a
    /// keypress at all: MoveInput::autoTraverse already hauls a body over a
    /// ledge it walks into, every ordinary step.
    Vertical,
    /// CORE. Was Sprint + Walk, folded into one HoldToggle: HOLD IT for a
    /// sprint, TAP IT to toggle a persistent walk, hold it again from a
    /// walk-toggle to cancel back to the ordinary jog. See main.cpp's own
    /// comment where `held.sprint`/`held.walk` are derived -- HoldToggle's
    /// existing latch-cancel rule (a press that cancels a latch does not
    /// re-read as "held" for that press) already makes sprint and walk
    /// mutually exclusive with no new code in controls.cpp/hpp at all.
    Sprint,
    /// CORE. Was Journal + Keys + Character + Map + Letters + Options. ONE
    /// screen, PAGES -- render::Session::toggleMenu()/menuPageNext()/
    /// menuPagePrev() orchestrate the six pre-existing toggle*() methods,
    /// which are UNCHANGED: this is a thin router in front of them, not a
    /// rewrite of any one page.
    Menu,
    /// CORE. Flips the Menu's pages backward -- LB on a pad, `[` on a
    /// keyboard (Q and E are Interact's and Attack's near neighbours and
    /// stay clear of them; brackets are free and sit together).
    PagePrev,
    /// CORE. Flips the Menu's pages forward -- RB on a pad, `]` on a
    /// keyboard.
    PageNext,
    /// CORE. Was named Menu. RENAMED to say what it always was: the system
    /// panic/save/quit screen (RESUME/SETTINGS/QUIT), kept deliberately
    /// separate from the new Menu above -- Eli's own brief, point 7: "don't
    /// leave [Options] reachable from both in a way that reads as two
    /// systems." It reads as ONE system here: Pause's SETTINGS row and the
    /// new Menu's Options page are the same optionsOpen_ state, reached two
    /// ways, the way a real pause screen's own shortcuts usually are --
    /// never two copies of the rebinding screen that could drift apart.
    Pause,
    /// CORE. Was QuickSlot1-0 + QuickNext + QuickPrev on a pad: HOLD to open,
    /// the D-pad steps the bar while it is held, release leaves the pick
    /// live. See controls.cpp's defaults for why this is a STEPPER and not a
    /// true radial: render::stickIntent only ever returns a direction's
    /// SIGN, never an angle, so there is no analogue wheel to build without
    /// new plumbing this task did not need to take on -- the D-pad is
    /// already four wired, discrete buttons (main.cpp's kPadTable), which is
    /// what the brief asked for when a stick angle is not readable.
    QuickWheel,

    // --- kept, but NOT one of the 12: the keyboard's plurality of input,
    // not the controller's scarcity of it. A mouse and a full keyboard can
    // afford instant direct shortcuts a pad cannot; QuickWheel is the one
    // button story a controller needs and these are the desktop bonus on
    // top of it, never the other way round. ---------------------------------
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
    /// Mouse-wheel quick-bar stepping. Still real, still rebindable, just not
    /// a pad button any more -- QuickWheel's D-pad already steps the bar on
    /// a controller, so binding these to bumpers as well would be two
    /// controls doing the same job.
    QuickNext,
    QuickPrev,

    /// NOT A GAMEPLAY CONTROL. A dev/capture utility, always F12, excluded
    /// from the ~10-12 count the way Eli's brief asked -- "not part of the
    /// Steam Input action set."
    Screenshot,

    // --- appended post-#85, per the insert-only rule: new actions go on the
    // END, so a saved settings file's action names never shift meaning. These
    // two therefore list AFTER the keyboard bonus bindings on the keys page,
    // which is cosmetic; being CORE is about the count and the validation
    // pass, not the row order. ----------------------------------------------

    /// CORE. Casts the currently equipped spell -- whatever the grimoire has
    /// selected. C on a keyboard, the right trigger on a pad (the genre's
    /// own "magic hand" position). render::Session::castEquipped() is the
    /// resolution rule, including every refusal (nothing equipped, out of
    /// reach, still cooling down) -- the key never does nothing silently.
    Cast,
    /// CORE. HELD, like QuickWheel: down is blocking, up is not, no latch --
    /// a latched guard is a footgun in a brawl. The right mouse button
    /// (freed by #85, which moved Interact's old secondary to PadSouth) and
    /// the left trigger on a pad. Blocking softens incoming blows in a
    /// brawl, scaled by the shieldwall skill -- sim::Tavern::tickBrawl() is
    /// where the held state is actually read.
    Block,
    /// CORE, #13, THE OWNER'S OWN BEND OF HIS 10-12 CEILING (see the enum
    /// header). The ward map: a full-screen top-down render of the district
    /// with the authored sign names on it -- Session::toggleDistrictMap() is
    /// the toggle, render::drawDistrictMap() the page. M on a keyboard (the
    /// owner named the key himself) and PadBack -- the SELECT button -- on a
    /// pad, the classic Start/Select split: Pause=Start, Map=Select. PadBack
    /// was Menu's shipped pad default before this action existed; Menu moved
    /// to PadUp (D-pad up, previously unbound), and fromText() carries an
    /// explicit migration for old files that still write Menu's old default
    /// -- see the MIGRATION comment in controls.cpp's fromText().
    ///
    /// SETTINGS-FILE NAME COLLISION, NOTED HONESTLY: pre-#85 files (the
    /// retired 38-action table) also had a "map" action -- the old map PAGE
    /// key, retired into Menu at #85. A surviving pre-#85 file's "bind map
    /// ..." line therefore parses again and lands on THIS action, which is
    /// semantically the right key doing semantically the right thing (the
    /// old map key opens the new map); #85's clean-break stance already
    /// declared those files unprotected either way.
    Map,

    // --- appended for UI-EA-SPEC sec. 4 violation #5, per the insert-only
    // rule: new actions go on the END. ---------------------------------------

    /// NOT CORE -- the keyboard's plurality again, the quick slots' own
    /// family: a desktop shortcut straight to the controls list. F1 by
    /// default, which is what --help and the ship note have always promised
    /// -- except until now F1 was HARD-CODED in main.cpp, outside the
    /// binding table, so it never printed on the very page it opens and the
    /// rebinding screen could not reach it. A pad reaches the same page
    /// through the pause menu's CONTROLS row; no pad default is spent here.
    KeysPage,
    /// Its sibling: straight to the settings/rebinding page. F2 by default,
    /// previously hard-coded beside F1 and now a bindable action for the
    /// identical reasons. The pause menu's SETTINGS row is the pad's door.
    OptionsPage,
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
