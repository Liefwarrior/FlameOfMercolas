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
#include "granadad/sim/tavern.hpp"

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

    // NINE AND THE STICKS. EVERY ONE OF THE NINE HAS A PAD DEFAULT, and the
    // pad half is Oblivion's own layout: the triggers are the two hands (RT
    // swings, LT guards), RB casts, A uses, B sneaks, Y jumps, D-pad up is
    // NOTES, START pauses, the stick click is RUN's optional second half.
    CHECK(keys.bound(Action::Attack, Key::PadRightTrigger));
    CHECK(keys.bound(Action::Block, Key::PadLeftTrigger));
    CHECK(keys.bound(Action::Cast, Key::PadRightBumper));
    CHECK(keys.bound(Action::Interact, Key::PadSouth));
    CHECK(keys.bound(Action::Crouch, Key::PadEast));
    CHECK(keys.bound(Action::Vertical, Key::PadNorth));
    CHECK(keys.bound(Action::Sprint, Key::PadLeftStick));
    CHECK(keys.bound(Action::Menu, Key::PadUp));
    CHECK(keys.bound(Action::Pause, Key::PadStart));

    // THE PAD BUTTONS THE NINE LEAVE FREE: X, R3, D-pad down. An unrecognised
    // press wakes the tutor bands (contract (c)); nothing else happens.
    CHECK(keys.actionFor(Key::PadWest) == Action::Count);
    CHECK(keys.actionFor(Key::PadRightStick) == Action::Count);
    CHECK(keys.actionFor(Key::PadDown) == Action::Count);
    // And the bumpers/brackets are page grammar, not world bindings on the
    // left: LB is free in the world, `[` `]` are unbound.
    CHECK(keys.actionFor(Key::PadLeftBumper) == Action::Count);
    CHECK(keys.actionFor(Key::LeftBracket) == Action::Count);
    CHECK(keys.actionFor(Key::RightBracket) == Action::Count);

    // THE TENTH: THE WARD MAP -- "a map that they can press M to see". M on
    // a keyboard and NO pad key: on a pad the map is a page of NOTES, one
    // bumper past the casebook (see the ring case in this file).
    CHECK(keys.bound(Action::Map, Key::M));
    CHECK(keys.actionFor(Key::M) == Action::Map);
    CHECK_FALSE(keyIsPad(keys.primary[static_cast<std::size_t>(Action::Map)]));
    CHECK(keys.secondary[static_cast<std::size_t>(Action::Map)] == Key::None);

    // THE BONUS SHORTCUTS. SELECT is WAIT now (Oblivion's own spend), and T
    // is its keyboard twin; the D-pad's left and right step the quick bar,
    // the QuickWheel's hold-and-step folded into two plain presses.
    CHECK(keys.bound(Action::Wait, Key::T));
    CHECK(keys.bound(Action::Wait, Key::PadBack));
    CHECK(keys.actionFor(Key::PadBack) == Action::Wait);
    CHECK(keys.bound(Action::QuickNext, Key::PadRight));
    CHECK(keys.bound(Action::QuickPrev, Key::PadLeft));
    CHECK(keys.actionFor(Key::PadRight) == Action::QuickNext);
    CHECK(keys.actionFor(Key::PadLeft) == Action::QuickPrev);

    // THE COMBAT TRIO ON THE KEYBOARD: the mouse's two buttons are the two
    // hands, C casts. Attack keeps the left button; Interact keeps E.
    CHECK(keys.bound(Action::Cast, Key::C));
    CHECK(keys.bound(Action::Block, Key::MouseRight));
    CHECK(keys.bound(Action::Attack, Key::MouseLeft));
    CHECK(keys.bound(Action::Interact, Key::E));
    // The F-keys and Q are free: nothing hard-coded, nothing bound.
    CHECK(keys.actionFor(Key::F1) == Action::Count);
    CHECK(keys.actionFor(Key::F2) == Action::Count);
    CHECK(keys.actionFor(Key::F3) == Action::Count);
    CHECK(keys.actionFor(Key::Q) == Action::Count);
}

