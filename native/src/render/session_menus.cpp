// Session menus: pause, options, the keys page, the quick bar, the wait page and the tiled menu.
#include "session_internal.hpp"

// THE AUDIO WIRING PASS, as in session.cpp: this file speaks to the audio engine in ids only; see
// setAudio() in session.hpp and the determinism note in audio_engine.hpp.
#include "granadad/audio/audio_engine.hpp"

namespace granadad::render {

/// PUTS THE NOTES AND THE KEY LIST DOWN. Called by every verb that acts on the
/// world, so a player who presses a game key with a menu up gets the game and
/// not a silent refusal -- and so the opening page can never be in the way of
/// the second thing a new player does.
void Session::dismissOverlays() noexcept {
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    waitOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    awaitingKey_ = false;
    firstRun_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    menuFocus_ = kMenuFocusJournal;
    // TASK #83. Ten call sites deep (climb, dropDown, steal, toggleCrouch,
    // lift, setCrouched, jump, interact, punch, restHere) and every one of
    // them can be the thing that closes a panel a player left open. One call
    // here catches all ten rather than repeating it at each.
    syncPanelAnim();
}

// ---------------------------------------------------------------------------
// #77: the controls, and the page that changes them
// ---------------------------------------------------------------------------

void Session::noteInputDevice(InputDevice device) {
    if (device == promptDevice_) {
        return;
    }
    // THE ONE STORED PROMPT. Every other prompt in this file is assembled at
    // draw time and re-words itself by construction; the opening hint is
    // state with a twelve-second life, so the hand-over re-words it in
    // place -- but ONLY while it is still the opening hint. A bark or an
    // alert that has since taken message_ is not this function's to touch.
    const bool hintUp = !message_.empty() && message_ == openingHintLine(controls_, promptDevice_);
    promptDevice_ = device;
    if (hintUp) {
        message_ = openingHintLine(controls_, promptDevice_);
        // UI-EA (LANE HUD): the tutor tier raises in full on a device
        // change. The hint's four seconds shrank from twelve in the word
        // diet, so a hand-over mid-read gets the whole read back in the new
        // vocabulary rather than the tail of the old one's clock.
        messageSteps_ = std::max(messageSteps_, 60 * 4);
    }
}

void Session::noteInputKey(Key key) {
    if (key == Key::None) {
        return;  // nobody spoke; Key::None must not read as the keyboard
    }
    noteInputDevice(deviceOfKey(key));
}

namespace {

/// WHICH FAMILY A VERB BELONGS TO, and therefore what colour its row is on the
/// controls page. Movement, the things you do, the screens you open, the quick
/// bar. See render/keys_page.hpp -- one accent per family is what makes a
/// thirty-row page scannable before a word of it is read.
[[nodiscard]] int keyGroupFor(Action action) noexcept {
    switch (action) {
        case Action::Forward:
        case Action::Back:
        case Action::StrafeLeft:
        case Action::StrafeRight:
        case Action::TurnLeft:
        case Action::TurnRight:
            return kKeysGroupMove;
        case Action::Menu:
        case Action::Pause:
        case Action::Map:
        case Action::Wait:
        case Action::Screenshot:
            return kKeysGroupScreen;
        case Action::QuickNext:
        case Action::QuickPrev:
            return kKeysGroupQuick;
        default:
            break;
    }
    if (action >= Action::QuickSlot1 && action <= Action::QuickSlot0) {
        return kKeysGroupQuick;
    }
    return kKeysGroupAct;
}

/// THE ROWS THE CONTROLS PAGE ADDS TO THE BINDINGS, and the only ones it still
/// hard-codes.
///
/// Everything a key is BOUND to is generated from ControlSettings -- see the
/// note in keyPageRows(). These are not bindings: they are what walking into
/// a ledge does, what the movement keys, JUMP, SWING and BACK do WHILE A
/// WIRE IS IN A LOCK, and -- NINE AND THE STICKS -- the PAGE GRAMMAR every
/// page reads raw ahead of any binding (confirm, back, the ring, the
/// sub-tabs, the second commit, the cursor). They carry bindable=false, and
/// the detail pane says so with a state label where the rebind verb would
/// otherwise be. Every keycap is the LIVE hand's (a pad reads A / B / LB RB /
/// LT RT / X here, a keyboard the return motif / ESC / < > / TAB / T), the
/// way every foot in the game reads.
///
/// `listRow` is the exact string the flat keyRows() list has always printed for
/// them, kept because two cases assert on it and because the one-line wording
/// genuinely differs from the two-column wording -- see KeysPageRow::listRow.
[[nodiscard]] std::vector<KeysPageRow> contextualRows(const ControlSettings& controls,
                                                      InputDevice device) {
    const bool pad = device == InputDevice::Pad;
    const std::string jump(promptLabel(controls, Action::Vertical, device));
    // MOUSE1 does not fit the flat list's sixteen cells beside "LOCK:" and
    // "FORCE"; the drawn page prints the full keycap, the flat row says LMB.
    const std::string swing(promptLabel(controls, Action::Attack, device));
    const std::string swingShort = swing == "MOUSE1" ? std::string("LMB") : swing;
    const std::string back(promptBackKey(device));
    const std::string confirm(promptConfirmKey(device));
    const std::string pages(promptPageKeys(device));
    const std::string tabs(promptTabKeys(device));
    const std::string second(promptAltCommitKey(device));
    const std::string cursor(promptMoveKeys(device));
    std::vector<KeysPageRow> rows;

    // ON EVERY PAGE -- the grammar, said once in controls.hpp and printed
    // here so a stranger can read it before they need it.
    rows.push_back(KeysPageRow{confirm, "CONFIRM", "",
                               "PICKS THE ROW UNDER THE CURSOR. ON A PAGE WITH A COMMIT "
                               "VERB AT ITS FOOT, THIS IS IT.",
                               false, confirm + "  CONFIRM", kKeysGroupPage});
    rows.push_back(KeysPageRow{back, "BACK", "",
                               "OUT OF WHATEVER IS OPEN, ONE LAYER AT A TIME. THE PAUSE MENU "
                               "COMES BACK BEFORE THE STREET DOES.",
                               false, back + "  BACK", kKeysGroupPage});
    rows.push_back(KeysPageRow{pages, "PAGE THE NOTES", "",
                               "THROUGH YOUR PAPERS: THE SHEET, THE CHART, THE LETTERS, THE "
                               "CASEBOOK, THE WARD MAP, THE GRIMOIRE, AND ROUND AGAIN.",
                               false, pages + "  PAGE", kKeysGroupPage});
    rows.push_back(KeysPageRow{tabs, "NEXT VIEW", "",
                               "THE VIEWS INSIDE ONE PAGE: THE WARD MAP'S FOUR, THE "
                               "CASEBOOK'S LEADS AND CASE, KEYS AND OPTIONS.",
                               false, tabs + "  VIEW", kKeysGroupPage});
    rows.push_back(KeysPageRow{second, "TRAVEL / TAKE", "",
                               "THE PAGE'S SECOND VERB. ON THE WARD MAP IT TRAVELS TO THE "
                               "PLACE UNDER THE CURSOR. ACROSS A COUNTER IT TAKES THEIR PRICE.",
                               false, second + "  TRAVEL", kKeysGroupPage});
    rows.push_back(KeysPageRow{cursor, "CURSOR", "",
                               pad ? "THE D-PAD AND THE LEFT STICK WALK EVERY LIST."
                                   : "THE ARROWS WALK EVERY LIST, AND THE DIGITS PICK A ROW "
                                     "OUTRIGHT. 0 IS MORE WHERE A LIST PAGES.",
                               false, cursor + "  CURSOR", kKeysGroupPage});

    // WHILE PICKING A LOCK -- a mode the simulation is in, not a verb with a
    // key of its own.
    rows.push_back(KeysPageRow{"WALK", "CLIMB A LEDGE", "",
                               "NO KEY FOR THIS -- THAT IS THE POINT. WALK AT A LOW LEDGE "
                               "AND YOU HAUL YOURSELF UP.",
                               false, "WALK AT A LEDGE", kKeysGroupNote});
    rows.push_back(KeysPageRow{pad ? std::string("\x06\x02 \x06\x03") : std::string("W  S"),
                               "AIM THE WIRE", "",
                               "TURNS THE WIRE IN THE LOCK. THE WARDS ARE NOT ALL AT THE "
                               "SAME DEPTH AND YOU HAVE TO FEEL FOR EACH ONE.",
                               false, pad ? "LOCK: \x06\x02\x06\x03  AIM" : "LOCK: W S  AIM",
                               kKeysGroupNote});
    rows.push_back(KeysPageRow{jump, "TRY THE PIN", "",
                               "PUSHES AT THE PIN YOU ARE AIMED AT. GET IT WRONG AND THE "
                               "WIRE COMPLAINS, LOUDLY, TO ANYBODY IN THE ROOM.",
                               false, "LOCK: " + jump + " TRY", kKeysGroupNote});
    rows.push_back(KeysPageRow{swing, "FORCE IT", "",
                               "STOPS BEING SUBTLE. FAST, CRUDE, AND IT COSTS YOU THE WIRE "
                               "MORE OFTEN THAN NOT.",
                               false, "LOCK: " + swingShort + " FORCE", kKeysGroupNote});
    rows.push_back(KeysPageRow{back, "LEAVE THE LOCK", "",
                               "TAKES THE WIRE BACK OUT. THE LOCK REMEMBERS NOTHING AND "
                               "NEITHER DOES THE WATCH, IF YOU ARE QUICK.",
                               false, "LOCK: " + back + "  OUT", kKeysGroupNote});
    return rows;
}

}  // namespace

std::vector<KeysPageRow> Session::keyPageRows() const {
    // GENERATED FROM THE LIVE BINDINGS, and that is the whole point of the
    // change. This page used to be a static array of strings sitting a hundred
    // lines away from a switch statement in the client, and its own comment
    // said it lived here so it "does not drift from them the moment somebody
    // rebinds one without looking down". It could not help drifting: nothing
    // connected the two. Now a rebinding shows up here by construction, because
    // this IS the binding table read out loud.
    //
    // NINE AND THE STICKS: IN THE ORDER A PLAYER READS, NOT THE ENUM'S. The
    // nine first, the map and WAIT, then movement, the bar, the shutter --
    // so the nine are the first thing on the first screen at every size.
    // And in the LIVE HAND'S OWN WORDS: the KEY column is the device that
    // last spoke (promptKey's own order), spelled the way every foot in the
    // game spells a key (promptKeyName: RT, SELECT, the D-pad cross, the
    // return motif -- never the settings file's PAD_RT), and the ALSO column
    // is the other hand. A pad's movement is its two sticks, not six axes it
    // does not have; a pad's map is a page of NOTES and its row says so.
    const InputDevice dev = promptDevice_;
    const bool pad = dev == InputDevice::Pad;
    std::vector<KeysPageRow> rows;
    rows.reserve(kActionCount + 14);
    const auto bound = [&](Action action) {
        const std::size_t i = static_cast<std::size_t>(action);
        KeysPageRow row;
        row.verb = std::string(actionLabel(action));
        row.help = std::string(actionHelp(action));
        row.group = keyGroupFor(action);
        row.bindable = true;
        const Key first = promptKey(controls_, action, dev);
        const Key other =
            first == controls_.primary[i] ? controls_.secondary[i] : controls_.primary[i];
        if (first != Key::None) {
            row.binding = std::string(promptKeyName(first));
        }
        if (other != Key::None) {
            row.alternate = std::string(promptKeyName(other));
        }
        return row;
    };

    // THE NINE.
    for (const Action action : {Action::Attack, Action::Block, Action::Cast, Action::Interact,
                                Action::Crouch, Action::Vertical, Action::Sprint, Action::Menu,
                                Action::Pause}) {
        rows.push_back(bound(action));
    }
    // THE TENTH, and how a pad actually reaches it: NOTES, then RB.
    {
        KeysPageRow map = bound(Action::Map);
        if (pad) {
            map.alternate = map.binding;
            map.binding = std::string(promptKeyName(Key::PadUp)) + " RB";
            map.bindable = false;
        }
        rows.push_back(map);
    }
    rows.push_back(bound(Action::Wait));

    // MOVEMENT: the sticks, or the mouse and the six axes.
    if (pad) {
        rows.push_back(KeysPageRow{std::string(promptKeyName(Key::PadLeftStick)), "MOVE", "",
                                   "PUSH TO WALK, FURTHER TO JOG, ALL THE WAY TO SPRINT. HOW "
                                   "FAR YOU PUSH IS YOUR GAIT.",
                                   false, "LS  MOVE", kKeysGroupMove});
        rows.push_back(KeysPageRow{std::string(promptKeyName(Key::PadRightStick)), "LOOK", "",
                                   "WHERE YOU POINT IS WHERE YOU LOOK. A SMALL PUSH AIMS, A "
                                   "FULL PUSH TURNS ROUND.",
                                   false, "RS  LOOK", kKeysGroupMove});
    } else {
        rows.push_back(KeysPageRow{"MOUSE", "LOOK", "",
                                   "WHERE YOU POINT IS WHERE YOU LOOK. RAW, WITH NO SMOOTHING "
                                   "AND NO ACCELERATION -- THE SENSITIVITY IS ON THE OPTIONS "
                                   "PAGE.",
                                   false, "MOUSE  LOOK", kKeysGroupMove});
        for (const Action action : {Action::Forward, Action::Back, Action::StrafeLeft,
                                    Action::StrafeRight, Action::TurnLeft, Action::TurnRight}) {
            rows.push_back(bound(action));
        }
    }

    // THE QUICK BAR: one row for the ten digits (keyboard only), the two
    // steps, and the shutter (a keyboard's F12).
    if (!pad) {
        KeysPageRow bar;
        bar.binding =
            std::string(keyName(controls_.primary[static_cast<std::size_t>(Action::QuickSlot1)])) +
            "-" +
            std::string(keyName(controls_.primary[static_cast<std::size_t>(Action::QuickSlot0)]));
        bar.verb = "QUICK BAR";
        bar.help = "THE NUMBER ROW READIES A CRAFTING WITH NOTHING OPEN. THE "
                   "OPTIONS PAGE LISTS ALL TEN.";
        bar.group = kKeysGroupQuick;
        bar.bindable = true;
        rows.push_back(std::move(bar));
    }
    rows.push_back(bound(Action::QuickNext));
    rows.push_back(bound(Action::QuickPrev));
    if (!pad) {
        rows.push_back(bound(Action::Screenshot));
    }

    for (KeysPageRow& row : contextualRows(controls_, dev)) {
        rows.push_back(std::move(row));
    }
    return rows;
}

KeysPageState Session::keysPageState() const {
    KeysPageState state;
    state.open = keysOpen_;
    state.title = "CONTROLS";
    // AND THE BUILD, HERE, WHERE A PLAYER GOES LOOKING FOR IT -- right-aligned
    // in the tab row, which is where the reference puts the number you want on
    // screen while you read the page. It used to be burnt into the top-left
    // corner of every captured frame at full HUD scale.
    state.readout = "GRANADAD " + std::string(sim::build_info().version);
    // The intro prose is retired (UI-EA-SPEC 1.7 #36): the tab row is the
    // header, the table is the page, and the universal grammar (ESC/B backs
    // out one layer) needs no sentence. The PAGE keeps printing BOTH devices'
    // bindings side by side -- the table's two slots ARE the two devices.
    state.rows = keyPageRows();
    state.cursor = caseCursor_;
    // NINE AND THE STICKS: the foot's keys in the live hand's own words. The
    // MORE key is the keyboard's `0`; a pad pages this list by walking the
    // cursor off the screen's end, so its foot names no digit.
    state.navMoveKeys = std::string(promptMoveKeys(promptDevice_));
    state.navTabKeys = std::string(promptTabKeys(promptDevice_));
    state.navMoreKey = promptDevice_ == InputDevice::Pad ? std::string() : std::string("0");
    return state;
}

std::vector<std::string> Session::keyRows() const {
    // DERIVED, NOT WRITTEN TWICE. See KeysPageRow::listRow: the flat list and
    // the drawn page were separate once, and separate lists of different
    // lengths is how a cursor ends up able to select a row nothing draws.
    std::vector<std::string> rows;
    for (const KeysPageRow& row : keyPageRows()) {
        rows.push_back(row.listRow.empty() ? row.binding + "  " + row.verb : row.listRow);
    }
    return rows;
}

std::vector<std::string> Session::optionRows() const {
    std::vector<std::string> rows;
    rows.reserve(kSliderRows + kActionCount);
    // THE FOUR SLIDERS, in the order somebody looking for them expects.
    rows.push_back("SENSITIVITY  " + std::to_string(controls_.mouse.sensitivity));
    rows.push_back(std::string("INVERT Y  ") + (controls_.mouse.invertY ? "ON" : "OFF"));
    rows.push_back("VIEW ANGLE  " + std::to_string(controls_.fovDegrees));
    rows.push_back("PAD DEADZONE " + std::to_string(controls_.pad.deadzonePercent));
    static_assert(Session::kSliderRows == 4, "the slider rows and adjustOption must agree");
    for (std::size_t i = 0; i < kActionCount; ++i) {
        std::string row(actionLabel(static_cast<Action>(i)));
        row += "  ";
        // The row the page is waiting on says so in place of a key, so there is
        // never a moment where the game is listening and nothing on screen says
        // it is.
        const bool listening =
            awaitingKey_ && optionCursor_ == static_cast<int>(kSliderRows + i);
        // NINE AND THE STICKS: the live hand's half of the row, promptKey's
        // own order -- a pad player reads and rebinds PAD_RT beside SWING,
        // a keyboard player MOUSE1 (see bindAwaited for the slot a press
        // lands in).
        row += listening ? "..."
                         : std::string(keyName(
                               promptKey(controls_, static_cast<Action>(i), promptDevice_)));
        rows.push_back(row);
    }
    return rows;
}

void Session::toggleOptions() {
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !optionsOpen_;
    if (willOpen) {
        // UI-EA-SPEC sec. 4 violation #4: REMEMBER THE OPENER, before the
        // stand-down below erases the evidence. Opened over the pause menu
        // (its SETTINGS row) means back returns there; opened over the
        // sibling Keys page (the TAB swap) inherits whatever that page owed
        // -- the pair is one tabbed surface as far as the way out is
        // concerned; opened from the street (F2) owes nothing.
        pageOpenedFromPause_ = pauseOpen_ || ((keysOpen_ || waitOpen_) && pageOpenedFromPause_);
    }
    optionsOpen_ = willOpen;
    if (willOpen) {
        // EVERY OTHER OVERLAY STANDS DOWN, INCLUDING THE PAUSE MENU. This used
        // to clear only the casebook and the keys page, which left a real hole:
        // open options with F2, then press F1, and keysOpen_ went true right
        // alongside it -- the keys page drew (dialogueView() checks it first)
        // while route_menu_key kept routing to the OPTIONS branch underneath,
        // because that check runs BEFORE the keys/casebook one. The page on
        // screen and the page reading your keystrokes were two different pages.
        // Reached this build via the pause menu's SETTINGS row, which is one
        // more way to get here than F2 alone used to have.
        casebookOpen_ = false;
        keysOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    awaitingKey_ = false;
    firstRun_ = false;
    optionCursor_ = 0;
    optionPage_ = 0;
    syncPanelAnim();
}

void Session::moveOptionCursor(int delta) {
    if (!optionsOpen_ || awaitingKey_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick moveTopicCursor speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(optionRows().size());
    if (count <= 0) {
        return;
    }
    optionCursor_ = ((optionCursor_ + delta) % count + count) % count;
    optionPage_ = optionCursor_ / kTopicPageSize;
}

void Session::adjustOption(int delta) {
    if (!optionsOpen_ || awaitingKey_ || delta == 0) {
        return;
    }
    switch (optionCursor_) {
        case 0:
            controls_.mouse.sensitivity += delta * kSensitivityStep;
            break;
        case 1:
            controls_.mouse.invertY = !controls_.mouse.invertY;
            break;
        case 2:
            controls_.fovDegrees += delta * kFovStep;
            break;
        case 3:
            controls_.pad.deadzonePercent += delta;
            break;
        default:
            // A BINDING ROW IS NOT A SLIDER. Nudging left on one is a player
            // looking for a value that is not there, and doing nothing is the
            // honest answer -- silently rebinding it would be worse.
            return;
    }
    controls_.sanitise();
}

void Session::chooseOption() {
    if (!optionsOpen_ || awaitingKey_) {
        return;
    }
    // AUDIO WIRING: the same accept every other list speaks -- a slider nudge
    // and a rebind-arm are both ENTER doing something.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    if (optionCursor_ < kSliderRows) {
        // ENTER ALWAYS DOES SOMETHING. On a slider it is a nudge up, so a player
        // who has learnt ENTER on the conversation surface is not met with a
        // key that silently refuses on half the rows of this one.
        adjustOption(1);
        return;
    }
    awaitingKey_ = true;
}

void Session::bindAwaited(Key key) {
    if (!optionsOpen_ || !awaitingKey_) {
        return;
    }
    awaitingKey_ = false;
    if (key == Key::None) {
        return;
    }
    const int index = optionCursor_ - kSliderRows;
    if (index < 0 || index >= static_cast<int>(kActionCount)) {
        return;
    }
    // STEALS, VISIBLY. See ControlSettings::bind: two verbs sharing a key is a
    // game where one of them stops working and the player cannot find out
    // which. The row it was taken from shows "--" on this very page.
    //
    // NINE AND THE STICKS: INTO THE SLOT OF THE KEY'S OWN DEVICE. The two
    // slots of a verb are its two hands -- a keyboard key and a pad key -- so
    // a pad button pressed at the prompt replaces the verb's PAD half and
    // leaves MOUSE1 where it was, and a keyboard key replaces the keyboard
    // half and leaves RT. It used to land in the primary slot whichever hand
    // pressed it, which cost a pad player their keyboard key for every verb
    // they touched. A verb with no key of that device yet takes the free
    // slot (pad keys go second, keyboard keys first, the shipped order).
    const Action action = static_cast<Action>(index);
    const std::size_t slot = static_cast<std::size_t>(index);
    const bool padKey = keyIsPad(key);
    const Key first = controls_.primary[slot];
    const Key second = controls_.secondary[slot];
    bool asSecondary = padKey;
    if (first != Key::None && keyIsPad(first) == padKey) {
        asSecondary = false;
    } else if (second != Key::None && keyIsPad(second) == padKey) {
        asSecondary = true;
    }
    controls_.bind(action, key, asSecondary);
}

// ---------------------------------------------------------------------------
// the pause menu
// ---------------------------------------------------------------------------

void Session::togglePause() {
    if (talking() || picking()) {
        return;
    }
    // JUSTICE BUILD: PAUSE still opens over the hearing (a player can always
    // quit the game) -- and is REFUSED on the rope's plate and the end rows,
    // whose two rows are the only live input (spec 5).
    if (ropeCeremonyUp()) {
        return;
    }
    const bool willOpen = !pauseOpen_;
    // EVERY OTHER OVERLAY STANDS DOWN, same as toggleOptions -- opening the
    // pause menu over an already-open casebook or keys page would be the same
    // split-brain bug that fix closes there: a page on screen that is not the
    // page reading the keyboard.
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    waitOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    pauseOpen_ = willOpen;
    pauseCursor_ = 0;
    quitArmed_ = false;
    firstRun_ = false;
    syncPanelAnim();
}

std::string Session::custodyWaitLine() const {
    // THE WAIT ROW'S OWN REFUSAL, by who has you: the priest at the bench,
    // the officer under the arrest beat before it. The rope's plate and the
    // end rows refuse the pause menu outright (togglePause), so no line is
    // needed there; the priest's stands for any custody the two above miss.
    if (courtOpen_) {
        return "THE PRIEST IS WAITING.";
    }
    if (takenHold_ > 0) {
        return "THE WATCH HAS YOU.";
    }
    return "THE PRIEST IS WAITING.";
}

std::vector<std::string> Session::pauseRows() const {
    // MORROWIND ROUND: CONTROLS IS NEW. The tiled Menu's four tiles have no
    // room for a long, read-top-to-bottom reference list (session.hpp's own
    // note), so Keys followed Options' own precedent -- SETTINGS has always
    // opened Menu's rebinding screen through the identical optionsOpen_ state
    // -- and took a row here too, rather than being left with no door into
    // the tiled Menu at all.
    // TIME-AND-TENURE BUILD: WAIT is new, second, and a door rather than a
    // page swap -- it opens the hour-select list (see openWait). Second
    // because the two rows a player reaches for mid-game (resume, pass the
    // hours) belong above the two they visit once (controls, settings), and
    // QUIT stays last where a quit belongs.
    // JUSTICE BUILD: at the bench the WAIT row is the refusal itself --
    // state changes the label (UI-REFERENCE: never a greyed-out row), and
    // choosePause says the same line and does nothing. The row keeps its
    // place so CONTROLS, SETTINGS and QUIT keep their digits.
    return {
        "RESUME",
        inCustody() ? custodyWaitLine() : std::string("WAIT"),
        "CONTROLS",
        "SETTINGS",
        // SHIP NOTE MOVE 3: the armed row names the device's own confirm.
        quitArmed_ ? "QUIT -- SURE? " + std::string(promptConfirmKey(promptDevice_))
                   : std::string("QUIT GRANADAD"),
    };
}

void Session::movePauseCursor(int delta) {
    if (!pauseOpen_) {
        return;
    }
    // MOVING THE CURSOR DISARMS QUIT. A player who backed off the row rather
    // than pressing it again plainly changed their mind, and leaving the arm
    // set for whichever row they land on next would fire QUIT off a key that
    // was never pressed twice.
    quitArmed_ = false;
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    const int count = static_cast<int>(pauseRows().size());
    if (count <= 0) {
        return;
    }
    pauseCursor_ = ((pauseCursor_ + delta) % count + count) % count;
}

void Session::choosePause() {
    if (!pauseOpen_) {
        return;
    }
    // AUDIO WIRING: the same accept every other list speaks. CONTROLS/
    // SETTINGS' page swaps add nothing further (conversingNow() stays true
    // across a swap, so panelAnim_ never flips); RESUME's close is spoken by
    // the panel pair in syncPanelAnim(), as every close is.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    switch (pauseCursor_) {
        case 0:
            // RESUME. The same thing ESC does from here, spelled out as a row
            // for the player who would rather click or press ENTER than learn
            // that ESC backs out of everything in this game.
            pauseOpen_ = false;
            pauseCursor_ = 0;
            quitArmed_ = false;
            syncPanelAnim();
            return;
        case 1:
            // WAIT (TIME-AND-TENURE BUILD). openWait() opens the hour-select
            // page and puts this menu down in the same call, the identical
            // shape the two rows below have. Wait mode, never sleep: the
            // healing door is the bed's Interact press and only that -- the
            // owner's ruling, enforced by which door you walked through.
            // JUSTICE BUILD: refused out loud in custody -- at the bench, and
            // under the officer's hand before it -- the page staying up; the
            // row already reads the refusal.
            if (inCustody()) {
                say(custodyWaitLine());
                return;
            }
            openWait(false);
            return;
        case 2:
            // CONTROLS. toggleKeys() opens it and puts this page down in the
            // same call, the identical shape SETTINGS below already has.
            // Relocated here from #85's own Menu cycle -- see toggleKeys()'s
            // own header on why the page itself is unchanged and only the
            // door moved.
            toggleKeys();
            return;
        case 3:
            // SETTINGS. toggleOptions() opens it and puts this page down in the
            // same call -- see its own comment on why every overlay does that,
            // AND syncs the panel anim itself -- no need to repeat it here.
            // This is the controls round's rebinding screen, reachable from the
            // menu a player actually pauses on rather than only from F2.
            toggleOptions();
            return;
        case 4:
            // QUIT. ITS OWN NUMBERED CASE, and now `default` truly is the
            // loud-failure arm the CONTROLS insertion promised: a future
            // seventh pause row that nobody wires here does NOTHING rather
            // than quietly arming QUIT.
            break;
        default:
            return;
    }
    // QUIT. Armed on the first press and confirmed on the second, so leaning on
    // ENTER once cannot close the window -- see quitArmed().
    if (!quitArmed_) {
        quitArmed_ = true;
        return;
    }
    quitArmed_ = false;
    quitRequested_ = true;
}

int Session::equippedQuickSlot() const noexcept {
    // The equipped cell derived fresh from the ONE equipped id sim::Tavern
    // holds -- the same id spellLine() reads for the CAST row, so the two
    // can never disagree about what the hand is holding.
    if (const sim::Spell* held = tavern_->equippedSpell(); held != nullptr) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::Spell* bound = tavern_->slotSpell(slot);
            if (bound != nullptr && bound->id == held->id) {
                return slot;
            }
        }
    }
    // THE CELLS PASS: ONE MARK FOR "IN YOUR HAND", CAST OR THING. The knife
    // walked onto slot 3 and pressed is as much in the hand as a crafting
    // is, and both critics read the strip's inverted cell as that mark and
    // found it lit for casts only -- so an item slot whose item is the one
    // held inverts the same way, off the same heldItem() the sheet's IN
    // HAND row reads, and gold stays the wheel's cursor alone.
    if (const sim::ItemDef* heldThing = tavern_->heldItem(); heldThing != nullptr) {
        for (std::int32_t slot = 0; slot < sim::Tavern::kQuickSlotCount; ++slot) {
            const sim::ItemDef* bound = tavern_->slotItem(slot);
            if (bound != nullptr && bound->id == heldThing->id) {
                return slot;
            }
        }
    }
    return -1;
}

