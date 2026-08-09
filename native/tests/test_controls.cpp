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
    CHECK(keys.bound(Action::Jump, Key::Space));
    CHECK(keys.bound(Action::Interact, Key::E));
    CHECK(keys.bound(Action::Journal, Key::Tab));
    CHECK(keys.bound(Action::Menu, Key::Escape));

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

    // SPACE IS JUMP AND NOT CLIMB, which is the single most important line in
    // this file. The S5 build put the mantle/leap verb on space; a player who
    // pressed it expecting a jump got a climb, and a player who walked at a
    // ledge expecting a climb got a wall. Both halves of that are fixed: space
    // jumps, walking into the ledge climbs it, and the explicit climb is still
    // bound for anyone who wants to line one up.
    CHECK(keys.actionFor(Key::Space) == Action::Jump);
    CHECK(keys.actionFor(Key::V) == Action::Traverse);
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

    keys.bind(Action::Jump, Key::W);
    CHECK(keys.bound(Action::Jump, Key::W));
    // Visibly taken off Forward -- not shared, and not silently ignored. A
    // rebinding screen that allows a collision produces a game where one of the
    // two verbs stops working and the player cannot find out which.
    CHECK_FALSE(keys.bound(Action::Forward, Key::W));
    CHECK(keys.actionFor(Key::W) == Action::Jump);
    // The arrow is still on Forward, so the player is not stranded.
    CHECK(keys.bound(Action::Forward, Key::Up));

    keys.bind(Action::Forward, Key::W);
    CHECK(keys.bound(Action::Forward, Key::W));
    CHECK_FALSE(keys.bound(Action::Jump, Key::W));
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
    mine.bind(Action::Jump, Key::MouseX1);
    mine.bind(Action::Crouch, Key::Z);
    mine.bind(Action::Forward, Key::PadUp, /*asSecondary=*/true);
    mine.mouse.sensitivity = 31;
    mine.mouse.invertY = true;
    mine.fovDegrees = 105;
    mine.pad.deadzonePercent = 9;

    const ControlSettings back = ControlSettings::fromText(mine.toText());
    CHECK(back.bound(Action::Jump, Key::MouseX1));
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
    const ControlSettings sparse = ControlSettings::fromText("bind jump MOUSE3\n");
    CHECK(sparse.bound(Action::Jump, Key::MouseMiddle));
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
    mine.bind(Action::Examine, Key::MouseMiddle);
    REQUIRE(saveControls(mine, file));
    REQUIRE(std::filesystem::exists(file));

    const ControlSettings reloaded = loadControls(file);
    CHECK(reloaded.mouse.sensitivity == 44);
    CHECK(reloaded.bound(Action::Examine, Key::MouseMiddle));

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
    previous.bind(Action::Jump, Key::K);
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