TEST_CASE("nine and the sticks: the core count is nine, plus the keyboard's map") {
    // NINE. Swing, Guard, Cast, Use, Sneak, Jump, Run, Notes, Pause -- the
    // owner's ruling ("minimizing the number of inputs necessary. Even a
    // game like Morrowind worked on the console with just a few buttons"),
    // down from the thirteen #85 shipped. Plus MAP, the one keyboard-only
    // direct shortcut he asked for by name; on a pad the map is a page of
    // NOTES and spends nothing. Movement axes, the arrow-key turn fallback,
    // the bonus shortcuts (WAIT, the digits, the wheel) and Screenshot are
    // excluded exactly as the brief asked. A COUNTING test: a verb added to
    // the core bucket without updating this case is a red build instead of
    // a drifted comment.
    const Action core[] = {
        Action::Attack, Action::Block,    Action::Cast, Action::Interact, Action::Crouch,
        Action::Vertical, Action::Sprint, Action::Menu, Action::Pause,
    };
    CHECK(static_cast<int>(sizeof(core) / sizeof(core[0])) == 9);
    // And the whole table, so a stray survivor of the old thirteen (the
    // wheel, the page pair, the F-key pages) cannot creep back in unnoticed:
    // six axes, nine verbs, the map, WAIT, ten slots, two steps, the shutter.
    CHECK(kActionCount == 6 + 9 + 1 + 1 + 10 + 2 + 1);
    CHECK(actionFromKey("quick_wheel") == Action::Count);
    CHECK(actionFromKey("page_prev") == Action::Count);
    CHECK(actionFromKey("page_next") == Action::Count);
    CHECK(actionFromKey("keys_page") == Action::Count);
    CHECK(actionFromKey("options_page") == Action::Count);
    // The labels are the verbs the owner named.
    CHECK(actionLabel(Action::Attack) == "SWING");
    CHECK(actionLabel(Action::Block) == "GUARD");
    CHECK(actionLabel(Action::Cast) == "CAST");
    CHECK(actionLabel(Action::Interact) == "USE");
    CHECK(actionLabel(Action::Crouch) == "SNEAK");
    CHECK(actionLabel(Action::Vertical) == "JUMP");
    CHECK(actionLabel(Action::Sprint) == "RUN");
    CHECK(actionLabel(Action::Menu) == "NOTES");
    CHECK(actionLabel(Action::Pause) == "PAUSE");
    CHECK(actionLabel(Action::Map) == "MAP");
    CHECK(actionLabel(Action::Wait) == "WAIT");
}

TEST_CASE("every one of the nine resolves an actual pad key, generically") {
    // GENERIC AND ENUM-DRIVEN, unlike the shipped-bindings case above, which
    // hardcodes each verb's exact pad button. This proves the PROPERTY --
    // every one of the nine carries at least one key that is a pad key,
    // whatever that key happens to be -- so a core verb added without a pad
    // default fails HERE rather than only if somebody remembers another
    // hardcoded CHECK. The tenth, MAP, is the deliberate exception (keyboard
    // only; its pad route is the NOTES ring, proved in its own case below),
    // and every one of the nine ALSO carries a keyboard/mouse key: a verb a
    // keyboard player cannot reach is as invisible as one a pad cannot.
    const ControlSettings keys = ControlSettings::defaults();
    const Action core[] = {
        Action::Attack, Action::Block,    Action::Cast, Action::Interact, Action::Crouch,
        Action::Vertical, Action::Sprint, Action::Menu, Action::Pause,
    };
    for (const Action action : core) {
        const std::size_t index = static_cast<std::size_t>(action);
        INFO("action ", actionKey(action), " primary=", keyName(keys.primary[index]),
             " secondary=", keyName(keys.secondary[index]));
        CHECK((keyIsPad(keys.primary[index]) || keyIsPad(keys.secondary[index])));
        CHECK(((keys.primary[index] != Key::None && !keyIsPad(keys.primary[index])) ||
               (keys.secondary[index] != Key::None && !keyIsPad(keys.secondary[index]))));
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
    // NINE AND THE STICKS: a file that names NO retired verb is read exactly
    // as written -- "bind menu ... PAD_BACK" here is the author's own hand,
    // so Menu keeps SELECT and WAIT (whose shipped pad half that is) is left
    // keyboard-only, honestly. Not core, so the validation pass leaves the
    // trade alone; the pause menu's WAIT row is still its door.
    CHECK(swapped.bound(Action::Menu, Key::PadBack));
    CHECK(swapped.actionFor(Key::PadBack) == Action::Menu);
    CHECK(swapped.bound(Action::Wait, Key::T));
    CHECK_FALSE(swapped.bound(Action::Wait, Key::PadBack));
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
    // ALL TEN -- the nine and the map -- and the sweep carries shapes that
    // attack every one of their keys, because a core action is only as
    // protected as the orderings the sweep actually tries.
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
        // NINE AND THE STICKS' own shapes: the triggers and the right bumper
        // stolen by neighbours, an old file's retired lines beside hostile
        // ones (the migration must fire AND the pass must still hold), and
        // WAIT's T and SELECT taken by core verbs.
        "bind block MOUSE1 PAD_RT\nbind cast MOUSE2 PAD_LT\n",
        "bind interact C PAD_RB\n",
        "bind quick_wheel Q PAD_RS\nbind attack E PAD_A\nbind interact MOUSE1 PAD_RT\n",
        "bind page_next RBRACKET PAD_RB\nbind cast MOUSE1 PAD_RT\nbind attack C PAD_RB\n",
        "bind pause T PAD_BACK\nbind menu ESC PAD_START\n",
        "bind wait ESC PAD_START\nbind map J PAD_UP\n",
    };
    for (const char* const file : files) {
        INFO("file: ", file);
        const ControlSettings loaded = ControlSettings::fromText(file);
        for (const Action action : {Action::Attack, Action::Block, Action::Cast,
                                     Action::Interact, Action::Crouch, Action::Vertical,
                                     Action::Sprint, Action::Menu, Action::Pause, Action::Map}) {
            const std::size_t index = static_cast<std::size_t>(action);
            INFO("action: ", actionKey(action),
                 " primary=", keyName(loaded.primary[index]),
                 " secondary=", keyName(loaded.secondary[index]));
            CHECK((loaded.primary[index] != Key::None || loaded.secondary[index] != Key::None));
        }
    }
}