void Session::selectQuickSlot(int slot) {
    if (slot < 0 || slot > 9) {
        return;
    }
    quickSlot_ = slot;
    showQuickBar();
    // SPELLS BUILD: a LOADED slot equips its crafting -- through
    // Tavern::equipSlot, which routes through the same equipSpellAt the
    // Grimoire page uses, so the CAST row, the strip's highlight and the next
    // press of C all read the one equipped id. An empty slot still says so
    // out loud: the binding is real, and a key that silently does nothing is
    // a key the player reads as broken.
    if (tavern_->equipSlot(slot)) {
        // AUDIO WIRING: the same accept every list speaks -- the press
        // changed what the hand holds.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiConfirm);
        }
        // `SLOT 3 - CLEAR THE HEAD` (UI-EA-SPEC sec. 5) used to be shouted
        // here on the alert row, at the glance register, directly under a
        // strip that already names the slot in the reading register -- all
        // three critics read the shout as outranking the thing the hand is
        // on. THE CELLS PASS: a loaded slot's press is the strip's to answer
        // (the digit inverts, the name sits beside the digits, the CAST or
        // hands row reads the same id) and the alert row stays quiet for it.
        // An empty slot and a thing no longer carried still say so below: a
        // key that silently does nothing is a key the player reads as broken.
        return;
    }
    if (tavern_->slotItem(slot) != nullptr) {
        // KIT BUILD: the slot names a thing no longer carried -- said, not
        // swallowed, the empty slot's own rule.
        if (audio_ != nullptr) {
            audio_->playOneShot(audio::SoundId::UiTick);
        }
        say("SLOT " + std::to_string(slot + 1) + " - " + tavern_->slotItem(slot)->name +
            ". NOT ON YOU.");
        return;
    }
    // AUDIO WIRING: the quiet tick a cursor move gets -- nothing changed.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    say("SLOT " + std::to_string(slot + 1) + " - EMPTY. THE GRIMOIRE BINDS.");
}

