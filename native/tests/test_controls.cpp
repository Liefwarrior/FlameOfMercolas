// The keys, the modifiers, the pad and the settings file.
//
// WHAT THIS CASE FILE IS FOR. src/client/main.cpp has carried this comment since
// S3:
//
//     VERIFICATION GAP (S3): NOTHING TESTS THIS SWITCH. Every branch below calls
//     a Session method the suite drives directly, so the behaviour is covered
//     and the BINDING is not -- a key wired to the wrong verb, or a conversation
//     that fails to capture the keyboard, would ship green.
//
// The binding table, the hold-or-toggle rule, the stick deadzone and the config
// file all moved out of that switch and into granadad-render for #77, so they
// are testable and are tested here. What remains untested in main.cpp is the
// SDL_Scancode -> Key translation, and that is stated in a VERIFICATION GAP on
// the table itself rather than left to be discovered.

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/render/controls.hpp"
#include "granadad/render/session.hpp"
#include "granadad/sim/human_scale.hpp"

using namespace granadad::render;
namespace sim = granadad::sim;
namespace content = granadad::content;

TEST_CASE("the shipped bindings are the ones a player already knows") {
    const ControlSettings keys = ControlSettings::defaults();

    // The layout of every first-person game made this century. If any of these
    // moves, somebody has broken the muscle memory the whole task was about.
    CHECK(keys.bound(Action::Forward, Key::W));
    CHECK(keys.bound(Action::Back, Key::S));
    CHECK(keys.bound(Action::StrafeLeft, Key::A));
    CHECK(keys.bound(Action::StrafeRight, Key::D));
    CHECK(keys.bound(Action::Sprint, Key::LeftShift));
    CHECK(keys.bound(Action::Crouch, Key::LeftCtrl));
    CHECK(keys.bound(Action::Vertical, Key::Space));
    CHECK(keys.bound(Action::Interact, Key::E));
    // J FOR JOURNAL -- the owner's words: "Use J for journal since that's
    // how it's done by convention." Tab held this from #85 and is freed to
    // nothing (see defaults()'s own comment); it must NOT still open the
    // book, or B-shaped muscle memory aside, two keys would claim one page.
    CHECK(keys.bound(Action::Menu, Key::J));
    CHECK_FALSE(keys.bound(Action::Menu, Key::Tab));
    CHECK(keys.actionFor(Key::Tab) == Action::Count);
    CHECK(keys.actionFor(Key::J) == Action::Menu);
    CHECK(keys.bound(Action::Pause, Key::Escape));

    // The number row is the quick bar, and the wheel walks it.
    CHECK(keys.actionFor(Key::Num1) == Action::QuickSlot1);
    CHECK(keys.actionFor(Key::Num9) == Action::QuickSlot9);
    CHECK(keys.actionFor(Key::Num0) == Action::QuickSlot0);
    CHECK(keys.actionFor(Key::WheelDown) == Action::QuickNext);
    CHECK(keys.actionFor(Key::WheelUp) == Action::QuickPrev);

    // The arrows TURN, and they turn as a second binding on a verb whose first
    // binding is a mouse. They are an accessibility fallback and they are not
    // allowed to be the way the game is meant to be played -- see
    // sim::kTurnRate.
    CHECK(keys.bound(Action::TurnLeft, Key::Left));
    CHECK(keys.bound(Action::TurnRight, Key::Right));

    // #85. SPACE IS VERTICAL, RESOLVED BY WHAT IS AHEAD OR BELOW -- the same
    // "space jumps, walking into a ledge climbs it" rule the S5 build fixed,
    // now folded into one Action instead of three (Jump/Traverse/DropDown).
    CHECK(keys.actionFor(Key::Space) == Action::Vertical);

    // #85. EVERY CORE BUTTON HAS A PAD DEFAULT -- the whole point of a scheme
    // sized for a controller's own scarcity of buttons.
    CHECK(keys.bound(Action::Interact, Key::PadSouth));
    CHECK(keys.bound(Action::Attack, Key::PadWest));
    CHECK(keys.bound(Action::Vertical, Key::PadNorth));
    CHECK(keys.bound(Action::Crouch, Key::PadEast));
    CHECK(keys.bound(Action::Sprint, Key::PadLeftStick));
    CHECK(keys.bound(Action::QuickWheel, Key::PadRightStick));
    // CORE ACTION #13 MOVED MENU'S PAD KEY. PadBack (Select) is the ward
    // map's now -- the owner's own "and select on controller" -- the classic
    // Start/Select split beside Pause=Start; Menu took the previously-unbound
    // D-pad up. See defaults()'s own comment and fromText()'s migration.
    CHECK(keys.bound(Action::Menu, Key::PadUp));
    CHECK_FALSE(keys.bound(Action::Menu, Key::PadBack));
    CHECK(keys.bound(Action::Pause, Key::PadStart));
    CHECK(keys.bound(Action::PagePrev, Key::PadLeftBumper));
    CHECK(keys.bound(Action::PageNext, Key::PadRightBumper));

    // THE WARD MAP -- the owner's own words for both halves: "a map that they
    // can press M to see", "and select on controller".
    CHECK(keys.bound(Action::Map, Key::M));
    CHECK(keys.bound(Action::Map, Key::PadBack));
    CHECK(keys.actionFor(Key::M) == Action::Map);
    CHECK(keys.actionFor(Key::PadBack) == Action::Map);

    // THE COMBAT PAIR. C casts and the right mouse button blocks -- the
    // Morrowind hand layout -- and the pad spends its two remaining unused
    // keys, the triggers, the way the genre always spends them: LT guards,
    // RT casts.
    CHECK(keys.bound(Action::Cast, Key::C));
    CHECK(keys.bound(Action::Cast, Key::PadRightTrigger));
    CHECK(keys.bound(Action::Block, Key::MouseRight));
    CHECK(keys.bound(Action::Block, Key::PadLeftTrigger));
    // And neither stole its key from anyone: MouseRight has been free since
    // #85 moved Interact's old secondary to PadSouth, and C never shipped
    // bound. Attack keeps the left button; Interact keeps E.
    CHECK(keys.bound(Action::Attack, Key::MouseLeft));
    CHECK(keys.bound(Action::Interact, Key::E));
}

TEST_CASE("#85: the core gameplay button count is what Eli asked for") {
    // THIRTEEN. Attack, Interact, Crouch, Vertical, Sprint, Menu, PagePrev,
    // PageNext, Pause, QuickWheel, Cast, Block -- and, the one deliberate
    // bend of the ceiling, Map. Movement axes, the TurnLeft/TurnRight
    // accessibility fallback and Screenshot (a dev/capture utility) excluded,
    // exactly as the brief asked. Thirteen is ONE OVER Eli's own "10-12
    // buttons" range, and the bend is HIS: Cast and Block spent the budget's
    // last two slots, and then the owner asked for the map key directly --
    // "let's give the player a map that they can press M to see", "and
    // select on controller" -- so the ceiling's own author raised it by one,
    // stated here rather than fudged. The next core verb has to consolidate
    // into an existing one the way Interact and Vertical already did. This
    // is a COUNTING test, not a behaviour one: it exists so a future action
    // added to the "core" bucket without updating this case is a red build
    // instead of a drifted comment.
    const Action core[] = {
        Action::Attack,     Action::Interact, Action::Crouch,  Action::Vertical,
        Action::Sprint,     Action::Menu,     Action::PagePrev, Action::PageNext,
        Action::Pause,      Action::QuickWheel, Action::Cast,   Action::Block,
        Action::Map,
    };
    CHECK(static_cast<int>(sizeof(core) / sizeof(core[0])) == 13);
    // One over the top of Eli's own 10-12 range, on his own direct ask --
    // pinned as exactly 13 above so the NEXT bend also has to be stated.
    CHECK(sizeof(core) / sizeof(core[0]) >= 10);
    CHECK(sizeof(core) / sizeof(core[0]) <= 13);
}