TEST_CASE("nine and the sticks migration: the S13-era file lands every old default "
          "on its new home and keeps the player's own rebind") {
    // THE BACKWARD-COMPAT GUARANTEE, PROVEN RATHER THAN ASSUMED, on the OLDEST
    // shape a real granadad-controls.cfg has on this machine (the pre-Cast/
    // Block, pre-Map, menu-on-TAB file): toText()'s own write order for that
    // build, every action spelled out, a real rebind (Vertical to K) to prove
    // the file's own lines still land. The file names quick_wheel and the
    // page pair, so it is a PRE-BREAK file and the per-slot rule applies:
    // an old shipped key is the old default carried forward and becomes the
    // new shipped key; anything else is the author's hand and stands.
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
        "bind quick_next WHEELDOWN\n"
        "bind quick_prev WHEELUP\n"
        "bind screenshot F12\n"
        "set sensitivity 22\n");

    // The old file's own lines landed.
    CHECK(loaded.bound(Action::Vertical, Key::K));
    CHECK(loaded.bound(Action::Vertical, Key::PadNorth));
    CHECK(loaded.mouse.sensitivity == 22);

    // THE MOVED DEFAULTS MOVED. Attack's X is RT now; Menu's TAB is J and its
    // SELECT is D-pad up; the bar's steps grew their D-pad halves.
    CHECK(loaded.bound(Action::Attack, Key::MouseLeft));
    CHECK(loaded.bound(Action::Attack, Key::PadRightTrigger));
    CHECK_FALSE(loaded.bound(Action::Attack, Key::PadWest));
    CHECK(loaded.bound(Action::Menu, Key::J));
    CHECK(loaded.bound(Action::Menu, Key::PadUp));
    CHECK_FALSE(loaded.bound(Action::Menu, Key::Tab));
    CHECK_FALSE(loaded.bound(Action::Menu, Key::PadBack));
    CHECK(loaded.bound(Action::QuickNext, Key::WheelDown));
    CHECK(loaded.bound(Action::QuickNext, Key::PadRight));
    CHECK(loaded.bound(Action::QuickPrev, Key::PadLeft));

    // THE ACTIONS THE FILE HAS NEVER HEARD OF ARE ON THEIR SHIPPED DEFAULTS,
    // reachable -- and the retired lines freed exactly the keys they hold:
    // RB (page_next's) for Cast, SELECT (menu's old) for Wait, Q and R3 for
    // nobody.
    CHECK(loaded.bound(Action::Cast, Key::C));
    CHECK(loaded.bound(Action::Cast, Key::PadRightBumper));
    CHECK(loaded.bound(Action::Block, Key::MouseRight));
    CHECK(loaded.bound(Action::Block, Key::PadLeftTrigger));
    CHECK(loaded.bound(Action::Map, Key::M));
    CHECK(loaded.bound(Action::Wait, Key::T));
    CHECK(loaded.bound(Action::Wait, Key::PadBack));
    CHECK(loaded.actionFor(Key::PadBack) == Action::Wait);
    CHECK(loaded.actionFor(Key::Q) == Action::Count);
    CHECK(loaded.actionFor(Key::PadRightStick) == Action::Count);
    CHECK(loaded.actionFor(Key::PadLeftBumper) == Action::Count);

    // Every one of the nine ends this load with a live key on BOTH device
    // families -- the whole backward-compat claim for a file this old.
    for (const Action action : {Action::Attack, Action::Block, Action::Cast, Action::Interact,
                                 Action::Crouch, Action::Vertical, Action::Sprint,
                                 Action::Menu, Action::Pause}) {
        const std::size_t index = static_cast<std::size_t>(action);
        const Key first = loaded.primary[index];
        const Key second = loaded.secondary[index];
        INFO("action ", actionKey(action), " primary=", keyName(first),
             " secondary=", keyName(second));
        CHECK(((first != Key::None && keyIsPad(first)) ||
               (second != Key::None && keyIsPad(second))));
        CHECK(((first != Key::None && !keyIsPad(first)) ||
               (second != Key::None && !keyIsPad(second))));
    }
    // And the file round-trips into the NEW format cleanly: saving what was
    // loaded writes a file this build reads back identically.
    CHECK(ControlSettings::fromText(loaded.toText()).toText() == loaded.toText());
}