void Session::showQuickBar() {
    // A couple of seconds past the last touch -- the strip is up exactly
    // while the wheel or the number row is being used. The countdown's own
    // decay is quickBarAnim_'s business, driven in syncPanelAnim()/step()
    // like every other row.
    quickBarShowSteps_ = kQuickBarShowSteps;
    // KIT FIX PASS. A SLOT-JUST-CHANGED EVENT SNAPS THE STRIP OPEN, THE SAME
    // WAY say() SNAPS message_ ON RATHER THAN LEAVING IT TO THE NEXT step().
    //
    // Before this, opening the strip from a dead stop (quickBarAnim_ never
    // targeted true before -- true of the very first slot press in a fresh
    // session) rode EasedToggle::setTarget's own "first tick free" bump
    // (1/riseSteps_, an eighth) and NOTHING ELSE, because nothing here ever
    // called advance() -- that only happens once a step(), and most callers
    // of this function do not take one before the caller returns. A
    // --settle-steps=0 capture right after a fresh selectQuickSlot() (see
    // docs/frames/kit/kit-slot-960x540.png, the strip at a bare 0.125 fade)
    // photographs exactly that one-eighth-open frame and calls it the
    // feature's own showcase, which is a real defect even though a live
    // player would never notice eight steps (~130ms) of ramp.
    //
    // KITFIX LANE. THE GUARD WAS TOO NARROW: value() <= 0.0F ONLY CAUGHT A
    // DEAD STOP. A capture off SmokeRunConfig::quickbar's own scripted
    // sequence -- the Grimoire opened, a crafting walked onto a slot, the
    // page closed, the number pressed, all with --settle-steps=0 and not
    // one step() between them -- showed no strip at all
    // (docs/frames/kit/, the --quickbar shutter): the countdown was
    // genuinely armed (quickBarWanted() true) but VALUE was whatever the
    // strip's LAST rise-and-fall had left it at, because closing the
    // Grimoire mid-sequence stands the strip down first (conversingNow()
    // counts an open Grimoire), and reopening it a moment later hit
    // setTarget(true)'s "reopening mid-close resumes from wherever it had
    // got to" rule -- correct for a live player watching the ease, wrong
    // for a shutter with no frames left to resume ACROSS.
    //
    // SO: every call snaps, not just the one off a dead stop. A player never
    // sees the difference -- snapping an already-open strip back to fully
    // open is a no-op, and snapping a mid-close strip back up is the same
    // "the hand touched it again" event setTarget(true) already fires on,
    // just resolved in one frame instead of a few -- and a --settle-steps=0
    // capture now shows exactly what showQuickBar()'s own promise always
    // said: the strip is up, snapped, the instant a slot is chosen. The
    // CLOSE still eases the ordinary way: syncPanelAnim()'s
    // quickBarAnim_.setTarget(barWanted) below still owns the fall once
    // quickBarShowSteps_ runs out, so the strip's one-transition-grammar
    // veil on the way OUT is untouched.
    quickBarAnim_.snapTo(true);
}

