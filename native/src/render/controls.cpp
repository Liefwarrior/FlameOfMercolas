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
    // already reaches for. J is freed the same way MouseRight/F were, for
    // PadBack -- the "show me my stuff" button on most pads already.
    set(Action::Menu, Key::Tab, Key::PadBack);
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
    // STARTS FROM THE DEFAULTS, so a file that mentions three verbs leaves the
    // other thirty playable. A settings file is a diff against the shipped
    // layout, not a replacement for it.
    ControlSettings out = defaults();
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
            const std::size_t index = static_cast<std::size_t>(action);
            // Written straight into the slots rather than through bind(), on
            // purpose: bind() steals, and a whole file applied through it would
            // have each line un-bind the line before whenever two share a key.
            out.primary[index] = keyFromName(first);
            std::string second;
            out.secondary[index] = (fields >> second) ? keyFromName(second) : Key::None;
        } else if (verb == "set") {
            std::string name;
            std::string value;
            if (!(fields >> name >> value)) {
                continue;
            }
            const std::int32_t number = std::atoi(value.c_str());
            const std::string what = lower(name);
            if (what == "sensitivity") {
                out.mouse.sensitivity = number;
            } else if (what == "invert_y") {
                out.mouse.invertY = number != 0;
            } else if (what == "fov") {
                out.fovDegrees = number;
            } else if (what == "pad_deadzone") {
                out.pad.deadzonePercent = number;
            } else if (what == "pad_saturation") {
                out.pad.saturationPercent = number;
            } else if (what == "pad_look") {
                out.pad.lookBamPerSecond = number;
            } else if (what == "pad_trigger_deadzone") {
                out.pad.triggerDeadzonePercent = number;
            }
        }
    }
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