TEST_CASE("nine and the sticks migration: the last shipped default file (thirteen verbs, "
          "map on SELECT, F1/F2) lands on the nine") {
    // THE SECOND REAL FILE ON THIS MACHINE: the build one before this one,
    // every action spelled out, nothing customised. After the break it must
    // read as a fresh default table, which is what a player who never
    // touched their controls expects to find.
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
        "bind vertical SPACE PAD_Y\n"
        "bind sprint LSHIFT PAD_LS\n"
        "bind menu J PAD_UP\n"
        "bind page_prev LBRACKET PAD_LB\n"
        "bind page_next RBRACKET PAD_RB\n"
        "bind pause ESC PAD_START\n"
        "bind quick_wheel Q PAD_RS\n"
        "bind quick_1 1\n"
        "bind quick_2 2\n"
        "bind quick_3 3\n"
        "bind quick_4 4\n"
        "bind quick_5 5\n"
        "bind quick_6 6\n"
        "bind quick_7 7\n"
        "bind quick_8 8\n"
        "bind quick_9 9\n"
        "bind quick_0 0\n"
        "bind quick_next WHEELDOWN\n"
        "bind quick_prev WHEELUP\n"
        "bind screenshot F12\n"
        "bind cast C PAD_RT\n"
        "bind block MOUSE2 PAD_LT\n"
        "bind map M PAD_BACK\n"
        "bind keys_page F1\n"
        "bind options_page F2\n"
        "set sensitivity 14\n"
        "set invert_y 0\n"
        "set fov 90\n"
        "set pad_deadzone 18\n"
        "set pad_saturation 95\n"
        "set pad_look 40000\n"
        "set pad_trigger_deadzone 12\n");
    // Byte-for-byte the shipped table: every old default became the new one
    // and nothing else was in the file.
    CHECK(loaded.toText() == ControlSettings::defaults().toText());
    CHECK(loaded.bound(Action::Attack, Key::PadRightTrigger));
    CHECK(loaded.bound(Action::Cast, Key::PadRightBumper));
    CHECK(loaded.bound(Action::Map, Key::M));
    CHECK(loaded.secondary[static_cast<std::size_t>(Action::Map)] == Key::None);
    CHECK(loaded.actionFor(Key::PadBack) == Action::Wait);
    CHECK(loaded.actionFor(Key::F1) == Action::Count);
    CHECK(loaded.actionFor(Key::F2) == Action::Count);
}

TEST_CASE("nine and the sticks migration: an old file's own choices stand, per slot") {
    // THE PER-SLOT RULE. A player who moved only the KEYBOARD half of Attack
    // (F for the fist, X untouched) keeps F and still gets the pad half the
    // break moved -- the old PAD_X was never their choice. A player who put
    // the map on N and left SELECT where it shipped keeps N, and the SELECT
    // half -- the old default carried forward, per slot -- goes where every
    // other pad's SELECT went: WAIT. The map is a page of NOTES on that pad,
    // same as on a fresh one. A cast bound to a bumper of their own choosing
    // stands where they put it, and RB is nobody's.
    const ControlSettings loaded = ControlSettings::fromText(
        "bind quick_wheel Q PAD_RS\n"
        "bind attack F PAD_X\n"
        "bind map N PAD_BACK\n"
        "bind cast C PAD_LB\n");
    CHECK(loaded.bound(Action::Attack, Key::F));
    CHECK(loaded.bound(Action::Attack, Key::PadRightTrigger));
    CHECK_FALSE(loaded.bound(Action::Attack, Key::PadWest));
    CHECK(loaded.bound(Action::Map, Key::N));
    CHECK_FALSE(loaded.bound(Action::Map, Key::PadBack));
    CHECK(loaded.actionFor(Key::N) == Action::Map);
    CHECK(loaded.bound(Action::Wait, Key::T));
    CHECK(loaded.bound(Action::Wait, Key::PadBack));
    CHECK(loaded.actionFor(Key::PadBack) == Action::Wait);
    CHECK(loaded.bound(Action::Cast, Key::PadLeftBumper));
    CHECK_FALSE(loaded.bound(Action::Cast, Key::PadRightBumper));
    CHECK(loaded.actionFor(Key::PadRightBumper) == Action::Count);
}