void Session::toggleKeys() {
    if (talking() || picking()) {
        return;
    }
    const bool willOpen = !keysOpen_;
    if (willOpen) {
        // Violation #4: remember the opener -- see toggleOptions(). Opened
        // over the pause menu's CONTROLS row, back returns there; the
        // sibling-tab swap from Options inherits the debt.
        pageOpenedFromPause_ =
            pauseOpen_ || ((optionsOpen_ || waitOpen_) && pageOpenedFromPause_);
    }
    keysOpen_ = willOpen;
    if (willOpen) {
        // See toggleOptions' comment: an overlay that opens without putting the
        // others down is how a page draws on screen while a different one is
        // still the one reading the keyboard.
        casebookOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        menuFocus_ = kMenuFocusJournal;
    }
    firstRun_ = false;
    caseCursor_ = 0;
    casePage_ = 0;
    caseEntry_ = -1;
    syncPanelAnim();
}

namespace {

// ---------------------------------------------------------------------------
// TIME-AND-TENURE BUILD: the Wait page
// ---------------------------------------------------------------------------
/// One row per hour ahead, round the WHOLE clock (UI-EA-SPEC 1.7 #38:
/// 12 -> 24). Any named hour is reachable in one pick from anywhere -- the
/// same skipSeconds machinery per row, rows offered only, no sim change.
constexpr int kWaitHours = 24;

/// "HH:00", two digits, the clock the compass row already speaks.
[[nodiscard]] std::string hourLabel(int hour) {
    const int wrapped = ((hour % 24) + 24) % 24;
    return (wrapped < 10 ? "0" : "") + std::to_string(wrapped) + ":00";
}

/// The name an hour is known by out loud, or "".
[[nodiscard]] const char* hourName(int hour) noexcept {
    switch (((hour % 24) + 24) % 24) {
        case 0: return "MIDNIGHT";
        case 6: return "DAWN";
        case 12: return "NOON";
        case 18: return "DUSK";
        default: return "";
    }
}

}  // namespace