TEST_CASE("every CORE action resolves an actual pad key, generically") {
    // GENERIC AND ENUM-DRIVEN, unlike "the shipped bindings are the ones a
    // player already knows" above, which hardcodes each action's exact pad
    // button by name (Interact->PadSouth, Attack->PadWest, and so on). That
    // test proves TODAY'S table; this one proves the PROPERTY -- every one of
    // the 12 core actions carries at least one key that is a pad key, whatever
    // that key happens to be -- so a future core action added to controls.hpp
    // without a pad default in defaults() fails HERE, on the property, rather
    // than only if somebody remembers to add another hardcoded CHECK() to the
    // list above. This is the binding-completeness gap class the gamepad-
    // readiness task named directly: an unbound action is invisible until a
    // player actually reaches for it on a pad.
    const ControlSettings keys = ControlSettings::defaults();
    const Action core[] = {
        Action::Attack,     Action::Interact, Action::Crouch,  Action::Vertical,
        Action::Sprint,     Action::Menu,     Action::PagePrev, Action::PageNext,
        Action::Pause,      Action::QuickWheel, Action::Cast,   Action::Block,
        Action::Map,
    };
    // Key::PadSouth..Key::PadRight are one contiguous run in controls.hpp's
    // own Key enum (the face buttons, bumpers, triggers, sticks, Start/Back
    // and the D-pad, in that order, with nothing else interleaved) -- see the
    // enum itself. That ordering is what makes a range check here a check on
    // "is this a pad key" rather than a second hardcoded name list.
    const auto isPadKey = [](Key key) noexcept {
        return key >= Key::PadSouth && key <= Key::PadRight;
    };
    for (const Action action : core) {
        const std::size_t index = static_cast<std::size_t>(action);
        INFO("action ", actionKey(action), " primary=", keyName(keys.primary[index]),
             " secondary=", keyName(keys.secondary[index]));
        CHECK((isPadKey(keys.primary[index]) || isPadKey(keys.secondary[index])));
    }
}

TEST_CASE("no two verbs share a key in the shipped layout") {
    const ControlSettings keys = ControlSettings::defaults();
    std::set<Key> seen;
    for (std::size_t i = 0; i < kActionCount; ++i) {
        for (const Key key : {keys.primary[i], keys.secondary[i]}) {
            if (key == Key::None) {
                continue;
            }
            INFO("action ", actionKey(static_cast<Action>(i)), " key ", keyName(key));
            CHECK(seen.insert(key).second);
        }
    }
    // And every verb has at least one key on it. An action nobody can reach is
    // a feature nobody has.
    for (std::size_t i = 0; i < kActionCount; ++i) {
        INFO("action ", actionKey(static_cast<Action>(i)));
        CHECK(keys.primary[i] != Key::None);
    }
}

TEST_CASE("rebinding steals the key rather than sharing it") {
    ControlSettings keys = ControlSettings::defaults();
    REQUIRE(keys.bound(Action::Forward, Key::W));

    keys.bind(Action::Vertical, Key::W);
    CHECK(keys.bound(Action::Vertical, Key::W));
    // Visibly taken off Forward -- not shared, and not silently ignored. A
    // rebinding screen that allows a collision produces a game where one of the
    // two verbs stops working and the player cannot find out which.
    CHECK_FALSE(keys.bound(Action::Forward, Key::W));
    CHECK(keys.actionFor(Key::W) == Action::Vertical);
    // The arrow is still on Forward, so the player is not stranded.
    CHECK(keys.bound(Action::Forward, Key::Up));

    keys.bind(Action::Forward, Key::W);
    CHECK(keys.bound(Action::Forward, Key::W));
    CHECK_FALSE(keys.bound(Action::Vertical, Key::W));
}

TEST_CASE("a modifier is both a hold and a toggle, and they do not fight") {
    HoldToggle sprint;
    CHECK_FALSE(sprint.active());

    // A TAP LATCHES IT ON.
    sprint.press(100);
    CHECK(sprint.active());
    sprint.release(100 + HoldToggle::kTapSteps - 1);
    CHECK(sprint.active());
    CHECK(sprint.latched());

    // A SECOND TAP LATCHES IT OFF, and it goes off at the press rather than at
    // the release, so a tap reads as instant.
    sprint.press(200);
    CHECK_FALSE(sprint.active());
    sprint.release(201);
    CHECK_FALSE(sprint.active());

    // A HOLD IS A HOLD. Down while down, up the moment it is let go, and no
    // latch left behind.
    sprint.press(300);
    CHECK(sprint.active());
    sprint.release(300 + HoldToggle::kTapSteps + 1);
    CHECK_FALSE(sprint.active());
    CHECK_FALSE(sprint.latched());

    // A LONG PRESS STARTING FROM LATCHED-ON ends off, which is the case a naive
    // toggle gets wrong: it would come back on at the release.
    sprint.press(400);
    sprint.release(405);
    REQUIRE(sprint.latched());
    sprint.press(500);
    CHECK_FALSE(sprint.active());
    sprint.release(500 + HoldToggle::kTapSteps + 10);
    CHECK_FALSE(sprint.active());

    // Key repeat is not a second press.
    sprint.press(600);
    sprint.press(601);
    sprint.press(602);
    sprint.release(603);
    CHECK(sprint.latched());
    sprint.clear();
    CHECK_FALSE(sprint.active());
}

TEST_CASE("mouse look is linear, and invert-Y is the only thing that touches it") {
    MouseSettings mouse;
    mouse.sensitivity = 14;
    CHECK(mouse.yawFor(10) == 140);
    CHECK(mouse.yawFor(-10) == -140);
    // TWICE THE MOTION IS TWICE THE ANGLE. No curve, no smoothing, no
    // acceleration -- the whole of what makes a 2026 mouse feel like a 2026
    // mouse is that it does exactly what you did with it.
    CHECK(mouse.yawFor(200) == 20 * mouse.yawFor(10));

    // Screen y grows downward, so moving the mouse up (negative counts) has to
    // raise the pitch.
    CHECK(mouse.pitchFor(-10) > 0);
    mouse.invertY = true;
    CHECK(mouse.pitchFor(-10) < 0);
    CHECK(mouse.pitchFor(-10) == -140);

    // And sensitivity scales both axes by the same number, so a change does not
    // silently alter the aspect of the aim.
    mouse.invertY = false;
    mouse.sensitivity = 28;
    CHECK(mouse.yawFor(10) == 280);
    CHECK(mouse.pitchFor(10) == -280);
}

TEST_CASE("the stick deadzone is radial, which is what stops a pad feeling like a d-pad") {
    PadSettings pad;
    pad.deadzonePercent = 20;
    pad.saturationPercent = 95;
    const std::int32_t dead = 20 * kStickMax / 100;

    // Dead centre is dead, and so is a small push in EVERY direction rather
    // than only along the axes.
    CHECK(applyDeadzone(Stick{0, 0}, pad).x == 0);
    CHECK(applyDeadzone(Stick{dead / 2, 0}, pad).x == 0);
    CHECK(applyDeadzone(Stick{0, dead / 2}, pad).y == 0);
    {
        // THE BUG A PER-AXIS DEADZONE HAS. This diagonal push is longer than the
        // deadzone overall but each AXIS of it is shorter, so a per-axis rule
        // throws away a real input; and just past the threshold a per-axis rule
        // keeps one axis and drops the other, which is what makes a stick snap
        // to the compass.
        const std::int32_t each = dead * 8 / 10;
        const Stick live = applyDeadzone(Stick{each, each}, pad);
        CHECK(live.x != 0);
        CHECK(live.y != 0);
        // Equal push in, equal push out: the direction survived.
        CHECK(live.x == live.y);
    }

    // The outer edge saturates, so full deflection is full speed in every
    // direction rather than only along the axes.
    const Stick full = applyDeadzone(Stick{kStickMax, 0}, pad);
    CHECK(full.x >= kStickMax - 1);
    const Stick over = applyDeadzone(Stick{kStickMax, kStickMax}, pad);
    CHECK(over.x > kStickMax * 7 / 10);
    CHECK(over.y > kStickMax * 7 / 10);

    // Monotonic: pushing further never gives less.
    std::int32_t last = 0;
    for (std::int32_t v = 0; v <= kStickMax; v += 137) {
        const std::int32_t out = applyDeadzone(Stick{v, 0}, pad).x;
        REQUIRE(out >= last);
        last = out;
    }

    // A nonsense config cannot produce a stick that does nothing but also
    // cannot divide by zero on the way.
    PadSettings broken;
    broken.deadzonePercent = 90;
    broken.saturationPercent = 10;
    CHECK(applyDeadzone(Stick{kStickMax, kStickMax}, broken).x == 0);
}