TEST_CASE("nine and the sticks migration: a file naming no retired verb is read as written") {
    // NO GUESSING WITHOUT THE SIGNAL. A hand-trimmed file that never names a
    // retired verb could be from either build, so nothing in it is treated
    // as an old default: "bind attack MOUSE1 PAD_X" is a player who WANTS X,
    // and gets X, with RT left free.
    const ControlSettings loaded = ControlSettings::fromText("bind attack MOUSE1 PAD_X\n");
    CHECK(loaded.bound(Action::Attack, Key::PadWest));
    CHECK_FALSE(loaded.bound(Action::Attack, Key::PadRightTrigger));
    CHECK(loaded.actionFor(Key::PadRightTrigger) == Action::Count);
    // And the same shape WITH the signal is the old default carried forward.
    const ControlSettings old =
        ControlSettings::fromText("bind keys_page F1\nbind attack MOUSE1 PAD_X\n");
    CHECK(old.bound(Action::Attack, Key::PadRightTrigger));
    CHECK_FALSE(old.bound(Action::Attack, Key::PadWest));
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
    CHECK(promptLabel(keys, Action::Pause, InputDevice::KeyboardMouse) == "ESC");
    CHECK(promptLabel(keys, Action::Pause, InputDevice::Pad) == "START");

    // NINE AND THE STICKS: the hands. RT swings, LT guards, RB casts -- and
    // the mouse's two buttons and C on a keyboard.
    CHECK(promptLabel(keys, Action::Attack, InputDevice::KeyboardMouse) == "MOUSE1");
    CHECK(promptLabel(keys, Action::Attack, InputDevice::Pad) == "RT");
    CHECK(promptLabel(keys, Action::Block, InputDevice::KeyboardMouse) == "MOUSE2");
    CHECK(promptLabel(keys, Action::Block, InputDevice::Pad) == "LT");
    CHECK(promptLabel(keys, Action::Cast, InputDevice::KeyboardMouse) == "C");
    CHECK(promptLabel(keys, Action::Cast, InputDevice::Pad) == "RB");
    // WAIT on both hands, and the bar's steps on the pad's own cross.
    CHECK(promptLabel(keys, Action::Wait, InputDevice::KeyboardMouse) == "T");
    CHECK(promptLabel(keys, Action::Wait, InputDevice::Pad) == "SELECT");
    CHECK(promptLabel(keys, Action::QuickNext, InputDevice::Pad) == "\x06\x05");
    CHECK(promptLabel(keys, Action::QuickPrev, InputDevice::Pad) == "\x06\x04");
    // The map has no pad half, so the pad's ask falls back to the keyboard's
    // M -- which is why no pad surface prints it: the ward map's own band
    // names B to close and the bumpers to page (see the live-session case).
    CHECK(promptLabel(keys, Action::Map, InputDevice::Pad) == "M");

    // THE PAGE GRAMMAR, said once: the page step and the sub-tab step, the
    // second commit, in both vocabularies.
    CHECK(promptPageKeys(InputDevice::KeyboardMouse) == "< >");
    CHECK(promptPageKeys(InputDevice::Pad) == "LB RB");
    CHECK(promptTabKeys(InputDevice::KeyboardMouse) == "TAB");
    CHECK(promptTabKeys(InputDevice::Pad) == "LT RT");
    CHECK(promptAltCommitKey(InputDevice::KeyboardMouse) == "T");
    CHECK(promptAltCommitKey(InputDevice::Pad) == "X");
}