void Session::openWait(bool sleepMode) {
    if (refusedInCustody()) {
        return;
    }
    if (talking() || picking()) {
        return;
    }
    // Violation #4: remember the opener -- see toggleOptions(). The pause
    // menu's WAIT row is a door, and back walks back through it.
    pageOpenedFromPause_ = pauseOpen_;
    // Every other overlay stands down -- toggleOptions' rule, same reason.
    casebookOpen_ = false;
    keysOpen_ = false;
    grimoireOpen_ = false;
    districtMapOpen_ = false;
    optionsOpen_ = false;
    pauseOpen_ = false;
    quitArmed_ = false;
    awaitingKey_ = false;
    menuFocus_ = kMenuFocusJournal;
    firstRun_ = false;
    waitOpen_ = true;
    waitSleep_ = sleepMode;
    waitCursor_ = 0;
    waitPage_ = 0;
    syncPanelAnim();
}

std::string Session::waitRefusal() const {
    // THE WHOLE OF "ANYWHERE SAFE", in check order -- see the header block in
    // session.hpp. Each clause reads state something else already owns and
    // hashes; nothing here is new simulation. Deliberately NOT consulted:
    // heat, warrants, darkness, altitude. Waiting out a warrant is an
    // intended tactic (the owner's ruling), and a roof at midnight is only as
    // dangerous as whoever is on it -- which the HOSTILE clause already asks.
    if (tavern_->playerFloored()) {
        return "NOT FROM THE FLOOR.";
    }
    if (tavern_->playerInBrawl()) {
        return "NOT WHILE FISTS ARE UP.";
    }
    // A HOSTILE body close enough to cross the room while your eyes are off
    // it. Three times a conversation's reach -- six tiles -- and Chebyshev
    // like every reach in this build. Band deliberately ignored: a hostile
    // one floor up a stair you cannot see is the conservative read.
    constexpr std::int32_t kHostileReachQ8 = 3 * sim::kReachQ8;
    for (const sim::Actor& actor : tavern_->actors()) {
        if (!actor.present()) {
            continue;
        }
        if (tavern_->dialogue().ledger().attitudeOf(actor.id()) != sim::Attitude::Hostile) {
            continue;
        }
        if (actor.distanceTo(body_->x(), body_->y()) <= kHostileReachQ8) {
            return "NOT WITH AN ENEMY THIS CLOSE.";
        }
    }
    if (body_->airborne()) {
        return "NOT IN MID-AIR.";
    }
    if (tiles_->fluidDepth(body_->tileX(), body_->tileY(), body_->band()) > 0) {
        return "NOT STANDING IN WATER.";
    }
    return {};
}