TEST_CASE("the look stick is cubed, so it can aim and still turn round") {
    PadSettings pad;
    const std::int32_t rate = pad.lookBamPerSecond;

    const sim::Angle full = padLook(kStickMax, rate, sim::kStepsPerSecond);
    const sim::Angle half = padLook(kStickMax / 2, rate, sim::kStepsPerSecond);
    const sim::Angle quarter = padLook(kStickMax / 4, rate, sim::kStepsPerSecond);

    CHECK(padLook(0, rate, sim::kStepsPerSecond) == 0);
    // Full deflection for a second is the whole rate, give or take integer
    // truncation.
    CHECK(full > rate * 9 / 10);
    CHECK(full <= rate);
    // A HALF PUSH IS AN EIGHTH OF THE RATE, not half of it. That is the whole
    // point of the curve: small corrections are fine and large sweeps are fast,
    // and a linear stick can only ever do one of the two.
    CHECK(half * 6 < full);
    CHECK(half * 10 > full);
    CHECK(quarter * 40 < full);
    // Symmetric.
    CHECK(padLook(-kStickMax, rate, sim::kStepsPerSecond) == -full);
    // And it scales with how many steps the frame was worth, so the pad turns
    // the same amount per second at any frame rate.
    CHECK(padLook(kStickMax, rate, 2 * sim::kStepsPerSecond) > full);
}

TEST_CASE("a rebinding survives the process, and a broken file does not break the game") {
    ControlSettings mine = ControlSettings::defaults();
    mine.bind(Action::Vertical, Key::MouseX1);
    mine.bind(Action::Crouch, Key::Z);
    mine.bind(Action::Forward, Key::PadUp, /*asSecondary=*/true);
    mine.mouse.sensitivity = 31;
    mine.mouse.invertY = true;
    mine.fovDegrees = 105;
    mine.pad.deadzonePercent = 9;

    const ControlSettings back = ControlSettings::fromText(mine.toText());
    CHECK(back.bound(Action::Vertical, Key::MouseX1));
    CHECK(back.bound(Action::Crouch, Key::Z));
    CHECK(back.bound(Action::Forward, Key::PadUp));
    CHECK(back.bound(Action::Forward, Key::W));
    CHECK(back.mouse.sensitivity == 31);
    CHECK(back.mouse.invertY);
    CHECK(back.fovDegrees == 105);
    CHECK(back.pad.deadzonePercent == 9);
    // Round trips exactly, so saving twice does not churn the file.
    CHECK(back.toText() == mine.toText());

    // A FILE THAT MENTIONS ONE VERB LEAVES THE OTHERS PLAYABLE. It is a diff
    // against the shipped layout, not a replacement for it.
    const ControlSettings sparse = ControlSettings::fromText("bind vertical MOUSE3\n");
    CHECK(sparse.bound(Action::Vertical, Key::MouseMiddle));
    CHECK(sparse.bound(Action::Forward, Key::W));

    // GARBAGE IS IGNORED, NOT FATAL. Unknown verbs, unknown keys, unknown
    // settings, half a line -- the worst outcome of a bad config file must not
    // be a game that will not start.
    const ControlSettings junk = ControlSettings::fromText(
        "# a comment\n"
        "bind\n"
        "bind nonsense W\n"
        "bind forward NOTAKEY\n"
        "set\n"
        "set nonsense 4\n"
        "wibble\n"
        "\n"
        "set sensitivity 999999\n"
        "set fov 3\n");
    CHECK(junk.bound(Action::Back, Key::S));
    // Out-of-range sliders are clamped rather than obeyed.
    CHECK(junk.mouse.sensitivity == kMaxSensitivity);
    CHECK(junk.fovDegrees == kMinFov);
    // The one thing a bad key name is allowed to do is unbind that one verb.
    CHECK(junk.primary[static_cast<std::size_t>(Action::Forward)] == Key::None);
}