TEST_CASE("nine and the sticks: the page step and the sub-tab step are raw page grammar") {
    // LB/RB and `[` `]` turn the PAGE; TAB and LT/RT step the SUB-TABS. Read
    // raw by the router ahead of any binding, which is what lets RB be CAST
    // and the triggers be SWING and GUARD in the world and still be
    // Oblivion's tab keys the moment a page is up.
    CHECK(pageStep(Key::PadLeftBumper) == -1);
    CHECK(pageStep(Key::PadRightBumper) == 1);
    CHECK(pageStep(Key::LeftBracket) == -1);
    CHECK(pageStep(Key::RightBracket) == 1);
    CHECK(tabStep(Key::PadLeftTrigger) == -1);
    CHECK(tabStep(Key::PadRightTrigger) == 1);
    CHECK(tabStep(Key::Tab) == 1);
    // Nothing else is either: the D-pad is list movement, the face buttons
    // keep their bindings, and the two steps never overlap.
    for (const Key key : {Key::PadSouth, Key::PadEast, Key::PadWest, Key::PadNorth, Key::PadUp,
                          Key::PadDown, Key::PadLeft, Key::PadRight, Key::PadStart, Key::PadBack,
                          Key::Escape, Key::Enter, Key::E, Key::T, Key::Q, Key::None}) {
        INFO("key ", keyName(key));
        CHECK(pageStep(key) == 0);
        CHECK(tabStep(key) == 0);
    }
    for (int k = 1; k < static_cast<int>(Key::Count); ++k) {
        const Key key = static_cast<Key>(k);
        CHECK_FALSE((pageStep(key) != 0 && tabStep(key) != 0));
    }
    // The second commit: T and X, nothing else.
    CHECK(isAltCommitKey(Key::T));
    CHECK(isAltCommitKey(Key::PadWest));
    CHECK_FALSE(isAltCommitKey(Key::PadSouth));
    CHECK_FALSE(isAltCommitKey(Key::Enter));
    // And the world binding of those keys is exactly what the grammar
    // outranks while a page is up: RB casts, the triggers are the hands, T
    // waits, X is nobody's.
    const ControlSettings keys = ControlSettings::defaults();
    CHECK(keys.actionFor(Key::PadRightBumper) == Action::Cast);
    CHECK(keys.actionFor(Key::PadRightTrigger) == Action::Attack);
    CHECK(keys.actionFor(Key::PadLeftTrigger) == Action::Block);
    CHECK(keys.actionFor(Key::T) == Action::Wait);
    CHECK(keys.actionFor(Key::PadWest) == Action::Count);
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
    // D-pad walks, the triggers step the views (Oblivion's sub-tabs), the
    // right stick zooms, the bumpers page to the map's neighbours in NOTES,
    // B closes, X travels, A commits -- and the keyboard's own literals.
    DistrictMapState padMap = session.districtMapState();
    // The movement keys are the keycap motifs now (UI-EA-SPEC sec. 5): the
    // d-pad cross sentinel on a pad, the four arrowheads on a keyboard.
    CHECK(padMap.navMoveKeys == std::string(kGlyphCross));
    CHECK(padMap.navTabKeys == "LT RT");
    CHECK(padMap.navZoomKeys == "RS");
    CHECK(padMap.navPageKeys == "LB RB");
    CHECK(padMap.navCloseKey == "B");
    CHECK(padMap.travelKey == "X");
    CHECK(padMap.commitKey == "A");
    session.noteInputKey(Key::M);
    DistrictMapState kbMap = session.districtMapState();
    CHECK(kbMap.navMoveKeys == std::string(kGlyphMoveKeys));
    CHECK(kbMap.navTabKeys == "TAB");
    CHECK(kbMap.navZoomKeys == "+ -");
    CHECK(kbMap.navPageKeys.empty());  // four slots, one row: the keyboard has M and `[` `]`
    CHECK(kbMap.navCloseKey == "M");
    CHECK(kbMap.travelKey == "T");
    CHECK(kbMap.commitKey == "\x01");

    // THE DIALOGUE WIDGET'S OWN KEYS ride the state the same way -- the
    // haggle's TAKE THEIR PRICE on the grammar's second commit, T / X.
    session.noteInputDevice(InputDevice::Pad);
    const DialogueViewState padView = session.dialogueView();
    CHECK(padView.confirmKey == "A");
    CHECK(padView.backKey == "B");
    CHECK(padView.takeKey == "X");
    CHECK(padView.letterDownLine.empty());
    session.noteInputKey(Key::Space);
    const DialogueViewState kbView = session.dialogueView();
    CHECK(kbView.confirmKey == "\x01");
    CHECK(kbView.backKey == "ESC");
    CHECK(kbView.takeKey == "T");
    CHECK(kbView.letterDownLine == "L PUTS IT DOWN");

    // THE CONTROLS PAGE'S OWN FOOT AND KEY COLUMN follow the hand too: a pad
    // player reads PAD_RT beside SWING and LT RT beside OPTIONS; a keyboard
    // player reads MOUSE1 and TAB. The lock's contextual rows name the live
    // keys the same way (Y tries the pin on a pad, SPACE on a keyboard).
    session.noteInputDevice(InputDevice::Pad);
    const KeysPageState padKeys = session.keysPageState();
    CHECK(padKeys.navTabKeys == "LT RT");
    CHECK(padKeys.navMoveKeys == std::string(kGlyphCross));
    bool padSwing = false;
    bool padPin = false;
    for (const KeysPageRow& row : padKeys.rows) {
        if (row.verb == "SWING") {
            padSwing = row.binding == "PAD_RT" && row.alternate == "MOUSE1";
        }
        if (row.verb == "TRY THE PIN") {
            padPin = row.binding == "Y";
        }
    }
    CHECK(padSwing);
    CHECK(padPin);
    session.noteInputKey(Key::W);
    const KeysPageState kbKeys = session.keysPageState();
    CHECK(kbKeys.navTabKeys == "TAB");
    bool kbSwing = false;
    for (const KeysPageRow& row : kbKeys.rows) {
        if (row.verb == "SWING") {
            kbSwing = row.binding == "MOUSE1" && row.alternate == "PAD_RT";
        }
    }
    CHECK(kbSwing);
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
    // NINE AND THE STICKS: the hand joins the band -- the owner's own
    // sentence is about LMB, so SWING is one of the first things read. The
    // keyboard keeps M MAP; a pad has no map button (the map is a page of
    // NOTES), so its band does not advertise one.
    CHECK(session.lastMessage() == "J NOTES  M MAP  E USE  MOUSE1 SWING");
    session.noteInputDevice(InputDevice::Pad);
    // HUD's dieted band (live verbs only, violation #11) carrying FLOW's
    // motif key: the pad's D-PAD UP is the cross+up sentinels.
    CHECK(session.lastMessage() == "\x06\x02 NOTES  A USE  RT SWING");
    session.noteInputKey(Key::A);
    CHECK(session.lastMessage() == "J NOTES  M MAP  E USE  MOUSE1 SWING");
}