std::vector<std::string> Session::waitRows() const {
    // Twelve rows off the LIVE clock, so the hour printed is the hour a pick
    // lands on -- skipToHour truncates to the top of the hour, and a list
    // that said "1 HOUR" while delivering forty minutes would be lying.
    std::vector<std::string> rows;
    rows.reserve(kWaitHours);
    const int nowHour = timeOfDay_ / 3600;
    for (int ahead = 1; ahead <= kWaitHours; ++ahead) {
        const int target = (nowHour + ahead) % 24;
        // `1 - DAWN 06:00` (UI-EA-SPEC 1.7 #38): the hours-ahead count, the
        // hour's name where it has one, and the clock it lands on. The unit
        // word went; the numbers did not.
        std::string row = std::to_string(ahead) + " - ";
        if (hourName(target)[0] != '\0') {
            row += hourName(target);
            row += ' ';
        }
        row += hourLabel(target);
        rows.push_back(std::move(row));
    }
    return rows;
}

void Session::moveWaitCursor(int delta) {
    if (!waitOpen_) {
        return;
    }
    // AUDIO WIRING: the same one quiet tick every other list speaks.
    if (audio_ != nullptr && delta != 0) {
        audio_->playOneShot(audio::SoundId::UiTick);
    }
    wrapCursorAndPage(waitCursor_, waitPage_, delta, kWaitHours);
}