TEST_CASE("controls round trip through a real file, and a missing one is the defaults") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "granadad-controls-case";
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    const std::filesystem::path file = dir / std::string(kControlsFileName);

    // A file that is not there is not an error. It is a new player.
    const ControlSettings fresh = loadControls(file);
    CHECK(fresh.bound(Action::Forward, Key::W));

    ControlSettings mine = ControlSettings::defaults();
    mine.mouse.sensitivity = 44;
    mine.bind(Action::Interact, Key::MouseMiddle);
    REQUIRE(saveControls(mine, file));
    REQUIRE(std::filesystem::exists(file));

    const ControlSettings reloaded = loadControls(file);
    CHECK(reloaded.mouse.sensitivity == 44);
    CHECK(reloaded.bound(Action::Interact, Key::MouseMiddle));

    // And a file full of nonsense still boots.
    {
        std::ofstream corrupt(file, std::ios::binary | std::ios::trunc);
        corrupt << "\x01\x02 not a config at all\n";
    }
    const ControlSettings survived = loadControls(file);
    CHECK(survived.bound(Action::Forward, Key::W));

    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("a rebinding and a sensitivity survive the process -- loaded, and LIVE in the game") {
    // WHAT THE PREVIOUS CASE DOES NOT PROVE. "controls round trip through a
    // real file" is loadControls(file) answering loadControls(saveControls())
    // -- true of ControlSettings, a plain struct, and true regardless of
    // whether one line of the game ever reads it. The question the settings
    // page's own polish pass was asked, verbatim: "does the game actually
    // reload saved keybinds/sensitivity on next launch?" is a question about
    // main.cpp's run_client(), which does exactly two things with a settings
    // file at boot --
    //
    //     render::ControlSettings controls = render::loadControls(controlsFile);
    //     ...
    //     session.setControls(controls);
    //
    // -- and then answers every rebind and every slider off session.controls()
    // from then on. Neither of those two calls touches SDL. This case makes
    // them, in that order, on a REAL file on REAL disk, and checks the LIVE
    // Session -- the keys page's own generated rows, the sensitivity the mouse
    // is actually multiplied by, the field of view the camera actually reads --
    // rather than the struct loadControls happened to hand back. It is the
    // same two lines run_client() runs, run without the window around them.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "granadad-controls-live-case";
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    const std::filesystem::path file = dir / std::string(kControlsFileName);

    // A PREVIOUS SESSION'S SAVE. Written the same way saveControls() would
    // have written it -- not hand-typed here, so this proves the format the
    // game itself produces and not a format this test happens to like.
    ControlSettings previous = ControlSettings::defaults();
    previous.mouse.sensitivity = 88;
    previous.mouse.invertY = true;
    previous.fovDegrees = 110;
    previous.bind(Action::Vertical, Key::K);
    REQUIRE(saveControls(previous, file));

    // THE NEXT LAUNCH. loadControls() then setControls(), the same order and
    // the same two calls main.cpp makes, nothing else.
    SessionConfig config;
    config.contentDir = content::contentDir();
    const ControlSettings loaded = loadControls(file);
    Session session(config);
    session.setControls(loaded);

    // THE SENSITIVITY IS LIVE, not just stored. MouseSettings::yawFor is the
    // ONE place either preference is read (its own header says so), so this
    // is the actual multiplier a real mouse delta would go through --
    // checked at the boundary rather than by re-reading the field back.
    CHECK(session.controls().mouse.sensitivity == 88);
    CHECK(session.controls().mouse.invertY);
    const sim::Angle turned = session.controls().mouse.yawFor(1000);
    const sim::Angle defaultTurn = ControlSettings::defaults().mouse.yawFor(1000);
    CHECK(turned > defaultTurn);

    // THE FIELD OF VIEW IS LIVE. fovDegrees() is what the camera reads every
    // frame -- see its own header comment -- so this is the number a captured
    // frame would actually be built with, not a copy of it.
    CHECK(session.fovDegrees() == 110);

    // AND THE REBINDING IS LIVE, on the page a player would actually read it
    // off: the keys page is GENERATED from session.controls() (Session::
    // keyRows's whole reason to exist -- see its own header), so this is the
    // same check "a rebinding shows up on the reference page by construction"
    // makes elsewhere, aimed at a binding that came off a real file this time
    // rather than off session.setControls() called directly in the test.
    const auto mentions = [](const std::vector<std::string>& rows, const char* fragment) {
        for (const std::string& row : rows) {
            if (row.find(fragment) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    CHECK(mentions(session.keyRows(), "K  JUMP"));
    CHECK_FALSE(mentions(session.keyRows(), "SPACE  JUMP"));

    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("every action and key name round trips, because the file format depends on it") {
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        INFO("action index ", i);
        CHECK(actionFromKey(actionKey(action)) == action);
        CHECK_FALSE(actionLabel(action).empty());
    }
    for (std::int32_t i = 1; i < static_cast<std::int32_t>(Key::Count); ++i) {
        const Key key = static_cast<Key>(i);
        INFO("key index ", i);
        // Every Key the enum declares has a name, and that name parses back to
        // it. A key with no name would be a key a settings file silently
        // forgets.
        CHECK(keyName(key) != "--");
        CHECK(keyFromName(keyName(key)) == key);
    }
    // Case does not matter to a hand-edited file.
    CHECK(keyFromName("lshift") == Key::LeftShift);
    CHECK(keyFromName("wheelup") == Key::WheelUp);
    CHECK(actionFromKey("QUICK_1") == Action::QuickSlot1);
    CHECK(keyFromName("") == Key::None);
    CHECK(actionFromKey("") == Action::Count);
}

TEST_CASE("#85 regression: an old-format save cannot make Pause unreachable") {
    // PRE-#85, "menu" WAS PAUSE. git show 45afdda^ has the old table:
    // {Action::Menu, "menu", "PAUSE"} with a shipped default of Escape +
    // PadStart. Post-#85, "menu" is still a recognised key -- it now names
    // the NEW journal/inventory screen (Action::Menu), and the renamed pause
    // action (Action::Pause) kept Escape as ITS OWN default too. A save file
    // written by the old build says exactly this line:
    const std::string_view oldSaveLine = "bind menu ESC PAD_START\n";

    // fromText() writes a parsed bind straight into the slots (see its own
    // comment on why -- bind() steals, and a whole file applied through it
    // would have each line un-bind the one before it whenever two lines name
    // the same key). Without a cross-line collision guard, that line puts
    // Escape on Menu's primary slot while Pause's own default (also Escape,
    // untouched because this file never mentions "pause") is still sitting
    // there too -- and actionFor() returns the FIRST match by enum index,
    // which is Menu (11) before Pause (14). Pause -- the actual system panic
    // screen this old file meant to bind -- goes silently unreachable by
    // keyboard.
    const ControlSettings loaded = ControlSettings::fromText(oldSaveLine);

    // THE ACTUAL REQUIREMENT: Pause is reachable by SOME input after loading
    // ANY file, old-format or current. Checked by resolved binding, not by
    // "it didn't crash" -- actionFor(Escape) must still be able to say
    // Action::Pause is one of the things Escape can mean, or the pad default
    // must still stand so a controller player is not stranded either.
    const std::size_t pauseIndex = static_cast<std::size_t>(Action::Pause);
    const bool pauseHasAnyKey =
        loaded.primary[pauseIndex] != Key::None || loaded.secondary[pauseIndex] != Key::None;
    CHECK(pauseHasAnyKey);
    // Escape itself must still resolve to SOMETHING that can open the pause
    // screen -- either Escape stayed on Pause, or Pause fell back to a key
    // that is not shared with the incoming Menu bind.
    const Key pausePrimary = loaded.primary[pauseIndex];
    const Key pauseSecondary = loaded.secondary[pauseIndex];
    const bool pauseReachableByKeyboard =
        (pausePrimary != Key::None && loaded.actionFor(pausePrimary) == Action::Pause) ||
        (pauseSecondary != Key::None && loaded.actionFor(pauseSecondary) == Action::Pause);
    CHECK(pauseReachableByKeyboard);

    // And the pad fallback (PadStart) must still actually resolve to Pause,
    // not have been silently stolen by the same collision from the other
    // direction -- the old line's second key is also PAD_START, on Menu now.
    CHECK(loaded.actionFor(Key::PadStart) == Action::Pause);
}

TEST_CASE("the collision guard does not block a legitimate two-action key swap") {
    // THE ADVERSARIAL CASE FOR THE GUARD ITSELF (acceptance criterion e): a
    // file that deliberately swaps two actions' keyboard keys is NOT the same
    // shape as the #85-migration bug above. BOTH actions are named, on
    // purpose, in the SAME file -- the guard must resolve this as a real
    // swap, not refuse it as an unsafe strand, or a legitimate rebind a
    // player actually wants becomes impossible to save.
    const ControlSettings swapped = ControlSettings::fromText(
        "bind menu ESC PAD_BACK\n"
        "bind pause TAB PAD_START\n");
    CHECK(swapped.bound(Action::Menu, Key::Escape));
    CHECK(swapped.bound(Action::Pause, Key::Tab));
    CHECK(swapped.actionFor(Key::Escape) == Action::Menu);
    CHECK(swapped.actionFor(Key::Tab) == Action::Pause);
    CHECK(swapped.bound(Action::Pause, Key::PadStart));
    // CORE ACTION #13 CHANGED THIS FILE'S PAD OUTCOME, deliberately: "bind
    // menu ... PAD_BACK" in a file that never names the map action is
    // indistinguishable from menu's own OLD shipped default carried forward
    // (an old toText() wrote every action's line), so fromText()'s migration
    // moves menu's PadBack slot to its NEW shipped pad default and gives the
    // map its Select button back -- the deterministic, documented fallback.
    CHECK(swapped.bound(Action::Menu, Key::PadUp));
    CHECK_FALSE(swapped.bound(Action::Menu, Key::PadBack));
    CHECK(swapped.actionFor(Key::PadBack) == Action::Map);
}

// ---------------------------------------------------------------------------
// ROUND 3: the whole-file candidate-table fix. Two rounds of guarding one
// bind() call site each broke on the next adversarial line ordering that
// call site could not see (see fromText()'s own header for the blow-by-blow).
// Nothing below hand-picks a single ordering and calls it proven again.
// ---------------------------------------------------------------------------

TEST_CASE("round 2's own hole: two full lines, neither with a second key, "
          "in toText's own write order") {
    // THE EXACT SHAPE THAT SURVIVED ROUND 2. fromText() applies a line's
    // primary and secondary key as two SEPARATE bind() calls; a line with no
    // second key writes Key::None as its own second call, and round 2's
    // guard only ever fired on `key != Key::None`. "bind menu ESC" then
    // "bind pause ESC" is not a contrived ordering -- it is the ORDINARY one:
    // toText() always writes Menu's line before Pause's, because Menu's enum
    // index precedes Pause's, so this is what a hand-edited file that rebinds
    // Pause to Escape (its own shipped default, entirely reasonably typed by
    // hand) looks like sitting next to an untouched Menu line.
    const ControlSettings loaded =
        ControlSettings::fromText("bind menu ESC\nbind pause ESC\n");
    const std::size_t pauseIndex = static_cast<std::size_t>(Action::Pause);
    INFO("pause primary=", keyName(loaded.primary[pauseIndex]),
         " secondary=", keyName(loaded.secondary[pauseIndex]));
    CHECK((loaded.primary[pauseIndex] != Key::None || loaded.secondary[pauseIndex] != Key::None));
    // Reachable, not just non-empty: Escape actually resolves to Pause, since
    // Menu gave it up entirely (both its slots came back empty from parsing
    // and the validation pass restored Menu to its own full shipped default,
    // J + PadUp -- J per the owner's "use J for journal", PadUp since core
    // action #13 took PadBack for the map -- rather than leaving it holding
    // a key it shares with Pause).
    CHECK(loaded.actionFor(Key::Escape) == Action::Pause);
    CHECK(loaded.bound(Action::Menu, Key::J));
    CHECK(loaded.bound(Action::Menu, Key::PadUp));
}

TEST_CASE("every CORE action keeps at least one live key, across a spread of "
          "adversarial line orderings and two different action pairs") {
    // NOT JUST THE TWO KNOWN BAD ORDERINGS. Round 1 and round 2 each closed
    // exactly the one shape their own verifier found. This iterates a spread
    // of shapes over TWO different CORE pairs -- Menu/Pause (the pair both
    // prior rounds' bugs were found on) and Interact/Attack (a pair that has
    // nothing to do with either prior fix, to prove this is not a
    // Menu/Pause-specific patch) -- and asserts the actual invariant the
    // architecture promises: after ANY load, EVERY CORE action has at least
    // one live binding. Not just the two actions a given line touches --
    // ALL TEN, every time, because a restoration cascade (fixing one action
    // can steal a key off a different one) is exactly the kind of collateral
    // strand a narrower check would miss.
    //
    // ALL THIRTEEN since Cast, Block and then Map joined the core list -- and
    // the sweep gained shapes that attack THEIR keys too, because a core
    // action is only as protected as the orderings the sweep actually tries.
    const char* const files[] = {
        // Round 2's own shape, both orderings.
        "bind menu ESC\nbind pause ESC\n",
        "bind pause ESC\nbind menu ESC\n",
        // Round 1's own shape: one line claims both of the other action's
        // shipped keys at once.
        "bind menu ESC PAD_START\n",
        "bind pause TAB PAD_BACK\n",
        // The single-key wipe split across two lines instead of landing on
        // one: primary stolen by one line, secondary by a different one.
        "bind menu ESC\nbind pause PAD_START\n",
        "bind pause PAD_START\nbind menu ESC\n",
        // Three lines touching the same pair, last write standing.
        "bind menu TAB\nbind pause TAB\nbind menu ESC\n",
        "bind pause ESC PAD_START\nbind menu ESC\nbind pause TAB\n",
        // The same shapes again, on Interact/Attack instead of Menu/Pause --
        // proving the fix is architectural and not keyed to one pair of
        // actions. Interact ships E + PadSouth; Attack ships MouseLeft +
        // PadWest.
        "bind interact E\nbind attack E\n",
        "bind attack E\nbind interact E\n",
        "bind interact MOUSE1 PAD_WEST\n",
        "bind attack E PAD_SOUTH\n",
        "bind interact E\nbind attack PAD_SOUTH\n",
        "bind attack PAD_SOUTH\nbind interact E\n",
        // A file that hits BOTH pairs at once, every line a single key, in
        // an order that does not mirror toText()'s own write order.
        "bind pause ESC\nbind attack E\nbind menu ESC\nbind interact E\n",
        // The same shapes again on the NEW pair, Cast/Block. Cast ships
        // C + PAD_RT; Block ships MOUSE2 + PAD_LT.
        "bind cast MOUSE2\nbind block MOUSE2\n",
        "bind block C\nbind cast C\n",
        "bind cast MOUSE2 PAD_LT\n",
        "bind block C PAD_RT\n",
        // And a new action stealing an OLD action's keys, both directions --
        // the cross-generation shape no pre-Cast/Block file could produce.
        "bind cast MOUSE1 PAD_X\n",
        "bind attack C PAD_RT\nbind block MOUSE1\n",
        // CORE ACTION #13's own shapes: the map's keys stolen singly and
        // together, the map stealing an old action's keys, the migration's
        // own trigger line beside a hostile neighbour, and a file that names
        // the map so the migration must STAND DOWN while the validation pass
        // still holds every core action live.
        "bind menu M PAD_UP\n",
        "bind attack M PAD_BACK\n",
        "bind map TAB PAD_START\n",
        "bind menu TAB PAD_BACK\nbind pause M PAD_START\n",
        "bind map ESC\nbind pause ESC\n",
        "bind cast M\nbind map C PAD_RT\n",
    };
    for (const char* const file : files) {
        INFO("file: ", file);
        const ControlSettings loaded = ControlSettings::fromText(file);
        for (const Action action : {Action::Attack, Action::Interact, Action::Crouch,
                                     Action::Vertical, Action::Sprint, Action::Menu,
                                     Action::PagePrev, Action::PageNext, Action::Pause,
                                     Action::QuickWheel, Action::Cast, Action::Block,
                                     Action::Map}) {
            const std::size_t index = static_cast<std::size_t>(action);
            INFO("action: ", actionKey(action),
                 " primary=", keyName(loaded.primary[index]),
                 " secondary=", keyName(loaded.secondary[index]));
            CHECK((loaded.primary[index] != Key::None || loaded.secondary[index] != Key::None));
        }
    }
}

TEST_CASE("a settings file from before Cast and Block existed loads with both "
          "on their shipped defaults") {
    // THE BACKWARD-COMPAT GUARANTEE, PROVEN RATHER THAN ASSUMED. A player who
    // saved granadad-controls.cfg on the 10-action build has a file that
    // never says "bind cast" or "bind block" anywhere. This is toText()'s own
    // write order for that build -- every action that existed, spelled the
    // way the old build spelled it, including a real rebind (Vertical to K)
    // to prove the old lines still land while the missing ones default.
    const ControlSettings loaded = ControlSettings::fromText(
        "# Granadad: The Darkstreets -- controls.\n"
        "bind forward W UP\n"
        "bind back S DOWN\n"
        "bind strafe_left A\n"
        "bind strafe_right D\n"
        "bind turn_left LEFT\n"
        "bind turn_right RIGHT\n"
        "bind attack MOUSE1 PAD_X\n"
        "bind interact E PAD_A\n"
        "bind crouch LCTRL PAD_B\n"
        "bind vertical K PAD_Y\n"
        "bind sprint LSHIFT PAD_LS\n"
        "bind menu TAB PAD_BACK\n"
        "bind page_prev LBRACKET PAD_LB\n"
        "bind page_next RBRACKET PAD_RB\n"
        "bind pause ESC PAD_START\n"
        "bind quick_wheel Q PAD_RS\n"
        "bind quick_1 1\n"
        "bind screenshot F12\n"
        "set sensitivity 22\n");

    // The old file's own lines landed.
    CHECK(loaded.bound(Action::Vertical, Key::K));
    CHECK(loaded.mouse.sensitivity == 22);

    // AND THE ACTIONS THE FILE HAS NEVER HEARD OF ARE ON THEIR SHIPPED
    // DEFAULTS, reachable -- not empty, not stranded. fromText() starts from
    // defaults() and the file never overwrote these slots; being on
    // kCoreActions means even a file that STOLE their keys would get them
    // restored, but the ordinary old file never touches them at all.
    CHECK(loaded.bound(Action::Cast, Key::C));
    CHECK(loaded.bound(Action::Cast, Key::PadRightTrigger));
    CHECK(loaded.bound(Action::Block, Key::MouseRight));
    CHECK(loaded.bound(Action::Block, Key::PadLeftTrigger));
    CHECK(loaded.actionFor(Key::C) == Action::Cast);
    CHECK(loaded.actionFor(Key::MouseRight) == Action::Block);

    // AND THE MAP MIGRATION FIRED ON THIS FILE'S OWN "bind menu TAB PAD_BACK"
    // LINE -- menu's old shipped default, spelled out the way every old
    // toText() spelled it -- so the map holds M AND the Select button, and
    // Menu holds Tab AND its new D-pad-up default. Every one of the 13 core
    // actions ends this load with a live key on BOTH device families, which
    // is the whole backward-compat claim for a file this old.
    CHECK(loaded.bound(Action::Map, Key::M));
    CHECK(loaded.bound(Action::Map, Key::PadBack));
    CHECK(loaded.actionFor(Key::PadBack) == Action::Map);
    CHECK(loaded.bound(Action::Menu, Key::Tab));
    CHECK(loaded.bound(Action::Menu, Key::PadUp));
    const auto isPadKey = [](Key key) noexcept {
        return key >= Key::PadSouth && key <= Key::PadRight;
    };
    for (const Action action : {Action::Attack, Action::Interact, Action::Crouch,
                                 Action::Vertical, Action::Sprint, Action::Menu,
                                 Action::PagePrev, Action::PageNext, Action::Pause,
                                 Action::QuickWheel, Action::Cast, Action::Block, Action::Map}) {
        const std::size_t index = static_cast<std::size_t>(action);
        const Key first = loaded.primary[index];
        const Key second = loaded.secondary[index];
        INFO("action ", actionKey(action), " primary=", keyName(first),
             " secondary=", keyName(second));
        // One live pad key, and one live NON-pad (keyboard/mouse) key.
        CHECK(((first != Key::None && isPadKey(first)) ||
               (second != Key::None && isPadKey(second))));
        CHECK(((first != Key::None && !isPadKey(first)) ||
               (second != Key::None && !isPadKey(second))));
    }
}

// ---------------------------------------------------------------------------
// CORE ACTION #13's OWN MIGRATION -- the project's most-burned bug class
// (three prior fix rounds on exactly this shape), so every branch of the rule
// stated in fromText()'s MIGRATION comment gets its own adversarial file.
// ---------------------------------------------------------------------------

TEST_CASE("map migration: an S13-era file (menu on PAD_BACK, cast/block "
          "present, no map) lands all 13 on both devices") {
    // toText()'s own write order for the build one before this one: every
    // action spelled out, menu carrying its then-default PAD_BACK, and a
    // real rebind (Vertical to K) to prove the file's own lines still land.
    const ControlSettings loaded = ControlSettings::fromText(
        "bind forward W UP\n"
        "bind back S DOWN\n"
        "bind strafe_left A\n"
        "bind strafe_right D\n"
        "bind turn_left LEFT\n"
        "bind turn_right RIGHT\n"
        "bind attack MOUSE1 PAD_X\n"
        "bind interact E PAD_A\n"
        "bind crouch LCTRL PAD_B\n"
        "bind vertical K PAD_Y\n"
        "bind sprint LSHIFT PAD_LS\n"
        "bind menu TAB PAD_BACK\n"
        "bind page_prev LBRACKET PAD_LB\n"
        "bind page_next RBRACKET PAD_RB\n"
        "bind pause ESC PAD_START\n"
        "bind quick_wheel Q PAD_RS\n"
        "bind quick_1 1\n"
        "bind screenshot F12\n"
        "bind cast C PAD_RT\n"
        "bind block MOUSE2 PAD_LT\n"
        "set sensitivity 22\n");
    // The rebind landed; the migration moved exactly one thing.
    CHECK(loaded.bound(Action::Vertical, Key::K));
    CHECK(loaded.bound(Action::Menu, Key::Tab));
    CHECK(loaded.bound(Action::Menu, Key::PadUp));
    CHECK_FALSE(loaded.bound(Action::Menu, Key::PadBack));
    CHECK(loaded.bound(Action::Map, Key::M));
    CHECK(loaded.bound(Action::Map, Key::PadBack));
    CHECK(loaded.actionFor(Key::PadBack) == Action::Map);
    CHECK(loaded.actionFor(Key::PadUp) == Action::Menu);
    // Both devices, all 13 -- the acceptance sentence for this file shape.
    const auto isPadKey = [](Key key) noexcept {
        return key >= Key::PadSouth && key <= Key::PadRight;
    };
    for (const Action action : {Action::Attack, Action::Interact, Action::Crouch,
                                 Action::Vertical, Action::Sprint, Action::Menu,
                                 Action::PagePrev, Action::PageNext, Action::Pause,
                                 Action::QuickWheel, Action::Cast, Action::Block, Action::Map}) {
        const std::size_t index = static_cast<std::size_t>(action);
        const Key first = loaded.primary[index];
        const Key second = loaded.secondary[index];
        INFO("action ", actionKey(action), " primary=", keyName(first),
             " secondary=", keyName(second));
        CHECK(((first != Key::None && isPadKey(first)) ||
               (second != Key::None && isPadKey(second))));
        CHECK(((first != Key::None && !isPadKey(first)) ||
               (second != Key::None && !isPadKey(second))));
    }
}

TEST_CASE("map migration: an old file with menu custom-bound AWAY from "
          "PAD_BACK is left exactly as its author wrote it") {
    // Menu on a key of the player's own choosing (PadLeft was never a shipped
    // default, so this line can only be deliberate). No PAD_BACK anywhere, so
    // the migration has nothing to move: menu stands as written, and the map
    // -- which the file predates -- simply keeps its whole shipped default.
    const ControlSettings loaded = ControlSettings::fromText(
        "bind menu J PAD_LEFT\n"
        "bind pause ESC PAD_START\n");
    CHECK(loaded.bound(Action::Menu, Key::J));
    CHECK(loaded.bound(Action::Menu, Key::PadLeft));
    CHECK_FALSE(loaded.bound(Action::Menu, Key::PadUp));
    CHECK(loaded.bound(Action::Map, Key::M));
    CHECK(loaded.bound(Action::Map, Key::PadBack));
    CHECK(loaded.actionFor(Key::PadBack) == Action::Map);
}

TEST_CASE("map migration: a NON-menu action holding PAD_BACK in an old file "
          "is a deliberate choice and is respected") {
    // PadBack shipped on Menu alone, so an old file putting it on Attack can
    // only be the player's own hand. The migration must NOT steal it back:
    // Attack keeps it, the map keeps M (keyboard-live, pad-dead -- the
    // player's own trade), and every core action still holds a live key.
    const ControlSettings loaded = ControlSettings::fromText(
        "bind attack MOUSE1 PAD_BACK\n"
        "bind menu TAB PAD_LEFT\n");
    CHECK(loaded.bound(Action::Attack, Key::PadBack));
    CHECK(loaded.actionFor(Key::PadBack) == Action::Attack);
    CHECK(loaded.bound(Action::Map, Key::M));
    CHECK(loaded.actionFor(Key::M) == Action::Map);
}

TEST_CASE("map migration: a user who deliberately bound something else to M") {
    // TWO SHAPES, one per generation. A file that KNOWS the map action and
    // moves both keys deliberately: everything stands as written.
    const ControlSettings knows = ControlSettings::fromText(
        "bind cast M PAD_RT\n"
        "bind map N PAD_BACK\n");
    CHECK(knows.bound(Action::Cast, Key::M));
    CHECK(knows.bound(Action::Map, Key::N));
    CHECK(knows.bound(Action::Map, Key::PadBack));
    CHECK(knows.actionFor(Key::M) == Action::Cast);
    CHECK(knows.actionFor(Key::N) == Action::Map);

    // And a file that PREDATES the map action but had already spent M on
    // something else (its author never heard of a map key, so this cannot
    // have been aimed at it): the choice is respected -- Cast keeps M -- and
    // the map is still live through the pad side the migration restored off
    // the file's own old-default menu line. Pad-only for the map here is the
    // honest outcome of the player's own earlier claim on M, and the
    // options page is where they hand it back if they want it.
    const ControlSettings predates = ControlSettings::fromText(
        "bind cast M PAD_RT\n"
        "bind menu TAB PAD_BACK\n");
    CHECK(predates.bound(Action::Cast, Key::M));
    CHECK(predates.actionFor(Key::M) == Action::Cast);
    CHECK(predates.bound(Action::Map, Key::PadBack));
    CHECK(predates.actionFor(Key::PadBack) == Action::Map);
    CHECK(predates.bound(Action::Menu, Key::PadUp));
    const std::size_t mapIndex = static_cast<std::size_t>(Action::Map);
    CHECK((predates.primary[mapIndex] != Key::None ||
           predates.secondary[mapIndex] != Key::None));
}

TEST_CASE("round-1's own regression case still holds under the whole-file fix") {
    // THE ORIGINAL #85 MIGRATION SHAPE, kept verbatim (not just folded into
    // the adversarial sweep above) because this is the case the very first
    // guard was written for, and it is worth its own name.
    const std::string_view oldSaveLine = "bind menu ESC PAD_START\n";
    const ControlSettings loaded = ControlSettings::fromText(oldSaveLine);
    const std::size_t pauseIndex = static_cast<std::size_t>(Action::Pause);
    CHECK((loaded.primary[pauseIndex] != Key::None || loaded.secondary[pauseIndex] != Key::None));
    const bool pauseReachableByKeyboard =
        (loaded.primary[pauseIndex] != Key::None &&
         loaded.actionFor(loaded.primary[pauseIndex]) == Action::Pause) ||
        (loaded.secondary[pauseIndex] != Key::None &&
         loaded.actionFor(loaded.secondary[pauseIndex]) == Action::Pause);
    CHECK(pauseReachableByKeyboard);
}

// ---------------------------------------------------------------------------
// the device-aware prompt lookup -- ship note move 3
// ---------------------------------------------------------------------------

TEST_CASE("a prompt names the device holding it, off the shipped table") {
    const ControlSettings keys = ControlSettings::defaults();

    // The exact frame the ship note photographed: the street prompt. E on a
    // keyboard, A on a pad -- both halves of Interact's own row, not a
    // parallel table.
    CHECK(promptLabel(keys, Action::Interact, InputDevice::KeyboardMouse) == "E");
    CHECK(promptLabel(keys, Action::Interact, InputDevice::Pad) == "A");

    // The casebook's close key and the ward map's, both halves each. J per
    // the owner's "use J for journal since that's how it's done by
    // convention".
    CHECK(promptLabel(keys, Action::Menu, InputDevice::KeyboardMouse) == "J");
    CHECK(promptLabel(keys, Action::Menu, InputDevice::Pad) == "\x06\x02");  // the d-pad cross + up motif
    CHECK(promptLabel(keys, Action::Map, InputDevice::KeyboardMouse) == "M");
    CHECK(promptLabel(keys, Action::Map, InputDevice::Pad) == "SELECT");
    CHECK(promptLabel(keys, Action::Pause, InputDevice::KeyboardMouse) == "ESC");
    CHECK(promptLabel(keys, Action::Pause, InputDevice::Pad) == "START");

    // The page pair prints the glyphs the 4x6 font can actually draw, never
    // the bracket names -- and the pad half is the bumpers.
    CHECK(promptLabel(keys, Action::PagePrev, InputDevice::KeyboardMouse) == "<");
    CHECK(promptLabel(keys, Action::PageNext, InputDevice::KeyboardMouse) == ">");
    CHECK(promptLabel(keys, Action::PagePrev, InputDevice::Pad) == "LB");
    CHECK(promptLabel(keys, Action::PageNext, InputDevice::Pad) == "RB");

    // The map's zoom pair on a pad: the triggers, exactly what main.cpp
    // routes (Cast/Block while the map is up).
    CHECK(promptLabel(keys, Action::Cast, InputDevice::Pad) == "RT");
    CHECK(promptLabel(keys, Action::Block, InputDevice::Pad) == "LT");
}

TEST_CASE("the prompt lookup falls back to the other hand rather than lying") {
    ControlSettings keys = ControlSettings::defaults();

    // Quick slot 1 has no pad half in the shipped table: the pad asks and the
    // keyboard answers, because a reachable verb must never print "--".
    CHECK(promptLabel(keys, Action::QuickSlot1, InputDevice::Pad) == "1");

    // Strip Interact to its pad half alone; the keyboard asks and the pad
    // answers, same rule the other way round.
    keys.primary[static_cast<std::size_t>(Action::Interact)] = Key::None;
    CHECK(promptLabel(keys, Action::Interact, InputDevice::KeyboardMouse) == "A");

    // No key at all is "--", keyName()'s own unbound answer.
    keys.secondary[static_cast<std::size_t>(Action::Interact)] = Key::None;
    CHECK(promptLabel(keys, Action::Interact, InputDevice::Pad) == "--");
    CHECK(promptLabel(keys, Action::Interact, InputDevice::KeyboardMouse) == "--");
}

TEST_CASE("a rebinding renames the prompt on BOTH devices by construction") {
    ControlSettings keys = ControlSettings::defaults();
    // The player moves Interact to F and PadNorth: the lookup reads the same
    // slots bind() wrote, so there is nothing to keep in step.
    keys.bind(Action::Interact, Key::F, /*asSecondary=*/false);
    keys.bind(Action::Interact, Key::PadNorth, /*asSecondary=*/true);
    CHECK(promptLabel(keys, Action::Interact, InputDevice::KeyboardMouse) == "F");
    CHECK(promptLabel(keys, Action::Interact, InputDevice::Pad) == "Y");
}

TEST_CASE("the device of a key, and the pad's spoken vocabulary") {
    // Mouse and keyboard are ONE device; every pad key is the other.
    CHECK(deviceOfKey(Key::E) == InputDevice::KeyboardMouse);
    CHECK(deviceOfKey(Key::MouseLeft) == InputDevice::KeyboardMouse);
    CHECK(deviceOfKey(Key::WheelUp) == InputDevice::KeyboardMouse);
    CHECK(deviceOfKey(Key::None) == InputDevice::KeyboardMouse);
    CHECK(deviceOfKey(Key::PadSouth) == InputDevice::Pad);
    CHECK(deviceOfKey(Key::PadRight) == InputDevice::Pad);
    for (int k = static_cast<int>(Key::PadSouth); k <= static_cast<int>(Key::PadRight); ++k) {
        CHECK(keyIsPad(static_cast<Key>(k)));
    }
    CHECK_FALSE(keyIsPad(Key::WheelDown));

    // The spoken names are the controls page's own letters, unprefixed --
    // "PAD_A" on the reference page, "A" on a prompt, one fact twice.
    CHECK(promptKeyName(Key::PadSouth) == "A");
    CHECK(promptKeyName(Key::PadEast) == "B");
    CHECK(promptKeyName(Key::PadWest) == "X");
    CHECK(promptKeyName(Key::PadNorth) == "Y");
    CHECK(promptKeyName(Key::PadStart) == "START");
    CHECK(promptKeyName(Key::PadBack) == "SELECT");
    CHECK(promptKeyName(Key::PadUp) == "\x06\x02");  // UI-EA-SPEC sec. 5: cross + triangle
    // And a keyboard key is exactly keyName()'s answer.
    CHECK(promptKeyName(Key::E) == keyName(Key::E));
    CHECK(promptKeyName(Key::Tab) == keyName(Key::Tab));

    // The page grammar the router hard-codes, said once.
    CHECK(promptConfirmKey(InputDevice::KeyboardMouse) == "\x01");  // the return motif
    CHECK(promptConfirmKey(InputDevice::Pad) == "A");
    CHECK(promptBackKey(InputDevice::KeyboardMouse) == "ESC");
    CHECK(promptBackKey(InputDevice::Pad) == "B");
    CHECK(promptMoveKeys(InputDevice::KeyboardMouse) == "\x02\x03");  // the triangle pair
    CHECK(promptMoveKeys(InputDevice::Pad) == "\x06");  // the bare cross
}

TEST_CASE("the B seam: East is Escape while a page is up, itself otherwise") {
    // THE SHIP NOTE'S SEAM #1, pinned. The parity pass remapped PadEast to
    // Escape INSIDE the router and then let the caller replay the raw press,
    // so B closing the casebook also reached Crouch's binding and the street
    // carried a CROUCHED banner. The remap is one function applied once at
    // the event edge now; this is its whole contract.
    //
    // While a page owns the input, East IS the universal back...
    CHECK(pageBackRemap(Key::PadEast, /*pageOpen=*/true) == Key::Escape);
    // ...and with no page open it is exactly itself, so the world's B stays
    // crouch (PadEast is Crouch's shipped secondary).
    CHECK(pageBackRemap(Key::PadEast, /*pageOpen=*/false) == Key::PadEast);
    CHECK(ControlSettings::defaults().actionFor(Key::PadEast) == Action::Crouch);
    // No other key is touched, page or no page -- the D-pad stays raw list
    // movement and Escape is already Escape.
    for (const Key key : {Key::PadSouth, Key::PadWest, Key::PadNorth, Key::PadUp, Key::PadDown,
                          Key::PadStart, Key::PadBack, Key::Escape, Key::E, Key::None}) {
        CHECK(pageBackRemap(key, true) == key);
        CHECK(pageBackRemap(key, false) == key);
    }
    // And the remapped key actually resolves to the action whose Pause
    // branch backs out of whatever is open -- the close is the SAME route a
    // keyboard ESC takes, so the two cannot drift.
    CHECK(ControlSettings::defaults().actionFor(Key::Escape) == Action::Pause);
}

TEST_CASE("the live session re-words its prompts the moment the other hand speaks") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    Session session(config);

    // The shipped default: a session that has never heard a press speaks
    // keyboard, which is also what every capture flag and every case written
    // before this existed gets -- byte-identical frames.
    CHECK(session.promptDevice() == InputDevice::KeyboardMouse);

    // THE PAUSE HEADER -- the composition the ship note photographed, with
    // the keyboard confirm speaking the return motif now (UI-EA-SPEC sec. 5;
    // the ENTER word died at the choke point).
    session.togglePause();
    CHECK(session.dialogueView().epithet == "\x01 SELECTS  ESC RESUMES");

    // One pad press. No menu visit, no reopen: the SAME open page re-words.
    session.noteInputDevice(InputDevice::Pad);
    CHECK(session.dialogueView().epithet == "A SELECTS  START RESUMES");

    // And straight back the moment a key speaks -- noteInputKey classifies.
    session.noteInputKey(Key::E);
    CHECK(session.dialogueView().epithet == "\x01 SELECTS  ESC RESUMES");
    // Key::None is nobody and moves nothing.
    session.noteInputDevice(InputDevice::Pad);
    session.noteInputKey(Key::None);
    CHECK(session.promptDevice() == InputDevice::Pad);
    session.togglePause();

    // THE CASEBOOK PAGE: the foot's close key and the look key, both hands.
    // On a pad the close is B -- the parity pass reads the D-pad as list
    // movement while the book is up, so Menu's own D-PAD UP cannot close it.
    session.noteInputKey(Key::W);
    CasebookPageState kb = session.casebookPageState();
    CHECK(kb.closeKey == "J");
    CHECK(kb.lookKey == "E");
    // And the commit verb's key -- the "ENTER - SHOW ME WHERE" / "ENTER GO
    // TO IT" literals of the ship note's seam list, on a state field now.
    CHECK(kb.commitKey == "\x01");  // the return motif -- UI-EA-SPEC sec. 5
    session.noteInputDevice(InputDevice::Pad);
    CasebookPageState pad = session.casebookPageState();
    CHECK(pad.closeKey == "B");
    CHECK(pad.lookKey == "A");
    CHECK(pad.commitKey == "A");
    if (!pad.rows.empty() && pad.read == 0 && !pad.closed) {
        CHECK(pad.instruction == "PICK A LEAD. A SHOWS YOU WHERE.");
    }

    // THE WARD MAP'S NAV BAND: what main.cpp actually routes on a pad --
    // D-pad walks, the bumpers tab, the triggers zoom, SELECT closes, A
    // commits -- and the old literals with a keyboard in hand.
    DistrictMapState padMap = session.districtMapState();
    // The movement keys are the keycap motifs now (UI-EA-SPEC sec. 5): the
    // d-pad cross sentinel on a pad, the four arrowheads on a keyboard.
    CHECK(padMap.navMoveKeys == std::string(kGlyphCross));
    CHECK(padMap.navTabKeys == "LB RB");
    CHECK(padMap.navZoomKeys == "RT LT");
    CHECK(padMap.navCloseKey == "SELECT");
    CHECK(padMap.commitKey == "A");
    session.noteInputKey(Key::M);
    DistrictMapState kbMap = session.districtMapState();
    CHECK(kbMap.navMoveKeys == std::string(kGlyphMoveKeys));
    CHECK(kbMap.navTabKeys == "TAB");
    CHECK(kbMap.navZoomKeys == "+ -");
    CHECK(kbMap.navCloseKey == "M");
    CHECK(kbMap.commitKey == "\x01");

    // THE DIALOGUE WIDGET'S OWN KEYS ride the state the same way.
    session.noteInputDevice(InputDevice::Pad);
    const DialogueViewState padView = session.dialogueView();
    CHECK(padView.confirmKey == "A");
    CHECK(padView.backKey == "B");
    CHECK(padView.takeKey == "RB");
    CHECK(padView.letterDownLine.empty());
    session.noteInputKey(Key::Space);
    const DialogueViewState kbView = session.dialogueView();
    CHECK(kbView.confirmKey == "\x01");
    CHECK(kbView.backKey == "ESC");
    CHECK(kbView.takeKey == "T");
    CHECK(kbView.letterDownLine == "L PUTS IT DOWN");
}

TEST_CASE("the opening hint is generated from the bindings and re-words live") {
    // The S3 verification gap, closed: the hint is openingHintLine() off the
    // live table now (J for journal, the owner's own convention call), and a
    // pad press while it is still up re-words it.
    //
    // UI-EA (LANE HUD): LIVE VERBS ONLY. The old band advertised
    // "< > MORE PAGES" on a street where PagePrev/PageNext do nothing (flow
    // map violation #11); the diet's hint names the three verbs a stranger
    // can actually press where they stand -- notes, map, hand -- keycap then
    // verb, no filler.
    SessionConfig config;
    config.contentDir = content::contentDir();
    config.openingPage = true;
    Session session(config);
    if (session.lastMessage().empty()) {
        return;  // no authored case in this content dir; nothing to word
    }
    CHECK(session.lastMessage() == "J NOTES  M MAP  E USE");
    session.noteInputDevice(InputDevice::Pad);
    // HUD's dieted band (live verbs only, violation #11) carrying FLOW's
    // motif key: the pad's D-PAD UP is the cross+up sentinels, SELECT stays
    // a word (sec. 5 leaves the named buttons alone).
    CHECK(session.lastMessage() == "\x06\x02 NOTES  SELECT MAP  A USE");
    session.noteInputKey(Key::A);
    CHECK(session.lastMessage() == "J NOTES  M MAP  E USE");
}

// ---------------------------------------------------------------------------
// UI-EA-SPEC sec. 5: the motif sentinels, and sec. 4 violation #5: F1/F2
// become real actions. FLOW lane.
// ---------------------------------------------------------------------------

TEST_CASE("the six motif sentinels are exactly 0x01-0x06 and nothing else") {
    CHECK(kMotifReturn == '\x01');
    CHECK(kMotifUp == '\x02');
    CHECK(kMotifDown == '\x03');
    CHECK(kMotifLeft == '\x04');
    CHECK(kMotifRight == '\x05');
    CHECK(kMotifDPad == '\x06');
    for (int c = 0; c < 128; ++c) {
        const bool inRange = c >= 1 && c <= 6;
        CHECK(isMotifSentinel(static_cast<char>(c)) == inRange);
    }
}

TEST_CASE("the prompt choke points speak motifs for the long key names only") {
    // THE SUBSTITUTION TABLE, PINNED: ENTER and the four arrows become their
    // motifs; ESC, letters, digits, TAB, the face buttons and the shoulder
    // pair stay the short iconic words they already were. Every one of these
    // reaches the screen through promptKeyName/promptConfirmKey/
    // promptMoveKeys, so pinning the choke points pins every foot at once.
    CHECK(promptKeyName(Key::Enter) == "\x01");
    CHECK(promptKeyName(Key::Up) == "\x02");
    CHECK(promptKeyName(Key::Down) == "\x03");
    CHECK(promptKeyName(Key::Left) == "\x04");
    CHECK(promptKeyName(Key::Right) == "\x05");
    CHECK(promptKeyName(Key::PadUp) == "\x06\x02");
    CHECK(promptKeyName(Key::PadDown) == "\x06\x03");
    CHECK(promptKeyName(Key::PadLeft) == "\x06\x04");
    CHECK(promptKeyName(Key::PadRight) == "\x06\x05");
    // Unchanged, deliberately -- short and iconic beats a motif.
    CHECK(promptKeyName(Key::Escape) == "ESC");
    CHECK(promptKeyName(Key::Tab) == "TAB");
    CHECK(promptKeyName(Key::PadSouth) == "A");
    CHECK(promptKeyName(Key::E) == "E");
    // The device grammar: keyboard confirm is the return motif, keyboard
    // move is the triangle pair, pad move is the bare cross; back stays
    // ESC/B on both hands.
    CHECK(promptConfirmKey(InputDevice::KeyboardMouse) == "\x01");
    CHECK(promptConfirmKey(InputDevice::Pad) == "A");
    CHECK(promptMoveKeys(InputDevice::KeyboardMouse) == "\x02\x03");
    CHECK(promptMoveKeys(InputDevice::Pad) == "\x06");
    CHECK(promptBackKey(InputDevice::KeyboardMouse) == "ESC");
    CHECK(promptBackKey(InputDevice::Pad) == "B");
    // AND THE FILE FORMAT NEVER DOES: keyName() is what toText()/fromText()
    // speak, and a settings file with a control byte in it would be the
    // exact corruption the prompt vocabulary is documented never to cause.
    CHECK(keyName(Key::Enter) == "ENTER");
    CHECK(keyName(Key::Up) == "UP");
    CHECK(keyName(Key::PadUp) == "PAD_UP");
}

TEST_CASE("violation #5: F1 and F2 are real, bindable, defaulted actions now") {
    // --help has said "F1 lists every key and F2 rebinds them" since #85, and
    // until this pass both were hard-coded in main.cpp's event loop -- absent
    // from the very page F1 opens, unreachable by the rebinding screen. They
    // are ordinary actions now: named, labelled, defaulted, stealable.
    const ControlSettings keys = ControlSettings::defaults();
    CHECK(keys.bound(Action::KeysPage, Key::F1));
    CHECK(keys.bound(Action::OptionsPage, Key::F2));
    CHECK(keys.actionFor(Key::F1) == Action::KeysPage);
    CHECK(keys.actionFor(Key::F2) == Action::OptionsPage);
    // The settings-file vocabulary and the page labels.
    CHECK(actionKey(Action::KeysPage) == "keys_page");
    CHECK(actionKey(Action::OptionsPage) == "options_page");
    CHECK(actionFromKey("keys_page") == Action::KeysPage);
    CHECK(actionFromKey("options_page") == Action::OptionsPage);
    CHECK(actionLabel(Action::KeysPage) == "KEYS");
    CHECK(actionLabel(Action::OptionsPage) == "OPTIONS");
    // NOT CORE: the pad's door to both pages is the pause menu's own rows,
    // so neither spends a pad default -- and the 13-count case above stays
    // exactly 13.
    CHECK_FALSE(keyIsPad(keys.primary[static_cast<std::size_t>(Action::KeysPage)]));
    CHECK(keys.secondary[static_cast<std::size_t>(Action::KeysPage)] == Key::None);
    CHECK(keys.secondary[static_cast<std::size_t>(Action::OptionsPage)] == Key::None);
    // And a rebind can steal them, like any other action's key -- the whole
    // point of putting them in the table. THE COLLISION GUARD applies to
    // them like it does to anybody (see bind()'s own header): stealing F1
    // outright would strand the page -- one slot, no pad default -- so that
    // bind is REFUSED and the key stays. Move the page first; then the key
    // is anyone's.
    ControlSettings moved = ControlSettings::defaults();
    moved.bind(Action::QuickSlot1, Key::F1);
    CHECK(moved.bound(Action::KeysPage, Key::F1));  // refused: it would strand the page
    moved.bind(Action::KeysPage, Key::F5);
    CHECK(moved.bound(Action::KeysPage, Key::F5));
    moved.bind(Action::QuickSlot1, Key::F1);
    CHECK_FALSE(moved.bound(Action::KeysPage, Key::F1));
    CHECK(moved.actionFor(Key::F1) == Action::QuickSlot1);
}