// ---------------------------------------------------------------------------
// NINE AND THE STICKS: the ring, the wait toggle -- the two Session seams the
// controls break routes through.
// ---------------------------------------------------------------------------

TEST_CASE("the NOTES ring: the bumpers page from the last tile onto the ward map, "
          "the grimoire, and round") {
    // A PAD HAS NO MAP BUTTON AND NO GRIMOIRE BUTTON. This is how it reaches
    // both: NOTES, then RB. The ring is read off which surface is up, so no
    // new state is kept and the ward map's own toggle (which puts every
    // other overlay down) is what opens it.
    SessionConfig config;
    config.contentDir = content::contentDir();
    Session session(config);
    session.toggleMenu();
    REQUIRE(session.casebookOpen());
    REQUIRE(session.menuFocus() == kMenuFocusJournal);

    session.menuPageNext();  // the last tile -> the ward map
    CHECK(session.districtMapOpen());
    CHECK_FALSE(session.casebookOpen());
    CHECK_FALSE(session.grimoireOpen());

    session.menuPageNext();  // the ward map -> the grimoire
    CHECK(session.grimoireOpen());
    CHECK_FALSE(session.districtMapOpen());

    session.menuPageNext();  // the grimoire -> round to the first tile
    CHECK(session.casebookOpen());
    CHECK(session.menuFocus() == kMenuFocusCharacter);
    CHECK_FALSE(session.grimoireOpen());

    // And backward, the other way round the same ring.
    session.menuPagePrev();  // the first tile -> the grimoire
    CHECK(session.grimoireOpen());
    session.menuPagePrev();  // the grimoire -> the ward map
    CHECK(session.districtMapOpen());
    session.menuPagePrev();  // the ward map -> the last tile
    CHECK(session.casebookOpen());
    CHECK(session.menuFocus() == kMenuFocusJournal);

    // The key that opened NOTES still closes it from the tile it is on, and
    // with nothing open the ring does not open anything.
    session.toggleMenu();
    CHECK_FALSE(session.menuOpen());
    session.menuPageNext();
    CHECK_FALSE(session.menuOpen());
    CHECK_FALSE(session.districtMapOpen());
    CHECK_FALSE(session.grimoireOpen());
}

TEST_CASE("a rebind on the settings page lands in the slot of the key's own device") {
    // A pad player who moves SWING to X keeps MOUSE1; a keyboard player who
    // moves it to F keeps RT. The page's row reads the live hand's half too.
    SessionConfig config;
    config.contentDir = content::contentDir();
    Session session(config);
    session.toggleOptions();
    REQUIRE(session.optionsOpen());
    const int swingRow = Session::kSliderRows + static_cast<int>(Action::Attack);
    // Walk the cursor onto SWING and arm the rebind.
    while (session.optionCursor() < swingRow) {
        session.moveOptionCursor(1);
    }
    REQUIRE(session.optionCursor() == swingRow);
    session.noteInputDevice(InputDevice::Pad);
    session.chooseOption();
    REQUIRE(session.awaitingKey());
    session.bindAwaited(Key::PadWest);
    CHECK(session.controls().bound(Action::Attack, Key::MouseLeft));
    CHECK(session.controls().bound(Action::Attack, Key::PadWest));
    CHECK_FALSE(session.controls().bound(Action::Attack, Key::PadRightTrigger));
    CHECK(session.controls().actionFor(Key::PadRightTrigger) == Action::Count);
    bool padRow = false;
    for (const std::string& row : session.optionRows()) {
        if (row.rfind("SWING  ", 0) == 0) {
            padRow = row == "SWING  PAD_X";
        }
    }
    CHECK(padRow);

    session.noteInputKey(Key::W);
    session.chooseOption();
    REQUIRE(session.awaitingKey());
    session.bindAwaited(Key::F);
    CHECK(session.controls().bound(Action::Attack, Key::F));
    CHECK(session.controls().bound(Action::Attack, Key::PadWest));
    CHECK_FALSE(session.controls().bound(Action::Attack, Key::MouseLeft));
    bool kbRow = false;
    for (const std::string& row : session.optionRows()) {
        if (row.rfind("SWING  ", 0) == 0) {
            kbRow = row == "SWING  F";
        }
    }
    CHECK(kbRow);
}

