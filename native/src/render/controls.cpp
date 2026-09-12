#include "granadad/render/controls.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "granadad/sim/human_scale.hpp"

namespace granadad::render {

namespace {

struct ActionNames {
    Action action;
    std::string_view key;
    std::string_view label;
    /// ONE SENTENCE SAYING WHAT THE KEY ACTUALLY DOES, for the controls page's
    /// detail pane. The page used to be a list of `KEY  VERB` pairs and nothing
    /// else, so "USE" and "JUMP" -- two verbs that each swallowed five or six
    /// older ones -- told a new player almost nothing about what they had been
    /// consolidated into. The label is what fits beside a key; this is what a
    /// reader gets when they put the cursor on the row.
    ///
    /// DRAWABLE CHARACTERS ONLY. test_copy.cpp sweeps every player-facing
    /// surface for glyphs the 4x6 font does not have, and this is one now.
    std::string_view help;
};

// The table. One row per action, and the ORDER IS THE KEYS PAGE'S ORDER --
// movement, then the ~10 core buttons, then the keyboard's own bonus bindings
// -- because a control list sorted by enum value is a control list nobody
// reads.
//
// EVERY LABEL IS SHORT ENOUGH TO SHARE A COLUMN WITH A KEY NAME. A topic column
// is eighteen glyphs at every resolution this game runs at
// (render/dialogue_view.hpp), the keys page prints "<key>  <label>", and the S10
// review's own finding on the previous version of this page was that it shipped
// "SPACE  UP: MANT." and "E  TALK TO WHOE.". A controls page that arrives
// truncated is worse than none, because a player reads the truncation as the
// binding.
//
// NINE AND THE STICKS -- THE SECOND CLEAN BREAK, stated once (controls.hpp's
// enum header says what was cut and where it went). The `key` column below is
// the settings file's own vocabulary (ControlSettings::toText/fromText), and
// every SURVIVING verb keeps the name it had, so a saved file's "bind attack",
// "bind interact", "bind menu" lines still parse onto the same verb. The
// three retired names -- quick_wheel, page_prev, page_next, keys_page,
// options_page -- stop matching (actionFromKey returns Action::Count and the
// loader drops the line), which is exactly the #85 stance: nothing corrupts,
// because an unrecognised line cannot rebind the wrong verb. What DID move is
// the shipped DEFAULT of five survivors, and a file written by the old build
// spells every old default out explicitly -- see fromText()'s MIGRATION for
// the rule that tells an old default carried forward from a player's own
// choice.
constexpr ActionNames kActions[] = {
    {Action::Forward, "forward", "FORWARD",
     "WALK. HOLD RUN WITH IT TO SPRINT, AND WALK STRAIGHT INTO A LOW LEDGE TO HAUL YOURSELF OVER IT."},
    {Action::Back, "back", "BACK",
     "WALK BACKWARDS, SLOWER THAN YOU CAME, AND WITH NO IDEA WHAT IS BEHIND YOU."},
    {Action::StrafeLeft, "strafe_left", "STEP LEFT",
     "SIDESTEP LEFT WITHOUT TURNING YOUR HEAD. WORTH KNOWING IN A DOORWAY AND IN A BRAWL."},
    {Action::StrafeRight, "strafe_right", "STEP RIGHT",
     "SIDESTEP RIGHT WITHOUT TURNING YOUR HEAD."},
    {Action::TurnLeft, "turn_left", "TURN L",
     "TURN LEFT ON THE SPOT. THE MOUSE DOES THIS BETTER; THE ARROWS ARE HERE FOR PLAYING WITHOUT ONE."},
    {Action::TurnRight, "turn_right", "TURN R",
     "TURN RIGHT ON THE SPOT, FOR WHOEVER PLAYS WITHOUT A MOUSE."},
    // THE NINE. The label is the word the reticle and the rows use; the help
    // is the one sentence the controls page's detail pane prints.
    {Action::Attack, "attack", "SWING",
     "HANDS DOWN, ONE PRESS BRINGS THEM UP AND SWINGS. HOLD IT TO SWING HARD. THE ROOM SEES YOUR FISTS COME UP."},
    {Action::Block, "block", "GUARD",
     "HELD, NEVER LATCHED. SOFTENS WHAT LANDS ON YOU, SCALED BY YOUR SHIELDWALL. FROM HANDS DOWN IT RAISES THEM WITHOUT A BLOW."},
    {Action::Cast, "cast", "CAST",
     "CASTS WHAT THE GRIMOIRE HAS READIED. IT REFUSES OUT LOUD RATHER THAN DOING NOTHING QUIETLY."},
    {Action::Interact, "interact", "USE",
     "ONE BUTTON FOR EVERYTHING IN REACH. IT TALKS, OPENS, LIFTS, PICKS AND ROBS BY YOUR STANCE AND WHAT YOU FACE. NOTHING IN REACH AND FISTS UP: IT LOWERS THEM."},
    {Action::Crouch, "crouch", "SNEAK",
     "DROP INTO A CROUCH. QUIETER, LOWER, AND THE STANCE EVERY THEFT IN THIS GAME RESOLVES OFF."},
    {Action::Vertical, "vertical", "JUMP",
     "GO UP, OR GO DOWN. A JUMP, A HAUL OVER A LEDGE, OR A DROP OFF A ROOF, WHICHEVER THE GROUND AHEAD ALLOWS."},
    {Action::Sprint, "sprint", "RUN",
     "HOLD IT TO SPRINT. LET GO TO JOG. SNEAK IS THE QUIET WALK."},
    {Action::Menu, "menu", "NOTES",
     "YOUR OWN PAPERS ON ONE SCREEN: THE SHEET, THE CHART, THE LETTERS, THE CASEBOOK. PAGE ON PAST THEM TO THE WARD MAP AND THE GRIMOIRE."},
    {Action::Pause, "pause", "PAUSE",
     "RESUME, WAIT, CONTROLS, SETTINGS, QUIT. THE DOCKS KEEP RUNNING WHILE YOU DECIDE, SO DO NOT TAKE ALL NIGHT."},
    // THE TENTH, keyboard only. On a pad the map is a page of NOTES.
    {Action::Map, "map", "MAP",
     "THE WARD MAP: WHERE YOU ARE, WHERE THE NAMED PLACES ARE, AND HOW TO GET FROM ONE TO THE OTHER. ON A PAD IT IS A PAGE OF NOTES."},
    // THE BONUS SHORTCUTS. Each has a door a new player finds without the key.
    {Action::Wait, "wait", "WAIT",
     "PASS THE HOURS SOMEWHERE SAFE. THE PAUSE MENU'S WAIT ROW IS THE SAME DOOR."},
    {Action::QuickSlot1, "quick_1", "SLOT 1",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE FIRST SLOT."},
    {Action::QuickSlot2, "quick_2", "SLOT 2",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE SECOND SLOT."},
    {Action::QuickSlot3, "quick_3", "SLOT 3",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE THIRD SLOT."},
    {Action::QuickSlot4, "quick_4", "SLOT 4",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE FOURTH SLOT."},
    {Action::QuickSlot5, "quick_5", "SLOT 5",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE FIFTH SLOT."},
    {Action::QuickSlot6, "quick_6", "SLOT 6",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE SIXTH SLOT."},
    {Action::QuickSlot7, "quick_7", "SLOT 7",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE SEVENTH SLOT."},
    {Action::QuickSlot8, "quick_8", "SLOT 8",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE EIGHTH SLOT."},
    {Action::QuickSlot9, "quick_9", "SLOT 9",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE NINTH SLOT."},
    {Action::QuickSlot0, "quick_0", "SLOT 10",
     "READIES WHATEVER THE GRIMOIRE BOUND TO THE TENTH SLOT."},
    {Action::QuickNext, "quick_next", "NEXT",
     "STEPS THE QUICK BAR FORWARD. THE WHEEL DOES IT WITHOUT LOOKING DOWN; SO DOES THE PAD'S RIGHT."},
    {Action::QuickPrev, "quick_prev", "PREV",
     "STEPS THE QUICK BAR BACK."},
    {Action::Screenshot, "screenshot", "SCREENSHOT",
     "WRITES A PNG BESIDE THE GAME. A CAPTURE TOOL, NOT A GAMEPLAY CONTROL."},
};
static_assert(sizeof(kActions) / sizeof(kActions[0]) == kActionCount,
              "every action needs a name and a label, or the keys page lies");

struct KeyName {
    Key key;
    std::string_view name;
};

constexpr KeyName kKeys[] = {
    {Key::A, "A"},           {Key::B, "B"},          {Key::C, "C"},
    {Key::D, "D"},           {Key::E, "E"},          {Key::F, "F"},
    {Key::G, "G"},           {Key::H, "H"},          {Key::I, "I"},
    {Key::J, "J"},           {Key::K, "K"},          {Key::L, "L"},
    {Key::M, "M"},           {Key::N, "N"},          {Key::O, "O"},
    {Key::P, "P"},           {Key::Q, "Q"},          {Key::R, "R"},
    {Key::S, "S"},           {Key::T, "T"},          {Key::U, "U"},
    {Key::V, "V"},           {Key::W, "W"},          {Key::X, "X"},
    {Key::Y, "Y"},           {Key::Z, "Z"},          {Key::Num0, "0"},
    {Key::Num1, "1"},        {Key::Num2, "2"},       {Key::Num3, "3"},
    {Key::Num4, "4"},        {Key::Num5, "5"},       {Key::Num6, "6"},
    {Key::Num7, "7"},        {Key::Num8, "8"},       {Key::Num9, "9"},
    {Key::F1, "F1"},         {Key::F2, "F2"},        {Key::F3, "F3"},
    {Key::F4, "F4"},         {Key::F5, "F5"},        {Key::F6, "F6"},
    {Key::F7, "F7"},         {Key::F8, "F8"},        {Key::F9, "F9"},
    {Key::F10, "F10"},       {Key::F11, "F11"},      {Key::F12, "F12"},
    {Key::Up, "UP"},         {Key::Down, "DOWN"},    {Key::Left, "LEFT"},
    {Key::Right, "RIGHT"},   {Key::Space, "SPACE"},  {Key::Enter, "ENTER"},
    {Key::Escape, "ESC"},    {Key::Tab, "TAB"},      {Key::Backspace, "BACKSPACE"},
    {Key::LeftShift, "LSHIFT"}, {Key::RightShift, "RSHIFT"},
    {Key::LeftCtrl, "LCTRL"},   {Key::RightCtrl, "RCTRL"},
    {Key::LeftAlt, "LALT"},     {Key::RightAlt, "RALT"},
    {Key::Minus, "MINUS"},   {Key::Equals, "EQUALS"},
    {Key::Comma, "COMMA"},   {Key::Period, "PERIOD"},
    {Key::Slash, "SLASH"},   {Key::Semicolon, "SEMICOLON"},
    {Key::Apostrophe, "APOSTROPHE"},
    {Key::LeftBracket, "LBRACKET"}, {Key::RightBracket, "RBRACKET"},
    {Key::Backslash, "BACKSLASH"},  {Key::Grave, "GRAVE"},
    {Key::MouseLeft, "MOUSE1"},     {Key::MouseRight, "MOUSE2"},
    {Key::MouseMiddle, "MOUSE3"},   {Key::MouseX1, "MOUSE4"},
    {Key::MouseX2, "MOUSE5"},       {Key::WheelUp, "WHEELUP"},
    {Key::WheelDown, "WHEELDOWN"},
    {Key::PadSouth, "PAD_A"},       {Key::PadEast, "PAD_B"},
    {Key::PadWest, "PAD_X"},        {Key::PadNorth, "PAD_Y"},
    {Key::PadLeftBumper, "PAD_LB"}, {Key::PadRightBumper, "PAD_RB"},
    {Key::PadLeftTrigger, "PAD_LT"},{Key::PadRightTrigger, "PAD_RT"},
    {Key::PadLeftStick, "PAD_LS"},  {Key::PadRightStick, "PAD_RS"},
    {Key::PadStart, "PAD_START"},   {Key::PadBack, "PAD_BACK"},
    {Key::PadUp, "PAD_UP"},         {Key::PadDown, "PAD_DOWN"},
    {Key::PadLeft, "PAD_LEFT"},     {Key::PadRight, "PAD_RIGHT"},
};

[[nodiscard]] std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

[[nodiscard]] std::int32_t clampInt(std::int32_t value, std::int32_t low,
                                    std::int32_t high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

// ---------------------------------------------------------------------------
// fromText()'s own whole-file candidate table. See fromText()'s header for
// why this exists as a second, deliberately UNGUARDED path instead of routing
// every line through ControlSettings::bind().
// ---------------------------------------------------------------------------

// THE NINE, PLUS MAP -- exactly the actions controls.hpp's own Action enum
// marks CORE (and the tenth, the keyboard's own map shortcut), and exactly
// the ten the "nine and the sticks: the core count" test counts. This is the
// list fromText()'s whole-file validation pass enforces "at least one live
// binding" against: movement axes, the TurnLeft/TurnRight accessibility
// fallback, the bonus shortcuts (WAIT, the digits, the wheel) and Screenshot
// are deliberately not on it. Keep this in step with controls.hpp if that
// list ever changes. Being ON this list is what makes the backward-compat
// guarantee real: an old settings file that has never heard of a verb leaves
// its slots at the shipped defaults (fromText starts from defaults()), and a
// file that STEALS its keys gets them restored by the validation pass.
constexpr Action kCoreActions[] = {
    Action::Attack, Action::Block,    Action::Cast,  Action::Interact, Action::Crouch,
    Action::Vertical, Action::Sprint, Action::Menu,  Action::Pause,    Action::Map,
};

// RAW, UNCONDITIONAL steal-and-set on a bare table, with none of
// ControlSettings::bind()'s own strand-refusal guard.
//
// THIS IS DELIBERATE, not a regression of round 1's fix. bind()'s guard
// exists to protect a single LIVE rebind -- one keystroke on the options
// page, applied to a table every other line of the game can already see --
// where refusing a strand outright is the only safe answer, because there is
// no "later" in which some OTHER line of the same file will fix it up.
// fromText()'s whole-file candidate table is not that: every line of the
// settings file gets applied to it before anyone judges whether an action
// ended up reachable, and the judging happens once, as a whole-table
// validation pass (see fromText()), not line by line. Routing that pass
// through bind()'s guard is exactly what broke twice -- round 1's guard let
// an old-format line strand Pause because the file's SECOND relevant line
// (Pause's own still-default entry) never got a chance to be seen as
// "protecting" anything; round 2's fix covered that one shape and then broke
// on a different one, because a per-call guard can only ever reason about
// the ONE call in front of it. A function that cannot strand permanently --
// because whatever it strands, the validation pass after it will notice and
// repair -- does not need to refuse anything up front.
void rawApplyBind(ControlSettings& table, Action action, Key key, bool asSecondary) noexcept {
    const std::size_t index = static_cast<std::size_t>(action);
    if (key != Key::None) {
        for (std::size_t i = 0; i < kActionCount; ++i) {
            if (i == index) {
                continue;  // trading a key between your OWN two slots is fine
            }
            if (table.primary[i] == key) {
                table.primary[i] = Key::None;
            }
            if (table.secondary[i] == key) {
                table.secondary[i] = Key::None;
            }
        }
    }
    (asSecondary ? table.secondary : table.primary)[index] = key;
}

}  // namespace

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------

std::string_view actionKey(Action action) noexcept {
    const std::size_t index = static_cast<std::size_t>(action);
    return index < kActionCount ? kActions[index].key : std::string_view{"?"};
}

std::string_view actionLabel(Action action) noexcept {
    const std::size_t index = static_cast<std::size_t>(action);
    return index < kActionCount ? kActions[index].label : std::string_view{"?"};
}

std::string_view actionHelp(Action action) noexcept {
    const std::size_t index = static_cast<std::size_t>(action);
    return index < kActionCount ? kActions[index].help : std::string_view{};
}

Action actionFromKey(std::string_view name) noexcept {
    const std::string wanted = lower(name);
    for (const ActionNames& row : kActions) {
        if (row.key == wanted) {
            return row.action;
        }
    }
    return Action::Count;
}

std::string_view keyName(Key key) noexcept {
    for (const KeyName& row : kKeys) {
        if (row.key == key) {
            return row.name;
        }
    }
    return "--";
}

Key keyFromName(std::string_view name) noexcept {
    const std::string wanted = upper(name);
    for (const KeyName& row : kKeys) {
        if (row.name == wanted) {
            return row.key;
        }
    }
    return Key::None;
}

// ---------------------------------------------------------------------------
// which device is holding the prompt
// ---------------------------------------------------------------------------

namespace {

/// The prompt vocabulary for the sixteen pad keys -- the OSK's own spoken
/// names, and the same letters kKeys already carries after its "PAD_"
/// prefix, so the controls page's "PAD_A" and a street prompt's "A" are one
/// fact in two registers rather than two facts. PadBack is "SELECT" because
/// that is what everyone calls the button (and what the ship notes have
/// always called it); "BACK" on a prompt would read as a verb.
///
/// DRAWABLE CHARACTERS OR MOTIF SENTINELS ONLY (UI-EA-SPEC sec. 5): the
/// four D-pad keys speak the cross-plus-triangle motif pair now -- two
/// drawn cells where "D-PAD RIGHT" spent eleven -- through panel.cpp's
/// motif table, no font change. Everything else stays the letters, digits,
/// '-' and space hud.cpp's 4x6 font has always had.
constexpr KeyName kPadPromptNames[] = {
    {Key::PadSouth, "A"},          {Key::PadEast, "B"},
    {Key::PadWest, "X"},           {Key::PadNorth, "Y"},
    {Key::PadLeftBumper, "LB"},    {Key::PadRightBumper, "RB"},
    {Key::PadLeftTrigger, "LT"},   {Key::PadRightTrigger, "RT"},
    {Key::PadLeftStick, "LS"},     {Key::PadRightStick, "RS"},
    {Key::PadStart, "START"},      {Key::PadBack, "SELECT"},
    {Key::PadUp, "\x06\x02"},      {Key::PadDown, "\x06\x03"},
    {Key::PadLeft, "\x06\x04"},    {Key::PadRight, "\x06\x05"},
};

}  // namespace

bool keyIsPad(Key key) noexcept { return key >= Key::PadSouth && key <= Key::PadRight; }

InputDevice deviceOfKey(Key key) noexcept {
    return keyIsPad(key) ? InputDevice::Pad : InputDevice::KeyboardMouse;
}

std::string_view promptKeyName(Key key) noexcept {
    for (const KeyName& row : kPadPromptNames) {
        if (row.key == key) {
            return row.name;
        }
    }
    // The 4x6 font has no bracket glyphs -- see the header. "<" and ">" are
    // in its table and are what the keys page already prints for the pair.
    if (key == Key::LeftBracket) {
        return "<";
    }
    if (key == Key::RightBracket) {
        return ">";
    }
    // UI-EA-SPEC sec. 5: the five keys whose NAMES were longer than a drawn
    // keycap. ENTER is the return motif; the arrows are their triangles. One
    // cell each where the words spent two to five -- and because this is the
    // one choke point every prompt reads, the substitution reaches every
    // foot and commit echo without any surface opting in.
    switch (key) {
        case Key::Enter:
            return "\x01";
        case Key::Up:
            return "\x02";
        case Key::Down:
            return "\x03";
        case Key::Left:
            return "\x04";
        case Key::Right:
            return "\x05";
        default:
            break;
    }
    return keyName(key);
}

Key promptKey(const ControlSettings& settings, Action action, InputDevice device) noexcept {
    if (action == Action::Count) {
        return Key::None;
    }
    const std::size_t index = static_cast<std::size_t>(action);
    const Key first = settings.primary[index];
    const Key second = settings.secondary[index];
    // The device's own half first, primary before secondary -- actionFor()'s
    // own resolution order, so the key a prompt names is the key a press
    // resolves through.
    if (first != Key::None && deviceOfKey(first) == device) {
        return first;
    }
    if (second != Key::None && deviceOfKey(second) == device) {
        return second;
    }
    // The other device's half rather than nothing: the verb IS reachable and
    // the prompt's job is to say how, even when the how is on the other hand.
    return first != Key::None ? first : second;
}

std::string_view promptLabel(const ControlSettings& settings, Action action,
                             InputDevice device) noexcept {
    const Key key = promptKey(settings, action, device);
    return key == Key::None ? std::string_view{"--"} : promptKeyName(key);
}

std::string_view promptConfirmKey(InputDevice device) noexcept {
    // UI-EA-SPEC sec. 5: the keyboard's confirm is the return motif -- one
    // drawn cell where "ENTER" spent five. The pad's "A" was already iconic.
    return device == InputDevice::Pad ? "A" : "\x01";
}

std::string_view promptBackKey(InputDevice device) noexcept {
    return device == InputDevice::Pad ? "B" : "ESC";
}

std::string_view promptMoveKeys(InputDevice device) noexcept {
    // UI-EA-SPEC sec. 5: triangles for the arrows, the bare cross for the
    // d-pad -- two cells and one where "UP DOWN" and "D-PAD" spent words.
    return device == InputDevice::Pad ? "\x06" : "\x02\x03";
}

int pageStep(Key key) noexcept {
    // The bumpers and the brackets, raw. Nothing else: TAB is a sub-tab step
    // (below), the D-pad is list movement, and the face buttons keep their
    // bindings.
    switch (key) {
        case Key::LeftBracket:
        case Key::PadLeftBumper:
            return -1;
        case Key::RightBracket:
        case Key::PadRightBumper:
            return 1;
        default:
            return 0;
    }
}

int tabStep(Key key) noexcept {
    // TAB steps forward only, exactly as it always did on the map and the
    // casebook page; the triggers step both ways, Oblivion's own sub-tab
    // pair. In the world the same two triggers are GUARD and SWING -- a page
    // owns the input while it is up, so the raw read here never reaches
    // those bindings (route_menu_key runs ahead of pressed()).
    switch (key) {
        case Key::PadLeftTrigger:
            return -1;
        case Key::Tab:
        case Key::PadRightTrigger:
            return 1;
        default:
            return 0;
    }
}

std::string_view promptPageKeys(InputDevice device) noexcept {
    // The 4x6 font has no bracket glyphs -- see promptKeyName. "< >" is what
    // the pair has always printed.
    return device == InputDevice::Pad ? "LB RB" : "< >";
}

std::string_view promptTabKeys(InputDevice device) noexcept {
    return device == InputDevice::Pad ? "LT RT" : "TAB";
}

std::string_view promptAltCommitKey(InputDevice device) noexcept {
    return device == InputDevice::Pad ? "X" : "T";
}

bool isAltCommitKey(Key key) noexcept { return key == Key::T || key == Key::PadWest; }

Key pageBackRemap(Key key, bool pageOpen) noexcept {
    // One key, one condition, no other business -- see the header on why the
    // whole B seam came down to this being applied in two places instead of
    // one. ONLY East: the D-pad stays raw list movement, the face buttons
    // keep their bindings, and Escape itself is already Escape.
    return pageOpen && key == Key::PadEast ? Key::Escape : key;
}

// ---------------------------------------------------------------------------
// hold and toggle
// ---------------------------------------------------------------------------

bool HoldToggle::activeNow() const noexcept {
    // LATCHED IS ON. HELD IS ON -- unless this press is the one that CANCELLED a
    // latch, in which case the player is holding the key that turned it off and
    // it stays off until they let go. Without that clause, tapping crouch off
    // and keeping your finger on the key crouches you again, which is the exact
    // "toggle and hold fight each other" bug this class exists to not have.
    return latched_ || (held_ && !cancelledLatch_);
}

void HoldToggle::press(std::int64_t stepNow) noexcept {
    if (held_) {
        // Key repeat, or two devices bound to the same verb. Not a new press.
        return;
    }
    held_ = true;
    pressedAt_ = stepNow;
    // PRESSING WHILE LATCHED TURNS IT OFF, and it turns it off IMMEDIATELY
    // rather than on release. That way a tap reads as an instant toggle, and a
    // long press starting from latched-on is a deliberate "off, and stay off
    // until I let go and press again" rather than a mystery.
    cancelledLatch_ = latched_;
    latched_ = false;
}

void HoldToggle::release(std::int64_t stepNow) noexcept {
    if (!held_) {
        return;
    }
    held_ = false;
    const std::int64_t heldFor = stepNow - pressedAt_;
    if (heldFor <= kTapSteps && !cancelledLatch_) {
        latched_ = true;
    }
    cancelledLatch_ = false;
}

void HoldToggle::clear() noexcept {
    held_ = false;
    latched_ = false;
    cancelledLatch_ = false;
}

// ---------------------------------------------------------------------------
// the mouse
// ---------------------------------------------------------------------------

sim::Angle MouseSettings::yawFor(std::int32_t countsX) const noexcept {
    return static_cast<sim::Angle>(countsX * sensitivity);
}

sim::Angle MouseSettings::pitchFor(std::int32_t countsY) const noexcept {
    // Screen y grows downward and pitch grows upward, so the ordinary case is
    // already a negation; invertY is a second one.
    const std::int32_t sign = invertY ? 1 : -1;
    return static_cast<sim::Angle>(sign * countsY * sensitivity);
}

// ---------------------------------------------------------------------------
// the gamepad
// ---------------------------------------------------------------------------

Stick applyDeadzone(Stick raw, const PadSettings& pad) noexcept {
    const std::int32_t dead = clampInt(pad.deadzonePercent, 0, 60) * kStickMax / 100;
    const std::int32_t saturate = clampInt(pad.saturationPercent, 40, 100) * kStickMax / 100;
    if (saturate <= dead) {
        return Stick{};
    }
    const std::int64_t sq = static_cast<std::int64_t>(raw.x) * raw.x +
                            static_cast<std::int64_t>(raw.y) * raw.y;
    const std::int32_t magnitude = static_cast<std::int32_t>(sim::isqrt64(sq));
    if (magnitude <= dead) {
        // RADIAL, so this is the whole vector and not one axis of it. See the
        // header on why the per-axis version makes a stick feel like a d-pad.
        return Stick{};
    }
    const std::int32_t live = magnitude >= saturate ? saturate - dead : magnitude - dead;
    // Rescale to full range: out = raw * (live / (saturate - dead)) / magnitude
    // * kStickMax, all in 64 bits so nothing overflows on the way.
    const std::int64_t numerator = static_cast<std::int64_t>(live) * kStickMax;
    const std::int64_t denominator = static_cast<std::int64_t>(saturate - dead) * magnitude;
    Stick out;
    out.x = static_cast<std::int32_t>((static_cast<std::int64_t>(raw.x) * numerator) / denominator);
    out.y = static_cast<std::int32_t>((static_cast<std::int64_t>(raw.y) * numerator) / denominator);
    out.x = clampInt(out.x, -kStickMax, kStickMax);
    out.y = clampInt(out.y, -kStickMax, kStickMax);
    return out;
}

std::int32_t stickMagnitude(Stick stick) noexcept {
    const std::int64_t sq = static_cast<std::int64_t>(stick.x) * stick.x +
                            static_cast<std::int64_t>(stick.y) * stick.y;
    const std::int32_t magnitude = static_cast<std::int32_t>(sim::isqrt64(sq));
    return magnitude > kStickMax ? kStickMax : magnitude;
}

std::int32_t stickIntent(std::int32_t axis) noexcept {
    // Anything the deadzone let through is a real push. The threshold that
    // matters -- walk against jog against sprint -- is read off the MAGNITUDE
    // by the caller; this is only which way.
    if (axis > 0) {
        return 1;
    }
    if (axis < 0) {
        return -1;
    }
    return 0;
}

sim::Angle padLook(std::int32_t axis, std::int32_t bamPerSecond, std::int32_t steps) noexcept {
    if (axis == 0 || steps <= 0) {
        return 0;
    }
    // CUBIC. t^3 with t as the fraction of full deflection, so a quarter push
    // turns at a sixty-fourth of the rate and a full push turns at all of it.
    const std::int64_t t = axis;
    const std::int64_t cubed = (t * t / kStickMax) * t / kStickMax;  // still signed
    const std::int64_t perStep = static_cast<std::int64_t>(bamPerSecond) * steps /
                                 sim::kStepsPerSecond;
    return static_cast<sim::Angle>(cubed * perStep / kStickMax);
}

// ---------------------------------------------------------------------------
// the settings
// ---------------------------------------------------------------------------

ControlSettings ControlSettings::defaults() noexcept {
    ControlSettings out;
    for (std::size_t i = 0; i < kActionCount; ++i) {
        out.primary[i] = Key::None;
        out.secondary[i] = Key::None;
    }
    const auto set = [&out](Action action, Key first, Key second = Key::None) {
        const std::size_t index = static_cast<std::size_t>(action);
        out.primary[index] = first;
        out.secondary[index] = second;
    };

    // WASD, and the arrows as the accessibility second binding.
    set(Action::Forward, Key::W, Key::Up);
    set(Action::Back, Key::S, Key::Down);
    set(Action::StrafeLeft, Key::A);
    set(Action::StrafeRight, Key::D);
    set(Action::TurnLeft, Key::Left);
    set(Action::TurnRight, Key::Right);

    // NINE AND THE STICKS. Every one of the nine carries a keyboard/mouse
    // half AND a pad half, and the pad half is Oblivion's own layout: the two
    // triggers are the two hands (RT swings, LT guards), RB casts, A uses, B
    // sneaks (and backs out of every page), Y jumps, D-pad up opens the
    // notes, Start pauses, and the left stick's own magnitude picks the gait
    // (L3 is RUN's optional second half). X, R3, LB in the world and the
    // three other D-pad directions are FREE -- an unrecognised press wakes
    // the tutor bands, which is contract (c) doing its job.
    //
    // SWING ON THE RIGHT TRIGGER, NOT X. The owner's sentence is "LMB =
    // fighting mode", and the pad analogue of the mouse's primary is the
    // right trigger -- every shooter, Oblivion included. It also puts GUARD
    // opposite SWING on the two triggers, the pair the ruling names. #85 had
    // Attack on X because the triggers were spent last; they are spent
    // first now.
    set(Action::Attack, Key::MouseLeft, Key::PadRightTrigger);
    set(Action::Block, Key::MouseRight, Key::PadLeftTrigger);
    set(Action::Cast, Key::C, Key::PadRightBumper);
    // MouseRight was Interact's old secondary once; PadSouth has held the
    // slot since #85. E alone is enough on a keyboard, and Interact is the
    // button pressed the most on a pad -- the primary-action position.
    set(Action::Interact, Key::E, Key::PadSouth);
    set(Action::Crouch, Key::LeftCtrl, Key::PadEast);
    set(Action::Vertical, Key::Space, Key::PadNorth);
    set(Action::Sprint, Key::LeftShift, Key::PadLeftStick);
    // ONE SCREEN, PAGES. J IS THE JOURNAL -- the owner's own words: "Use J
    // for journal since that's how it's done by convention." D-pad up on a
    // pad, since #85's parity pass. The pages inside step on the RAW
    // bumpers and brackets (pageStep), which is why no PagePrev/PageNext
    // action exists to bind any more.
    set(Action::Menu, Key::J, Key::PadUp);
    set(Action::Pause, Key::Escape, Key::PadStart);
    // THE TENTH, keyboard only: "a map that they can press M to see". The
    // pad's SELECT used to open it and is WAIT now; on a pad the ward map is
    // one bumper past the casebook inside NOTES.
    set(Action::Map, Key::M);

    // THE BONUS SHORTCUTS. WAIT is Oblivion's own T, and the pad's SELECT
    // (Back), which the map fold freed -- the pause menu's WAIT row is the
    // door a new player finds first on both devices.
    set(Action::Wait, Key::T, Key::PadBack);
    set(Action::QuickSlot1, Key::Num1);
    set(Action::QuickSlot2, Key::Num2);
    set(Action::QuickSlot3, Key::Num3);
    set(Action::QuickSlot4, Key::Num4);
    set(Action::QuickSlot5, Key::Num5);
    set(Action::QuickSlot6, Key::Num6);
    set(Action::QuickSlot7, Key::Num7);
    set(Action::QuickSlot8, Key::Num8);
    set(Action::QuickSlot9, Key::Num9);
    set(Action::QuickSlot0, Key::Num0);
    // THE BAR STEPS ON THE WHEEL AND ON THE D-PAD'S LEFT AND RIGHT -- the
    // QuickWheel's hold-and-step, folded into two plain presses on the two
    // D-pad directions the world never used (D-pad up is NOTES; down is
    // free). No hold to teach, no radial to build: render::stickIntent only
    // ever returned a direction's sign, and a stepper was always the honest
    // shape.
    set(Action::QuickNext, Key::WheelDown, Key::PadRight);
    set(Action::QuickPrev, Key::WheelUp, Key::PadLeft);
    set(Action::Screenshot, Key::F12);
    return out;
}

Action ControlSettings::actionFor(Key key) const noexcept {
    if (key == Key::None) {
        return Action::Count;
    }
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (primary[i] == key || secondary[i] == key) {
            return static_cast<Action>(i);
        }
    }
    return Action::Count;
}

bool ControlSettings::bound(Action action, Key key) const noexcept {
    if (action == Action::Count || key == Key::None) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(action);
    return primary[index] == key || secondary[index] == key;
}

void ControlSettings::bind(Action action, Key key, bool asSecondary) noexcept {
    if (action == Action::Count) {
        return;
    }
    if (key != Key::None) {
        // THE COLLISION GUARD. Stealing is still the point -- see the header --
        // but stealing a key from an action that has NO OTHER key leaves that
        // action reachable by nothing at all, on any device, and nothing about
        // that is visible: its row still prints the old key, right up until
        // somebody presses it and nothing happens. #85's control-consolidation
        // migration bug was exactly this, one level removed -- an old save's
        // "bind menu ESC" line (menu WAS pause, pre-#85) landed Escape on the
        // new Menu action while Pause's own still-Escape default sat there
        // unmentioned, and actionFor() silently preferred Menu, the lower enum
        // index. REFUSE rather than strand: if taking `key` would leave some
        // OTHER action with nothing on either slot, this bind is a no-op --
        // the same outcome an unrecognised settings-file line already has --
        // and the key stays exactly where it was.
        const std::size_t askingIndex = static_cast<std::size_t>(action);
        for (std::size_t i = 0; i < kActionCount; ++i) {
            if (i == askingIndex) {
                continue;  // trading a key between your OWN two slots is fine
            }
            const bool holdsPrimary = primary[i] == key;
            const bool holdsSecondary = secondary[i] == key;
            if (!holdsPrimary && !holdsSecondary) {
                continue;
            }
            const Key otherSlot = holdsPrimary ? secondary[i] : primary[i];
            if (otherSlot == Key::None) {
                return;
            }
        }
        // STOLEN, VISIBLY. See the header: two verbs sharing a key is a game
        // where one of them silently stops working.
        for (std::size_t i = 0; i < kActionCount; ++i) {
            if (primary[i] == key) {
                primary[i] = Key::None;
            }
            if (secondary[i] == key) {
                secondary[i] = Key::None;
            }
        }
    }
    const std::size_t index = static_cast<std::size_t>(action);
    (asSecondary ? secondary : primary)[index] = key;
}

void ControlSettings::sanitise() noexcept {
    mouse.sensitivity = clampInt(mouse.sensitivity, kMinSensitivity, kMaxSensitivity);
    fovDegrees = clampInt(fovDegrees, kMinFov, kMaxFov);
    pad.deadzonePercent = clampInt(pad.deadzonePercent, 0, 60);
    pad.saturationPercent = clampInt(pad.saturationPercent, 40, 100);
    pad.triggerDeadzonePercent = clampInt(pad.triggerDeadzonePercent, 0, 80);
    pad.lookBamPerSecond = clampInt(pad.lookBamPerSecond, 2000, 200000);
    if (pad.saturationPercent <= pad.deadzonePercent) {
        pad.saturationPercent = std::min(100, pad.deadzonePercent + 10);
    }
}

std::string ControlSettings::toText() const {
    std::ostringstream out;
    out << "# Granadad: The Darkstreets -- controls.\n"
        << "#\n"
        << "# Rebind in the game (the OPTIONS page) or here; either way this\n"
        << "# file is what survives. One line per verb:\n"
        << "#\n"
        << "#   bind <verb> <key> [second key]\n"
        << "#\n"
        << "# Key names are what the keys page prints: W, LSHIFT, SPACE, MOUSE2,\n"
        << "# WHEELUP, PAD_A. `none` unbinds. A verb this file does not mention\n"
        << "# keeps the shipped default, and a line this build does not\n"
        << "# understand is ignored rather than fatal.\n\n";
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        out << "bind " << actionKey(action) << ' ' << keyName(primary[i]);
        if (secondary[i] != Key::None) {
            out << ' ' << keyName(secondary[i]);
        }
        out << '\n';
    }
    out << "\nset sensitivity " << mouse.sensitivity << '\n'
        << "set invert_y " << (mouse.invertY ? 1 : 0) << '\n'
        << "set fov " << fovDegrees << '\n'
        << "set pad_deadzone " << pad.deadzonePercent << '\n'
        << "set pad_saturation " << pad.saturationPercent << '\n'
        << "set pad_look " << pad.lookBamPerSecond << '\n'
        << "set pad_trigger_deadzone " << pad.triggerDeadzonePercent << '\n';
    return out.str();
}

namespace {

/// NINE AND THE STICKS -- THE MIGRATION TABLE. An old toText() wrote EVERY
/// action's line, old default and custom choice spelled identically, so a
/// file from before the break carries "bind attack MOUSE1 PAD_X" whether
/// the player ever touched Attack or not. The honest, deterministic rule:
/// in a file that PREDATES the break, a slot that holds the verb's OLD
/// shipped key is the old default carried forward and becomes the NEW
/// shipped key; a slot that holds anything else is the player's own hand
/// and stands as written. Per SLOT, not per line, so a player who moved
/// only the keyboard half keeps that half and still gets the pad half the
/// break moved. The five verbs whose defaults moved, and the two older
/// shapes Menu was shipped in before this (TAB then J on the keyboard,
/// SELECT then D-pad up on the pad):
struct SlotMigration {
    Action action;
    bool secondary;
    Key from;
    Key to;
};
constexpr SlotMigration kSlotMigrations[] = {
    {Action::Attack, true, Key::PadWest, Key::PadRightTrigger},
    {Action::Cast, true, Key::PadRightTrigger, Key::PadRightBumper},
    {Action::Map, true, Key::PadBack, Key::None},
    {Action::Menu, false, Key::Tab, Key::J},
    {Action::Menu, true, Key::PadBack, Key::PadUp},
    {Action::QuickNext, true, Key::None, Key::PadRight},
    {Action::QuickPrev, true, Key::None, Key::PadLeft},
};

/// The action names the break RETIRED. A file that names any of them was
/// written by the old build -- the one certain signal, since every old
/// toText() wrote all of them and no new build ever will. A hand-trimmed
/// file that names none is read as written: nothing in it is guessed at.
constexpr std::string_view kRetiredActionNames[] = {
    "quick_wheel", "page_prev", "page_next", "keys_page", "options_page",
};

[[nodiscard]] bool isRetiredActionName(std::string_view name) noexcept {
    for (const std::string_view retired : kRetiredActionNames) {
        if (retired == name) {
            return true;
        }
    }
    return false;
}

/// One slot of one parsed line, through the migration table: the new key
/// when the old default is what the file carries, the file's own key
/// otherwise.
[[nodiscard]] Key migrateSlot(Action action, bool secondary, Key parsed) noexcept {
    for (const SlotMigration& row : kSlotMigrations) {
        if (row.action == action && row.secondary == secondary && row.from == parsed) {
            return row.to;
        }
    }
    return parsed;
}

}  // namespace

ControlSettings ControlSettings::fromText(std::string_view text) {
    // WHOLE-FILE, THEN VALIDATE, THEN ADOPT. THIRD ATTEMPT AT THIS FILE'S ONE
    // BUG, and the first two both patched a single bind() call site -- a
    // collision guard gated on `key != Key::None` (round 1), and stealing
    // through bind()'s existing guard call-by-call for every line (round 2,
    // routed through bind() below this comment used to). Both broke on the
    // NEXT adversarial line ordering, because a guard on one call can only
    // ever see the one call in front of it: fromText() applies a "bind"
    // line's primary key and secondary key as two SEPARATE calls, so a line
    // with no secondary key writes Key::None as its own second call, and
    // nothing that guards `key != Key::None` fires for that write at all --
    // it wipes whatever the FIRST call's steal happened to leave behind,
    // unconditionally. Patching that call site a third time just moves the
    // hole to whatever ordering this task's own adversarial test does not
    // happen to try.
    //
    // So this does not judge anything line by line any more. It parses the
    // WHOLE file onto a bare candidate table with no strand-refusal at all
    // (rawApplyBind, above -- steal is unconditional, same as bind() minus
    // its own guard), then, ONCE, after every line has landed, VALIDATES the
    // finished table as a unit: does every CORE action still hold at least
    // one live key. Anything that does not gets ITS OWN shipped default
    // back -- not the whole file's defaults, just the one stranded verb --
    // and that restoration steals through rawApplyBind exactly like a real
    // bind line would, so the recovered action is actually REACHABLE and not
    // just holding a key value nobody's actionFor() will ever return for it.
    //
    // Two explicit lines in the SAME file naming two DIFFERENT actions --a
    // deliberate two-key swap a player actually asked for -- never trips
    // this at all: both actions come out of parsing with a live key each,
    // the validation pass has nothing to do, and the swap stands exactly as
    // written. See "the collision guard does not block a legitimate
    // two-action key swap" and the broader adversarial-ordering case below.
    const ControlSettings shipped = defaults();
    // STARTS FROM THE DEFAULTS, so a file that mentions three verbs leaves the
    // other thirty playable. A settings file is a diff against the shipped
    // layout, not a replacement for it.
    ControlSettings candidate = shipped;

    // MIGRATION, NINE AND THE STICKS: WHICH BUILD WROTE THIS FILE. Decided
    // over the whole file BEFORE a line is applied, off the one certain
    // signal -- a retired action name (see kRetiredActionNames). An old file
    // then gets the per-slot rule in kSlotMigrations; a file from this build
    // (or a hand-trimmed one naming no retired verb) is applied exactly as
    // written. The retired lines themselves are dropped either way, which is
    // what frees Q, R3, the bumpers and the F-keys for their new jobs.
    bool preBreak = false;
    {
        std::istringstream scan{std::string(text)};
        std::string line;
        while (std::getline(scan, line)) {
            std::istringstream fields(line);
            std::string word;
            std::string actionName;
            if (!(fields >> word >> actionName)) {
                continue;
            }
            if (lower(word) == "bind" && isRetiredActionName(lower(actionName))) {
                preBreak = true;
                break;
            }
        }
    }

    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string word;
        if (!(fields >> word)) {
            continue;
        }
        if (word.empty() || word[0] == '#') {
            continue;
        }
        const std::string verb = lower(word);
        if (verb == "bind") {
            std::string actionName;
            std::string first;
            if (!(fields >> actionName >> first)) {
                continue;
            }
            const Action action = actionFromKey(actionName);
            if (action == Action::Count) {
                continue;
            }
            std::string second;
            const bool hasSecond = static_cast<bool>(fields >> second);
            Key firstKey = keyFromName(first);
            Key secondKey = hasSecond ? keyFromName(second) : Key::None;
            if (preBreak) {
                firstKey = migrateSlot(action, /*secondary=*/false, firstKey);
                secondKey = migrateSlot(action, /*secondary=*/true, secondKey);
            }
            rawApplyBind(candidate, action, firstKey, /*asSecondary=*/false);
            rawApplyBind(candidate, action, secondKey, /*asSecondary=*/true);
        } else if (verb == "set") {
            std::string name;
            std::string value;
            if (!(fields >> name >> value)) {
                continue;
            }
            const std::int32_t number = std::atoi(value.c_str());
            const std::string what = lower(name);
            if (what == "sensitivity") {
                candidate.mouse.sensitivity = number;
            } else if (what == "invert_y") {
                candidate.mouse.invertY = number != 0;
            } else if (what == "fov") {
                candidate.fovDegrees = number;
            } else if (what == "pad_deadzone") {
                candidate.pad.deadzonePercent = number;
            } else if (what == "pad_saturation") {
                candidate.pad.saturationPercent = number;
            } else if (what == "pad_look") {
                candidate.pad.lookBamPerSecond = number;
            } else if (what == "pad_trigger_deadzone") {
                candidate.pad.triggerDeadzonePercent = number;
            }
        }
    }

    // THE WHOLE-TABLE VALIDATION PASS. Every line of the file has already
    // landed on `candidate` by this point, and NOTHING has looked at whether
    // any action is reachable yet -- that question is asked exactly once,
    // here, over the finished table, not once per bind() call the way both
    // earlier rounds asked it.
    //
    // BOUNDED, NOT ONE PASS. Restoring one stranded CORE action steals its
    // shipped keys back from whoever the file gave them to -- which can be
    // ANOTHER core action that was fine a moment ago (its own shipped keys
    // might be the very ones just reclaimed). One pass therefore is not
    // always enough; a pass that repairs nothing is always enough, because
    // there is nothing left to repair. Capped at kActionCount passes purely
    // as a termination proof for a loop that should never need more than two
    // or three: each restoration can only ever affect the handful of actions
    // whose slots it steals from, so the chain cannot outrun the action
    // table itself.
    for (std::size_t pass = 0; pass < kActionCount; ++pass) {
        bool strandedAny = false;
        for (const Action action : kCoreActions) {
            const std::size_t index = static_cast<std::size_t>(action);
            if (candidate.primary[index] != Key::None || candidate.secondary[index] != Key::None) {
                continue;  // reachable by at least one live key -- nothing to do
            }
            // STRANDED. Give it back ITS OWN shipped default, stealing those
            // specific keys off whoever holds them now -- a plain overwrite
            // without the steal would leave two actions holding the same
            // key value, and actionFor()'s first-match-by-enum-index rule
            // would then resolve that key to whichever action comes first,
            // which is exactly the original #85 migration bug one level
            // removed: "has a key" is not "is reachable."
            rawApplyBind(candidate, action, shipped.primary[index], /*asSecondary=*/false);
            rawApplyBind(candidate, action, shipped.secondary[index], /*asSecondary=*/true);
            strandedAny = true;
        }
        if (!strandedAny) {
            break;
        }
    }

    // ATOMIC ADOPTION. Nothing before this line is the return value -- every
    // intermediate shape `candidate` passed through while parsing, and every
    // shape it passed through while the validation loop above was still
    // repairing a cascade, stays local to this function. The caller (and a
    // second, concurrent loadControls() elsewhere) only ever sees the fully
    // parsed, fully validated table below, in one assignment.
    ControlSettings out = candidate;
    out.sanitise();
    return out;
}

ControlSettings loadControls(const std::filesystem::path& file) {
    std::error_code ignored;
    if (!std::filesystem::exists(file, ignored)) {
        return ControlSettings::defaults();
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return ControlSettings::defaults();
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return ControlSettings::fromText(buffer.str());
}

bool saveControls(const ControlSettings& settings, const std::filesystem::path& file) {
    std::error_code ignored;
    const std::filesystem::path parent = file.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ignored);
    }
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << settings.toText();
    return static_cast<bool>(output);
}

}  // namespace granadad::render
