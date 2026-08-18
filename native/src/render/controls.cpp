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
// #85. A CLEAN BREAK, NOT A MIGRATION. The `key` column below is the settings
// file's own vocabulary (ControlSettings::toText/fromText), and the file
// format already tolerates drift by design: fromText() starts from defaults()
// and skips any line it does not recognise (actionFromKey returns
// Action::Count for a name this build has never heard of, and the loader
// throws that line away rather than failing). So a save file written by the
// old 38-action build -- "bind examine Q", "bind traverse V", "bind menu
// ESC" -- loses exactly the lines that named a retired action and keeps
// every line that still means the same thing (forward, crouch, jump's old
// key now feeding Vertical under the "interact"-shaped name change, and so
// on are NOT silently reinterpreted as something else; "examine"/"steal"/
// "lift"/"rest"/"traverse"/"drop_down"/"journal"/"keys"/"options"/
// "character"/"map"/"letters"/"walk" simply stop matching and the shipped
// default takes over for those verbs). NOTHING CORRUPTS: an unrecognised
// bind line cannot rebind the wrong verb, because actionFromKey has no
// partial match, only an exact one or Action::Count. This is the right
// call because there are no real players yet to migrate -- stated here
// rather than left for somebody to wonder whether a save format changed out
// from under them.
constexpr ActionNames kActions[] = {
    {Action::Forward, "forward", "FORWARD"},
    {Action::Back, "back", "BACK"},
    {Action::StrafeLeft, "strafe_left", "STEP LEFT"},
    {Action::StrafeRight, "strafe_right", "STEP RIGHT"},
    {Action::TurnLeft, "turn_left", "TURN L"},
    {Action::TurnRight, "turn_right", "TURN R"},
    {Action::Attack, "attack", "ATTACK"},
    {Action::Interact, "interact", "USE"},
    {Action::Crouch, "crouch", "SNEAK"},
    {Action::Vertical, "vertical", "JUMP"},
    {Action::Sprint, "sprint", "RUN"},
    {Action::Menu, "menu", "MENU"},
    {Action::PagePrev, "page_prev", "PAGE <"},
    {Action::PageNext, "page_next", "PAGE >"},
    {Action::Pause, "pause", "PAUSE"},
    {Action::QuickWheel, "quick_wheel", "QUICK WHEEL"},
    {Action::QuickSlot1, "quick_1", "SLOT 1"},
    {Action::QuickSlot2, "quick_2", "SLOT 2"},
    {Action::QuickSlot3, "quick_3", "SLOT 3"},
    {Action::QuickSlot4, "quick_4", "SLOT 4"},
    {Action::QuickSlot5, "quick_5", "SLOT 5"},
    {Action::QuickSlot6, "quick_6", "SLOT 6"},
    {Action::QuickSlot7, "quick_7", "SLOT 7"},
    {Action::QuickSlot8, "quick_8", "SLOT 8"},
    {Action::QuickSlot9, "quick_9", "SLOT 9"},
    {Action::QuickSlot0, "quick_0", "SLOT 10"},
    {Action::QuickNext, "quick_next", "NEXT"},
    {Action::QuickPrev, "quick_prev", "PREV"},
    {Action::Screenshot, "screenshot", "SCREENSHOT"},
    // APPENDED, NOT INSERTED "near Attack". actionKey()/actionLabel() index
    // this table BY ENUM VALUE, so its order must mirror the enum's order --
    // and the enum's insert-only rule puts new actions on the END. The
    // static_assert below only counts rows; it cannot catch a reorder.
    {Action::Cast, "cast", "CAST"},
    {Action::Block, "block", "BLOCK"},
    // #13, THE WARD MAP. "map" was also a pre-#85 retired action name (the
    // old map PAGE, folded into Menu); reintroducing it means a surviving
    // pre-#85 file's "bind map ..." line parses again and lands here --
    // which is the old map key opening the new map, the right outcome, and
    // those files were declared unprotected by #85's clean break anyway.
    {Action::Map, "map", "MAP"},
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

// THE 13 CORE BUTTONS -- exactly the ones controls.hpp's own Action enum
// marks CORE in its doc comments, and exactly the thirteen the "#85: the core
// gameplay button count is what Eli asked for" test counts. This is the list
// fromText()'s whole-file validation pass enforces "at least one live
// binding" against: movement axes, the TurnLeft/TurnRight accessibility
// fallback, and Screenshot (a dev/capture utility, not a Steam-Input-style
// gameplay action) are deliberately not on it. Keep this in step with
// controls.hpp if that list ever changes. Being ON this list is what makes
// the backward-compat guarantee real for Cast and Block: an old settings
// file that has never heard of them leaves their slots at the shipped
// defaults (fromText starts from defaults()), and a file that STEALS their
// keys gets them restored by the validation pass, same as the other ten.
constexpr Action kCoreActions[] = {
    Action::Attack,   Action::Interact, Action::Crouch,    Action::Vertical,
    Action::Sprint,   Action::Menu,     Action::PagePrev,  Action::PageNext,
    Action::Pause,    Action::QuickWheel, Action::Cast,    Action::Block,
    Action::Map,
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

    // #85. THE ~10 CORE BUTTONS. Every one of these carries a pad default now
    // -- the earlier build left all but two Actions with no gamepad binding
    // at all, which is a strange thing to ship for a scheme whose whole point
    // is "small enough to hand to a controller". Face buttons read the way a
    // lot of console action games already train a thumb to expect: A is the
    // primary-action position (Interact, the button pressed the most), X is
    // the attack, Y sits north for "vertical", B is the stance modifier.
    // EVERY ONE OF THESE HAS A PAD KEY NOW, and every one of them had to give
    // something up to fit it: two binding slots per action, one keyboard key
    // that stays and one that moves to the pad. RightShift/RightCtrl were
    // always a redundant duplicate of the other hand's key, not a loss; F and
    // MouseRight were real alternates and are the actual trade -- see each
    // set() below for which.
    set(Action::Sprint, Key::LeftShift, Key::PadLeftStick);
    set(Action::Crouch, Key::LeftCtrl, Key::PadEast);
    set(Action::Vertical, Key::Space, Key::PadNorth);
    // ATTACK PROMOTES THE MOUSE TO PRIMARY. The old Punch had F first and the
    // mouse second, which is backwards from every shooter's own convention.
    // F is freed by giving the pad slot to PadWest instead -- a keyboard
    // player still has the mouse, which is where an attack belongs anyway.
    set(Action::Attack, Key::MouseLeft, Key::PadWest);
    // MouseRight was Interact's old secondary; PadSouth takes that slot
    // instead. E alone is enough on a keyboard, and Interact is the button
    // pressed the most on a pad -- the primary-action position is where it
    // belongs.
    set(Action::Interact, Key::E, Key::PadSouth);

    // ONE SCREEN, PAGES. Tab is where Journal always was -- the row a player
    // already reaches for. THE PAD SIDE MOVED for action #13: PadBack (the
    // Select button) was Menu's from #85 until the ward map arrived, and the
    // owner's own ask -- "a map that they can press M to see... and select
    // on controller" -- put the map there instead, the classic Start/Select
    // split (Pause=Start, Map=Select). Menu takes PadUp, D-pad up, which no
    // shipped action had ever used. fromText() migrates old files that still
    // write Menu's PAD_BACK -- see the MIGRATION comment there.
    set(Action::Menu, Key::Tab, Key::PadUp);
    set(Action::PagePrev, Key::LeftBracket, Key::PadLeftBumper);
    set(Action::PageNext, Key::RightBracket, Key::PadRightBumper);
    // RENAMED FROM Menu, UNCHANGED KEY: this was always Escape's job.
    set(Action::Pause, Key::Escape, Key::PadStart);
    // HELD. Right-stick click sits opposite the left stick that steers, so a
    // thumb already on the stick that is NOT driving movement is the one that
    // opens the wheel -- see the header on why the D-pad, not the stick
    // angle, is what actually picks a slot while this is down.
    set(Action::QuickWheel, Key::Q, Key::PadRightStick);

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
    set(Action::QuickNext, Key::WheelDown);
    set(Action::QuickPrev, Key::WheelUp);
    set(Action::Screenshot, Key::F12);

    // THE COMBAT PAIR, actions 11 and 12 of the 12. No collision either way:
    // C was never a shipped default, and MouseRight has been free since #85
    // moved Interact's old secondary to PadSouth (see Interact's own set()
    // above). The pad side finally spends the two triggers -- the only pad
    // keys the other ten left unused -- in the position every first-person
    // game with a shield puts them: LT guards, RT casts.
    set(Action::Cast, Key::C, Key::PadRightTrigger);
    set(Action::Block, Key::MouseRight, Key::PadLeftTrigger);

    // #13, THE WARD MAP -- the owner's own words for both defaults: "a map
    // that they can press M to see", "and select on controller". M was never
    // a shipped default before this; PadBack is Menu's OLD pad key, freed by
    // moving Menu to PadUp above.
    set(Action::Map, Key::M, Key::PadBack);
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
    // Whether the file ever names the map action at all. A file that does is
    // from a build that knows Map exists (or is a pre-#85 relic reusing the
    // retired name -- see kActions' own note), and either way its author's
    // lines stand as written; a file that does NOT predates action #13 and is
    // what the MIGRATION pass below exists for.
    bool fileNamedMap = false;
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
            if (action == Action::Map) {
                fileNamedMap = true;
            }
            std::string second;
            const bool hasSecond = static_cast<bool>(fields >> second);
            rawApplyBind(candidate, action, keyFromName(first), /*asSecondary=*/false);
            rawApplyBind(candidate, action, hasSecond ? keyFromName(second) : Key::None,
                         /*asSecondary=*/true);
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

    // MIGRATION: ACTION #13 TOOK MENU'S OLD PAD DEFAULT, AND OLD FILES WRITE
    // IT OUT EXPLICITLY. toText() has always written EVERY action's line, so
    // a file saved before Map existed carries "bind menu TAB PAD_BACK" --
    // menu's own OLD shipped default, spelled out -- and parsing it above
    // steals PadBack off Map's shipped secondary, leaving the map with no pad
    // key at all on every controller in the world. That is this project's
    // most-burned bug class (three prior fix rounds on this exact shape), so
    // the rule is stated and implemented EXPLICITLY rather than left to the
    // strand-repair pass below, which never fires here (Map still holds M, so
    // it is not stranded, merely half-dead):
    //
    //   IF the file predates Map (no "bind map" line anywhere) AND Menu came
    //   out of parsing holding PadBack, that PadBack is treated as menu's own
    //   old shipped default carried forward, NOT as a user's custom choice --
    //   the two are indistinguishable from the file (an old toText() wrote
    //   both the same way), and the honest, deterministic fallback the design
    //   settled is: Menu's PadBack slot becomes its NEW shipped pad default
    //   (PadUp), and Map gets PadBack back. Documented outcome, same every
    //   time.
    //
    //   IF a NON-Menu action holds PadBack in a pre-Map file, that binding
    //   could only ever have been a deliberate user choice (PadBack shipped
    //   on Menu alone), so it is respected: Map keeps whatever it still has
    //   (M, unless the file deliberately took that too -- in which case the
    //   validation pass below restores the fully-stranded Map to its whole
    //   shipped default, stealing both keys back, exactly as it would for any
    //   other stranded core action).
    //
    //   IF the file names Map at all, no migration: the author knows the
    //   action exists and their lines stand as written, under the ordinary
    //   validation pass alone.
    if (!fileNamedMap) {
        const std::size_t menuIndex = static_cast<std::size_t>(Action::Menu);
        const std::size_t mapIndex = static_cast<std::size_t>(Action::Map);
        for (int slot = 0; slot < 2; ++slot) {
            const bool asSecondary = slot == 1;
            const Key held = asSecondary ? candidate.secondary[menuIndex]
                                         : candidate.primary[menuIndex];
            if (held != Key::PadBack) {
                continue;
            }
            // Menu's PadBack slot becomes its new shipped pad default. Raw
            // steal semantics, same as every parsed line: PadUp comes off
            // whoever holds it (nobody, in any file old enough to trip this
            // -- PadUp was never a shipped default before Menu's move).
            rawApplyBind(candidate, Action::Menu, Key::PadUp, asSecondary);
            // And Map gets its shipped pad key back, into whichever of its
            // own slots is free (the secondary, unless the file stole M too).
            const bool mapSecondaryFree = candidate.secondary[mapIndex] == Key::None;
            rawApplyBind(candidate, Action::Map, Key::PadBack,
                         /*asSecondary=*/mapSecondaryFree);
            break;
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