TEST_CASE("WAIT is a toggle: T and SELECT open the hour page and close it again") {
    SessionConfig config;
    config.contentDir = content::contentDir();
    Session session(config);
    REQUIRE_FALSE(session.waitOpen());
    session.toggleWait();
    CHECK(session.waitOpen());
    CHECK_FALSE(session.waitSleeping());  // the pause row's plain WAIT, not a bed
    session.toggleWait();
    CHECK_FALSE(session.waitOpen());
    // Inert over a page that owns the keyboard the way every toggle is: the
    // pause menu is up, and WAIT does not open under it.
    session.togglePause();
    REQUIRE(session.pauseOpen());
    session.toggleWait();
    CHECK(session.waitOpen());  // the pause menu's own door -- opens over it, as the row does
    session.toggleWait();
    CHECK_FALSE(session.waitOpen());
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

TEST_CASE("nine and the sticks: the F-keys, the wheel and the page pair are gone from the table") {
    // What #85 and the UI-EA pass put in, the owner's ruling took out: F1/F2
    // (the pause menu's CONTROLS and SETTINGS rows are the door on both
    // devices), F3 (alt-tab gives the cursor back; every page brings it out
    // on its own), the QuickWheel (the bar steps on the wheel and the D-pad;
    // the Grimoire is a page of NOTES), and the page pair as ACTIONS (the
    // bumpers and brackets are raw page grammar now). None of them parses,
    // none of them binds, none of them prints.
    const ControlSettings keys = ControlSettings::defaults();
    for (const char* retired : {"keys_page", "options_page", "quick_wheel", "page_prev",
                                "page_next"}) {
        INFO("retired name ", retired);
        CHECK(actionFromKey(retired) == Action::Count);
    }
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const std::string_view label = actionLabel(static_cast<Action>(i));
        CHECK(label != "QUICK WHEEL");
        CHECK(label != "KEYS");
        CHECK(label != "OPTIONS");
        CHECK(label != "PAGE <");
        CHECK(label != "PAGE >");
    }
    CHECK(keys.actionFor(Key::F1) == Action::Count);
    CHECK(keys.actionFor(Key::F2) == Action::Count);
    CHECK(keys.actionFor(Key::F3) == Action::Count);
    CHECK(keys.actionFor(Key::Q) == Action::Count);
    CHECK(keys.actionFor(Key::PadRightStick) == Action::Count);
    // A file from the old build that binds any of them is dropped line by
    // line and nothing else in it is disturbed.
    const ControlSettings loaded = ControlSettings::fromText(
        "bind keys_page F5\nbind quick_wheel Q PAD_RS\nbind page_next K\nbind vertical H\n");
    CHECK(loaded.actionFor(Key::F5) == Action::Count);
    CHECK(loaded.actionFor(Key::Q) == Action::Count);
    CHECK(loaded.actionFor(Key::K) == Action::Count);
    CHECK(loaded.bound(Action::Vertical, Key::H));
}

// ---------------------------------------------------------------------------
// COMBAT: Attack became a HELD button (down-edge starts the sim's hold clock,
// release-edge resolves the swing, hard iff the hold reached the tap/hold
// boundary). Two things this migration rides on that this file owns: the ONE
// tap/hold number the whole game shares, and the Attack bindings surviving
// unchanged so the muscle memory the scheme was built on still holds.
// ---------------------------------------------------------------------------

TEST_CASE("the hard-swing hold clock and the client's tap/hold boundary are one number") {
    // main.cpp ties these with a file-scope static_assert -- the sim header
    // cannot include render, so the seam that includes both is where the two
    // meet. This case pins the same equality from the test side, so a change
    // to EITHER constant that drifts them apart is a red build here as well as
    // a compile error in the client: the hold-clock model measures a hard
    // swing at kHardSwingHoldSteps, and the client's HoldToggle calls anything
    // at or under kTapSteps a tap -- if those diverge, "held long enough to be
    // a hard swing" and "held long enough to not be a tap" stop being the same
    // instant and the swing tier the player feels no longer matches the guard.
    CHECK(static_cast<std::int64_t>(sim::kHardSwingHoldSteps) == HoldToggle::kTapSteps);
    // The value itself, pinned so a silent edit to either side is caught even
    // if the other were edited to match by accident.
    CHECK(HoldToggle::kTapSteps == 15);
}

TEST_CASE("SWING is the primary hand on both devices: MOUSE1 and the right trigger") {
    // The down-edge/release-edge model changed WHEN Attack resolves; nine and
    // the sticks changed only its PAD key -- from X to RT, the pad analogue
    // of the mouse's primary. MouseLeft is exactly where it has always been,
    // and the lockpick's forceLock resolves off these same two keys.
    const ControlSettings keys = ControlSettings::defaults();
    CHECK(keys.bound(Action::Attack, Key::MouseLeft));
    CHECK(keys.bound(Action::Attack, Key::PadRightTrigger));
    CHECK(keys.actionFor(Key::MouseLeft) == Action::Attack);
    CHECK(keys.actionFor(Key::PadRightTrigger) == Action::Attack);
    CHECK(keys.actionFor(Key::PadWest) == Action::Count);
}