void Session::nextWaitPage() {
    if (!waitOpen_) {
        return;
    }
    advancePage(waitPage_, waitCursor_, static_cast<std::size_t>(kWaitHours));
}

void Session::chooseWaitRow(int slot) {
    if (!waitOpen_ || slot < 0 || slot >= kTopicPageSize) {
        return;
    }
    const int index = waitPage_ * kTopicPageSize + slot;
    if (index >= kWaitHours) {
        return;
    }
    waitCursor_ = index;
    const int hours = index + 1;
    const int target = ((timeOfDay_ / 3600) + hours) % 24;
    // AUDIO WIRING: the same accept every list speaks, refusals included --
    // the press is what is acknowledged, exactly chooseTopic's rule.
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::UiConfirm);
    }
    if (waitSleep_) {
        // THE BED'S OWN GATE, not waitRefusal(): sleep answers where sleep
        // has always answered. Refused out loud in restRefusal's words, the
        // page staying up -- the same door the R verb names.
        const sim::ServiceResult slept = tavern_->sleepUntil(target);
        if (slept != sim::ServiceResult::Served) {
            say(std::string(sim::restRefusal(slept)));
            return;
        }
        syncClockAfterSkip();
    } else {
        // RE-CHECKED ON THE PRESS, not only at the page's opening: a brawler
        // can close the distance while the list is up, and the page and the
        // key must name the same door.
        const std::string refusal = waitRefusal();
        if (!refusal.empty()) {
            say(refusal);
            return;
        }
        // skipToHour is Tavern::skipTo plus the calendar sync -- heat cools
        // on the elapsed seconds in there, which is what makes waiting out a
        // warrant a tactic rather than an exploit.
        skipToHour(target);
    }
    waitOpen_ = false;
    waitCursor_ = 0;
    waitPage_ = 0;
    syncPanelAnim();
    say((waitSleep_ ? std::string("SLEPT UNTIL ") : std::string("WAITED UNTIL ")) +
        hourLabel(target) + ".");
}

void Session::toggleMenuFocused(int focus) {
    if (refusedInCustody()) {
        return;
    }
    if (talking() || picking()) {
        return;
    }
    const int wrapped = ((focus % kMenuFocusCount) + kMenuFocusCount) % kMenuFocusCount;
    if (casebookOpen_) {
        if (menuFocus_ == wrapped) {
            // THE KEY THAT OPENED IT CLOSES IT -- the same rule every one of
            // #85's six pages used to have on its own, now scoped to "the tile
            // you are already looking at" rather than to a page that no
            // longer exists on its own.
            casebookOpen_ = false;
            caseCursor_ = 0;
            casePage_ = 0;
            caseEntry_ = -1;
            characterCursor_ = 0;
            characterPage_ = 0;
            characterPack_ = false;
            mapCursor_ = 0;
            mapPage_ = 0;
            lettersCursor_ = 0;
            lettersPage_ = 0;
            lettersEntry_ = -1;
            lettersBodyPage_ = 0;
            menuFocus_ = kMenuFocusJournal;
        } else {
            // ALREADY OPEN, DIFFERENT TILE ASKED FOR: MOVE FOCUS, DO NOT
            // RESET. Each tile keeps its own cursor/page/picked-entry --
            // switching focus to read the Letters tile and back should not
            // have thrown away where the Journal tile's cursor was.
            menuFocus_ = wrapped;
        }
    } else {
        // OPENING FRESH. Every other overlay stands down, same as
        // toggleOptions()/toggleKeys()/togglePause() already do on their own.
        keysOpen_ = false;
        grimoireOpen_ = false;
        waitOpen_ = false;
        districtMapOpen_ = false;
        optionsOpen_ = false;
        pauseOpen_ = false;
        quitArmed_ = false;
        awaitingKey_ = false;
        casebookOpen_ = true;
        menuFocus_ = wrapped;
        caseCursor_ = 0;
        casePage_ = 0;
        caseEntry_ = -1;
        characterCursor_ = 0;
        characterPage_ = 0;
        characterPack_ = false;
        mapCursor_ = 0;
        mapPage_ = 0;
        lettersCursor_ = 0;
        lettersPage_ = 0;
        lettersEntry_ = -1;
        lettersBodyPage_ = 0;
    }
    firstRun_ = false;
    syncPanelAnim();
}

void Session::toggleCasebook() {
    if (refusedInCustody()) {
        return;
    }
    recordWatchOp(WatchOpKind::Casebook);
    const WatchDepthGuard watchGuard(watchDepth_);
    toggleMenuFocused(kMenuFocusJournal);
}

void Session::toggleCharacter() { toggleMenuFocused(kMenuFocusCharacter); }

void Session::toggleMap() { toggleMenuFocused(kMenuFocusMap); }

void Session::toggleLetters() {
    recordWatchOp(WatchOpKind::Letters);
    const WatchDepthGuard watchGuard(watchDepth_);
    toggleMenuFocused(kMenuFocusLetters);
}

// ---------------------------------------------------------------------------
// Morrowind round: one Menu, four tiles (Keys and Options moved to Pause)
// ---------------------------------------------------------------------------

bool Session::menuOpen() const noexcept {
    // districtMapOpen_ joined this list with core action #13: the ward map
    // is a full-screen page, so the movement keys stand down under it the
    // way they do under every other page (main.cpp's `listening`).
    return casebookOpen_ || keysOpen_ || grimoireOpen_ || optionsOpen_ || districtMapOpen_;
}

void Session::toggleMenu() {
    // THE KEY THAT OPENED IT CLOSES IT -- toggleMenuFocused() already carries
    // this rule for "the tiled Menu, focused on the tile it is already
    // focused on", which is exactly what re-pressing the single Menu action
    // (never any of the four toggle*() entry points a real key no longer
    // reaches) always asks for.
    //
    // NINE AND THE STICKS: the ward map and the grimoire are pages of NOTES
    // (the ring), so NOTES pressed on either of them puts the papers DOWN --
    // back to the street -- rather than flipping back to the tiles over
    // them. J puts your papers down whichever page you are on.
    if (districtMapOpen_) {
        toggleDistrictMap();
        return;
    }
    if (grimoireOpen_) {
        toggleGrimoire();
        return;
    }
    toggleMenuFocused(menuFocus_);
}

void Session::menuPageNext() {
    // NINE AND THE STICKS: THE RING -- see the declaration. Four tiles, then
    // the ward map, then the grimoire, then round again. The two full-screen
    // pages open through their own toggles (which put every other overlay
    // down, the tiled Menu included) and the tiles reopen through
    // toggleMenuFocused, so no new state is kept: the ring is read off which
    // surface is up.
    //
    // EVERY STEP OF THE RING IS ONE PAGE-TURN, and it sounds like one: the
    // BookFlip the tile cycle has always played, once per step. The two
    // steps INTO the tiles clear the full-screen page's own flag directly
    // rather than toggling it shut first -- a close-then-open would flip
    // syncPanelAnim's one panel toggle twice inside a step and speak
    // BookClose + BookOpen for what is one turn of the paper (the same
    // sound bug setMenuFocus's own header names).
    //
    // Keys and Options have no tiles of their own to step between, so this
    // is a no-op while either of them is what is open; NOT OPEN AT ALL is
    // the same no-op it always was, so a bumper press with nothing open
    // still does not open anything.
    if (districtMapOpen_) {
        toggleGrimoire();  // ward map -> grimoire (the toggle puts the map down)
        pageTurnSound();
        return;
    }
    if (grimoireOpen_) {
        grimoireOpen_ = false;  // grimoire -> the first tile
        grimoireCursor_ = 0;
        grimoirePage_ = 0;
        toggleMenuFocused(kMenuFocusCharacter);
        pageTurnSound();
        return;
    }
    if (!casebookOpen_) {
        return;
    }
    if (menuFocus_ == kMenuFocusJournal) {
        toggleDistrictMap();  // the last tile -> ward map (the toggle puts the tiles down)
        pageTurnSound();
        return;
    }
    // AUDIO WIRING: the plan's "page cycle in the tiled Menu -> BookFlip".
    pageTurnSound();
    menuFocus_ = ((menuFocus_ + 1) % kMenuFocusCount + kMenuFocusCount) % kMenuFocusCount;
}

void Session::setMenuFocus(int focus) {
    // THE POINTER PASS (pointer lane, flagged): the mouse's own door onto the
    // focus the bumpers cycle -- setCasebookCursor's "a printed digit, or a
    // mouse click" shape, applied to tiles. ONE page-turn when the focus
    // actually moves, not one per step a cycle would have taken: a hover
    // crossing from Character to Letters is one gesture, and three stacked
    // BookFlips for it would be a sound bug a case cannot see.
    if (!casebookOpen_ || focus < 0 || focus >= kMenuFocusCount || focus == menuFocus_) {
        return;
    }
    pageTurnSound();
    menuFocus_ = focus;
}

void Session::menuPagePrev() {
    // The ring, backward -- see menuPageNext.
    if (districtMapOpen_) {
        districtMapOpen_ = false;  // ward map -> the last tile
        toggleMenuFocused(kMenuFocusJournal);
        pageTurnSound();
        return;
    }
    if (grimoireOpen_) {
        toggleDistrictMap();  // grimoire -> ward map (the toggle puts the grimoire down)
        pageTurnSound();
        return;
    }
    if (!casebookOpen_) {
        return;
    }
    if (menuFocus_ == kMenuFocusCharacter) {
        toggleGrimoire();  // the first tile -> grimoire (the toggle puts the tiles down)
        pageTurnSound();
        return;
    }
    // AUDIO WIRING: the same page-turn, backward.
    pageTurnSound();
    menuFocus_ =
        ((menuFocus_ - 1) % kMenuFocusCount + kMenuFocusCount) % kMenuFocusCount;
}

void Session::pageTurnSound() {
    if (audio_ != nullptr) {
        audio_->playOneShot(audio::SoundId::BookFlip);
    }
}

void Session::toggleWait() {
    if (waitOpen_) {
        closeConversation();
        return;
    }
    openWait(/*sleepMode=*/false);
}

}  // namespace granadad::render
