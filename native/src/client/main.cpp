// Granadad: The Darkstreets — client entry point.
//
// Everything that draws lives in granadad-render, which knows nothing about
// SDL. This file is the window, the keyboard, the mouse and the fixed-timestep
// loop that turns wall-clock into a whole number of MOVEMENT STEPS — and
// nothing else. That split is what lets `--screenshot` work with no window at
// all, and it is why the docker gate can render a frame of the Docks on every
// build.
//
// 3D BUILD (2026-09-10). THE WINDOW IS RAYLIB'S NOW. granadad-render3d-rl
// owns the window, the GL context, the 3D pass and the composite; SDL stays
// for exactly two things it is still best at -- the audio device and the
// gamepad -- and is initialised with SDL_INIT_GAMEPAD alone, never VIDEO.
// The keyboard and the mouse come out of raylib in a neutral vocabulary (HID
// usage ids == SDL scancodes) and are pushed onto SDL's own event queue by
// VideoBridge below, so the five thousand lines of input routing under it
// -- the scancode table, route_menu_key, pressed()/released(), the pad
// parity, the virtual pad harness -- keep reading the SDL_Event they always
// read. See VideoBridge's header for what that buys and what it costs.
//
// Floats are legal in this file and its neighbours under src/client. They are
// not legal anywhere under src/sim.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "granadad/audio/backend_sdl.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/compound.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/case_watch.hpp"
#include "granadad/render/casebook_page.hpp"
#include "granadad/render/controls.hpp"
#include "granadad/render/creation.hpp"
#include "granadad/render/demo.hpp"
#include "granadad/render/dialogue_view.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/keys_page.hpp"
#include "granadad/render/lighting.hpp"
#include "granadad/render/map_view.hpp"
#include "granadad/render/menu_view.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/step_pump.hpp"
#include "granadad/render/world_renderer.hpp"
#include "granadad/render3d/backend.hpp"
#include "granadad/render3d/scene.hpp"
#include "granadad/render3d/actor_instances.hpp"
#include "granadad/render3d/viewmodel.hpp"
#include "granadad/render3d/world_scene.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/build_info.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/fixed.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tavern.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace {

namespace render = granadad::render;
namespace render3d = granadad::render3d;
namespace sim = granadad::sim;

/// 3D BUILD. Whether a shift key is held, kept by VideoBridge off the edges
/// raylib reports. It replaces SDL_GetModState(), which only ever knew the
/// answer while SDL owned the keyboard.
bool g_video_shift_held = false;

// COMBAT. The hard-swing hold threshold (sim, deterministic movement steps) and
// the tap/hold boundary the client's HoldToggle already uses are ONE number by
// design -- the whole game reads a single tap/hold line. The sim header cannot
// include render, so the two are tied here, at the one seam that includes both.
static_assert(sim::kHardSwingHoldSteps == render::HoldToggle::kTapSteps,
              "the hard-swing hold clock and the client tap/hold boundary must "
              "be the same number of steps");

void print_build_banner() {
    const sim::BuildInfo info = sim::build_info();
    std::printf("Granadad: The Darkstreets %.*s (%.*s) [%.*s, %.*s]\n",
                static_cast<int>(info.version.size()), info.version.data(),
                static_cast<int>(info.revision.size()), info.revision.data(),
                static_cast<int>(info.target.size()), info.target.data(),
                static_cast<int>(info.compiler.size()), info.compiler.data());
}

/// S7. The ward's own economy, printed. Two years of the compounds by default.
///
/// NO WINDOW, and it needs none: the compounds are simulation and the report is
/// text. It is the same call the build gate makes, so what a player reads here
/// and what the gate enforces cannot drift apart.
int run_ward(std::int64_t days) {
    const sim::WardSoakResult soak =
        sim::runWardSoak(days, granadad::content::contentDir(), 0x4752414E41444144ull);
    std::fputs(soak.report.c_str(), stdout);
    if (!soak.passed) {
        std::printf("\n  OUT OF BALANCE: %s\n", soak.problem.c_str());
        return 1;
    }
    std::printf("\n  the ward fed itself for %lld days.\n", static_cast<long long>(soak.days));
    return 0;
}

/// #80. THE DISTRICT'S ROLL, BROKEN OUT, AND WHO SLEEPS ON A ROOF.
///
/// NO WINDOW. The two things this round is judged on are population facts, not
/// pixels: how many bodies are homed on a deck (and of which trades), and
/// whether the mouse count actually moves when something eats one. A frame can
/// show you a figure on a roof; it cannot tell you that thirteen of them live
/// there and that the Skyrunners are among them.
///
/// `hours` is how long to run before printing, so the same command answers both
/// questions -- 0 for the bake, and long enough for a cat to get hungry for the
/// food chain.
int run_people(std::int32_t startHour, std::int64_t hours) {
    granadad::content::World world =
        granadad::content::loadWorldFile(granadad::content::bakedMap(sim::docks::kWorldName));
    const sim::TileQuery tiles(world);
    sim::PhasedEngine engine(0x4752414E41444144ull, world);
    auto owned = std::make_unique<sim::WardPopulation>(
        tiles, sim::hourOfDay(startHour), 0x4752414E41444144ull,
        granadad::content::contentDir());
    const sim::WardPopulation* people = owned.get();
    engine.register_system(std::move(owned));
    engine.boot();
    for (std::int64_t t = 0; t < hours * 3600; ++t) {
        engine.tick();
    }

    const sim::WardCensus roll = people->census();
    std::printf("the ward at %02d:00 after %lld hour(s)\n\n", people->secondOfDay() / 3600,
                static_cast<long long>(hours));
    std::printf("  roll %d  alive %d  people %d  beasts %d  starved %d\n", roll.total, roll.alive,
                roll.people, roll.beasts, roll.starved);
    std::printf("  at a post %d   at home %d   hungry %d\n\n", roll.atPost, roll.atHome,
                roll.hungry);

    std::printf("  %-12s %6s %6s\n", "trade", "roll", "roofed");
    for (std::size_t i = 0; i < sim::kWardTypeCount; ++i) {
        const sim::WardType type = static_cast<sim::WardType>(i);
        const std::string_view name = sim::wardTypeName(type);
        std::printf("  %-12.*s %6d %6d%s\n", static_cast<int>(name.size()), name.data(),
                    roll.byType[i], roll.roofHomedByType[i],
                    sim::wardTypeClimbs(type) ? "   (climbs)" : "");
    }
    std::printf("\n  ON THE ROOFS: %d beds on a deck, %d bodies standing on one right now.\n",
                roll.roofHomed, roll.onRoofNow);
    std::printf("  roof beds the bake refused as one-way: %d\n", people->roofHomesRefused());
    // AND WHERE THEY ARE, so a capture can be aimed at a hut rather than at a
    // guess. One line per deck cell, first occupant only.
    std::int32_t printed = 0;
    std::int32_t lastX = -1;
    std::int32_t lastY = -1;
    for (const sim::WardActor& actor : people->actors()) {
        if (!actor.homeOnTheRoof || printed >= 16) {
            continue;
        }
        if (actor.homeX == lastX && actor.homeY == lastY) {
            continue;
        }
        lastX = actor.homeX;
        lastY = actor.homeY;
        const std::string_view name = sim::wardTypeName(actor.type);
        std::printf("    bed (%3d,%3d,z%d)  %-10.*s  now at (%3d,%3d,z%d)\n", actor.homeX,
                    actor.homeY, actor.homeBand, static_cast<int>(name.size()), name.data(),
                    actor.x, actor.y, actor.band);
        ++printed;
    }
    std::printf("\n  THE FOOD CHAIN: %d of %d mice on the board, %lld caught, %lld chases "
                "abandoned as futile.\n",
                roll.preyUp, roll.prey, static_cast<long long>(people->catches()),
                static_cast<long long>(people->futileChases()));
    std::printf("\n  %s\n", people->reportLine().c_str());
    return 0;
}

// Exercises the deterministic primitives without touching SDL, so it can be run
// on a machine with no display. `granadad.exe --selftest` is the cheapest
// possible "did the cross-compile actually produce a working binary" check.
int run_selftest() {
    struct Case {
        const char* name;
        bool ok;
    };
    // Typed bounds, never the literals: `-2147483648` parses as unary minus on
    // a value too big for an int, so it is a long (or long long) and picks the
    // 64-bit wrap_* overload -- or, on a platform where long is 32 bits, is an
    // ambiguous call. See the same note in src/sim/fixed.cpp.
    constexpr std::int32_t kI32Min = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t kI32Max = std::numeric_limits<std::int32_t>::max();

    const Case cases[] = {
        {"wrap_add overflow", sim::wrap_add(kI32Max, 1) == kI32Min},
        {"wrap_sub underflow", sim::wrap_sub(kI32Min, 1) == kI32Max},
        {"wrap_abs INT_MIN", sim::wrap_abs(kI32Min) == kI32Min},
        {"q16 identity", sim::q16_mul(sim::Q16_ONE, sim::Q16_ONE) == sim::Q16_ONE},
        {"floor_div negative", sim::floor_div(-1, 32) == -1},
        {"floor_mod negative", sim::floor_mod(-1, 32) == 31},
        {"north is -Y", sim::forward_y_q16(sim::kFacingNorth) == -sim::kTrigOne},
        {"east is +X", sim::forward_x_q16(sim::kFacingEast) == sim::kTrigOne},
        {"Q8 floors", sim::q8_tile(-1) == -1},
    };

    int failures = 0;
    for (const Case& c : cases) {
        if (!c.ok) {
            std::printf("  FAIL %s\n", c.name);
            ++failures;
        }
    }
    std::printf("selftest: %d case(s), %d failure(s)\n",
                static_cast<int>(sizeof(cases) / sizeof(cases[0])), failures);
    return failures == 0 ? 0 : 1;
}

// A SECOND, DELIBERATELY SEPARATE "no window" check -- `run_selftest()` above
// stays SDL-free on purpose (its own header says so: it has to run on a
// machine with no display), so a gamepad probe does not belong inside it.
// This one DOES touch SDL, because what it proves is what SDL's own gamepad
// subsystem sees, not sim-core's arithmetic -- but it still opens no window
// and no renderer, the same way `--ward`/`--people` open none: gamepad
// enumeration on Windows (XInput/DirectInput/HID) needs no surface at all.
//
// WHY THIS EXISTS. Before it, the only place this build ever reported a
// gamepad was `run_client()`'s own "connected" line -- reachable only by
// actually opening the real windowed game, which is exactly the kind of
// check `--selftest` exists to avoid needing. This gives a scriptable,
// CI-friendly answer to "does this binary, on this machine, right now, see
// any gamepad SDL can enumerate" -- 0 included, so a verifier can tell
// "checked, found none" apart from "never checked" without a human pressing
// a physical button. It is NOT a substitute for the real windowed client's
// own hot-plug handling (see the SDL_EVENT_GAMEPAD_ADDED/REMOVED cases in
// run_client()) and it does not claim Steam Input hardware-level proof --
// only that SDL's own enumeration ran and reports what it reports.
int run_gamepad_selftest() {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::printf("gamepad-selftest: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    std::printf("gamepad-selftest: gamepads detected=%d\n", count);
    if (ids != nullptr) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetGamepadNameForID(ids[i]);
            std::printf("gamepad-selftest: [%d] %s\n", i, name != nullptr ? name : "(unnamed)");
        }
        SDL_free(ids);
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    SDL_Quit();
    // ALWAYS ZERO. Finding no gamepad is not a failure of this binary -- it is
    // a true fact about the machine it ran on, exactly as legitimate an
    // answer as finding one. The only failure mode here is SDL itself
    // refusing to initialise, handled above.
    return 0;
}

// THE AUDIO WIRING PASS'S OWN SCRIPTABLE PROOF, run_gamepad_selftest's exact
// shape and for the exact reason it exists: before this, the only place the
// SDL audio backend ever opened was inside the real windowed game, which a
// headless verifier cannot run and nobody can HEAR from a script anyway. This
// opens the same engine run_client() opens (createSdlAudioEngine: real sound
// bank, real device attempt, silence-degrade contract), drives the same calls
// the session hooks make, and prints what happened -- so "the backend opens
// and the hooks have something real on the other end" is a stdout fact, not a
// claim about ears.
//
// ALWAYS ZERO for "no device": a machine without a sound card is a true fact
// about the machine, exactly as the gamepad probe's own note says of finding
// no pad. The only failure here is the engine failing to construct at all.
int run_audio_selftest() {
    const std::unique_ptr<granadad::audio::AudioEngine> audio =
        granadad::audio::createSdlAudioEngine();
    if (audio == nullptr) {
        std::printf("audio-selftest: engine allocation FAILED\n");
        return 1;
    }
    std::printf("audio-selftest: device %s\n",
                audio->deviceOpen() ? "open (48kHz float stereo)"
                                    : "absent -- engine in silent no-op mode");
    // The same calls the wired session makes, in miniature: a bed, a clock, a
    // footstep on the material-0 surface, one UI one-shot, and a second of
    // update()s for the crossfade and cadence clocks to move through.
    audio->setTimeOfDay(12 * 3600);
    audio->startBed(granadad::audio::BedId::Harbour);
    const bool stepped = audio->footstep(0, false, 0);
    audio->playOneShot(granadad::audio::SoundId::UiConfirm);
    for (int i = 0; i < 60; ++i) {
        audio->update(1.0F / 60.0F);
    }
    std::printf("audio-selftest: bed=%d footstep_sounded=%d\n",
                static_cast<int>(audio->currentBed()), stepped ? 1 : 0);
    return 0;
}

// ---------------------------------------------------------------------------
// command line
// ---------------------------------------------------------------------------

struct Options {
    render::SmokeRunConfig smoke;
    bool wantsSmoke = false;
    int windowScale = 2;
    /// 3D BUILD. THE DEFAULT NOW: the world is drawn by the raylib backend
    /// -- the Docks as chunk meshes (render3d/world_scene.hpp) -- and the
    /// software frame is drawn WITHOUT its world pass, as a transparent
    /// overlay over it. `--2d` turns it off: the software world rides inside
    /// the overlay and the picture is the one this build showed before the
    /// 3D programme; the window, the composite and the capture go through
    /// raylib either way. `--3d` is still accepted and means the default.
    bool video3d = true;
    /// Mouse look sensitivity, BAM per mouse count. Only used when NAMED: the
    /// settings file is the source of truth, and a command line that always
    /// overrode it would silently undo the options page on every launch.
    int sensitivity = 14;
    bool sensitivityGiven = false;
    bool invertY = false;
    /// Where the bindings live. Overridable so a capture, a case or a second
    /// player on the same machine can have their own.
    std::filesystem::path controlsFile;
    /// #80: capture the origin-select/customize flow with no window, the
    /// same shape --pause and --character already have. Off by default;
    /// creationStep is "origin" or "customize".
    bool wantsCreation = false;
    std::string creationStep = "origin";
    /// THE PARITY PASS, VERIFICATION ONLY. A comma-separated pad script -- see
    /// PadScript below. Empty means no virtual pad is attached at all and the
    /// windowed loop is byte-for-byte the shipped one.
    std::string padScript;
    /// The same, for the CHARACTER SCREEN -- the window a windowed launch opens
    /// first. Separate because they are separate windows with separate SDL
    /// lifetimes; see PadDriver.
    std::string padCreation;
    /// Where a pad script's `shot:NAME` beats land.
    std::filesystem::path padShotDir;
    /// THE DEMO. Plays the curated route in render/demo.hpp, unattended, at a
    /// fixed cadence, and closes itself when the route ends. `demoSection`
    /// empty starts at the top; naming one skips straight to it, which is how
    /// the route was iterated on without watching the whole thing each time.
    bool demo = false;
    std::string demoSection;
    /// Where `--demo-capture=DIR` writes the route's own frames. Empty takes
    /// no pictures and is the ordinary way to watch it.
    std::filesystem::path demoShotDir;
    /// CASE WATCH. Plays THE QUIET TENANT windowed at the demo's cadence --
    /// the recorded --case drive, replayed one step per rendered frame. See
    /// render/case_watch.hpp. Plain --case is untouched: this flag skips the
    /// creation window and the smoke path both, and runs the client loop with
    /// a CaseWatchDirector where --demo would put its DemoDirector.
    bool caseWatch = false;
    /// Where `--case-watch-capture=DIR` writes one PNG per landed beat.
    std::filesystem::path caseWatchShotDir;
    /// VERIFICATION ONLY. Where `--framedump=DIR` writes every presented frame
    /// of a --demo/--case-watch run, numbered f<NNNNN>.png by the frame loop's
    /// own counter. Empty dumps nothing and is the ordinary run. This exists
    /// because a transition seam -- a cut, a fade, a plate easing in -- is a
    /// claim about CONSECUTIVE frames, and the 40 ms cadence of a pad script's
    /// shot: beats can step right over a one-frame defect.
    std::filesystem::path frameDumpDir;
};

// ---------------------------------------------------------------------------
// the virtual pad
// ---------------------------------------------------------------------------
//
// WHY THIS EXISTS, AND WHAT IT IS NOT.
//
// scripts/drive-windowed.ps1 proves the keyboard and the mouse by pressing real
// keys through Win32 SendInput -- real scancodes, real relative motion, the same
// path a physical device goes down. There is no SendInput for a gamepad. A
// verifier with no controller plugged in therefore had NO way to photograph the
// pad driving anything, which is precisely how "the D-pad is bound to nothing"
// survived into a shipping build with a nav band on screen advertising it.
//
// SDL's own virtual joystick closes that. SDL_AttachVirtualJoystick creates a
// device SDL itself reports through SDL_EVENT_GAMEPAD_ADDED, opens as a real
// SDL_Gamepad, delivers as real SDL_EVENT_GAMEPAD_BUTTON_DOWN events, and
// answers SDL_GetGamepadAxis for. So the frame loop below is not stubbed, not
// branched and does not know this is happening: `pad`, `route_menu_key`,
// `key_of_pad_button`, the stick latch and the trigger edges are the SHIPPED
// code paths, exercised whole.
//
// WHAT IT DOES NOT PROVE: that a particular physical controller enumerates, or
// that Steam Input routes to it. Those need a hand on a real pad. This proves
// everything between SDL's gamepad layer and the screen, which is where all of
// this pass's defects were.

struct PadBeat {
    enum class Kind : std::uint8_t { Button, Stick, Wait, Shot } kind = Kind::Wait;
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
    /// Stick beats: which axis, and which way past the deadzone.
    SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_LEFTX;
    Sint16 value = 0;
    int millis = 0;
    std::string name;
};

/// Parses "back,wait:400,down,down,shot:pad-map,a" into beats. Unknown words are
/// reported and refuse the run rather than being skipped -- a capture that
/// quietly dropped the press it was taken to prove is worse than no capture.
[[nodiscard]] bool parse_pad_script(const std::string& text, std::vector<PadBeat>& out) {
    struct Named {
        const char* word;
        SDL_GamepadButton button;
    };
    static constexpr Named kButtons[] = {
        {"a", SDL_GAMEPAD_BUTTON_SOUTH},          {"b", SDL_GAMEPAD_BUTTON_EAST},
        {"x", SDL_GAMEPAD_BUTTON_WEST},           {"y", SDL_GAMEPAD_BUTTON_NORTH},
        {"back", SDL_GAMEPAD_BUTTON_BACK},        {"start", SDL_GAMEPAD_BUTTON_START},
        {"lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {"up", SDL_GAMEPAD_BUTTON_DPAD_UP},       {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},   {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    };
    struct NamedStick {
        const char* word;
        SDL_GamepadAxis axis;
        Sint16 value;
    };
    // PAST kNavStickOn (18000) BY A MARGIN, so the latch in the frame loop is
    // being crossed and not grazed.
    static constexpr NamedStick kSticks[] = {
        {"lsup", SDL_GAMEPAD_AXIS_LEFTY, -28000},
        {"lsdown", SDL_GAMEPAD_AXIS_LEFTY, 28000},
        {"lsleft", SDL_GAMEPAD_AXIS_LEFTX, -28000},
        {"lsright", SDL_GAMEPAD_AXIS_LEFTX, 28000},
        // THE TRIGGERS ARE AXES, NOT BUTTONS, and that is SDL's rule rather
        // than this harness's: S13's own note in the frame loop is that
        // SDL reports LT/RT through SDL_GAMEPAD_AXIS_*_TRIGGER and never as a
        // button, which is why the game synthesizes their press edges by
        // polling. So they are driven here the way the game reads them --
        // well past the shipped triggerDeadzonePercent, and back to rest on
        // the release beat, which is the release edge.
        {"lt", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 28000},
        {"rt", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 28000},
    };

    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t comma = text.find(',', at);
        const std::string word =
            text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        at = comma == std::string::npos ? text.size() + 1 : comma + 1;
        if (word.empty()) {
            continue;
        }
        PadBeat beat;
        if (word.rfind("wait:", 0) == 0) {
            beat.kind = PadBeat::Kind::Wait;
            beat.millis = std::atoi(word.c_str() + 5);
            out.push_back(beat);
            continue;
        }
        if (word.rfind("shot:", 0) == 0) {
            beat.kind = PadBeat::Kind::Shot;
            beat.name = word.substr(5);
            out.push_back(beat);
            continue;
        }
        bool found = false;
        for (const Named& row : kButtons) {
            if (word == row.word) {
                beat.kind = PadBeat::Kind::Button;
                beat.button = row.button;
                out.push_back(beat);
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }
        for (const NamedStick& row : kSticks) {
            if (word == row.word) {
                beat.kind = PadBeat::Kind::Stick;
                beat.axis = row.axis;
                beat.value = row.value;
                out.push_back(beat);
                found = true;
                break;
            }
        }
        if (!found) {
            std::printf("granadad: --padscript: unknown beat '%s'\n", word.c_str());
            (void)std::fflush(stdout);
            return false;
        }
    }
    return true;
}

/// The virtual device and the playhead over a script. ONE OF THESE PER WINDOW,
/// because a windowed launch opens two -- the character screen and then the
/// world -- and each runs its own SDL_Init/SDL_Quit pair, which a virtual
/// joystick cannot survive. Sharing the struct is what keeps the two capture
/// paths from being two different harnesses that could disagree.
struct PadDriver {
    std::vector<PadBeat> beats;
    SDL_Joystick* device = nullptr;
    SDL_JoystickID id = 0;
    std::filesystem::path shotDir;
    std::size_t at = 0;
    Uint64 until = 0;
    bool held = false;

    [[nodiscard]] bool live() const { return device != nullptr && !beats.empty(); }

    /// Attaches and returns true when `script` is non-empty and parsed.
    bool attach(const std::string& script, std::filesystem::path shots) {
        shotDir = std::move(shots);
        if (script.empty() || !parse_pad_script(script, beats)) {
            beats.clear();
            return false;
        }
        SDL_VirtualJoystickDesc desc{};
        desc.version = static_cast<Uint32>(sizeof(desc));
        desc.type = static_cast<Uint16>(SDL_JOYSTICK_TYPE_GAMEPAD);
        desc.naxes = static_cast<Uint16>(SDL_GAMEPAD_AXIS_COUNT);
        desc.nbuttons = static_cast<Uint16>(SDL_GAMEPAD_BUTTON_COUNT);
        // WITHOUT THE MASKS SDL BUILDS A MAPPING WITH NOTHING ON IT. They are
        // what tells the auto-generated gamepad mapping which of the declared
        // axes and buttons actually exist, and an empty mask is a pad that
        // enumerates, opens, and reports every button unpressed forever --
        // which would look exactly like the bug being tested for.
        desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1U;
        desc.axis_mask = (1U << SDL_GAMEPAD_AXIS_COUNT) - 1U;
        desc.name = "Granadad Verification Pad";
        id = SDL_AttachVirtualJoystick(&desc);
        if (id == 0) {
            std::printf("granadad: --padscript: SDL_AttachVirtualJoystick failed: %s\n",
                        SDL_GetError());
            beats.clear();
            return false;
        }
        device = SDL_OpenJoystick(id);
        std::printf("granadad: --padscript: virtual pad attached, %d beats\n",
                    static_cast<int>(beats.size()));
        // FLUSHED, EVERY LINE. A harness whose progress is invisible until it
        // exits is a harness you cannot debug when it does not exit -- and
        // mingw's stdout to a pipe is fully buffered, so a run stopped on a
        // timeout handed back an EMPTY log however far it had actually got.
        (void)std::fflush(stdout);
        return device != nullptr;
    }

    /// A BUTTON EDGE, PUSHED ONTO SDL'S OWN EVENT QUEUE -- and this is the one
    /// place the harness is not the device.
    ///
    /// MEASURED, NOT ASSUMED: SDL 3.4.12's virtual joystick delivers AXES and
    /// does NOT deliver BUTTONS. With a 400 ms hold and a per-frame poll,
    /// SDL_SetJoystickVirtualButton returned true every time while
    /// SDL_GetGamepadButton stayed 0 and no SDL_EVENT_JOYSTICK_BUTTON_DOWN was
    /// ever queued -- while SDL_SetJoystickVirtualAxis on the same device, in
    /// the same frame, moved SDL_GetGamepadAxis to 28000 and arrived as a real
    /// SDL_EVENT_GAMEPAD_AXIS_MOTION. The generated mapping was correct and
    /// complete (`a:b0 ... dpdown:b12 ... lefty:a1`), so this is SDL's own
    /// device layer, not the mapping and not this file.
    ///
    /// WHAT THE PUSH THEREFORE PROVES, EXACTLY: everything from SDL's event
    /// queue onward -- key_of_pad_button, route_menu_key, every branch under
    /// it, the pages themselves. That is where every defect this pass fixed
    /// was. WHAT IT DOES NOT PROVE: SDL's own translation from a physical HID
    /// report into that event, which is SDL's job, is not code in this repo,
    /// and IS exercised end-to-end by the stick beats above -- the same device,
    /// the same open gamepad, the same frame loop.
    void pushButton(SDL_GamepadButton button, bool down) const {
        SDL_Event out{};
        out.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        out.gbutton.timestamp = SDL_GetTicksNS();
        out.gbutton.which = id;
        out.gbutton.button = static_cast<Uint8>(button);
        out.gbutton.down = down;
        (void)SDL_PushEvent(&out);
    }

    void detach() {
        if (device != nullptr) {
            SDL_CloseJoystick(device);
            device = nullptr;
        }
        if (id != 0) {
            (void)SDL_DetachVirtualJoystick(id);
            id = 0;
        }
    }

    /// One beat's worth of progress. CALLED AFTER THE PRESENT, so a `shot:`
    /// beat photographs the frame that has just been drawn -- every press
    /// before it accounted for, none after it -- and the next press is set on
    /// the device before the next poll reads it. A button is HELD for one
    /// advance and released on the next, which is what makes SDL emit a genuine
    /// DOWN and then a genuine UP.
    ///
    /// Returns false when the script has run out: the caller closes its window,
    /// so the harness terminates itself rather than needing a kill.
    bool advance(const render::Framebuffer& frame) {
        if (!live()) {
            return true;
        }
        const Uint64 now = SDL_GetTicks();
        if (now < until) {
            return true;
        }
        if (held) {
            // RELEASE. Both kinds return to rest through the same beat, so a
            // stick beat crosses the latch's lower threshold and re-arms it
            // exactly as a thumb coming off the stick does.
            const PadBeat& beat = beats[at];
            if (beat.kind == PadBeat::Kind::Button) {
                pushButton(beat.button, false);
            } else if (beat.kind == PadBeat::Kind::Stick) {
                (void)SDL_SetJoystickVirtualAxis(device, static_cast<int>(beat.axis), 0);
            }
            held = false;
            ++at;
            until = now + 90;
            return true;
        }
        if (at >= beats.size()) {
            std::printf("granadad: --padscript: script complete\n");
            (void)std::fflush(stdout);
            return false;
        }
        const PadBeat& beat = beats[at];
        switch (beat.kind) {
            case PadBeat::Kind::Button:
                pushButton(beat.button, true);
                held = true;
                until = now + 90;
                break;
            case PadBeat::Kind::Stick:
                (void)SDL_SetJoystickVirtualAxis(device, static_cast<int>(beat.axis), beat.value);
                held = true;
                until = now + 90;
                break;
            case PadBeat::Kind::Wait:
                until = now + static_cast<Uint64>(std::max(beat.millis, 0));
                ++at;
                break;
            case PadBeat::Kind::Shot: {
                std::filesystem::path out =
                    shotDir.empty() ? std::filesystem::path(".") : shotDir;
                std::error_code ec;
                std::filesystem::create_directories(out, ec);
                out /= beat.name + ".png";
                // THE FRAMEBUFFER ITSELF, NOT THE WINDOW. The deliverable is
                // judged at the render size (640x360 by default), and upscaling
                // it first would hide exactly the thing the size is judged for.
                const bool ok = render::writePng(frame, out.string());
                std::printf("granadad: --padscript: %s %s\n", ok ? "wrote" : "FAILED to write",
                            out.string().c_str());
                (void)std::fflush(stdout);
                ++at;
                until = now + 40;
                break;
            }
        }
        return true;
    }
};

[[nodiscard]] bool starts_with(const char* text, const char* prefix, const char** rest) {
    const std::size_t length = std::strlen(prefix);
    if (std::strncmp(text, prefix, length) != 0) {
        return false;
    }
    *rest = text + length;
    return true;
}

void print_usage() {
    std::printf(
        "usage: granadad [options]\n"
        "  --smoke=N            run N movement steps, then capture and exit\n"
        "  --screenshot=PATH    write the captured frame as a PNG. Composited\n"
        "                       through the 3D backend: the shipped build opens\n"
        "                       a window for the shutter and closes it; the\n"
        "                       headless (rlsw) build opens none\n"
        "  --2d                 draw the world with the software raycaster inside\n"
        "                       the overlay instead of the 3D chunk meshes (the\n"
        "                       picture this build showed before the 3D programme)\n"
        "  --3d                 the default: the Docks as 3D geometry through the\n"
        "                       raylib backend, the terminal HUD composited over it\n"
        "  --width=N            internal render width  (default 640)\n"
        "  --height=N           internal render height (default 360)\n"
        "  --scale=N            window / capture upscale, nearest neighbor (default 2)\n"
        "  --time=HH            hour of the day, 0-23. The windowed game opens\n"
        "                       at 8, which is when the ward goes to work; a\n"
        "                       capture with no --time defaults to 20. A\n"
        "                       scripted line sets its own hour when this is not\n"
        "                       given -- --skyrun wants 22, when Finch is in\n"
        "                       the snug -- and never overrides one that is\n"
        "  --fov=DEG            horizontal field of view (default 90)\n"
        "  --spawn=X,Y,Z        spawn tile (default the authored Tarwalk spawn)\n"
        "  --yaw=DEG            spawn facing, 0 = north (default 0)\n"
        "  --sensitivity=N      mouse look, BAM per count (default 14). The\n"
        "                       OPTIONS page (F2) has a slider for this and\n"
        "                       it is what survives between runs -- this only\n"
        "                       overrides it for one launch\n"
        "  --invert-y           invert the look axis for one launch\n"
        "  --controls=PATH      bindings file (default granadad-controls.cfg\n"
        "                       beside the executable)\n"
        "  --clock=N            simulated seconds per real second (default 1)\n"
        "  --hold               do not walk during --smoke; let the world move\n"
        "  --talk               open a conversation before the shutter goes\n"
        "  --topic=N[,N...]     pick these topics once it is open (1-based, as\n"
        "                       the numbers printed beside them on screen)\n"
        "  --offer=N            name this number across a counter\n"
        "  --cursor=N           put the topic cursor on row N without picking\n"
        "                       it, so a frame can be taken OF a long label\n"
        "  --keys-row=N         open the CONTROLS page (implies --pause=controls)\n"
        "                       with its cursor on row N, so the converted\n"
        "                       master/detail layout can be photographed with\n"
        "                       a row other than its first one showing\n"
        "  --again              close the conversation and open it again\n"
        "  --pause[=WHERE]      open the pause menu before the shutter goes.\n"
        "                       WHERE is menu (freshly opened), controls\n"
        "                       (CONTROLS chosen, so the keys page is what\n"
        "                       gets photographed), settings (SETTINGS\n"
        "                       chosen, so the rebinding screen is what gets\n"
        "                       photographed) or armed (the cursor on QUIT\n"
        "                       with the first of its two presses in)\n"
        "  --character          open the tiled Menu, Character tile focused,\n"
        "                       before the shutter goes\n"
        "  --map                open the tiled Menu, Map tile focused,\n"
        "                       before the shutter goes\n"
        "  --map-overlay        open the WARD MAP (the full-screen district\n"
        "                       plan the M key opens) before the shutter\n"
        "                       goes, so it is photographable headless\n"
        "  --map-place=NAME     put the ward map's cursor on an authored\n"
        "                       place name (\"The Weighhouse\"). THE MAP\n"
        "                       PASS; implies --map-overlay\n"
        "  --map-tab=VIEW       overview, people, index or legend\n"
        "  --map-zoom=N         rungs in from the whole ward, 0..3\n"
        "  --travel=NAME        press the ward map's TRAVEL verb on an\n"
        "                       authored place: the clock advances by the\n"
        "                       walk's real cost and the body arrives -- or\n"
        "                       the refusal is printed, in its exact words,\n"
        "                       in the summary's | travel segment\n"
        "  --case-lead=ID       THE CASEBOOK PASS: open the casebook page with\n"
        "                       its cursor on this casebook.json lead id\n"
        "                       (weighhouse-ledger)\n"
        "  --case-tab=VIEW      leads or case\n"
        "  --case-route         press the casebook's commit verb, which routes\n"
        "                       the highlighted lead onto the ward map. The\n"
        "                       only headless path to a picture of the route\n"
        "  --face=NAME          VERIFICATION ONLY: turn the body toward an\n"
        "                       authored place (\"The Gilded Gull\") and leave\n"
        "                       it standing there with no page open, so the\n"
        "                       CROSSHAIR PROMPT can be photographed naming a\n"
        "                       door. Pair it with --spawn to choose where\n"
        "                       you are looking from\n"
        "  --threshold=WHERE    VERIFICATION ONLY: walk the body across one\n"
        "                       authored place boundary so the THRESHOLD\n"
        "                       PLATE is on the frame at the shutter. WHERE\n"
        "                       is saltgate (out of the Quayward's east gate\n"
        "                       onto Saltgate Rise, under the district gate\n"
        "                       frame), gull (in at the Gilded Gull's door),\n"
        "                       piers (north over the quay lip) or gallows\n"
        "                       (east off Gallows Row onto the Rise)\n"
        "  --threshold-end=HOW  where the camera is left: in (default, facing\n"
        "                       the way you walked) or back (turned round to\n"
        "                       look at the gate you came through)\n"
        "  --sprint=N           VERIFICATION ONLY: hold forward+sprint for N\n"
        "                       real steps before the shutter -- the fatigue\n"
        "                       bar's mid/empty states and the winded refusal\n"
        "  --punch              VERIFICATION ONLY: walk up to the nearest\n"
        "                       person, put them on the crosshair and tap\n"
        "                       the punch key until one lands, before the\n"
        "                       shutter goes\n"
        "  --block              VERIFICATION ONLY: pick that same fight, raise\n"
        "                       the guard, hold it until a blow is softened\n"
        "  --cast               VERIFICATION ONLY: press the cast key once\n"
        "                       (pair with --flame to have a spell to cast)\n"
        "  --held=ID[,ID]       VERIFICATION ONLY: equip each crafting by id\n"
        "                       and cast until its link opens, waiting out\n"
        "                       cooldowns -- photographs live holds with\n"
        "                       their clocks. Pair with --flame\n"
        "  --grimoire           VERIFICATION ONLY: open the Grimoire page (the\n"
        "                       same page a tap of the QuickWheel key opens)\n"
        "                       before the shutter goes; pair with --flame so\n"
        "                       the list has craftings on it\n"
        "  --wait               VERIFICATION ONLY: open the Wait page through\n"
        "                       the pause menu's own WAIT row before the\n"
        "                       shutter goes\n"
        "  --petition           VERIFICATION ONLY: find clergy standing on\n"
        "                       compound ground, read the roll for it and\n"
        "                       petition the vacant charge -- the leasehold\n"
        "                       line, end to end\n"
        "  --radiant[=END]      VERIFICATION ONLY: find the radiant board's\n"
        "                       own giver out in the ward, talk, and take the\n"
        "                       errand off them. END `offer` stops with the\n"
        "                       giver's row on the open list; `taken` (the\n"
        "                       default) presses it and checks the journal\n"
        "  --quickbar           VERIFICATION ONLY: bind the first crafting to\n"
        "                       slot 3 through the Grimoire page, close it and\n"
        "                       press the number, so the bottom-centre strip\n"
        "                       and the CAST row are photographed agreeing\n"
        "  --padscript=BEATS    VERIFICATION ONLY: attach an SDL VIRTUAL\n"
        "                       GAMEPAD and play a comma-separated script of\n"
        "                       beats through it, so the pad's own code path\n"
        "                       can be photographed on a machine with no\n"
        "                       controller plugged in. Beats: a b x y back\n"
        "                       start lb rb up down left right (buttons),\n"
        "                       lsup lsdown lsleft lsright (a left-stick push\n"
        "                       past the deadzone and back), lt rt (the\n"
        "                       triggers, which SDL reports as axes and not\n"
        "                       as buttons), wait:MS, shot:NAME. An unknown\n"
        "                       beat REFUSES THE WHOLE SCRIPT rather than\n"
        "                       being skipped -- a capture that quietly\n"
        "                       dropped the press it was taken to prove is\n"
        "                       worse than no capture. Windowed runs only\n"
        "  --padcreation=BEATS  the same, for the CHARACTER SCREEN -- the\n"
        "                       window a windowed launch opens first, and so\n"
        "                       the one a --padscript has to get past\n"
        "  --padshots=DIR       where a --padscript shot:NAME beat writes\n"
        "  --demo[=SECTION]     PLAY THE SCRIPTED DEMO: a curated route\n"
        "                       through the ward that runs itself, paced for\n"
        "                       a human eye, and ends on a card rather than\n"
        "                       dumping you mid-street. Deterministic: a fixed\n"
        "                       number of simulation steps per rendered frame,\n"
        "                       so demo frame N is the same picture on every\n"
        "                       machine. Drives the character screen too.\n"
        "                       SECTION skips into the route -- quay,\n"
        "                       saltgate, case, map, night, end\n"
        "  --demo-capture=DIR   the same route, writing a PNG of each of its\n"
        "                       named shots into DIR, so a trailer or a\n"
        "                       screenshot set falls out of the same run\n"
        "  --framedump=DIR      VERIFICATION ONLY: write EVERY presented frame\n"
        "                       into DIR as f<NNNNN>.png -- consecutive frames,\n"
        "                       so a transition (a cut, a fade, a plate easing\n"
        "                       in, a page opening) can be read frame by frame\n"
        "                       instead of argued about. Works in the ordinary\n"
        "                       windowed session as well as under --demo and\n"
        "                       --case-watch; keep the run short. The shot is\n"
        "                       the framebuffer after the overlays, exactly\n"
        "                       what the window presented\n"
        "  --creation[=STEP]    capture the character-creation flow with no\n"
        "                       window and no world. STEP is origin (default),\n"
        "                       calling (the nine-trade roster), quiz (question\n"
        "                       five, meters mid-tally), verdict (the tally's\n"
        "                       card), background (a biography question),\n"
        "                       review (a taken calling converged on the\n"
        "                       customize screen), customize (CUSTOM, a few\n"
        "                       points spent), devin or gabri (fixed sheets)\n"
        "  --settle             run to the AT-REST frame before the shutter\n"
        "                       (the default for a screenshot): ~4s of\n"
        "                       zero-input steps, past the page's open ease\n"
        "                       AND the tutor bands' page-open raise easing\n"
        "                       back down -- the frame the word budgets bind\n"
        "  --no-settle          capture the overlay's opening bump instead\n"
        "                       of its settled at-rest frame -- only useful\n"
        "                       for proving the transition itself does not\n"
        "                       flash on its first drawn frame\n"
        "  --settle-steps=N     run exactly N zero-input steps before the\n"
        "                       shutter instead of the at-rest default: 0\n"
        "                       photographs the raised state, a small N a\n"
        "                       mid-transition frame of the panel eases\n"
        "  --refocus=TILE       VERIFICATION ONLY: after --character/--map has\n"
        "                       been given --settle-steps to genuinely finish\n"
        "                       opening, switch the tiled Menu's focus to TILE\n"
        "                       (character, map, letters or journal) and run\n"
        "                       --refocus-steps more zero-input steps before\n"
        "                       the shutter -- a real step() gap between the\n"
        "                       two toggle*() calls that --character --map\n"
        "                       together could never give, so the Menu's own\n"
        "                       focus-swap crossfade can be photographed\n"
        "                       genuinely mid-transition\n"
        "  --refocus-steps=N    how many steps to run after --refocus switches\n"
        "                       focus, before the shutter (default 0, the very\n"
        "                       first frame of the swap). Ignored without\n"
        "                       --refocus\n"
        "  --tile-page=N        VERIFICATION ONLY: N presses of the 0/MORE key\n"
        "                       after the menu-opening flags, so a tile's\n"
        "                       second page (the character sheet's faction\n"
        "                       ladders live there) can be photographed\n"
        "  --street[=WHO]       stand next to somebody out in the WARD and talk\n"
        "                       to them. WHO is hand, watch, priest, disciple,\n"
        "                       keeper, fisher, sailor, carter, wastrel, urchin,\n"
        "                       thief, drover, cat or dog (default hand). The\n"
        "                       body is PLACED beside them -- the same landing\n"
        "                       --spawn makes -- and everything after that is\n"
        "                       the game: the real key, the real director and\n"
        "                       the owner's own tables\n"
        "  --streettopic=N      pick topic N of that conversation, 1-based, as\n"
        "                       the numbers printed beside them on screen\n"
        "  --flame[=WHERE]      run the Priest of the Flame line and capture it.\n"
        "                       WHERE is talk (the finished conversation), bench\n"
        "                       (the workshop standing open) or away (closed, so\n"
        "                       the HUD's own rung and objective are visible)\n"
        "  --roofs[=WHERE]      climb onto the Gilded Gull's roof and look down.\n"
        "                       WHERE is roof (on the lead), leap (across the\n"
        "                       alley) or street (the drop back down)\n"
        "  --skyrun[=WHERE]     play the Skyrunner line -- sign on, two purses, a\n"
        "                       box, the roof, the alley, the fence, a lean and a\n"
        "                       bale past the Watch. WHERE is talk or away\n"
        "  --watch-halt[=WHERE] draw steel in the Gull in Watchman Cull's sight\n"
        "                       and stand there: the halt, the arrest at reach,\n"
        "                       the street. WHERE is halt or street\n"
        "  --nemesis[=WHERE]    lose a fist fight to a named laborer three\n"
        "                       times and watch him rise: a rung, a guild\n"
        "                       with members in it, a permanent cut of the\n"
        "                       ward's prices and his name on the compound\n"
        "                       roll as a Den Duke. WHERE is talk or away\n"
        "  --contract[=WHERE]   play the ward's own bounty -- take it off the\n"
        "                       Watch, get the Flame's mark, hunt the taproom\n"
        "                       and get paid. WHERE is talk, away or held\n"
        "                       (stop after the mark, the job still in hand)\n"
        "  --burgle[=WHERE]     rob the Gilded Gull at four in the morning --\n"
        "                       crouch, cross a dark taproom unseen, lift a\n"
        "                       purse, up the stair, wire into a guest's box\n"
        "                       and empty it. WHERE is box, lock (the wire in\n"
        "                       the next box), taproom or street\n"
        "  --case[=WHERE]       play THE QUIET TENANT end to end -- the\n"
        "                       courier's sheet, the Gull by day, the wait to\n"
        "                       the small hours, the box, the brawl, TAKE HIM\n"
        "                       UP and the Mission's back room. WHERE is\n"
        "                       sheet, gull, night, down, or empty for the\n"
        "                       whole errand delivered\n"
        "  --case-watch[=WHERE] WATCH the same errand: the identical --case\n"
        "                       drive, recorded and replayed in a window at\n"
        "                       the demo's own cadence -- one simulation step\n"
        "                       per rendered frame on the demo's frame\n"
        "                       deadline -- with captions, so a human can sit\n"
        "                       through what --case proves. Same route, same\n"
        "                       beats, same end state, about three minutes;\n"
        "                       the wait to two is a cut, the demo's own\n"
        "                       clock rule. ESC leaves. WHERE as --case\n"
        "  --case-watch-capture=DIR\n"
        "                       the same watch run, writing one PNG per\n"
        "                       landed beat into DIR\n"
        "  --world=NAME         baked world to load (default docks_surface)\n"
        "  --ward[=DAYS]        run the ward's compounds -- courtyard farms,\n"
        "                       ground rents, bonds and the priest's hearings\n"
        "                       -- for DAYS (default 730) and print what the\n"
        "                       land gave, what the mouths took and who went\n"
        "                       hungry. No window\n"
        "  --trail[=WHERE]      WALK THE BLOODLETTER TRAIL. The investigation\n"
        "                       the district is about: the body at the Mission,\n"
        "                       the erased line in the Weighhouse ledger, the\n"
        "                       grate corroded shut from OUTSIDE, and what is\n"
        "                       behind the doors of a warehouse that has been\n"
        "                       condemned for nine years. WHERE is notes (the\n"
        "                       casebook open), start (the opening page of a\n"
        "                       new game), mission, weighhouse, hold, letters\n"
        "                       (Maell's own letters, opened after his second\n"
        "                       lead is read), opened (THE CASEBOOK PASS: stop\n"
        "                       on the step the Weighhouse ledger opens three\n"
        "                       leads, so the lead-opened notice is on the\n"
        "                       frame), or keys (the in-game controls page)\n"
        "  --nohud              draw the world and NOTHING over it -- no HUD,\n"
        "                       no conversation surface, no build stamp. It is\n"
        "                       a ruler: capture a scene twice, once with it\n"
        "                       and once without, and every pixel that differs\n"
        "                       is interface. That is how the numbers in\n"
        "                       docs/HUD-REAL-ESTATE.md were measured\n"
        "  --people[=H[,N]]     print the district's roll: every trade, how\n"
        "                       many of each sleep on a ROOF DECK, who is up\n"
        "                       there right now, and how the food chain is\n"
        "                       doing (mice on the board, mice caught). H is\n"
        "                       the hour to bake at and N how many hours to\n"
        "                       run first -- a cat takes about five to get\n"
        "                       hungry enough to hunt. No window\n"
        "  --selftest           deterministic primitives only, no window\n"
        "  --gamepad-selftest   ask SDL what gamepads it sees, no window --\n"
        "                       prints the count (0 included) and every name,\n"
        "                       so a controller's presence can be proved from\n"
        "                       a script without a human pressing a button\n"
        "  --audio-selftest     open the real SDL audio engine, no window --\n"
        "                       prints whether a device opened (no device is\n"
        "                       a fact, not a failure: the game runs silent)\n"
        "                       and drives the same bed/footstep/UI calls the\n"
        "                       game makes, so the sound path can be proved\n"
        "                       from a script without ears\n"
        "  --version            print the build banner and exit\n"
        "\n"
        "IN THE GAME: WASD moves, the mouse looks, SHIFT sprints, CTRL\n"
        "crouches (both HOLD and TAP), SPACE jumps, E talks, M opens the\n"
        "ward map, J opens your casebook, F1 lists every key and F2\n"
        "rebinds them.\n"
        "\n"
        "WALK INTO A LEDGE TO CLIMB IT. There is no climb key to learn --\n"
        "though V still works if you would rather line a leap up yourself.\n"
        "The keys are IN the game and every one of them is rebindable; this\n"
        "page is a convenience and not the reference.\n");
}

[[nodiscard]] Options parse(int argc, char** argv, bool& stop, int& exitCode) {
    Options options;
    options.smoke.session.timeOfDay = 20 * 3600;
    stop = false;
    exitCode = 0;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* value = nullptr;
        if (std::strcmp(arg, "--ward") == 0) {
            stop = true;
            exitCode = run_ward(730);
            return options;
        }
        if (starts_with(arg, "--ward=", &value)) {
            stop = true;
            exitCode = run_ward(std::atoi(value));
            return options;
        }
        if (std::strcmp(arg, "--people") == 0) {
            stop = true;
            exitCode = run_people(8, 0);
            return options;
        }
        if (starts_with(arg, "--people=", &value)) {
            // HOUR,HOURS -- the clock to bake at, and how long to run before
            // printing. `--people=6,13` is the district at six in the morning
            // soaked until the evening, which is what it takes to see the food
            // chain move.
            int hour = 8;
            int hours = 0;
            if (std::sscanf(value, "%d,%d", &hour, &hours) < 1) {
                std::printf("granadad: --people wants HOUR[,HOURS]\n");
                stop = true;
                exitCode = 2;
                return options;
            }
            stop = true;
            exitCode = run_people(hour, hours);
            return options;
        }
        if (std::strcmp(arg, "--selftest") == 0) {
            stop = true;
            exitCode = run_selftest();
            return options;
        }
        if (std::strcmp(arg, "--gamepad-selftest") == 0) {
            stop = true;
            exitCode = run_gamepad_selftest();
            return options;
        }
        if (std::strcmp(arg, "--audio-selftest") == 0) {
            stop = true;
            exitCode = run_audio_selftest();
            return options;
        }
        if (std::strcmp(arg, "--version") == 0) {
            stop = true;
            return options;
        }
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            print_usage();
            stop = true;
            return options;
        }
        if (starts_with(arg, "--smoke=", &value)) {
            options.smoke.steps = std::atoi(value);
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--screenshot=", &value)) {
            options.smoke.screenshot = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--width=", &value)) {
            options.smoke.session.width = std::max(64, std::atoi(value));
        } else if (starts_with(arg, "--height=", &value)) {
            options.smoke.session.height = std::max(64, std::atoi(value));
        } else if (starts_with(arg, "--scale=", &value)) {
            options.windowScale = std::clamp(std::atoi(value), 1, 8);
            options.smoke.captureScale = options.windowScale;
        } else if (starts_with(arg, "--time=", &value)) {
            options.smoke.session.timeOfDay = (std::atoi(value) % 24) * 3600;
            // Named, so a scripted line does not set its own clock over the
            // top of it. See render::scriptedStartHour.
            options.smoke.session.timeOfDayGiven = true;
        } else if (starts_with(arg, "--fov=", &value)) {
            options.smoke.session.fovDegrees = std::clamp(std::atoi(value), 40, 130);
        } else if (starts_with(arg, "--yaw=", &value)) {
            options.smoke.session.spawnYaw = sim::angle_from_degrees(std::atoi(value));
            options.smoke.session.spawnYawGiven = true;
        } else if (starts_with(arg, "--sensitivity=", &value)) {
            options.sensitivity =
                std::clamp(std::atoi(value), render::kMinSensitivity, render::kMaxSensitivity);
            options.sensitivityGiven = true;
        } else if (std::strcmp(arg, "--invert-y") == 0) {
            options.invertY = true;
        } else if (std::strcmp(arg, "--3d") == 0) {
            options.video3d = true;
        } else if (std::strcmp(arg, "--2d") == 0) {
            options.video3d = false;
        } else if (starts_with(arg, "--controls=", &value)) {
            options.controlsFile = value;
        } else if (starts_with(arg, "--clock=", &value)) {
            options.smoke.session.clockScale = std::clamp(std::atoi(value), 1, 3600);
        } else if (std::strcmp(arg, "--hold") == 0) {
            options.smoke.walk = false;
        } else if (std::strcmp(arg, "--nohud") == 0) {
            options.smoke.session.hud = false;
            options.smoke.stamp = false;
        } else if (std::strcmp(arg, "--talk") == 0) {
            options.smoke.talk = true;
        } else if (std::strcmp(arg, "--again") == 0) {
            options.smoke.talk = true;
            options.smoke.again = true;
        } else if (std::strcmp(arg, "--pause") == 0) {
            options.smoke.pause = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--pause=", &value)) {
            options.smoke.pause = true;
            options.smoke.pauseEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--character") == 0) {
            options.smoke.character = true;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--map") == 0) {
            options.smoke.map = true;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--map-overlay") == 0) {
            // CORE ACTION #13. See SmokeRunConfig::mapOverlay's own header.
            options.smoke.mapOverlay = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--map-place=", &value)) {
            // THE MAP PASS. See SmokeRunConfig::mapPlace's own header for why
            // the page's cursor needs a capture path of its own.
            options.smoke.mapPlace = value;
            options.smoke.mapOverlay = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--map-tab=", &value)) {
            options.smoke.mapTab = value;
            options.smoke.mapOverlay = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--case-lead=", &value)) {
            // THE CASEBOOK PASS. See SmokeRunConfig::caseLead's own header: the
            // book is a composed page with a cursor now, and a capture needs a
            // way to put that cursor somewhere on purpose.
            options.smoke.caseLead = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--case-tab=", &value)) {
            options.smoke.caseTab = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--case-route") == 0) {
            options.smoke.caseRoute = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--map-zoom=", &value)) {
            options.smoke.mapZoom = std::atoi(value);
            options.smoke.mapOverlay = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--travel=", &value)) {
            // FAST TRAVEL (TRAVEL lane). See SmokeRunConfig::travelTo: the
            // ward map's TRAVEL verb pressed on an authored place, the
            // summary's own `| travel` segment carrying the whole claim.
            options.smoke.travelTo = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--face=", &value)) {
            // THE CROSSHAIR PASS. See SmokeRunConfig::face's own header: the
            // aim prompt names a door only when a door is under the reticle,
            // and "under the reticle" is a yaw with no key that sets it.
            options.smoke.face = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--threshold=", &value)) {
            // DISTRICT PHASE D. VERIFICATION ONLY. See
            // SmokeRunConfig::threshold's own header -- the plate is up for
            // two seconds after a crossing and for no other reason, so
            // without this there is no headless path to a picture of it.
            options.smoke.threshold = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--threshold-end=", &value)) {
            // Only read when --threshold is also given.
            options.smoke.thresholdEnd = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--sprint=", &value)) {
            // VERIFICATION ONLY. See SmokeRunConfig::sprintSteps's own header.
            options.smoke.sprintSteps = std::max(0, std::atoi(value));
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--punch") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::punch's own header.
            options.smoke.punch = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--charge=", &value)) {
            // VERIFICATION ONLY. See SmokeRunConfig::chargeSteps' own header.
            options.smoke.chargeSteps = std::max(0, std::atoi(value));
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--block") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::block's own header.
            options.smoke.block = true;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--cast") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::cast's own header.
            options.smoke.cast = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--held=", &value)) {
            // VERIFICATION ONLY. See SmokeRunConfig::heldSpells's own header.
            options.smoke.heldSpells = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--grimoire") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::grimoire's own header.
            options.smoke.grimoire = true;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--wait") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::wait's own header.
            options.smoke.wait = true;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--petition") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::petition's own header.
            options.smoke.petition = true;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--radiant") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::radiant's own header.
            options.smoke.radiant = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--radiant=", &value)) {
            // `--radiant=offer` stops with the giver's row on the open list,
            // the same NAME=END spelling --flame/--contract/--roofs use.
            options.smoke.radiant = true;
            options.smoke.radiantEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--quickbar") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::quickbar's own header.
            options.smoke.quickbar = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--padscript=", &value)) {
            // THE PARITY PASS, VERIFICATION ONLY -- see PadScript's header. It
            // does nothing at all outside the windowed loop.
            options.padScript = value;
        } else if (starts_with(arg, "--padcreation=", &value)) {
            options.padCreation = value;
        } else if (starts_with(arg, "--padshots=", &value)) {
            options.padShotDir = value;
        } else if (std::strcmp(arg, "--demo") == 0) {
            options.demo = true;
        } else if (starts_with(arg, "--demo=", &value)) {
            options.demo = true;
            options.demoSection = value;
        } else if (starts_with(arg, "--framedump=", &value)) {
            options.frameDumpDir = value;
        } else if (starts_with(arg, "--demo-capture=", &value)) {
            options.demo = true;
            options.demoShotDir = value;
        } else if (std::strcmp(arg, "--creation") == 0) {
            options.wantsCreation = true;
        } else if (starts_with(arg, "--creation=", &value)) {
            options.wantsCreation = true;
            options.creationStep = value;
        } else if (std::strcmp(arg, "--settle") == 0) {
            options.smoke.settle = true;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--no-settle") == 0) {
            // SmokeRunConfig::settle now defaults to true -- see its own
            // header. This is the escape hatch for the one caller that
            // deliberately wants the opening bump captured instead.
            options.smoke.settle = false;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--settle-steps=", &value)) {
            // VERIFICATION ONLY. See SmokeRunConfig::settleSteps's own
            // header -- a mid-transition frame for the panel-geometry and
            // menu-focus eases, neither of which `--settle`/`--no-settle`
            // alone can photograph.
            options.smoke.settleSteps = std::max(0, std::atoi(value));
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--refocus=", &value)) {
            // PLANNING SPRINT (item #1). VERIFICATION ONLY. See
            // SmokeRunConfig::refocus's own header -- the CLI mechanism a
            // real step() gap between two toggle*() calls needed, so the
            // Menu's focus-swap crossfade can be photographed mid-transition
            // and not just proven correct in an isolated test.
            options.smoke.refocus = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--refocus-steps=", &value)) {
            // VERIFICATION ONLY. See SmokeRunConfig::refocusSteps's own
            // header. Only read when --refocus is also given.
            options.smoke.refocusSteps = std::max(0, std::atoi(value));
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--tile-page=", &value)) {
            // SHEETS BUILD. VERIFICATION ONLY. See SmokeRunConfig::tilePage's
            // own header -- N presses of the 0/MORE key after the menu opens,
            // so a tile's second page can be photographed.
            options.smoke.tilePage = std::max(0, std::atoi(value));
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--street") == 0) {
            options.smoke.street = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--street=", &value)) {
            options.smoke.street = true;
            options.smoke.streetWho = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--streettopic=", &value)) {
            options.smoke.streetTopic = std::max(0, std::atoi(value));
        } else if (std::strcmp(arg, "--flame") == 0) {
            options.smoke.flame = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--flame=", &value)) {
            options.smoke.flame = true;
            options.smoke.flameEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--contract") == 0) {
            options.smoke.contract = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--contract=", &value)) {
            options.smoke.contract = true;
            options.smoke.contractEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--roofs") == 0) {
            options.smoke.roofs = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--roofs=", &value)) {
            options.smoke.roofs = true;
            options.smoke.roofsEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--watch-halt") == 0) {
            // BARKS LANE (feel/build). See SmokeRunConfig::watchHalt.
            options.smoke.watchHalt = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--watch-halt=", &value)) {
            options.smoke.watchHalt = true;
            options.smoke.watchHaltEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--nemesis") == 0) {
            options.smoke.nemesis = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--nemesis=", &value)) {
            options.smoke.nemesis = true;
            options.smoke.nemesisEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--trail") == 0) {
            options.smoke.trail = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--trail=", &value)) {
            options.smoke.trail = true;
            options.smoke.trailEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--burgle") == 0) {
            options.smoke.burgle = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--burgle=", &value)) {
            options.smoke.burgle = true;
            options.smoke.burgleEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--case") == 0) {
            // COURIER CASE (lane: case). THE QUIET TENANT end to end -- see
            // SmokeRunConfig::caseRun.
            options.smoke.caseRun = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--case=", &value)) {
            options.smoke.caseRun = true;
            options.smoke.caseEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--eviction") == 0) {
            // EVICTION CASE (lane: eviction). The owner's third case,
            // participate path end to end -- see SmokeRunConfig::evictionRun.
            options.smoke.evictionRun = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--eviction=", &value)) {
            // --eviction=refused is the disrupt path; the rest are shutters.
            options.smoke.evictionRun = true;
            options.smoke.evictionEnd = value;
            options.wantsSmoke = true;
        } else if (std::strcmp(arg, "--case-watch") == 0) {
            // CASE WATCH. caseRun is set WITHOUT wantsSmoke: the drive is
            // recorded inside the client (recordCaseDrive reads the same
            // smoke config the harness would), and main() routes to the
            // windowed loop, never to runSmoke.
            options.caseWatch = true;
            options.smoke.caseRun = true;
        } else if (starts_with(arg, "--case-watch=", &value)) {
            options.caseWatch = true;
            options.smoke.caseRun = true;
            options.smoke.caseEnd = value;
        } else if (starts_with(arg, "--case-watch-capture=", &value)) {
            options.caseWatch = true;
            options.smoke.caseRun = true;
            options.caseWatchShotDir = value;
        } else if (std::strcmp(arg, "--skyrun") == 0) {
            options.smoke.skyrun = true;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--skyrun=", &value)) {
            options.smoke.skyrun = true;
            options.smoke.skyrunEnd = value;
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--cursor=", &value)) {
            options.smoke.cursorRow = std::max(0, std::atoi(value));
            options.smoke.talk = true;
        } else if (starts_with(arg, "--keys-row=", &value)) {
            // PANES PASS. Implies --pause=controls, because the row it names
            // is a row of that page -- and does NOT imply --talk, which is
            // what --cursor does and what makes --cursor useless here: a
            // conversation refuses to let the keys page open over it.
            options.smoke.keysRow = std::max(0, std::atoi(value));
            options.smoke.pause = true;
            options.smoke.pauseEnd = "controls";
            options.wantsSmoke = true;
        } else if (starts_with(arg, "--offer=", &value)) {
            options.smoke.offer = std::atoi(value);
            options.smoke.talk = true;
        } else if (starts_with(arg, "--topic=", &value)) {
            options.smoke.talk = true;
            const char* cursor = value;
            while (*cursor != 0) {
                // ONE-BASED, because the HUD numbers the list from one and the
                // S3 review caught the mismatch: `--topic=6` picked list item
                // seven, which would have quietly mislabelled every capture a
                // later sprint took. The internal index is still zero-based;
                // the conversion happens exactly here.
                options.smoke.topics.push_back(std::atoi(cursor) - 1);
                while (*cursor != 0 && *cursor != ',') {
                    ++cursor;
                }
                if (*cursor == ',') {
                    ++cursor;
                }
            }
        } else if (starts_with(arg, "--world=", &value)) {
            options.smoke.session.world = value;
        } else if (starts_with(arg, "--spawn=", &value)) {
            int x = 0;
            int y = 0;
            int z = 0;
            if (std::sscanf(value, "%d,%d,%d", &x, &y, &z) == 3) {
                options.smoke.session.spawnX = x;
                options.smoke.session.spawnY = y;
                options.smoke.session.spawnBand = z;
            } else {
                std::printf("granadad: --spawn wants X,Y,Z\n");
                stop = true;
                exitCode = 2;
                return options;
            }
        } else {
            std::printf("granadad: unknown option %s\n", arg);
            print_usage();
            stop = true;
            exitCode = 2;
            return options;
        }
    }
    return options;
}

// ---------------------------------------------------------------------------
// the lists
// ---------------------------------------------------------------------------

/// Routes a key to whatever LIST is open -- a conversation's topics, the
/// casebook, the keys page, the options page, the workbench, the counter, the
/// wire in a lock -- and says whether it was taken.
///
/// BY ACTION, NOT BY KEY, and that is the point. It asks the binding table what
/// the key means and then uses the answer: a player who rebinds forward to Z
/// walks a topic list with Z, because "up the list" and "forward" are the same
/// intention wearing different clothes. Routing menus by hard-coded W and S --
/// which is what shipped before -- means rebinding movement quietly leaves half
/// the interface on the old keys.
///
/// Returns false for anything it does not want, and the caller then treats the
/// press as an ordinary game verb -- every one of which puts the page away
/// first (Session::dismissOverlays). A player who presses a game key with their
/// notes up gets the game.
[[nodiscard]] bool route_menu_key(render::Session& session, render::Key key) {
    if (key == render::Key::None) {
        return false;
    }
    // A REBINDING EATS THE NEXT KEY, and it is handled by the caller so that
    // even a key bound to something in this function is captured raw.
    if (session.awaitingKey()) {
        return false;
    }

    // THE PARITY PASS. B IS BACK, on every page -- PadEast arrives here
    // ALREADY REMAPPED to Escape while a page owns the input, by
    // render::pageBackRemap at the gamepad event edge (the one place a
    // PadEast can enter). It used to be remapped LOCALLY, right here, and
    // that was the B seam the ship note's drive found: this function would
    // judge the ESCAPE and fall through (so the close could happen), but the
    // caller then called pressed() with the REAL PadEast -- whose binding is
    // Action::Crouch -- so one press closed the casebook AND toggled crouch,
    // and the street after closing the book carried a CROUCHED banner nobody
    // asked for. Remapping at the edge means the router and the fall-through
    // press read the SAME key: Escape backs out (every branch below either
    // answers it -- the workbench's endForge -- or deliberately falls through
    // to Action::Pause, which "backs out of whatever is open"), and crouch
    // never hears the press. With no page open the remap does not fire and B
    // is crouch in the world exactly as it has always been; while the
    // options page is listening for a key to bind, the remap also stands
    // down, so PadEast itself can still be bound.
    const render::Action action = session.controls().actionFor(key);
    // UI-EA-SPEC sec. 4 violation #3: THE PAGE-TOGGLE KEY ALWAYS TOGGLES.
    // PadUp opens the tiled Menu (Action::Menu's pad default) and was then
    // eaten as list movement inside it -- so the key that opened the page
    // could never close it, breaking "the key that opened a page closes it"
    // for the one surface a pad opens most. While the Menu surface is up,
    // a PadUp that IS the Menu binding falls straight through to pressed(),
    // whose Action::Menu case is the toggle that closes it. In-page
    // cursor-up rides the left stick (the stickNav block synthesizes raw
    // arrow keys now) and the other three D-pad directions stay list
    // movement everywhere; on pages PadUp did NOT open -- the map, the
    // pause family -- it stays cursor-up too, because there it is not the
    // toggle of anything on screen.
    if (key == render::Key::PadUp && action == render::Action::Menu &&
        (session.casebookOpen() || session.casebookPageOpen())) {
        return false;
    }
    // The five list movements, in the vocabulary of intent. Arrows always work
    // as well, bound or not, because a list is the one place arrow keys are
    // unambiguous.
    //
    // AND SO DOES THE D-PAD, for exactly the same reason and by exactly the
    // same mechanism -- the raw key, ahead of any binding. THE PARITY PASS
    // FOUND THIS DEAD: of the four D-pad directions only PadUp carried an
    // action at all (Menu, since the ward map took PadBack), so PadDown,
    // PadLeft and PadRight resolved to Action::Count here, took no branch, fell
    // through to pressed() and did NOTHING -- while the nav band along the foot
    // of the map said ARROWS NEXT PLACE and the casebook's said UP DOWN NEXT
    // LEAD. Every one of those bands was advertising a verb the pad could not
    // perform. A page is the one place the D-pad is as unambiguous as an arrow
    // key, so it is read the same way and outranks its own binding there --
    // which is what takes PadUp off Menu for as long as a NON-Menu list is up
    // (the violation-#3 rule above carves out the Menu surface itself).
    const bool up = key == render::Key::Up || key == render::Key::PadUp ||
                    action == render::Action::Forward || action == render::Action::QuickPrev;
    const bool downward = key == render::Key::Down || key == render::Key::PadDown ||
                          action == render::Action::Back || action == render::Action::QuickNext;
    const bool leftward = key == render::Key::Left || key == render::Key::PadLeft ||
                          action == render::Action::StrafeLeft;
    const bool rightward = key == render::Key::Right || key == render::Key::PadRight ||
                           action == render::Action::StrafeRight;
    const bool confirm = key == render::Key::Enter || action == render::Action::Interact;
    // The printed number beside a row. Ten of them, and the tenth turns the page
    // -- see kTopicPageSize.
    const int slotBase = static_cast<int>(render::Action::QuickSlot1);
    const int slot = static_cast<int>(action) - slotBase;
    const bool numbered = slot >= 0 && slot < 9;
    const bool pageKey = action == render::Action::QuickSlot0;

    if (session.pauseOpen()) {
        // THE PAUSE MENU. Three rows, its own cursor, no sliders and no paging
        // -- the smallest of the four surfaces this router knows, and still
        // its own branch rather than folded into moveTopicCursor/chooseTopic,
        // because ESC and every navigation key here also has to disarm QUIT
        // (see Session::movePauseCursor) and none of the other surfaces do.
        if (up) {
            session.movePauseCursor(-1);
            return true;
        }
        if (downward) {
            session.movePauseCursor(1);
            return true;
        }
        if (confirm) {
            // Contract (b): the commit beat -- RESUME, a door row, the
            // quit-arm and the quit-confirm all answer with it.
            session.armCommitPulse();
            session.choosePause();
            return true;
        }
        if (numbered) {
            // THE NUMBER PRINTED BESIDE EACH ROW ACTUALLY PICKS IT. Every
            // topic list this surface draws prints "1 ", "2 ", "3 " ahead of
            // the label (dialogue_view.cpp's topicRowsFor), so a page that
            // left the digits live but unread would show a number nothing
            // answers to -- which is worse than not printing one.
            session.chooseVisibleTopic(slot);
            return true;
        }
        if (pageKey) {
            // SWALLOWED, NOT ROUTED. Three rows never paginate (kTopicPageSize
            // is nine), so "0 MORE" never prints here -- but the digit that
            // opens it elsewhere must not fall through to the quick bar behind
            // the menu, which is what it used to do before this branch existed.
            return true;
        }
        // ESCAPE FALLS THROUGH ON PURPOSE, same as the options page below: the
        // key that opened this closes it, and closeConversation() already
        // knows to disarm QUIT on the first press rather than leaving the page
        // entirely.
        return false;
    }

    if (session.optionsOpen()) {
        // UI-EA-SPEC sec. 4 violation #2: KEYS AND OPTIONS ARE SIBLING TABS,
        // stepped on TAB and the bumpers like every other tabbed pair --
        // they were only ever reachable from each other by the F-keys, which
        // no foot could honestly print for a pad. Two siblings, so either
        // direction lands the other one. The pause-return note travels with
        // the swap: toggleKeys/toggleOptions re-derive it from pauseReturn_
        // (see Session::pageOpenedFromPause_'s own header).
        if (key == render::Key::Tab || action == render::Action::PageNext ||
            action == render::Action::PagePrev) {
            session.armCommitPulse();  // rule 2: a tab step answers instantly
            session.toggleKeys();
            return true;
        }
        if (up) {
            session.moveOptionCursor(-1);
            return true;
        }
        if (downward) {
            session.moveOptionCursor(1);
            return true;
        }
        if (leftward) {
            session.adjustOption(-1);
            return true;
        }
        if (rightward) {
            session.adjustOption(1);
            return true;
        }
        if (confirm) {
            // Contract (b): the commit beat -- a slider nudge or the REBIND
            // arm, both of them ENTER doing something.
            session.armCommitPulse();
            session.chooseOption();
            return true;
        }
        if (numbered) {
            session.chooseVisibleTopic(slot);
            return true;
        }
        if (pageKey) {
            session.nextTopicPage();
            return true;
        }
        // ESCAPE AND F2 FALL THROUGH on purpose, so the key that opened the
        // page always closes it and Menu always backs out of it.
        return false;
    }

    if (session.grimoireOpen()) {
        // SPELLS BUILD. The Grimoire page: up/down walk the craftings,
        // left/right walk the cursor row's SLOT binding (the options page's
        // own slider shape, and the reason a pad can bind at all), the
        // printed number or ENTER readies one, 0 pages a long list. ESC
        // falls through, so the key that closes every page closes this one.
        if (up) {
            session.moveGrimoireCursor(-1);
            return true;
        }
        if (downward) {
            session.moveGrimoireCursor(1);
            return true;
        }
        if (leftward) {
            session.adjustGrimoireSlot(-1);
            return true;
        }
        if (rightward) {
            session.adjustGrimoireSlot(1);
            return true;
        }
        if (numbered) {
            session.armCommitPulse();  // contract (b): readying a crafting
            session.chooseGrimoireRow(slot);
            return true;
        }
        if (pageKey) {
            session.nextGrimoirePage();
            return true;
        }
        if (confirm) {
            session.armCommitPulse();
            session.chooseGrimoireRow(session.grimoireCursor() -
                                      session.grimoirePage() * render::kTopicPageSize);
            return true;
        }
        return false;
    }

    if (session.waitOpen()) {
        // TIME-AND-TENURE BUILD. The Wait page: up/down walk the hours, the
        // printed number or ENTER passes them (or is refused out loud), 0
        // pages the list. ESC falls through, so the key that closes every
        // page closes this one.
        if (up) {
            session.moveWaitCursor(-1);
            return true;
        }
        if (downward) {
            session.moveWaitCursor(1);
            return true;
        }
        if (numbered) {
            session.armCommitPulse();  // contract (b): passing hours commits
            session.chooseWaitRow(slot);
            return true;
        }
        if (pageKey) {
            session.nextWaitPage();
            return true;
        }
        if (confirm) {
            session.armCommitPulse();
            session.chooseWaitRow(session.waitCursor() -
                                  session.waitPage() * render::kTopicPageSize);
            return true;
        }
        return false;
    }

    if (session.districtMapOpen()) {
        // THE MAP PASS. THE WARD MAP IS DRIVEN NOW. It was a static picture
        // (v1: no pan, no zoom, digits swallowed) and the owner's complaint
        // about it -- "Makes it hard to figure out where the place you're
        // looking for is" -- is answered by a cursor, four views over the
        // selection and a zoom ladder, so every one of those has to reach it.
        //
        // The arrows walk PLACES rather than tiles (map_view.hpp's
        // mapPlaceToward), which is why plain arrow keys are enough and there
        // is no separate "cursor speed": four presses cross the ward.
        if (up) {
            session.moveDistrictMapCursor(render::MapStep::North);
            return true;
        }
        if (downward) {
            session.moveDistrictMapCursor(render::MapStep::South);
            return true;
        }
        if (leftward) {
            session.moveDistrictMapCursor(render::MapStep::West);
            return true;
        }
        if (rightward) {
            session.moveDistrictMapCursor(render::MapStep::East);
            return true;
        }
        if (key == render::Key::Tab) {
            // TAB CYCLES THE VIEWS while the map is up, and does NOT open the
            // casebook underneath it. The key that means "the next tab" on
            // every other tabbed surface in the world means it here too, and a
            // page that let its own tab key fall through to a different page
            // would be the split-brain bug toggleOptions' comment describes.
            session.armCommitPulse();  // rule 2: a tab step answers instantly
            session.cycleDistrictMapTab(1);
            return true;
        }
        // THE PARITY PASS: THE BUMPERS ARE THE PAD'S TAB KEY. Tab above is a
        // keyboard key and the pad's Tab (PadUp, Action::Menu) is now the
        // cursor's own UP -- so without this the four views the tab row prints
        // were reachable by keyboard and mouse and by no pad button at all.
        // PagePrev/PageNext is where "the next page of this thing" already
        // lives, LB/RB is where a thumb expects a tab, and `[`/`]` come along
        // for free on the keyboard side.
        if (action == render::Action::PageNext) {
            session.armCommitPulse();
            session.cycleDistrictMapTab(1);
            return true;
        }
        if (action == render::Action::PagePrev) {
            session.armCommitPulse();
            session.cycleDistrictMapTab(-1);
            return true;
        }
        if (key == render::Key::Equals) {
            session.adjustDistrictMapZoom(1);
            return true;
        }
        if (key == render::Key::Minus) {
            session.adjustDistrictMapZoom(-1);
            return true;
        }
        // THE ZOOM LADDER, ON THE TRIGGERS. Same argument as the bumpers: `=`
        // and `-` are keyboard keys, and the owner's own complaint about this
        // page -- "hard to figure out where the place you're looking for is" --
        // was answered by a zoom a pad could not reach. Cast/Block are the two
        // pad keys a full-screen map has no other use for (S13 gave them RT and
        // LT), and this is the same "a verb wearing a different mode's clothes"
        // the haggle branch below already spends PageNext on.
        if (action == render::Action::Cast) {
            session.adjustDistrictMapZoom(1);
            return true;
        }
        if (action == render::Action::Block) {
            session.adjustDistrictMapZoom(-1);
            return true;
        }
        // FAST TRAVEL (TRAVEL lane). The page's second commit: T is a raw
        // map-page key exactly as Tab/=/- are, and Attack is the pad's own
        // half of the verb -- X (PadWest), the one face button unclaimed on
        // this page (A is FACE IT, B backs out, Y is free but Attack is what
        // X binds), the same "verb wearing a different mode's clothes" the
        // zoom triggers above argue. NOT Interact: Interact's pad half is
        // PadSouth, which is confirm/FACE IT, so travelling on it would steal
        // the existing commit. The verb row at the detail foot names whichever
        // half is in the player's hands. Refusals are the Session's to say,
        // out loud, with the page staying up.
        // The Attack half is PAD ONLY -- keyIsPad gates it -- because Attack's
        // keyboard binding is MouseLeft, and a left-click on the map is
        // already the pointer's select-then-FACE-IT (session_pointer below).
        // Without the gate a mouse click would both face AND travel.
        if (key == render::Key::T ||
            (render::keyIsPad(key) && action == render::Action::Attack)) {
            // Contract (b): the commit beat, armed at commit routing.
            session.armCommitPulse();
            session.travelDistrictMapSelection();
            return true;
        }
        if (confirm) {
            // The commit verb at the foot of the detail pane: turn to face the
            // selection and put the map away. Contract (b): the commit beat.
            session.armCommitPulse();
            session.faceDistrictMapSelection();
            return true;
        }
        if (numbered) {
            // THE PRINTED DIGITS ON THE TAB ROW ACTUALLY SELECT. Four tabs,
            // `1` through `4`; the rest are swallowed rather than routed, for
            // the pause branch's own reason -- a number pressed over a
            // full-screen page must not reach the quick bar behind it.
            // Transition rule 2: a LANDING tab step answers with the pulse --
            // a dead digit (5-9, which setDistrictMapTab refuses) must not
            // beat for a press that did nothing.
            if (slot < render::kMapTabCount) {
                session.armCommitPulse();
            }
            session.setDistrictMapTab(slot);
            return true;
        }
        if (pageKey) {
            // UI-EA-SPEC sec. 4 violation #6: `0` = MORE where a list pages.
            // The People and Index tabs page (mapDetailScroll); when PAGES
            // lands the ten-row roster paging (budgets #22/#23) this routes
            // `0` to the detail page-turn. Inert-but-swallowed until then.
            return true;
        }
        return false;
    }

    if (session.picking()) {
        // S9. THE WIRE OWNS THE KEYBOARD WHILE IT IS IN. A mode the SIMULATION
        // is in, which the client reads and routes for.
        if (up) {
            session.movePick(1);
            return true;
        }
        if (downward) {
            session.movePick(-1);
            return true;
        }
        // #85. Jump and Punch RETIRED into Vertical and Attack -- the wire
        // still reuses whichever key opens the lock and whichever one forces
        // it, unchanged in feel, under the new names.
        if (key == render::Key::Space || action == render::Action::Vertical) {
            session.probeLock();
            return true;
        }
        if (action == render::Action::Attack) {
            session.forceLock();
            return true;
        }
        return false;
    }

    if (session.talking()) {
        if (session.forging()) {
            // The workbench takes the keyboard the way the counter does. Up and
            // down walk the five fields, left and right change the one under the
            // cursor, confirm asks for it.
            if (up) {
                session.moveForgeField(-1);
                return true;
            }
            if (downward) {
                session.moveForgeField(1);
                return true;
            }
            if (leftward) {
                session.adjustForge(-1);
                return true;
            }
            if (rightward) {
                session.adjustForge(1);
                return true;
            }
            if (confirm) {
                session.commitForge();
                return true;
            }
            if (key == render::Key::Escape) {
                session.endForge();
                return true;
            }
            return true;  // nothing else reaches the world through a workbench
        }
        if (session.haggling()) {
            const int stride = g_video_shift_held ? 5 : 1;
            if (leftward || downward) {
                session.adjustOffer(-stride);
                return true;
            }
            if (rightward || up) {
                session.adjustOffer(stride);
                return true;
            }
            if (confirm) {
                session.makeOffer();
                return true;
            }
            // #85. Lift RETIRED into Interact, which this branch already
            // spends on `confirm` (making YOUR OWN offer) -- the two cannot
            // share a key inside one haggle. PageNext is free here (nothing
            // in a haggle pages anything) and reused for "take the number on
            // the table", the same "a verb wearing a different mode's
            // clothes" pattern route_menu_key already uses throughout --
            // Forward/Back become movePick in the lockpicking branch above,
            // for instance.
            if (action == render::Action::PageNext) {
                session.takeAskingPrice();
                return true;
            }
            // SHIP NOTE MOVE 3, in passing: the band has advertised
            // "T - TAKE THEIR PRICE" since the haggle shipped, and T was
            // never routed -- only PageNext above was. The advertised key
            // now does the advertised thing; PageNext stays for the pad's
            // RB, which is what the band prints with a pad in hand. Same
            // yield rule as F1/F2/F3: a verb somebody BINDS to T outranks
            // the convenience.
            if (key == render::Key::T && action == render::Action::Count) {
                session.takeAskingPrice();
                return true;
            }
            return false;
        }
        if (up) {
            session.moveTopicCursor(-1);
            return true;
        }
        if (downward) {
            session.moveTopicCursor(1);
            return true;
        }
        if (numbered) {
            // The number printed BESIDE the topic, which is a slot on the
            // visible page and not an index into the whole list. On page two, 1
            // is the tenth topic.
            session.chooseVisibleTopic(slot);
            return true;
        }
        if (pageKey) {
            session.nextTopicPage();
            return true;
        }
        if (confirm) {
            session.interact();
            return true;
        }
        return false;
    }

    if (session.casebookPageOpen()) {
        // THE CASEBOOK PASS. The book is a composed master/detail page now and
        // every one of its verbs has to reach it -- the cursor, the two views,
        // the printed digits and the commit.
        //
        // UI-EA-SPEC sec. 4 violation #1: THE TABS STEP ON TAB AND THE
        // BUMPERS NOW, the same grammar as the map -- one tab key across
        // every tabbed surface, which is the whole point of a grammar. TAB
        // was Action::Menu when this page was drawn and the arrows were the
        // stopgap; Menu lives on J now and TAB is free, so the stopgap
        // retires. LEFT/RIGHT are FREED for in-view movement -- taken and
        // held inert here until the detail pane grows its paging (PAGES
        // lane, budget #27/#28), because letting them fall through would
        // close the page and turn the player, which is the exact class of
        // surprise sec. 4 exists to kill.
        if (up) {
            session.moveCasebookCursor(-1);
            return true;
        }
        if (downward) {
            session.moveCasebookCursor(1);
            return true;
        }
        if (key == render::Key::Tab || action == render::Action::PageNext) {
            session.armCommitPulse();  // rule 2: a tab step answers instantly
            session.cycleCasebookTab(1);
            return true;
        }
        if (action == render::Action::PagePrev) {
            session.armCommitPulse();
            session.cycleCasebookTab(-1);
            return true;
        }
        if (leftward || rightward) {
            // Reserved -- see the header note above.
            return true;
        }
        if (numbered) {
            // THE PRINTED DIGITS BESIDE THE FIRST NINE LEADS ACTUALLY SELECT.
            // Rows past the ninth print no number at all, so there is no digit
            // here that answers to nothing.
            session.setCasebookCursor(slot);
            return true;
        }
        if (confirm) {
            // The commit verb at the foot of the detail pane. State chooses
            // which one it is -- see Session::commitCasebookLead.
            // Contract (b): the commit beat.
            session.armCommitPulse();
            session.commitCasebookLead();
            return true;
        }
        if (pageKey) {
            // UI-EA-SPEC sec. 4 violation #6: `0` IS MORE WHERE A LIST PAGES,
            // INERT ELSEWHERE -- never BACK. Today the lead list follows its
            // cursor, so `0` is inert here; the moment PAGES lands the
            // eight-row paging (budget #27) this branch routes it to the
            // page-turn, and the foot prints `0` again. Swallowed either way:
            // a digit over a full page must not reach the quick bar.
            return true;
        }
        // Anything else falls through to the ordinary bindings, and every verb
        // down there puts the page away first.
        return false;
    }

    if (session.casebookOpen() || session.keysOpen() || session.characterOpen() ||
        session.mapOpen() || session.lettersOpen()) {
        // UI-EA-SPEC sec. 4 violation #2, the other half: from the keys page,
        // TAB and the bumpers step to the sibling Options page. Scoped to
        // keysOpen -- on the tiled Menu the bumpers already step tile focus
        // through pressed()'s PagePrev/PageNext dispatch, and that stays.
        if (session.keysOpen() &&
            (key == render::Key::Tab || action == render::Action::PageNext ||
             action == render::Action::PagePrev)) {
            session.armCommitPulse();  // rule 2
            session.toggleOptions();
            return true;
        }
        if (up) {
            session.moveTopicCursor(-1);
            return true;
        }
        if (downward) {
            session.moveTopicCursor(1);
            return true;
        }
        if (numbered) {
            session.chooseVisibleTopic(slot);
            return true;
        }
        if (pageKey) {
            session.nextTopicPage();
            return true;
        }
        if (confirm) {
            session.chooseTopic(static_cast<std::size_t>(session.topicCursor()));
            return true;
        }
        // Anything else falls through to the ordinary bindings, and every verb
        // down there puts the page away first.
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// the pointer
// ---------------------------------------------------------------------------
//
// THE PARITY PASS. WHAT WAS WRONG: the window was opened with
// SDL_SetWindowRelativeMouseMode(window, true) and never left that mode except
// through the F3 escape hatch, so while a full-screen page was up there was no
// pointer on screen AT ALL. That is not "the mouse does not select"; it is "the
// mouse does not exist". Meanwhile mapPlaceAtPixel() and casebookLeadAtPixel()
// had both been written, both documented as "the inverse of what was drawn",
// and both given cases in test_map_view.cpp and test_casebook_page.cpp -- and
// NOTHING IN THE PROGRAM CALLED EITHER OF THEM. Two tested hit-tests, dead.
//
// AND THE POINTER MIRRORS THE CURSOR rather than running beside it -- the rule
// run_creation_window() already keeps for the character screen (see its own
// header). Hovering a lead calls the same setCasebookCursor() a D-pad press
// calls; hovering a building calls the same selection a d-pad step calls. There
// is no second "hovered" highlight the pad cannot see, so the three devices
// cannot disagree about which row is live, and a click is only ever "put the
// cursor here, then confirm".
//
// FRAMEBUFFER PIXELS, NOT WINDOW PIXELS. Both hit-tests are the inverse of a
// draw into a `width x height` framebuffer that SDL then presents with
// SDL_SetRenderLogicalPresentation at an integer scale; at the default
// --scale=2 a window coordinate is twice a frame coordinate, and at a resized
// window there is a letterbox as well. SDL_ConvertEventToRenderCoordinates is
// the only correct conversion across both, and the caller does it before
// calling in here -- same as the creation window.

/// True while a page owns the input, which is the one condition under which the
/// player gets a pointer back. Deliberately the same six flags `listening`
/// reads in the frame loop: "is a page eating the keyboard" and "should there be
/// a cursor on screen" are the same question, and two hand-kept copies of that
/// list is the drift session.hpp's own menuOpen() comment warns about.
[[nodiscard]] bool pointer_page_open(const render::Session& session) {
    return session.menuOpen() || session.pauseOpen() || session.talking() ||
           session.waitOpen() || session.picking();
}

/// A hover or a click at framebuffer pixel (px, py). `click` commits; a hover
/// only moves the cursor. Returns true when the page took it -- a false says
/// "no page wanted this pixel", and the caller then leaves the press alone.
bool session_pointer(render::Session& session, int frameWidth, int frameHeight, int px, int py,
                     bool click) {
    if (session.districtMapOpen()) {
        // THE WARD MAP. mapPageLayout() is the same layout drawDistrictMap()
        // composes from -- map_view.hpp exposes it for exactly this reason
        // ("a mouse, a test and the scrolling arithmetic all need the same
        // answer the drawing used") -- so the viewport handed to mapPlaceAtPixel
        // is the viewport the plan was drawn through, not a second guess at it.
        const render::DistrictMapState plan = session.districtMapState();
        const render::MapPageLayout layout =
            render::mapPageLayout(frameWidth, frameHeight, plan);
        if (!layout.usable) {
            return false;
        }
        // UI-EA-SPEC sec. 4 violation #8: THE TAB ROW ANSWERS CLICKS NOW,
        // through the same setDistrictMapTab a digit presses -- the casebook's
        // own pattern, which this page took-and-dropped while its sibling
        // answered. Click-only, like the casebook's: a pointer crossing the
        // frame must not flip the detail pane.
        const int tab = render::mapTabAtPixel(plan, frameWidth, frameHeight, px, py);
        if (tab >= 0) {
            if (click && tab != static_cast<int>(plan.tab)) {
                session.armCommitPulse();  // rule 2: a tab step answers
                session.setDistrictMapTab(tab);
            }
            return true;
        }
        const int at = render::mapPlaceAtPixel(layout.viewport, px, py);
        if (at < 0) {
            // OFF THE PLAN IS NOT A MISS THAT FALLS THROUGH. A click on the
            // detail pane, the tab row or the frame is still a click ON THE
            // PAGE, and letting it reach pressed() would put a punch through a
            // full-screen map. Taken and dropped.
            return true;
        }
        const std::vector<render::MapPlace>& places = render::mapPlaces();
        if (at >= static_cast<int>(places.size())) {
            return true;
        }
        (void)session.selectDistrictMapPlace(places[static_cast<std::size_t>(at)].name);
        if (click) {
            // THE COMMIT VERB AT THE FOOT OF THE DETAIL PANE, which is what the
            // page prints and what ENTER and PadSouth already do: turn to face
            // it and put the map away. A click is a select-then-confirm, so a
            // player who only wants to look moves the pointer and does not
            // press. Contract (b): the commit beat.
            session.armCommitPulse();
            session.faceDistrictMapSelection();
        }
        return true;
    }
    if (session.keysOpen()) {
        // THE CONTROLS PAGE. keysRowAtPixel is the exact inverse of what
        // drawKeysPage drew (same composition, same whole-list plan, same
        // scroll). A row here is a reference, not a choice -- the number keys
        // only move the cursor (chooseVisibleTopic's own keysOpen_ rule) -- so
        // hover AND click both put the cursor on the row, through the same
        // moveTopicCursor a W press walks, and nothing else fires.
        const render::KeysPageState page = session.keysPageState();
        const int at = render::keysRowAtPixel(page, frameWidth, frameHeight, px, py);
        if (at >= 0 && at != page.cursor) {
            // The exact delta lands exactly: the keysOpen_ cursor wraps modulo
            // the list (wrapCursorAndPage), and |delta| < count here.
            session.moveTopicCursor(at - page.cursor);
        }
        return true;
    }
    if (session.casebookPageOpen()) {
        const render::CasebookPageState page = session.casebookPageState();
        // THE TAB ROW FIRST -- the ship note names it: LEFT/RIGHT worked and
        // clicking LEADS / THE CASE did nothing. Hover deliberately does NOT
        // switch the view (the tab row is not a cursor; a pointer crossing the
        // frame must not flip the detail pane), so this is click-only, through
        // the same cycleCasebookTab an arrow press turns.
        const int tab = render::casebookTabAtPixel(page, frameWidth, frameHeight, px, py);
        if (tab >= 0) {
            if (click && tab != static_cast<int>(page.tab)) {
                session.armCommitPulse();  // rule 2: a tab step answers
                session.cycleCasebookTab(tab - static_cast<int>(page.tab));
            }
            return true;
        }
        const int at = render::casebookLeadAtPixel(page, frameWidth, frameHeight, px, py);
        if (at < 0) {
            return true;
        }
        session.setCasebookCursor(at);
        if (click) {
            session.armCommitPulse();  // contract (b): the commit beat
            session.commitCasebookLead();
        }
        return true;
    }
    if (session.casebookOpen()) {
        // THE TILED MENU -- the three top tiles plus the unfocused Journal
        // band (Journal FOCUS is the composed book page, handled above). The
        // state handed to the hit-test is assembled from the same four public
        // views drawFrame assembles its own from; only list geometry is read
        // (topics, cursor, page, the Journal's prose, an open letter), so the
        // focus eases and the alert routing the drawing also carries cannot
        // move a row.
        render::MenuTileState tiles;
        tiles.open = true;
        tiles.character = session.characterPanelView();
        tiles.map = session.mapPanelView();
        tiles.letters = session.lettersPanelView();
        tiles.journal = session.journalPanelView();
        tiles.focus = session.menuFocus();
        const render::MenuTileHit hit =
            render::menuTileHitAtPixel(tiles, frameWidth, frameHeight, px, py);
        if (hit.tile < 0) {
            // The frame, a rule, a divider: on the page, not on a tile.
            return true;
        }
        if (hit.tile == render::kMenuFocusJournal) {
            // FOCUSING THE JOURNAL SWAPS THE WHOLE SURFACE (casebookPageOpen
            // becomes true and the composed book replaces the tiles), so it is
            // a CLICK verb, never a hover one -- a pointer drifting across the
            // bottom band must not tear the screen out from under itself.
            if (click) {
                session.setMenuFocus(render::kMenuFocusJournal);
            }
            return true;
        }
        // HOVER FOCUSES THE TILE UNDER THE POINTER -- Session's own
        // setMenuFocus, the same focus state the bumpers cycle, so there is
        // no second focus the pad cannot see.
        session.setMenuFocus(hit.tile);
        if (hit.row >= 0) {
            const render::DialogueViewState& view = hit.tile == render::kMenuFocusCharacter
                                                        ? tiles.character
                                                    : hit.tile == render::kMenuFocusMap
                                                        ? tiles.map
                                                        : tiles.letters;
            if (hit.row != view.cursor) {
                // The same wrap-by-delta a held arrow walks; exact for
                // |delta| < count.
                session.moveTopicCursor(hit.row - view.cursor);
            }
            if (click) {
                // The commit: opens a letter under the pointer; on the
                // Character and Chart tiles chooseTopic is the same honest
                // read-only no-op a number press is.
                session.chooseTopic(static_cast<std::size_t>(hit.row));
            }
        } else if (hit.more && click) {
            session.nextTopicPage();
        }
        return true;
    }
    if (session.picking()) {
        // THE WIRE. Not a composed page -- the lock is one HUD row (a mode,
        // hud.cpp's own ruling) -- so there is no geometry to invert. The
        // pointer's verbs mirror the keys the row advertises: a click PROBES,
        // exactly what SPACE does, and the wheel already walks the pick
        // through route_menu_key's QuickPrev/QuickNext. Off the row or on it,
        // the click stays on the mode.
        if (click) {
            session.probeLock();
        }
        return true;
    }
    if (session.stripCardOpen()) {
        // UI-EA-SPEC 1.7, joined at integration: the pause stack (pause,
        // wait, options, grimoire) draws as PAGES' composed card now, so the
        // pointer inverts THAT composition -- creationPageHitTest off the
        // same stripCard() the frame drew -- not the old strip-band geometry,
        // which no longer matches anything on screen. The verbs mirror the
        // keyboard router above, exactly: hover walks the cursor by delta,
        // a click is select-then-confirm with the commit beat, and a click
        // on the BACK keycap is the ESC it prints. The card's unselectable
        // `+N` row reports no hit; `0` still turns the page.
        const render::CreationPage card = session.stripCard();
        const render::CreationHit hit =
            render::creationPageHitTest(card, frameWidth, frameHeight, px, py);
        if (hit.zone == render::CreationHit::Zone::Back) {
            if (click) {
                session.closeConversation();
            }
            return true;
        }
        if (hit.zone == render::CreationHit::Zone::Row) {
            const int delta = hit.index - card.cursor;
            if (delta != 0) {
                if (session.waitOpen()) {
                    session.moveWaitCursor(delta);
                } else if (session.pauseOpen()) {
                    session.movePauseCursor(delta);
                } else if (session.grimoireOpen()) {
                    session.moveGrimoireCursor(delta);
                } else {
                    session.moveOptionCursor(delta);
                }
            }
            if (click) {
                session.armCommitPulse();  // contract (b): the commit beat
                if (session.waitOpen()) {
                    session.chooseWaitRow(session.waitCursor() -
                                          session.waitPage() * render::kTopicPageSize);
                } else if (session.pauseOpen()) {
                    session.choosePause();
                } else if (session.grimoireOpen()) {
                    session.chooseGrimoireRow(session.grimoireCursor() -
                                              session.grimoirePage() * render::kTopicPageSize);
                } else {
                    session.chooseOption();
                }
            }
        }
        // On the page is on the page: a miss must not swing a fist through
        // the card.
        return true;
    }
    if (session.talking()) {
        // ONE PAGE LEFT ON THE WIDGET: a live conversation still draws its
        // list through drawDialogue's bottom band, so dialogueTopicAtPixel
        // is its mouse. The pause stack that used to share this branch now
        // draws as the composed card and is inverted above.
        const render::DialogueViewState view = session.dialogueView();
        if (view.haggling || view.forging || view.letter) {
            // The counter, the workbench and an open letter stay modal for
            // now: the band's body is not the topic list there, and a click
            // must still not swing a fist through the panel.
            return true;
        }
        const render::DialogueTopicHit hit =
            render::dialogueTopicAtPixel(view, frameWidth, frameHeight, px, py);
        if (hit.more) {
            // The 0 MORE row: a click turns the page, exactly what 0 does.
            if (click) {
                if (session.waitOpen()) {
                    session.nextWaitPage();
                } else if (session.grimoireOpen()) {
                    session.nextGrimoirePage();
                } else {
                    session.nextTopicPage();
                }
            }
            return true;
        }
        if (hit.index < 0) {
            return true;
        }
        // HOVER MIRRORS THE CURSOR -- the same per-page move a D-pad press
        // makes, by exact delta (every one of these cursors wraps modulo its
        // own count, and |delta| < count). Only when it actually moves, so a
        // resting pointer costs nothing and cannot, say, disarm QUIT.
        const int delta = hit.index - view.cursor;
        if (delta != 0) {
            if (session.waitOpen()) {
                session.moveWaitCursor(delta);
            } else if (session.pauseOpen()) {
                session.movePauseCursor(delta);
            } else if (session.grimoireOpen()) {
                session.moveGrimoireCursor(delta);
            } else if (session.optionsOpen()) {
                session.moveOptionCursor(delta);
            } else {
                session.moveTopicCursor(delta);
            }
        }
        if (click) {
            // A CLICK IS SELECT-THEN-CONFIRM (the map's own rule): the commit
            // is each page's ENTER -- pass the hours, ready the crafting,
            // fire the pause row, nudge or arm the option, say the thing.
            // Contract (b): the commit beat, the same arm the keyboard's
            // confirm gets.
            session.armCommitPulse();
            if (session.waitOpen()) {
                session.chooseWaitRow(hit.slot);
            } else if (session.grimoireOpen()) {
                session.chooseGrimoireRow(hit.slot);
            } else if (session.optionsOpen()) {
                session.chooseOption();
            } else {
                session.chooseVisibleTopic(hit.slot);
            }
        }
        return true;
    }
    // ANY PAGE STILL WITHOUT ITS OWN HIT-TEST STAYS MODAL: a click lands on
    // the page and stops there rather than swinging a fist at somebody
    // through it. After the pointer pass this is the closing tail of an
    // animation and nothing else.
    return pointer_page_open(session);
}

// ---------------------------------------------------------------------------
// SDL <-> this game's own key vocabulary
// ---------------------------------------------------------------------------
//
// granadad-render owns the bindings, the hold-or-toggle modifiers, the stick
// deadzones and the settings file, and it does all of that without one SDL
// include -- see render/controls.hpp on why. What is left in this file is the
// translation, and it is a table.
//
// SCANCODES, NOT KEYCODES, for anything held. A scancode is a PHYSICAL key, so
// WASD stays where the fingers are on an AZERTY or Dvorak layout instead of
// scattering itself across the board. Keycodes would make "W" mean the letter,
// which is the wrong question for a movement key.
//
// VERIFICATION GAP (#77): THIS TABLE IS NOT TESTED. Everything downstream of it
// is -- render/controls.hpp's binding table, the modifiers, the pad and the
// settings file all have cases in tests/test_controls.cpp, and every verb it
// dispatches to has cases of its own. What no case covers is whether
// SDL_SCANCODE_W actually arrives here as Key::W. That is a smaller hole than
// the one S3 left ("NOTHING TESTS THIS SWITCH", three hundred lines of game
// logic), but it is the same KIND of hole and it is still open: closing it needs
// an SDL harness, not another case.

struct ScanRow {
    render::Key key;
    SDL_Scancode scancode;
};

constexpr ScanRow kScanTable[] = {
    {render::Key::A, SDL_SCANCODE_A},
    {render::Key::B, SDL_SCANCODE_B},
    {render::Key::C, SDL_SCANCODE_C},
    {render::Key::D, SDL_SCANCODE_D},
    {render::Key::E, SDL_SCANCODE_E},
    {render::Key::F, SDL_SCANCODE_F},
    {render::Key::G, SDL_SCANCODE_G},
    {render::Key::H, SDL_SCANCODE_H},
    {render::Key::I, SDL_SCANCODE_I},
    {render::Key::J, SDL_SCANCODE_J},
    {render::Key::K, SDL_SCANCODE_K},
    {render::Key::L, SDL_SCANCODE_L},
    {render::Key::M, SDL_SCANCODE_M},
    {render::Key::N, SDL_SCANCODE_N},
    {render::Key::O, SDL_SCANCODE_O},
    {render::Key::P, SDL_SCANCODE_P},
    {render::Key::Q, SDL_SCANCODE_Q},
    {render::Key::R, SDL_SCANCODE_R},
    {render::Key::S, SDL_SCANCODE_S},
    {render::Key::T, SDL_SCANCODE_T},
    {render::Key::U, SDL_SCANCODE_U},
    {render::Key::V, SDL_SCANCODE_V},
    {render::Key::W, SDL_SCANCODE_W},
    {render::Key::X, SDL_SCANCODE_X},
    {render::Key::Y, SDL_SCANCODE_Y},
    {render::Key::Z, SDL_SCANCODE_Z},
    {render::Key::Num0, SDL_SCANCODE_0},
    {render::Key::Num1, SDL_SCANCODE_1},
    {render::Key::Num2, SDL_SCANCODE_2},
    {render::Key::Num3, SDL_SCANCODE_3},
    {render::Key::Num4, SDL_SCANCODE_4},
    {render::Key::Num5, SDL_SCANCODE_5},
    {render::Key::Num6, SDL_SCANCODE_6},
    {render::Key::Num7, SDL_SCANCODE_7},
    {render::Key::Num8, SDL_SCANCODE_8},
    {render::Key::Num9, SDL_SCANCODE_9},
    {render::Key::F1, SDL_SCANCODE_F1},
    {render::Key::F2, SDL_SCANCODE_F2},
    {render::Key::F3, SDL_SCANCODE_F3},
    {render::Key::F4, SDL_SCANCODE_F4},
    {render::Key::F5, SDL_SCANCODE_F5},
    {render::Key::F6, SDL_SCANCODE_F6},
    {render::Key::F7, SDL_SCANCODE_F7},
    {render::Key::F8, SDL_SCANCODE_F8},
    {render::Key::F9, SDL_SCANCODE_F9},
    {render::Key::F10, SDL_SCANCODE_F10},
    {render::Key::F11, SDL_SCANCODE_F11},
    {render::Key::F12, SDL_SCANCODE_F12},
    {render::Key::Up, SDL_SCANCODE_UP},
    {render::Key::Down, SDL_SCANCODE_DOWN},
    {render::Key::Left, SDL_SCANCODE_LEFT},
    {render::Key::Right, SDL_SCANCODE_RIGHT},
    {render::Key::Space, SDL_SCANCODE_SPACE},
    {render::Key::Enter, SDL_SCANCODE_RETURN},
    {render::Key::Escape, SDL_SCANCODE_ESCAPE},
    {render::Key::Tab, SDL_SCANCODE_TAB},
    {render::Key::Backspace, SDL_SCANCODE_BACKSPACE},
    {render::Key::LeftShift, SDL_SCANCODE_LSHIFT},
    {render::Key::RightShift, SDL_SCANCODE_RSHIFT},
    {render::Key::LeftCtrl, SDL_SCANCODE_LCTRL},
    {render::Key::RightCtrl, SDL_SCANCODE_RCTRL},
    {render::Key::LeftAlt, SDL_SCANCODE_LALT},
    {render::Key::RightAlt, SDL_SCANCODE_RALT},
    {render::Key::Minus, SDL_SCANCODE_MINUS},
    {render::Key::Equals, SDL_SCANCODE_EQUALS},
    {render::Key::Comma, SDL_SCANCODE_COMMA},
    {render::Key::Period, SDL_SCANCODE_PERIOD},
    {render::Key::Slash, SDL_SCANCODE_SLASH},
    {render::Key::Semicolon, SDL_SCANCODE_SEMICOLON},
    {render::Key::Apostrophe, SDL_SCANCODE_APOSTROPHE},
    {render::Key::LeftBracket, SDL_SCANCODE_LEFTBRACKET},
    {render::Key::RightBracket, SDL_SCANCODE_RIGHTBRACKET},
    {render::Key::Backslash, SDL_SCANCODE_BACKSLASH},
    {render::Key::Grave, SDL_SCANCODE_GRAVE},
};

struct PadRow {
    render::Key key;
    SDL_GamepadButton button;
};

constexpr PadRow kPadTable[] = {
    {render::Key::PadSouth, SDL_GAMEPAD_BUTTON_SOUTH},
    {render::Key::PadEast, SDL_GAMEPAD_BUTTON_EAST},
    {render::Key::PadWest, SDL_GAMEPAD_BUTTON_WEST},
    {render::Key::PadNorth, SDL_GAMEPAD_BUTTON_NORTH},
    {render::Key::PadLeftBumper, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {render::Key::PadRightBumper, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {render::Key::PadLeftStick, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {render::Key::PadRightStick, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {render::Key::PadStart, SDL_GAMEPAD_BUTTON_START},
    {render::Key::PadBack, SDL_GAMEPAD_BUTTON_BACK},
    {render::Key::PadUp, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {render::Key::PadDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {render::Key::PadLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {render::Key::PadRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};

[[nodiscard]] render::Key key_of_scancode(SDL_Scancode code) {
    for (const ScanRow& row : kScanTable) {
        if (row.scancode == code) {
            return row.key;
        }
    }
    return render::Key::None;
}

[[nodiscard]] SDL_Scancode scancode_of_key(render::Key key) {
    for (const ScanRow& row : kScanTable) {
        if (row.key == key) {
            return row.scancode;
        }
    }
    return SDL_SCANCODE_UNKNOWN;
}

[[nodiscard]] render::Key key_of_pad_button(Uint8 button) {
    for (const PadRow& row : kPadTable) {
        if (static_cast<Uint8>(row.button) == button) {
            return row.key;
        }
    }
    return render::Key::None;
}

[[nodiscard]] render::Key key_of_mouse_button(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return render::Key::MouseLeft;
        case SDL_BUTTON_RIGHT:
            return render::Key::MouseRight;
        case SDL_BUTTON_MIDDLE:
            return render::Key::MouseMiddle;
        case SDL_BUTTON_X1:
            return render::Key::MouseX1;
        case SDL_BUTTON_X2:
            return render::Key::MouseX2;
        default:
            return render::Key::None;
    }
}

/// #80. A letter, a space, a hyphen or an apostrophe off a Key -- everything
/// CreationFlow::typeNameChar accepts, and nothing it does not. Shift is not
/// read: the name field stores everything upper-case regardless of case, the
/// same way the rest of this game's font draws lower-case as upper-case, so
/// there is nothing a shifted key would change.
[[nodiscard]] char name_char_of_key(render::Key key) noexcept {
    if (key >= render::Key::A && key <= render::Key::Z) {
        return static_cast<char>('A' + (static_cast<int>(key) - static_cast<int>(render::Key::A)));
    }
    if (key == render::Key::Space) {
        return ' ';
    }
    if (key == render::Key::Minus) {
        return '-';
    }
    if (key == render::Key::Apostrophe) {
        return '\'';
    }
    return '\0';
}

/// Is this key down right now? Keyboard, mouse or pad -- so a movement key can
/// be rebound to any of the three and the held-key sweep below does not care
/// which it got.
[[nodiscard]] bool key_is_down(render::Key key, const bool* keyboard, Uint32 mouseButtons,
                               SDL_Gamepad* pad) {
    if (key == render::Key::None) {
        return false;
    }
    const SDL_Scancode code = scancode_of_key(key);
    if (code != SDL_SCANCODE_UNKNOWN) {
        return keyboard != nullptr && keyboard[code];
    }
    switch (key) {
        case render::Key::MouseLeft:
            return (mouseButtons & SDL_BUTTON_LMASK) != 0;
        case render::Key::MouseRight:
            return (mouseButtons & SDL_BUTTON_RMASK) != 0;
        case render::Key::MouseMiddle:
            return (mouseButtons & SDL_BUTTON_MMASK) != 0;
        case render::Key::MouseX1:
            return (mouseButtons & SDL_BUTTON_X1MASK) != 0;
        case render::Key::MouseX2:
            return (mouseButtons & SDL_BUTTON_X2MASK) != 0;
        default:
            break;
    }
    if (pad != nullptr) {
        for (const PadRow& row : kPadTable) {
            if (row.key == key) {
                return SDL_GetGamepadButton(pad, row.button);
            }
        }
    }
    return false;
}

/// True when either binding of this action is held.
[[nodiscard]] bool action_is_down(const render::ControlSettings& controls, render::Action action,
                                  const bool* keyboard, Uint32 mouseButtons, SDL_Gamepad* pad) {
    const std::size_t index = static_cast<std::size_t>(action);
    return key_is_down(controls.primary[index], keyboard, mouseButtons, pad) ||
           key_is_down(controls.secondary[index], keyboard, mouseButtons, pad);
}

// ---------------------------------------------------------------------------
// #93. ONE DISPATCH, THREE DEVICES -- controller, mouse and keyboard parity
// ---------------------------------------------------------------------------
//
// WHAT WAS WRONG. run_creation_window() opened with SDL_Init(SDL_INIT_VIDEO)
// and read SDL_EVENT_KEY_DOWN. That is the whole story: the first screen of the
// game could not be driven by a pad at all -- the subsystem was not even
// initialised -- and the mouse did nothing whatsoever. A player who picked up a
// controller to start a new game got a window that ignored them.
//
// WHAT THIS IS. Every device funnels into ONE function, creation_input(), which
// is the only place that knows what a key MEANS on this screen. A keyboard
// arrow, a d-pad press, a left-stick push and a mouse click on a row all end up
// calling the same CreationFlow method with the same argument. Parity is not
// three parallel handlers kept in sync by discipline; it is one handler with
// three doors into it, so a verb cannot exist for one device and not another.
//
// AND THE POINTER MIRRORS THE CURSOR rather than running beside it. Hovering a
// row calls the same set*Cursor() a d-pad press would; there is no second
// "hovered" highlight the keyboard cannot see and no way for the two to
// disagree about which row is live. That is why CreationFlow grew clamping
// setters (creation.hpp) instead of this file keeping a pointer-row of its own.

/// A digit key, 1-9, or 0 for anything else. DIRECT SELECT is the reference's
/// own idiom -- "Numbered rows, direct-select" -- and the numbers the screen
/// prints beside its rows have to actually do something or they are decoration.
[[nodiscard]] int creation_digit_of_key(render::Key key) noexcept {
    if (key >= render::Key::Num1 && key <= render::Key::Num9) {
        return 1 + (static_cast<int>(key) - static_cast<int>(render::Key::Num1));
    }
    return 0;
}

/// What a key MEANS here, whatever device it arrived from. The pad's face
/// buttons take the platform-conventional roles (south confirms, east backs
/// out) and the d-pad doubles the arrows; WASD doubles them too, because this
/// screen is the one place in the game where a player may not yet have found
/// the arrow keys.
enum class CreationVerb : std::uint8_t { None, Up, Down, Left, Right, Confirm, Cancel };

[[nodiscard]] CreationVerb creation_verb_of_key(render::Key key) noexcept {
    switch (key) {
        case render::Key::Up:
        case render::Key::W:
        case render::Key::PadUp:
            return CreationVerb::Up;
        case render::Key::Down:
        case render::Key::S:
        case render::Key::PadDown:
            return CreationVerb::Down;
        case render::Key::Left:
        case render::Key::A:
        case render::Key::PadLeft:
            return CreationVerb::Left;
        case render::Key::Right:
        case render::Key::D:
        case render::Key::PadRight:
            return CreationVerb::Right;
        case render::Key::Enter:
        case render::Key::Space:
        case render::Key::PadSouth:
        case render::Key::PadStart:
            return CreationVerb::Confirm;
        case render::Key::Escape:
        case render::Key::PadEast:
        case render::Key::PadBack:
            return CreationVerb::Cancel;
        default:
            return CreationVerb::None;
    }
}

/// Did this key come off a PAD? The on-screen keyboard is opened by that fact
/// and by nothing else, which is what keeps a typing player from ever seeing
/// it: a keyboard ENTER on the NAME row opens text entry exactly as it always
/// did, and only a pad confirm opens the grid as well.
[[nodiscard]] bool creation_key_is_pad(render::Key key) noexcept {
    switch (key) {
        case render::Key::PadSouth:
        case render::Key::PadEast:
        case render::Key::PadWest:
        case render::Key::PadNorth:
        case render::Key::PadStart:
        case render::Key::PadBack:
        case render::Key::PadUp:
        case render::Key::PadDown:
        case render::Key::PadLeft:
        case render::Key::PadRight:
            return true;
        default:
            return false;
    }
}

/// One input, applied. Returns false when the player asked to leave the screen
/// entirely (cancel at the front door), which is the one thing the flow itself
/// has no way to express.
bool creation_input(render::CreationFlow& flow, render::Key key) {
    // TEXT ENTRY OWNS THE INPUT WHOLE, on every device -- the same rule the
    // options page's own awaitingKey() keeps for a binding. WASD have to be
    // letters here, not arrows, which is exactly why this branch comes first.
    if (flow.step() == render::CreationStep::Customize && flow.editingName()) {
        // THE ON-SCREEN KEYBOARD, WHEN IT IS UP, OWNS THE INPUT IN ITS TURN.
        // Nested inside text entry rather than beside it, because it is the
        // name field's own grid and not a seventh step of the flow.
        if (flow.oskOpen()) {
            switch (key) {
                case render::Key::Up:
                case render::Key::PadUp:
                    flow.moveOskCursor(0, -1);
                    return true;
                case render::Key::Down:
                case render::Key::PadDown:
                    flow.moveOskCursor(0, 1);
                    return true;
                case render::Key::Left:
                case render::Key::PadLeft:
                    flow.moveOskCursor(-1, 0);
                    return true;
                case render::Key::Right:
                case render::Key::PadRight:
                    flow.moveOskCursor(1, 0);
                    return true;
                case render::Key::PadSouth:
                    flow.commitOsk();
                    return true;
                case render::Key::PadWest:
                    flow.backspaceName();
                    return true;
                case render::Key::PadStart:
                case render::Key::Enter:
                    // THE COMMIT VERB AT THE FOOT, pressed. Puts the keyboard
                    // away and closes text entry with the name kept -- which is
                    // exactly what chooseCustomizeRow() on the NAME row does.
                    flow.closeOsk();
                    flow.chooseCustomizeRow();
                    return true;
                case render::Key::PadEast:
                case render::Key::Escape:
                    flow.closeOsk();
                    flow.backToOrigin();
                    return true;
                default:
                    break;
            }
            // A REAL KEYSTROKE STILL TYPES, and typeNameChar() puts the grid
            // away as it goes. Somebody who reaches for the keyboard mid-way
            // gets the keyboard.
            const char typedOnGrid = name_char_of_key(key);
            if (typedOnGrid != '\0') {
                flow.typeNameChar(typedOnGrid);
            }
            return true;
        }
        if (key == render::Key::PadSouth || key == render::Key::PadStart) {
            // A PAD ASKING TO TYPE GETS SOMETHING IT CAN TYPE WITH. Before this
            // the same press closed text entry again, which is why a pad-only
            // player could reach the NAME row and never put a glyph in it.
            flow.openOsk();
            return true;
        }
        if (key == render::Key::Enter) {
            flow.chooseCustomizeRow();
            return true;
        }
        if (key == render::Key::Escape || key == render::Key::PadEast) {
            flow.backToOrigin();
            return true;
        }
        if (key == render::Key::Backspace || key == render::Key::PadWest) {
            flow.backspaceName();
            return true;
        }
        const char typed = name_char_of_key(key);
        if (typed != '\0') {
            flow.typeNameChar(typed);
        }
        return true;
    }

    const CreationVerb verb = creation_verb_of_key(key);
    const int digit = creation_digit_of_key(key);
    const render::CreationStep step = flow.step();

    if (step == render::CreationStep::Origin) {
        // UI-EA-SPEC sec. 4 violation #7: ESC AT THE DOOR ARMS BEFORE IT
        // LEAVES. One slip of the universal back key used to close the whole
        // window unarmed -- the exact "eats your evening once and is never
        // trusted again" the in-world quit already asks twice about. First
        // ESC arms (the page prints `ESC AGAIN - LEAVE` off flow.quitArmed(),
        // PAGES' row copy); a second in a row leaves; any other press is a
        // change of mind and disarms, the pause menu's own movePauseCursor
        // manners.
        switch (verb) {
            case CreationVerb::Up:
                flow.disarmQuit();
                flow.moveOriginCursor(-1);
                return true;
            case CreationVerb::Down:
                flow.disarmQuit();
                flow.moveOriginCursor(1);
                return true;
            case CreationVerb::Confirm:
                flow.disarmQuit();
                flow.chooseOrigin();
                return true;
            case CreationVerb::Cancel:
                if (!flow.quitArmed()) {
                    flow.armQuit();
                    return true;
                }
                return false;
            default:
                break;
        }
        if (digit > 0 && digit <= static_cast<int>(render::originTemplates().size())) {
            flow.disarmQuit();
            flow.setOriginCursor(digit - 1);
            flow.chooseOrigin();
        }
        return true;
    }

    if (step == render::CreationStep::Calling || step == render::CreationStep::Quiz ||
        step == render::CreationStep::Background) {
        switch (verb) {
            case CreationVerb::Up:
                flow.moveChoiceCursor(-1);
                return true;
            case CreationVerb::Down:
                flow.moveChoiceCursor(1);
                return true;
            case CreationVerb::Confirm:
                flow.chooseChoice();
                return true;
            case CreationVerb::Cancel:
                flow.back();
                return true;
            default:
                break;
        }
        if (digit > 0) {
            // DIRECT SELECT. The row numbers are printed; pressing one picks
            // that row and commits it, which is what a printed number means in
            // this register. setChoiceCursor clamps, so a digit past the end of
            // a five-answer question moves nothing and commits nothing new.
            flow.setChoiceCursor(digit - 1);
            if (flow.choiceCursor() == digit - 1) {
                flow.chooseChoice();
            }
        }
        return true;
    }

    // The sheet.
    switch (verb) {
        case CreationVerb::Up:
            flow.moveCustomizeCursor(-1);
            return true;
        case CreationVerb::Down:
            flow.moveCustomizeCursor(1);
            return true;
        case CreationVerb::Left:
            flow.adjustCustomizeRow(-1);
            return true;
        case CreationVerb::Right:
            flow.adjustCustomizeRow(1);
            return true;
        case CreationVerb::Confirm:
            flow.chooseCustomizeRow();
            // WHICHEVER ROW OPENED TEXT ENTRY -- NAME directly, or BEGIN
            // refusing a blank name and putting the cursor there -- a PAD that
            // opened it gets the on-screen keyboard with it, in the same press.
            // A keyboard confirm does not, and that is the whole of the "a
            // typist never sees it" rule at this end.
            if (creation_key_is_pad(key) && flow.editingName()) {
                flow.openOsk();
            }
            return true;
        case CreationVerb::Cancel:
            flow.backToOrigin();
            return true;
        default:
            return true;
    }
}

// ---------------------------------------------------------------------------
// THE DEMO'S FIRST BEAT: the character screen, walked
// ---------------------------------------------------------------------------
//
// THE BRIEF'S FIRST BEAT IS THE QUIZ, and the quiz lives on the FIRST window a
// launch opens, not on the world one. So the demo needs a driver here too --
// separate from the world's DemoDirector for exactly the reason PadDriver is
// separate from PadDriver: this window runs its own SDL_Init/SDL_Quit pair and
// its own flow object, and nothing survives between them.
//
// A STATE MACHINE, NOT A KEY LIST. The obvious version -- "Down, Enter, Enter,
// Enter, ..." -- is a second, silent description of how many questions the quiz
// has and how many rows the sheet has, and it goes wrong the day content adds a
// question. This looks at the flow each tick and decides the next key from what
// is actually on screen, which is what a player does. It presses NOTHING the
// keyboard cannot press: every key goes through creation_input(), the same call
// SDL_EVENT_KEY_DOWN makes.
//
// PACED. The first three questions are answered slowly, with the cursor visibly
// walking to the answer first, because the beat being shown is AN ANSWER'S
// CONSEQUENCE LANDING -- the four meters stepping as each one commits. The
// remaining seven go by at a reading pace: this is a taste of the flow, not the
// whole flow, and a demo that sat through ten identical questions would lose
// the room before it reached the ward.
struct CreationReel {
    /// The name typed into the sheet. THE GAME'S OWN WORD for the player --
    /// DOCKS-GAZETTEER calls the protagonist the Wielder -- so the demo invents
    /// no name any more than it invents a place.
    static constexpr const char* kName = "WIELDER";

    bool active = false;
    int wait = 0;
    int answered = 0;
    std::size_t nameAt = 0;
    bool nameDone = false;
    bool onBegin = false;
    int guard = 0;

    /// One frame's worth. Returns the key to press, or None.
    [[nodiscard]] render::Key next(const render::CreationFlow& flow) {
        if (!active || flow.done()) {
            return render::Key::None;
        }
        if (wait > 0) {
            --wait;
            return render::Key::None;
        }
        // A HARD CEILING. A flow that somehow stopped answering to keys must
        // not spin the reel forever with a window open and nothing happening;
        // past this the demo simply stops driving and the screen is the
        // player's, which is a visible failure rather than a hang.
        if (++guard > 4000) {
            active = false;
            return render::Key::None;
        }
        switch (flow.step()) {
            case render::CreationStep::Origin:
                // Row 1 is ANSWER FOR YOURSELF -- the quiz door.
                if (flow.originCursor() != 1) {
                    wait = 26;
                    return render::Key::Down;
                }
                wait = 60;
                return render::Key::Enter;
            case render::CreationStep::Quiz: {
                const bool slow = answered < 3;
                if (slow && flow.choiceCursor() != answered % 3) {
                    wait = 22;
                    return render::Key::Down;
                }
                ++answered;
                wait = slow ? 78 : 26;
                return render::Key::Enter;
            }
            case render::CreationStep::Calling:
            case render::CreationStep::Background:
                wait = 22;
                return render::Key::Enter;
            case render::CreationStep::Customize:
                break;
        }
        if (flow.editingName()) {
            if (nameAt < std::strlen(kName)) {
                const char c = kName[nameAt++];
                wait = 7;
                return static_cast<render::Key>(
                    static_cast<int>(render::Key::A) + (c - 'A'));
            }
            nameDone = true;
            wait = 40;
            return render::Key::Enter;
        }
        if (!nameDone) {
            // The cursor lands on NAME (row 0) when the sheet opens, and ENTER
            // on that row is what opens text entry.
            wait = 34;
            return render::Key::Enter;
        }
        if (!onBegin) {
            // BEGIN IS THE LAST ROW and the cursor wraps, so ONE press of UP
            // from NAME reaches it whatever the sheet's length -- which is the
            // whole reason this is a state machine and not a count of Downs.
            onBegin = true;
            wait = 46;
            return render::Key::Up;
        }
        wait = 30;
        return render::Key::Enter;
    }
};

/// A pointer at (px, py) in FRAMEBUFFER pixels. `click` false is a hover, which
/// moves the cursor and nothing else.
bool creation_pointer(render::CreationFlow& flow, int frameWidth, int frameHeight, int px, int py,
                      bool click) {
    const render::CreationHit hit = render::creationHitTest(flow, frameWidth, frameHeight, px, py);
    switch (hit.zone) {
        case render::CreationHit::Zone::Row:
            // HOVER MIRRORS THE CURSOR. Same setter a d-pad press reaches, so
            // there is exactly one live row on this screen and every device
            // agrees which one it is.
            if (flow.oskOpen()) {
                // The grid reads ACROSS and is now DRAWN across, so the
                // hit-test answers in the cursor's own order and the transpose
                // that used to live here is gone. See CreationFlow::oskCells().
                flow.setOskCursor(hit.index);
                if (click) {
                    flow.commitOsk();
                }
                return true;
            }
            switch (flow.step()) {
                case render::CreationStep::Origin:
                    flow.setOriginCursor(hit.index);
                    break;
                case render::CreationStep::Customize:
                    flow.setCustomizeCursor(hit.index);
                    break;
                default:
                    flow.setChoiceCursor(hit.index);
                    break;
            }
            if (click) {
                return creation_input(flow, render::Key::Enter);
            }
            return true;
        case render::CreationHit::Zone::Commit:
            // The commit verb at the foot of the detail pane is a real target:
            // it says what ENTER does, so clicking it does that.
            if (click) {
                return creation_input(flow, render::Key::Enter);
            }
            return true;
        case render::CreationHit::Zone::Back:
            if (click) {
                return creation_input(flow, render::Key::Escape);
            }
            return true;
        case render::CreationHit::Zone::None:
        default:
            return true;
    }
}

// ---------------------------------------------------------------------------
// #80. THE ORIGIN-SELECT/CUSTOMIZE FLOW, CAPTURED WITH NO WINDOW.
// ---------------------------------------------------------------------------
//
// NO WINDOW AND NO WORLD, the same reason --screenshot never needed one:
// CreationFlow draws through render::drawDialogue, which is a pure function
// of a framebuffer and a state struct.
//
// "customize" LANDS ON CUSTOM AND SPENDS A REAL FEW POINTS: their sheet is
// fixed and adjustCustomizeRow is a no-op on it by design (see
// creation.hpp), so capturing CUSTOM is the only way to photograph
// LEFT/RIGHT actually having done something. Three skill rows get one point
// each so the picture shows real numbers -- PRIMARY 3/3 or similar -- rather
// than every row reading "Undesignated", the same reason --character's own
// capture flag spends nothing on an empty sheet.
//
// "devin"/"gabri" LAND ON THEIR OWN FIXED SHEET, unspent -- there is nothing
// to spend. Added while verifying this round's LOOK row: gabri.json's
// appearanceType is "priest_of_the_flame", whose label ("PRIEST OF THE
// FLAME") is nineteen glyphs, longer than any CUSTOM skill row this pass
// checked by looking, and headless was the only way to actually see whether
// it clips the identical way the skill rows did before this round's fix.
int run_creation_capture(const Options& options) {
    render::CreationFlow flow(granadad::content::contentDir());
    // TASK #92: answers one quiz question by its AUTHORED index (0=A, 1=B,
    // 2=C -- the raws author the axes in order) through the same public
    // calls a keyboard reaches, translating through the display shuffle the
    // screen itself applies. Pure walking, no back door.
    const auto answerQuiz = [&flow](int authored) {
        const std::array<int, 3> order =
            render::quizDisplayOrder(static_cast<int>(flow.quizAnswers().size()));
        for (int pos = 0; pos < 3; ++pos) {
            if (order[static_cast<std::size_t>(pos)] == authored) {
                while (flow.choiceCursor() != pos) {
                    flow.moveChoiceCursor(1);
                }
                flow.chooseChoice();
                return;
            }
        }
    };
    // Walks the calling door to NETTER (roster index 3 in the raws' own
    // authored order) -- a mixed-axis sheet, so the preview and the review
    // both show something less symmetric than the first row would.
    const auto takeNetter = [&flow] {
        flow.chooseOrigin();  // cursor 0 = TAKE A CALLING
        for (int i = 0; i < 3; ++i) {
            flow.moveChoiceCursor(1);
        }
        flow.chooseChoice();
    };
    // #93. THE PARITY WALKS. Three devices, ONE journey, and the proof is that
    // the three frames come out byte-identical.
    //
    // A claim that a screen is drivable by controller, mouse and keyboard is
    // worth nothing without a frame behind it, and a real SDL window cannot be
    // driven inside a headless gate. So each of these replays the SAME journey
    // -- open the quiz door, answer four questions, hover the second answer --
    // through creation_input()/creation_pointer(), which is the EXACT code the
    // SDL loop calls; the only thing skipped is SDL's event pump handing them
    // the key. Different keys, different device, same destination.
    //
    // The mouse walk finds its own pixels by asking the screen's own hit-test
    // where a row is, so it is clicking what a player would click and not a
    // coordinate typed in by hand.
    const auto pixelOfRow = [](const render::CreationFlow& flow, int w, int h, int row, int* px,
                               int* py) {
        const render::CreationPage page = flow.page();
        const render::CreationLayout layout = render::creationLayout(page, w, h);
        if (!layout.usable) {
            return false;
        }
        const int stepY = std::max(1, layout.metric.cellH() / 2);
        const int stepX = std::max(1, layout.metric.cellW());
        for (int y = layout.listRect.y; y < layout.listRect.bottom(); y += stepY) {
            for (int x = layout.listRect.x; x < layout.listRect.right(); x += stepX) {
                const render::CreationHit hit = render::creationPageHitTest(page, w, h, x, y);
                if (hit.zone == render::CreationHit::Zone::Row && hit.index == row) {
                    *px = x;
                    *py = y;
                    return true;
                }
            }
        }
        return false;
    };
    if (options.creationStep == "input-keyboard" || options.creationStep == "input-pad" ||
        options.creationStep == "input-mouse") {
        const int w = std::max(64, options.smoke.session.width);
        const int h = std::max(64, options.smoke.session.height);
        if (options.creationStep == "input-mouse") {
            const auto press = [&](int row) {
                int px = 0;
                int py = 0;
                if (!pixelOfRow(flow, w, h, row, &px, &py)) {
                    std::printf("granadad: no pixel found for row %d\n", row);
                    return;
                }
                std::printf("granadad: pointer row %d at (%d,%d)\n", row, px, py);
                (void)creation_pointer(flow, w, h, px, py, false);
                (void)creation_pointer(flow, w, h, px, py, true);
            };
            press(1);  // ANSWER FOR YOURSELF
            for (int i = 0; i < 4; ++i) {
                press(0);
            }
            int px = 0;
            int py = 0;
            if (pixelOfRow(flow, w, h, 1, &px, &py)) {
                std::printf("granadad: hover row 1 at (%d,%d)\n", px, py);
                (void)creation_pointer(flow, w, h, px, py, false);
            }
        } else {
            const bool viaPad = options.creationStep == "input-pad";
            const render::Key down = viaPad ? render::Key::PadDown : render::Key::Down;
            const render::Key confirm = viaPad ? render::Key::PadSouth : render::Key::Enter;
            (void)creation_input(flow, down);
            (void)creation_input(flow, confirm);
            for (int i = 0; i < 4; ++i) {
                (void)creation_input(flow, confirm);
            }
            (void)creation_input(flow, down);
        }
    } else if (options.creationStep == "customize") {
        flow.moveOriginCursor(2);  // CUSTOM
        flow.chooseOrigin();
        for (int i = 0; i < 3; ++i) {
            flow.moveCustomizeCursor(1);
            flow.adjustCustomizeRow(1);
        }
    } else if (options.creationStep == "osk") {
        // THE ON-SCREEN KEYBOARD, WALKED BY THE PAD'S OWN CALLS. Nothing here
        // reaches past the public surface a d-pad reaches: the cursor is moved
        // and committed, never the name assigned.
        flow.moveOriginCursor(2);  // CUSTOM
        flow.chooseOrigin();
        flow.chooseCustomizeRow();  // the cursor lands on NAME; this opens entry
        flow.openOsk();
        // Rub out the template's suggested name, then spell four letters.
        for (int i = 0; i < static_cast<int>(render::kMaxNameLength); ++i) {
            flow.setOskCursor(29);  // the rub-out
            flow.commitOsk();
        }
        for (const int cell : {12, 0, 17, 11}) {  // M A R L
            flow.setOskCursor(cell);
            flow.commitOsk();
        }
        flow.setOskCursor(4);  // the cursor rests on E, one press from MARLE
    } else if (options.creationStep == "devin") {
        flow.moveOriginCursor(4);
        flow.chooseOrigin();
    } else if (options.creationStep == "gabri") {
        flow.moveOriginCursor(3);
        flow.chooseOrigin();
    } else if (options.creationStep == "calling") {
        // The roster, hovering NETTER so the top band speaks a one-liner and
        // the centre shows a real mixed sheet.
        flow.chooseOrigin();
        for (int i = 0; i < 3; ++i) {
            flow.moveChoiceCursor(1);
        }
    } else if (options.creationStep == "quiz") {
        // Four answers in (A, A, B, C), question five on screen, meters at a
        // genuinely mid-quiz 2/1/1 -- and the hovered answer is row two, so
        // the centre text and the highlight both photograph off the default.
        flow.moveOriginCursor(1);  // ANSWER FOR YOURSELF
        flow.chooseOrigin();
        answerQuiz(0);
        answerQuiz(0);
        answerQuiz(1);
        answerQuiz(2);
        flow.moveChoiceCursor(1);
    } else if (options.creationStep == "verdict") {
        // All ten: 5 A, 3 B, 2 C -- A dominant short of pure, B over C, the
        // tally table's DECKHAND row -- so the card photographs the
        // accept-or-decline choice over a with-secondary verdict.
        flow.moveOriginCursor(1);
        flow.chooseOrigin();
        for (int i = 0; i < 5; ++i) {
            answerQuiz(0);
        }
        for (int i = 0; i < 3; ++i) {
            answerQuiz(1);
        }
        for (int i = 0; i < 2; ++i) {
            answerQuiz(2);
        }
    } else if (options.creationStep == "background") {
        // The calling door into the biography: three questions answered, B4
        // on screen mid-past.
        takeNetter();
        for (int i = 0; i < 3; ++i) {
            flow.chooseChoice();  // answer (a) of each -- authored order
        }
    } else if (options.creationStep == "review") {
        // THE CONVERGENCE: a taken calling, all twelve questions answered,
        // landing on the same customize/review screen every other door ends
        // on -- pre-designated sheet, dagger readout in the status line.
        takeNetter();
        for (int i = 0; i < 12; ++i) {
            flow.chooseChoice();
        }
    } else if (options.creationStep != "origin") {
        std::printf(
            "granadad: --creation wants origin, calling, quiz, verdict, background, "
            "osk, "
            "review, customize, devin or gabri\n");
        return 2;
    }

    const int width = std::max(64, options.smoke.session.width);
    const int height = std::max(64, options.smoke.session.height);
    render::Framebuffer frame(width, height);
    render::drawCreation(frame, flow);

    std::printf("granadad: creation flow captured, step=%s\n", options.creationStep.c_str());
    if (!options.smoke.screenshot.empty()) {
        const render::Framebuffer output = render::upscaleNearest(frame, options.windowScale);
        if (!render::writePng(output, options.smoke.screenshot)) {
            std::printf("granadad: FAILED to write %s\n",
                        options.smoke.screenshot.string().c_str());
            return 1;
        }
        std::printf("granadad: wrote %s\n", options.smoke.screenshot.string().c_str());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 3D BUILD: THE WINDOW IS RAYLIB'S; THE LOOP STILL SPEAKS SDL EVENTS
// ---------------------------------------------------------------------------
//
// WHAT CHANGED. raylib (granadad-render3d-rl) owns the window and the GL
// context now, because the 3D pass has to be drawn INTO the window and SDL's
// 2D renderer cannot host it. SDL is therefore never initialised with VIDEO
// in this file any more -- it keeps the audio device and the gamepad, both of
// which work with SDL_INIT_GAMEPAD alone (the gamepad selftest has always
// done exactly that).
//
// WHAT DID NOT CHANGE, AND WHY. Every line under the event loops below reads
// an SDL_Event: the scancode table, route_menu_key, pressed()/released(), the
// pad parity pass, the virtual-pad harness that pushes its own button edges
// with SDL_PushEvent. That code was proven page by page and frame by frame,
// and rewriting it onto a second input vocabulary in the toolchain lane would
// be a week of re-proving for no new feature. So the bridge does the one
// translation that exists anyway -- raylib reports keys as PHYSICAL keys in
// GLFW's US-layout tokens, the adapter turns those into USB HID usage ids,
// and a HID usage id IS an SDL scancode -- and hands the loop what it already
// speaks: SDL_EVENT_KEY_DOWN/UP with a scancode, MOUSE_MOTION with relative
// counts, MOUSE_BUTTON_DOWN/UP and MOUSE_WHEEL with the pointer already in
// FRAMEBUFFER pixels (what SDL_ConvertEventToRenderCoordinates used to do),
// and QUIT. SDL_PushEvent needs no video subsystem; the queue is the event
// subsystem's, which SDL_INIT_GAMEPAD implies.
//
// The two things the loop used to POLL rather than receive -- the held-key
// array (SDL_GetKeyboardState) and the mouse button mask (SDL_GetMouseState)
// -- are kept here off the same edges, because without VIDEO those two calls
// answer for a keyboard SDL no longer reads.
struct VideoBridge {
    std::array<bool, SDL_SCANCODE_COUNT> held{};
    Uint32 mouseMask = 0;

    /// Reads a frame of raylib input and pushes it as SDL events. Once per
    /// frame, right before the SDL_PollEvent loop drains them.
    void pump(render3d::Backend& video, int frameWidth, int frameHeight) {
        const render3d::InputFrame in = video.poll();
        const render3d::OverlayPlacement placement =
            video.overlayPlacement(frameWidth, frameHeight);
        const Uint64 now = SDL_GetTicksNS();

        if (in.closeRequested) {
            SDL_Event out{};
            out.type = SDL_EVENT_QUIT;
            out.common.timestamp = now;
            (void)SDL_PushEvent(&out);
        }
        for (const render3d::KeyEdge& key : in.keys) {
            if (key.hid >= held.size()) {
                continue;
            }
            if (!key.repeat) {
                held[key.hid] = key.down;
            }
            g_video_shift_held = held[SDL_SCANCODE_LSHIFT] || held[SDL_SCANCODE_RSHIFT];
            SDL_Event out{};
            out.type = key.down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            out.key.timestamp = now;
            out.key.scancode = static_cast<SDL_Scancode>(key.hid);
            out.key.down = key.down;
            out.key.repeat = key.repeat;
            out.key.mod = static_cast<SDL_Keymod>(g_video_shift_held ? SDL_KMOD_SHIFT : SDL_KMOD_NONE);
            (void)SDL_PushEvent(&out);
        }
        for (const render3d::MouseButtonEdge& button : in.buttons) {
            const Uint32 bit = SDL_BUTTON_MASK(button.button);
            if (button.down) {
                mouseMask |= bit;
            } else {
                mouseMask &= ~bit;
            }
            SDL_Event out{};
            out.type = button.down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            out.button.timestamp = now;
            out.button.button = button.button;
            out.button.down = button.down;
            out.button.clicks = 1;
            out.button.x = static_cast<float>(placement.toFrameX(button.x));
            out.button.y = static_cast<float>(placement.toFrameY(button.y));
            (void)SDL_PushEvent(&out);
        }
        if (in.mouseDeltaX != 0.0F || in.mouseDeltaY != 0.0F) {
            SDL_Event out{};
            out.type = SDL_EVENT_MOUSE_MOTION;
            out.motion.timestamp = now;
            out.motion.state = mouseMask;
            out.motion.x = static_cast<float>(placement.toFrameX(in.mouseX));
            out.motion.y = static_cast<float>(placement.toFrameY(in.mouseY));
            // Raw window counts, exactly what SDL's relative mode delivered:
            // MouseSettings::yawFor turns them into BAM downstream.
            out.motion.xrel = in.mouseDeltaX;
            out.motion.yrel = in.mouseDeltaY;
            (void)SDL_PushEvent(&out);
        }
        if (in.wheelY != 0.0F) {
            SDL_Event out{};
            out.type = SDL_EVENT_MOUSE_WHEEL;
            out.wheel.timestamp = now;
            out.wheel.x = 0.0F;
            out.wheel.y = in.wheelY;
            (void)SDL_PushEvent(&out);
        }
    }
};

/// 3D BUILD. THE DOCKS, PLACED FOR A SESSION: the session's own tiles meshed
/// by chunk (built once, on the first frame), lit from its own clock, its
/// baked lamps and the Gull's own hearth and candles, the sky dome on the
/// eye, and the camera from the session's own Camera -- Session::camera(),
/// the body's Q8 position and BAM facing through Camera::fromBody, combat
/// impulses included. The rig borrows the session's tiles, atlas and lamp
/// field, so it must not outlive the session it was first refreshed with
/// -- it never does: one rig per run_client / shutter.
/// A LANE. Where the skinned rigs live when the licensed export has been
/// run: content/art/lot-3d/characters (gitignored; absent on a fresh
/// checkout and in the docker gate, and the adapter falls back to the
/// placeholder figures per rig without a word).
[[nodiscard]] std::string rig_model_dir() {
    return (granadad::content::contentDir() / "art" / "lot-3d" / "characters").string();
}

/// V LANE. Where the static weapon exports live (the mace and the dagger
/// hung on the viewmodel's Hand_R when they exist): content/art/lot-3d/
/// static, same licence, same gitignore, same silent fallback.
[[nodiscard]] std::string weapon_model_dir() {
    return (granadad::content::contentDir() / "art" / "lot-3d" / "static").string();
}

struct SceneRig {
    render3d::SceneDescription scene;
    std::unique_ptr<render3d::WorldScene> world;

    void refresh(const render::Session& session, float aspect) {
        if (world == nullptr) {
            world = std::make_unique<render3d::WorldScene>(session.tiles(), session.atlas(),
                                                           &session.renderer().glow());
        }
        render3d::WorldSceneParams params;
        params.timeOfDaySeconds = session.timeOfDay();
        params.dynamicLamps = session.tavernLights();
        world->refresh(scene, session.camera(), aspect, params);
        // A LANE: the people. Sixteen placeholder rigs put once (the adapter
        // swaps in the glb by name where it has one), and the instances
        // rewritten every frame off the roster with wardSprites' own slide.
        render3d::putActorRigs(scene);
        scene.actors = render3d::actorInstances(session, session.camera());
        // V LANE: the player's own hands. The placeholder parts put once
        // (the adapter swaps in the arms glb by name where it has one), the
        // pose rewritten every frame off Session::viewmodel() -- the machine
        // step() runs beside the sim, so a scripted swing photographs the
        // same frame on every machine.
        render3d::putViewmodelMeshes(scene);
        scene.viewmodel = render3d::viewmodelInstance(session);
    }
};

/// 3D BUILD. ONE FRAME THROUGH THE BACKEND: clear to the sky, the 3D pass
/// when --3d is on, the software frame as the overlay, present -- and, when
/// asked, the finished frame read back before the swap.
render3d::SceneStats present_frame(render3d::Backend& video, const Options& options,
                                   SceneRig& rig, const render::Session& session,
                                   const render::Framebuffer& overlay,
                                   render::Framebuffer* capture) {
    const float aspect = static_cast<float>(std::max(1, video.width())) /
                         static_cast<float>(std::max(1, video.height()));
    rig.refresh(session, aspect);
    render3d::SceneStats stats;
    if (options.video3d) {
        video.beginFrame(rig.scene.clearColour);
        stats = video.drawScene(rig.scene);
    } else {
        video.beginFrame(render3d::Rgba8{0, 0, 0, 255});
    }
    video.drawOverlay(overlay);
    video.endFrame(capture);
    return stats;
}

/// 3D BUILD. THE SMOKE PATH'S SHUTTER: runSmoke drove the session and drew
/// its software frame; this composites that frame through the backend the
/// window uses and writes what the window would have shown. On the shipped
/// build that opens a real window for the length of one frame -- which is
/// exactly what scripts/verify-windows.ps1 wants proved on the host -- and on
/// the headless build it opens none.
[[nodiscard]] bool shutter_through_backend(const Options& options, const render::Session& session,
                                           const render::Framebuffer& software) {
    render3d::BackendConfig config;
    config.width = software.width();
    config.height = software.height();
    config.windowScale = options.smoke.captureScale;
    config.resizable = false;
    config.vsync = false;
    config.modelDir = rig_model_dir();
    config.weaponDir = weapon_model_dir();
    std::unique_ptr<render3d::Backend> video = render3d::Backend::open(config);
    if (video == nullptr) {
        std::printf("granadad: the 3D backend could not open for the shutter\n");
        return false;
    }
    render::Framebuffer overlay = software;
    if (options.video3d) {
        (void)session.drawFrame(overlay, render::Session::FramePasses{.world = false});
    }
    SceneRig rig;
    render::Framebuffer shot(1, 1);
    const render3d::SceneStats stats =
        present_frame(*video, options, rig, session, overlay, &shot);
    // The headless build's frame is the framebuffer's own size; the window
    // build already presented at the capture scale.
    const bool needsUpscale =
        shot.width() == software.width() && options.smoke.captureScale > 1;
    const render::Framebuffer output =
        needsUpscale ? render::upscaleNearest(shot, options.smoke.captureScale) : shot;
    const bool ok = render::writePng(output, options.smoke.screenshot);
    // THE SCENE HASH IS PRINTED ON PURPOSE: it is the whole SceneDescription
    // (chunks, sky, the crowd, the hands, the camera) folded to one number,
    // so two separate processes of the same scripted drive prove the 3D
    // frame's INPUT twin-runs identical across processes -- the frame's
    // bytes are never hashed on a GPU, the description is.
    std::printf("granadad: 3d shutter -- %s backend, %dx%d, %zu instance(s), %zu triangle(s), "
                "%zu bod%s (%zu skinned, %zu rig file(s)), hands %s/%s %s %zu part(s)%s%s, "
                "scene 0x%016llX%s\n",
                video->kind() == render3d::VideoKind::Software ? "rlsw" : "gpu", output.width(),
                output.height(), stats.instancesDrawn, stats.trianglesDrawn, stats.actorsDrawn,
                stats.actorsDrawn == 1 ? "y" : "ies", stats.actorsSkinned, stats.rigModelsLoaded,
                render3d::viewmodelKindName(
                    static_cast<render3d::ViewmodelKind>(rig.scene.viewmodel.kind))
                    .data(),
                render::viewmodelStateName(rig.scene.viewmodel.state).data(),
                session.viewmodel().handsUp ? "up" : "down", stats.viewmodelPartsDrawn,
                stats.viewmodelSkinned ? " (skinned glb)" : "",
                stats.viewmodelWeaponLoaded ? " (weapon file)" : "",
                static_cast<unsigned long long>(render3d::sceneHash(rig.scene)),
                options.video3d ? " (the Docks in 3D under the HUD)"
                                : " (software world in the overlay: --2d)");
    return ok;
}

// ---------------------------------------------------------------------------
// #80: the origin-select/customize flow, in its own small window
// ---------------------------------------------------------------------------
//
// ITS OWN LOOP RATHER THAN A RESTRUCTURED run_client(). No Session and no
// world exist yet at this point in the boot sequence -- this screen has to
// run BEFORE either -- and the alternative (hoisting run_client's own setup
// above the Session construction it currently follows) touches code every
// other page in this build was proven against.
//
// 3D BUILD: ONE WINDOW FOR THE WHOLE LAUNCH. This used to open its own SDL
// window and tear it down before run_client opened the world's; the raylib
// window is opened once in main() and handed to both, so the creation->world
// cut is a veil over one window instead of a window swap. The pad still gets
// its own SDL_Init/QuitSubSystem pair here, exactly as before, so the virtual
// pad harness sees the same lifetime it always did.
//
// Returns an UNCONFIRMED CreationResult if the player closed the window --
// main() treats that exactly like closing the game, not like starting one.
render::CreationResult run_creation_window(const Options& options, render3d::Backend& video) {
    render::CreationFlow flow(granadad::content::contentDir());

    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::printf("SDL_Init (creation) failed: %s\n", SDL_GetError());
        return {};
    }
    const int width = options.smoke.session.width;
    const int height = options.smoke.session.height;
    VideoBridge bridge;
    // A pointer, not mouselook: this screen is lists and a name field.
    video.setRelativeMouse(false);

    // #93. A PAD THAT IS PLUGGED IN SHOULD JUST WORK, on the FIRST screen of
    // the game and not only after it. Opened here and hot-plugged below, the
    // same way run_client() already does it for the world.
    SDL_Gamepad* pad = nullptr;
    {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids != nullptr) {
            if (count > 0) {
                pad = SDL_OpenGamepad(ids[0]);
            }
            SDL_free(ids);
        }
    }
    // THE STICK IS A CURSOR, NOT A CAMERA, on this screen. An analog axis has
    // no press event, so it is latched: one step when it crosses the deadzone,
    // and nothing more until it comes back under. Without the latch a leaned
    // stick would scroll a list at the frame rate, which is unusable.
    constexpr Sint16 kStickOn = 18000;
    constexpr Sint16 kStickOff = 9000;
    bool stickVertical = false;
    bool stickHorizontal = false;

    render::Framebuffer frame(width, height);
    bool cancelled = false;

    // UI-EA-SPEC sec. 3 rule 3: BOOT IS A WORLD SEAM AND WEARS THE VEIL. The
    // first dozen frames of the first window ease up from black instead of
    // slamming the door screen on -- the same vocabulary the travel dip and
    // the creation->world cut below speak, windowed-only so no headless
    // capture is touched. Counted in frames of this window's own loop
    // (vsync'd; the reel path is already clamped to 60), not steps: there is
    // no step pump on this screen and nothing here is captured.
    constexpr int kCreationVeilFrames = 12;
    int bootVeilFrame = kCreationVeilFrames;

    // THE PARITY PASS. THE CHARACTER SCREEN IS THE FIRST SURFACE A PLAYER
    // TOUCHES, and a windowed launch opens it before the world -- so a pad
    // script aimed at the world would never reach the world without one aimed
    // at this. Its own driver, because this window runs its own SDL_Init/
    // SDL_Quit pair and a virtual joystick does not survive that. See
    // PadDriver's header.
    PadDriver creationPad;
    (void)creationPad.attach(options.padCreation, options.padShotDir);

    // THE DEMO'S FIRST BEAT. See CreationReel's header. When it is not asked
    // for, `active` is false and next() returns None on every frame, so this
    // window is byte-for-byte the shipped one.
    CreationReel reel;
    reel.active = options.demo;
    std::string lastShot;
    int shotIn = -1;
    Uint64 reelLastNs = 0;

    while (!flow.done() && !cancelled) {
        SDL_Event event;
        // 3D BUILD: raylib's keys and pointer onto SDL's queue, ahead of the
        // drain -- see VideoBridge.
        bridge.pump(video, width, height);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                cancelled = true;
                continue;
            }
            switch (event.type) {
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (pad == nullptr) {
                        pad = SDL_OpenGamepad(event.gdevice.which);
                    }
                    continue;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (pad != nullptr &&
                        SDL_GetGamepadID(pad) == event.gdevice.which) {
                        SDL_CloseGamepad(pad);
                        pad = nullptr;
                    }
                    continue;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                    const render::Key key = key_of_pad_button(event.gbutton.button);
                    // SHIP NOTE MOVE 3: the feet re-word for the pad before
                    // this press is even routed -- see CreationFlow::
                    // promptDevice().
                    flow.noteInputDevice(render::InputDevice::Pad);
                    if (key != render::Key::None && !creation_input(flow, key)) {
                        cancelled = true;
                    }
                    continue;
                }
                case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                    const Sint16 value = event.gaxis.value;
                    const Sint16 magnitude =
                        static_cast<Sint16>(value < 0 ? -std::max<int>(value, -32767) : value);
                    if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                        if (!stickVertical && magnitude >= kStickOn) {
                            stickVertical = true;
                            // A stick push past the on-threshold is a
                            // discrete, deliberate list step -- a press.
                            flow.noteInputDevice(render::InputDevice::Pad);
                            (void)creation_input(flow, value < 0 ? render::Key::Up
                                                                 : render::Key::Down);
                        } else if (stickVertical && magnitude <= kStickOff) {
                            stickVertical = false;
                        }
                    } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                        if (!stickHorizontal && magnitude >= kStickOn) {
                            stickHorizontal = true;
                            flow.noteInputDevice(render::InputDevice::Pad);
                            (void)creation_input(flow, value < 0 ? render::Key::Left
                                                                 : render::Key::Right);
                        } else if (stickHorizontal && magnitude <= kStickOff) {
                            stickHorizontal = false;
                        }
                    }
                    continue;
                }
                case SDL_EVENT_MOUSE_MOTION:
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    // ALREADY IN FRAMEBUFFER PIXELS. The window presents the
                    // frame at an integer scale with a letterbox, so a window
                    // coordinate is not a frame coordinate; VideoBridge
                    // converts through the backend's own placement before the
                    // event is queued (what SDL_ConvertEventToRenderCoordinates
                    // did when SDL owned the window).
                    const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    if (click) {
                        // A click is a press; bare motion deliberately is
                        // not -- Session::promptDevice() states the rule.
                        flow.noteInputDevice(render::InputDevice::KeyboardMouse);
                    }
                    const float fx = click ? event.button.x : event.motion.x;
                    const float fy = click ? event.button.y : event.motion.y;
                    if (click && event.button.button == SDL_BUTTON_RIGHT) {
                        // RIGHT-CLICK IS BACK, everywhere. The one gesture a
                        // mouse has that nothing else on this screen wants.
                        if (!creation_input(flow, render::Key::Escape)) {
                            cancelled = true;
                        }
                        continue;
                    }
                    if (click && event.button.button != SDL_BUTTON_LEFT) {
                        continue;
                    }
                    if (!creation_pointer(flow, width, height, static_cast<int>(fx),
                                          static_cast<int>(fy), click)) {
                        cancelled = true;
                    }
                    continue;
                }
                default:
                    break;
            }
            if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
                continue;
            }
            flow.noteInputKey(key_of_scancode(event.key.scancode));
            if (!creation_input(flow, key_of_scancode(event.key.scancode))) {
                cancelled = true;
            }
        }

        if (reel.active) {
            const render::Key key = reel.next(flow);
            if (key != render::Key::None && !creation_input(flow, key)) {
                cancelled = true;
            }
        }

        flow.advance();
        render::drawCreation(frame, flow);
        // THE DEMO'S OWN SHUTTER on this screen, taken a beat AFTER the state
        // it names arrives -- the answer-commit pulse and the panel fades are
        // steps-based, so a frame grabbed on the instant of a change is a
        // photograph of something half-open.
        if (reel.active && !options.demoShotDir.empty()) {
            std::string want;
            switch (flow.step()) {
                case render::CreationStep::Origin:
                    want = "creation-origin";
                    break;
                case render::CreationStep::Quiz:
                    want = reel.answered >= 3 ? "creation-quiz-answered" : "creation-quiz";
                    break;
                case render::CreationStep::Customize:
                    want = reel.nameDone ? "creation-sheet" : "creation-name";
                    break;
                default:
                    break;
            }
            if (!want.empty() && want != lastShot) {
                lastShot = want;
                shotIn = 40;
            }
            if (shotIn == 0) {
                std::error_code ec;
                std::filesystem::create_directories(options.demoShotDir, ec);
                (void)render::writePng(frame,
                                       (options.demoShotDir / (lastShot + ".png")).string());
            }
            if (shotIn >= 0) {
                --shotIn;
            }
        }
        // The boot veil, applied AFTER the reel's shutter reads the frame --
        // committed reel PNGs are veil-free by construction (and the shutter
        // waits 40 frames regardless).
        if (bootVeilFrame > 0) {
            --bootVeilFrame;
            frame.fillRect(0, 0, frame.width(), frame.height(), render::Rgb{0.0F, 0.0F, 0.0F},
                           static_cast<float>(bootVeilFrame) /
                               static_cast<float>(kCreationVeilFrames));
        }
        // No 3D pass on this screen: the sheet is the whole frame, opaque.
        video.beginFrame(render3d::Rgba8{0, 0, 0, 255});
        video.drawOverlay(frame);
        video.endFrame();
        if (reel.active) {
            // The same 60 Hz floor the world loop keeps while the demo is up,
            // and for the same reason: the reel's pacing is counted in frames,
            // so a 144 Hz panel would otherwise play the character screen at
            // two and a half times speed.
            constexpr Uint64 kBudgetNs = 1'000'000'000ULL / 60ULL;
            const Uint64 nowNs = SDL_GetTicksNS();
            reelLastNs = reelLastNs == 0 || nowNs > reelLastNs + kBudgetNs
                             ? nowNs + kBudgetNs
                             : reelLastNs + kBudgetNs;
            for (Uint64 t = SDL_GetTicksNS(); t < reelLastNs; t = SDL_GetTicksNS()) {
                SDL_DelayNS(reelLastNs - t);
            }
        }
        if (!creationPad.advance(frame)) {
            // THE SCRIPT ENDING IS NOT A CANCEL. A pad script that walked the
            // flow to its confirm has already set flow.done(); one that has not
            // leaves this window the way closing it does.
            cancelled = !flow.done();
            break;
        }
    }

    // UI-EA-SPEC sec. 3 rule 3: CREATION->WORLD IS DRESSED. Black falls over
    // the finished sheet BEFORE the world's session is built -- so the moment
    // that follows reads as one deliberate cut to black, not as the app
    // restarting -- and run_client dresses the other side, easing the world
    // up from black through the travel dip's own machinery
    // (Session::dressInstantCut). Windowed-only, played only on a COMPLETED
    // flow: a cancel (the armed door quit) still leaves plainly.
    if (flow.done() && !cancelled) {
        for (int i = 1; i <= kCreationVeilFrames; ++i) {
            render::drawCreation(frame, flow);
            frame.fillRect(0, 0, frame.width(), frame.height(), render::Rgb{0.0F, 0.0F, 0.0F},
                           static_cast<float>(i) / static_cast<float>(kCreationVeilFrames));
            video.beginFrame(render3d::Rgba8{0, 0, 0, 255});
            video.drawOverlay(frame);
            video.endFrame();
            // The input this frame brought in is drained, not acted on: the
            // flow is done and nothing here reads it, but a queue left full
            // would hand run_client a stale click.
            bridge.pump(video, width, height);
            SDL_Event drained;
            while (SDL_PollEvent(&drained)) {
            }
        }
    }

    const render::CreationResult result = flow.done() ? flow.result() : render::CreationResult{};

    creationPad.detach();
    if (pad != nullptr) {
        SDL_CloseGamepad(pad);
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    return result;
}

// ---------------------------------------------------------------------------
// the window
// ---------------------------------------------------------------------------

int run_client(const Options& options, const render::CreationResult& chosen,
               render3d::Backend& video) {
    // A NEW GAME OPENS ON THE CASE, AND AT DAWN.
    //
    // The window path -- and only the window path. A scripted capture and two
    // hundred test cases build a Session too and most of them want a frame of
    // the world rather than a frame of a menu over it; see
    // SessionConfig::openingPage. And the hour: the gazetteer has the Wielder
    // arriving at the Docks at dawn with a passport and white garb, which is
    // also simply the right hour to hand somebody a district -- the ward is at
    // work, the Weighhouse is open and the Tarwalk is not a black corridor.
    // `--time` still wins, because an hour the player asked for is an hour they
    // meant.
    render::SessionConfig start = options.smoke.session;
    if (options.caseWatch) {
        // CASE WATCH: THE HARNESS'S OWN SESSION, not the client's. The watch
        // replays the recorded --case drive onto this session, and the replay
        // is only the drive's twin if the two were born identical -- so no
        // opening page, no courier config (the tape hails on its own, exactly
        // as runCaseLine does), and the scripted line's own hour unless the
        // caller named one. Everything below that would touch simulation
        // state (the chargen sheet, the biography) is skipped the same way.
        if (!start.timeOfDayGiven) {
            const int scriptedHour = render::scriptedStartHour(options.smoke);
            if (scriptedHour >= 0) {
                start.timeOfDay = scriptedHour * 3600;
            }
        }
    } else {
        start.openingPage = true;
        // COURIER CASE (lane: case). The windowed game gets the courier; the
        // demo does NOT -- its route and its committed frames predate the
        // sheet, and a hail landing mid-reel would move street frames the ship
        // note proves byte-identical. Same off-by-default-on-in-the-client
        // pattern as openingPage, one line up.
        start.courier = !options.demo;
        if (!start.timeOfDayGiven) {
            start.timeOfDay = 8 * 3600;
        }
    }
    render::Session session(start);
    if (!session.body().spawnedLegally()) {
        std::printf("granadad: spawn tile is not standable -- check --spawn\n");
        return 1;
    }

    // UI-EA-SPEC sec. 3 rule 3: THE WORLD EASES UP FROM BLACK AT BOOT -- the
    // other half of the dressed creation->world cut (the creation window let
    // black fall before its teardown; this window rises from it), through
    // the travel dip's own machinery so boot, travel and the case-watch seam
    // all speak one veil. NOT under --demo or --case-watch: their committed
    // frames and byte-stable replays predate the veil, and a boot dip would
    // move every early frame of both. Their guards stay green untouched.
    if (!options.demo && !options.caseWatch) {
        session.dressInstantCut();
    }

    // #84. THE SEAM #80 NAMED, CLOSED. run_creation_window() built a real
    // point-bought Chargen sheet or handed back DEVIN's/GABRI's real fixed
    // one, and until now `chosen` stopped being read the moment this
    // function's own signature ended -- SEEN, ABOVE, AS A COMMENT NAMING
    // ITSELF: "Nothing downstream of this line reads either yet." It is read
    // now, against the SAME SkillTrack every mechanic in this build already
    // checks -- session.tavern().dialogue().skills(), the one
    // DialogueDirector talkToWard()'s own comment names as shared between the
    // taproom and the whole ward ("the SAME director, the same authored
    // tables"). A Devin who signed on fluent in cracksmanship now opens a
    // strongbox measurably faster than a custom sheet that never touched it,
    // haggles off STREETWISE the same way, and is safer on the roofs off
    // SKYRUNNING -- the same three skills characterRows() already prints, so
    // the sheet built at the door is the sheet the character screen shows
    // back.
    //
    // ATTRIBUTES FINALLY CROSS THE SEAM. The comment that stood here from #84
    // to the fatigue build said "DELIBERATELY LEFT UNCROSSED", because no
    // mechanic weighed an AttributeId and wiring the block into nothing would
    // have been inventing a mechanic. The fatigue build is the mechanic:
    // MGT feeds the punch, AGI the legs and the climb costs, VIG the fatigue
    // pool's size and recovery, WIT the cast check and the link's own
    // recovery clock (sim/fatigue.hpp carries every formula, each exactly
    // neutral at the base-40 sheet). So the AttributeBlock Chargen::apply()
    // was [[nodiscard]] for is finally KEPT, the companions' preset sheets
    // walk their derivedAttribute() rows into the same shape, and one call
    // hands the whole sheet to the room and the body together --
    // Session::applyPlayerAttributes. CHARGEN'S ATTRIBUTE SPENDING MATTERS
    // FROM THIS BOOT ON.
    // CASE WATCH SKIPS THE WHOLE SHEET: the harness applies no chargen, so the
    // watch must not either -- a Wielder who punches harder than the drive's
    // default sheet would put Finch down on a different blow.
    if (!options.caseWatch) {
        sim::SkillTrack& playerSkills = session.tavern().dialogue().skills();
        sim::AttributeBlock sheet;
        if (chosen.companion.loaded()) {
            const std::int32_t matched = chosen.companion.applyStartingSkills(playerSkills);
            for (std::size_t i = 0; i < sim::kAttributeCount; ++i) {
                const auto attribute = static_cast<sim::AttributeId>(i);
                sheet.setValue(attribute, chosen.companion.derivedAttribute(attribute));
            }
            std::printf("granadad: %s's sheet set %d skill(s)\n", chosen.companion.name().c_str(),
                        static_cast<int>(matched));
        } else {
            sheet = chosen.chargen.apply(playerSkills);
            std::printf("granadad: custom sheet set %d skill(s)\n",
                        static_cast<int>(chosen.chargen.picks().size()));
        }
        session.applyPlayerAttributes(sheet);
        std::printf("granadad: attributes crossed: MGT %d AGI %d VIG %d WIT %d -- fatigue %d\n",
                    static_cast<int>(sheet.value(sim::AttributeId::Might)),
                    static_cast<int>(sheet.value(sim::AttributeId::Agility)),
                    static_cast<int>(sheet.value(sim::AttributeId::Vigor)),
                    static_cast<int>(sheet.value(sim::AttributeId::Wit)),
                    static_cast<int>(session.tavern().playerFatigue().maxPoints()));
    }

    // #92, THE SIM HALF OF THE DAGGERFALL FLOW. Everything the biography (or
    // the custom path's advantage shop) did to this character BEYOND the
    // sheet rides CreationResult::effects -- see chargen_raws.hpp -- and is
    // applied here, once, through the engine's own public seams: the same
    // SkillTrack the block above just wrote, Tavern::setPlayerCoin beside the
    // purse the barter verbs move, FactionLedger::seedStanding (the DIRECT
    // row write the doc's zero-sum bookkeeping requires -- addStanding's
    // rival mirror would double-count an authored spread), SocialLedger::seed
    // (the entry point built for "the seeded starting standing an authored
    // relationship implies"), CrimeLedger::addHeat over a heat of zero, and
    // Tavern::setPlayerHealth over the base the field itself declares. A
    // default ChargenEffects -- DEVIN, GABRI, or a custom sheet that skipped
    // the biography -- makes every branch below a no-op, so the three old
    // doors boot exactly the character they always did.
    //
    // ORDER MATTERS ONCE: skill deltas land AFTER the sheet, because the
    // doc's own mechanism line is "added onto the designated start". Each
    // effect is applied exactly once from the engine's own base values, which
    // is what keeps the whole application idempotent per boot -- the
    // accumulator itself is pure (chargen_raws.hpp).
    // (Skipped under --case-watch with the sheet above, and for the same
    // reason -- though a default-constructed ChargenEffects is a no-op in
    // every branch anyway, per its own header.)
    if (!options.caseWatch) {
        const sim::ChargenEffects& fx = chosen.effects;
        sim::DialogueDirector& talk = session.tavern().dialogue();
        sim::SkillTrack& playerSkills = talk.skills();

        const std::int32_t skillsTouched = sim::applySkillDeltas(playerSkills, fx);

        // The dagger. At zero points this computes EXACTLY the neutral 256
        // (daggerMultiplierQ8ForPoints's own guarantee), so setting it
        // unconditionally is the same track every earlier build booted.
        playerSkills.setAdvanceMultiplierQ8(
            sim::daggerMultiplierQ8ForPoints(fx.daggerPoints));

        if (fx.coinDelta != 0) {
            session.tavern().setPlayerCoin(
                std::max(0, session.tavern().playerCoin() + fx.coinDelta));
        }

        std::int32_t factionRows = 0;
        if (!fx.factionStandings.empty() && talk.standings().registry() != nullptr) {
            for (const auto& [factionId, delta] : fx.factionStandings) {
                const std::int32_t index = talk.standings().registry()->indexOf(factionId);
                if (index >= 0 && delta != 0) {
                    // Rows are zero at boot, so the authored delta IS the
                    // seeded standing.
                    talk.standings().seedStanding(index, delta);
                    ++factionRows;
                }
            }
        }

        // The seeds name notables.json ids; the ledger keys ward actor ids.
        // The roster already bound twenty-nine of the Forty to real bodies
        // (WardIdentity::notableId), so the join is a walk, once, at boot. A
        // notable the roster did not bind is skipped and the count below is
        // how a verifier reading stdout finds out.
        std::int32_t seeded = 0;
        for (const auto& [notableId, disposition] : fx.dispositionSeeds) {
            for (const sim::WardActor& actor : session.people().actors()) {
                if (session.people().identity(actor.id).notableId == notableId) {
                    talk.ledger().seed(actor.id, disposition);
                    ++seeded;
                    break;
                }
            }
        }

        if (fx.heat != 0) {
            talk.crimes().addHeat(fx.heat);
        }

        if (fx.hpMaxDelta != 0) {
            const std::int32_t hpMax =
                std::max(1, session.tavern().playerHpMax() + fx.hpMaxDelta);
            session.tavern().setPlayerHealth(hpMax, hpMax);
        }

        if (skillsTouched > 0 || fx.coinDelta != 0 || factionRows > 0 || seeded > 0 ||
            fx.heat != 0 || fx.hpMaxDelta != 0 || fx.daggerPoints != 0) {
            std::printf(
                "granadad: biography applied -- %d skill delta(s), coin %+d, "
                "%d faction row(s), %d seed(s), heat %+d, hpMax %+d, dagger q8=%d\n",
                static_cast<int>(skillsTouched), static_cast<int>(fx.coinDelta),
                static_cast<int>(factionRows), static_cast<int>(seeded),
                static_cast<int>(fx.heat), static_cast<int>(fx.hpMaxDelta),
                static_cast<int>(playerSkills.advanceMultiplierQ8()));
        }
    }

    // THE CONTROLS SURVIVE THE PROCESS. Beside the executable, because this game
    // has no installer and no user-profile directory yet, and a file the player
    // can see and delete beats one they cannot find.
    const std::filesystem::path controlsFile =
        options.controlsFile.empty() ? std::filesystem::path(render::kControlsFileName)
                                     : options.controlsFile;
    render::ControlSettings controls = render::loadControls(controlsFile);
    if (options.sensitivityGiven) {
        controls.mouse.sensitivity = options.sensitivity;
    }
    if (options.invertY) {
        controls.mouse.invertY = true;
    }
    controls.fovDegrees = start.fovDegrees;
    controls.sanitise();
    session.setControls(controls);
    // SHIP NOTE SEAM #2, CLOSED: THE FEET READ THE HAND FROM THE DOOR. The
    // creation window already knew which device drove it (CreationFlow's own
    // promptDevice), and that fact used to die with the flow -- so a pad
    // player's first world screen, the auto-opened casebook, said ENTER
    // SHOWS YOU WHERE until their first world press. The result carries the
    // device now and the Session is seeded with it at spawn, AFTER
    // setControls so the opening hint re-words against the live table. A
    // keyboard result is the Session's own default and this is a no-op.
    session.noteInputDevice(chosen.device);

    std::printf("granadad: %s loaded, %zu lamp(s), art=%s\n",
                options.smoke.session.world.c_str(), session.lampCount(),
                session.atlas().fromAuthoredArt() ? "content/art/custom" : "procedural fallback");
    std::printf("granadad: controls from %s\n", controlsFile.string().c_str());

    // SDL_INIT_GAMEPAD, and ONLY that. A pad that is plugged in should just
    // work; asking a player to turn one on in a menu is a 2006 courtesy. 3D
    // BUILD: no VIDEO -- the window is raylib's (see VideoBridge) and SDL's
    // video subsystem would open a second one nobody draws into.
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // THE AUDIO WIRING PASS. Once, after SDL_Init, exactly as
    // audio_engine.hpp's plan states (createSdlAudioEngine does its own
    // SDL_InitSubSystem(SDL_INIT_AUDIO), so the VIDEO|GAMEPAD line above did
    // not change). Null only on allocation failure; a machine with no sound
    // device gets a fully functional engine whose every call is a cheap
    // no-op, and one line here says which of the three this launch got --
    // the same "a verifier reading stdout can know for certain" reasoning
    // the gamepad count above states for itself. The scripted/headless paths
    // (--smoke/--screenshot, every test) never reach this function, so none
    // of them ever open a device.
    //
    // NOT const: the shutdown at the foot of this function has to drop the
    // engine BEFORE SDL_Quit(), not on the way out of scope after it. See the
    // comment there for the crash that ordering caused.
    std::unique_ptr<granadad::audio::AudioEngine> audio =
        granadad::audio::createSdlAudioEngine();
    if (audio != nullptr) {
        std::printf("granadad: audio %s\n",
                    audio->deviceOpen() ? "device open (48kHz float stereo)"
                                        : "no output device -- running silent");
        session.setAudio(audio.get());
    } else {
        std::printf("granadad: audio engine unavailable -- running silent\n");
    }

    // 3D BUILD. The window was opened in main() and handed in; it presents
    // at the largest integer scale that fits (nearest neighbour, always --
    // the chunkiness is the art direction) with vsync on, exactly the two
    // properties the SDL renderer used to be configured for here. The
    // bridge is what turns its keys and pointer into the SDL events the
    // loop below reads; the rig is the 3D scene under the overlay.
    VideoBridge bridge;
    SceneRig rig;
    std::printf("granadad: video %s backend, %dx%d window%s\n",
                video.kind() == render3d::VideoKind::Software ? "rlsw" : "gpu", video.width(),
                video.height(), options.video3d ? ", 3D world" : ", --2d software world");

    render::Framebuffer frame(options.smoke.session.width, options.smoke.session.height);

    bool mouseLook = true;
    video.setRelativeMouse(true);
    // THE PARITY PASS. THE POINTER COMES BACK WHEN A PAGE IS UP, and this is
    // the whole of the mechanism: one bool that says which of the two mouse
    // modes the window is in, flipped at the top of the frame off
    // pointer_page_open(). Relative mode is mouselook and hides the cursor;
    // absolute mode is a pointer over a full-screen page, which is the only
    // time the game has anything for a pointer to point AT.
    //
    // The owner asked for "easily navigatable with controller or mouse or
    // keyboard" and the mouse half of that was one call away the whole time --
    // the hit-tests were written and tested, the window was simply never taking
    // the pointer out of the corner. F3 still forces relative mode off entirely
    // for a player who wants their cursor to alt-tab; that is unrelated and
    // untouched.
    bool pointerLive = false;
    /// What SDL was last told. Kept so the flip is an EDGE and not a call every
    /// frame, and so F3's own toggle and the page's both read off one place.
    bool relativeMouse = true;
    /// The last place the pointer was seen this frame, and whether it moved --
    /// see the MOUSE_MOTION case on why the hover is resolved once per frame
    /// and not once per event.
    int pointerX = 0;
    int pointerY = 0;
    bool pointerMoved = false;

    // Whichever pad turned up first. One player, one pad.
    //
    // THE COUNT IS PRINTED UNCONDITIONALLY, ZERO INCLUDED. Before this, the
    // only gamepad line this build ever printed was "connected", which meant
    // a reader of stdout could not tell "SDL looked and found nothing" apart
    // from "this code never ran" -- exactly the gap that made an empirical
    // check of Steam Input routing depend on a human pressing a physical
    // button. A verifier (drive-windowed.ps1's own stdout capture, a CI run
    // with no controller attached, a player who wants to know why a pad is
    // not doing anything) can now read this line and know for certain.
    SDL_Gamepad* pad = nullptr;
    {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        std::printf("granadad: gamepads detected=%d\n", count);
        if (ids != nullptr) {
            if (count > 0) {
                pad = SDL_OpenGamepad(ids[0]);
            }
            SDL_free(ids);
        }
    }
    if (pad != nullptr) {
        std::printf("granadad: gamepad '%s' connected\n", SDL_GetGamepadName(pad));
    }

    // --- THE VIRTUAL PAD, VERIFICATION ONLY -------------------------------
    //
    // ATTACHED BEFORE THE LOOP, DRIVEN INSIDE IT, and everything downstream is
    // the shipped path -- see PadBeat's header on why that is the whole point.
    // SDL_EVENT_GAMEPAD_ADDED fires during the first poll and the ordinary
    // handler above opens it into `pad`, exactly as a controller plugged in
    // mid-game does.
    PadDriver padDriver;
    (void)padDriver.attach(options.padScript, options.padShotDir);

    // --- THE SCRIPTED DEMO -------------------------------------------------
    //
    // See render/demo.hpp. Three things change while it is running and nothing
    // changes when it is not:
    //
    //   1. THE CADENCE IS FIXED. One simulation step per rendered frame,
    //      instead of StepPump's wall-clock catch-up, so demo frame N is the
    //      same picture on a 60 Hz laptop, a 144 Hz desktop and inside a
    //      capture. The wall clock is used only to SLEEP so the route does not
    //      play at double speed; it cannot change what is drawn.
    //   2. THE PLAYER'S HANDS ARE OFF IT. `listening` below is forced true, so
    //      no key, stick, trigger or held movement reaches the session, and
    //      mouse look is off. ESCAPE ends the run -- the one input that still
    //      does anything, because a demo you cannot stop is a demo nobody dares
    //      start.
    //   3. THE ROUTE OWNS MoveInput. The director hands back the same struct a
    //      held W does, which is why the walk in the demo is the walk in the
    //      game and not a camera on rails.
    std::unique_ptr<render::DemoDirector> demo;
    if (options.demo) {
        demo = std::make_unique<render::DemoDirector>(options.demoSection, options.demoShotDir);
        mouseLook = false;
        std::printf("granadad: DEMO -- %d frames from '%s' at %d steps/frame\n",
                    render::demoFrameCount(options.demoSection),
                    options.demoSection.empty() ? "the top" : options.demoSection.c_str(), 1);
        if (!options.demoShotDir.empty()) {
            std::printf("granadad: DEMO -- frames to %s\n",
                        options.demoShotDir.string().c_str());
        }
        (void)std::fflush(stdout);
    }

    // --- CASE WATCH --------------------------------------------------------
    //
    // See render/case_watch.hpp. The same three demo rules hold and nothing
    // changes when it is off: the cadence is fixed (one step per frame, from
    // the tape), the player's hands are off it (ESC alone still works, the
    // same swallow as the demo's), and the tape owns MoveInput. The drive is
    // recorded HERE, headless, before the first frame -- the identical
    // runCaseLine the --case harness runs -- and the loop below replays it.
    std::unique_ptr<render::CaseWatchDirector> watch;
    if (options.caseWatch) {
        watch = std::make_unique<render::CaseWatchDirector>(
            render::recordCaseDrive(options.smoke), options.caseWatchShotDir);
        mouseLook = false;
        const render::CaseWatchDrive& drive = watch->drive();
        std::printf(
            "granadad: WATCH -- THE QUIET TENANT: drive recorded, %zu op(s), %d step(s), "
            "beats=%d/%d mask=%d\n",
            drive.ops.size(), static_cast<int>(drive.stepCount), static_cast<int>(drive.beats),
            static_cast<int>(drive.beatsWanted), static_cast<int>(drive.mask));
        std::printf("granadad: WATCH -- %d frame(s) planned at 60/s (~%d s). ESC leaves.\n",
                    static_cast<int>(watch->plannedFrames()),
                    static_cast<int>(watch->plannedFrames() / 60));
        if (!options.caseWatchShotDir.empty()) {
            std::printf("granadad: WATCH -- beat frames to %s\n",
                        options.caseWatchShotDir.string().c_str());
        }
        (void)std::fflush(stdout);
    }

    // The body advances on a fixed 60 Hz cadence whatever the frame rate does,
    // so what the simulation sees is a whole number of identical steps and a
    // slow machine plays the same game as a fast one. StepPump owns that, and
    // owns the mouse-look carry that a frame producing zero steps used to drop
    // on the floor — see granadad/render/step_pump.hpp.
    render::StepPump pump;
    using Clock = std::chrono::steady_clock;
    Clock::time_point last = Clock::now();

    // HOLD AND TOGGLE, on both of them. Players disagree about whether sprint
    // and crouch should latch and the argument has no winner, so the same key
    // does both: a tap latches, a hold holds. render::HoldToggle owns the rule
    // and tests/test_controls.cpp owns the proof.
    render::HoldToggle sprint;
    render::HoldToggle crouch;
    std::int64_t stepClock = 0;
    int quickSlot = 0;
    // #85. THE QUICK WHEEL. Plain held-down state, not a HoldToggle: there is
    // no tap-vs-hold ambiguity to resolve here the way Sprint/Crouch have --
    // it is open for exactly as long as QuickWheel is down and never latches.
    //
    // SPELLS BUILD: A TAP OF IT IS THE GRIMOIRE PAGE NOW. Held, the key is
    // still the wheel, exactly as above; released within HoldToggle's own
    // kTapSteps without ever stepping a slot, the press plainly was not FOR
    // the wheel -- it used to do nothing at all -- and the one key that
    // means "craftings" opens the list page instead. No new binding, no new
    // Menu tile, per the owner's ruling. The two trackers below are what
    // tells the taps apart.
    bool quickWheelOpen = false;
    std::int64_t quickWheelDownAt = 0;
    bool quickWheelStepped = false;
    // COMBAT. THE ATTACK-ARMED SELF-GUARD, the QuickWheel's exact shape. Attack
    // is now a down-edge/release-edge pair: the down edge starts the sim's hold
    // clock, the release edge resolves the swing. But every UP edge in this loop
    // reaches released() DIRECTLY, bypassing route_menu_key -- so a press the
    // district-map fast-travel branch or a lockpick or pointerLive already ate
    // still emits a release. This flag is set ONLY inside the world pressed()
    // Attack branch below; released() resolves a swing only when it is set, and
    // clears it. A page-consumed press never set it, so its release is inert --
    // no phantom world swing. Unlike the wheel, there is no press-step timestamp
    // to keep: the SIM owns the charge counter (it advances every movement step
    // and the tier is measured room-side), so the client only guards the edges.
    bool attackHeld = false;
    // S13. THE TRIGGERS' OWN EDGE STATE. SDL reports LT/RT as AXES, never as
    // button events, so kPadTable can never produce them; the poll after the
    // stick section synthesizes pressed()/released() on threshold crossings
    // and these two remember which side of the threshold each trigger was on
    // last frame. They clear themselves the frame a pad goes away (or goes
    // quiet behind a menu): livePad reading null is an ordinary release edge.
    bool leftTriggerDown = false;
    bool rightTriggerDown = false;
    // THE PARITY PASS. The left stick's own latch state while a page owns the
    // input -- see the stickNav block in the frame loop.
    bool navStickVertical = false;
    bool navStickHorizontal = false;
    // UI-EA-SPEC sec. 2, contract (c): the device edge. noteInputDevice()
    // is called on every press and is deliberately quiet about whether the
    // hand actually CHANGED; the tutor bands want the change alone, so the
    // loop keeps yesterday's answer and wakes them on the flip.
    render::InputDevice lastPromptDevice = session.promptDevice();

    bool running = true;
    std::int64_t frames = 0;
    /// The demo's next frame deadline. See the throttle at the foot of the loop.
    Uint64 demoDeadlineNs = 0;
    while (running) {
        sim::MoveInput held;
        SDL_Event event;
        // WHICH MOUSE THE WINDOW HAS, decided once, before a single event is
        // read. Ahead of the pump on purpose: the page this pointer will click
        // on is the page that was DRAWN last frame, so the hit-tests and the
        // picture agree by construction rather than by a frame's luck.
        {
            // A PAGE OUTRANKS mouseLook, not the other way round. F3 is "give
            // me my cursor back to alt-tab with"; a page is "there is something
            // on screen to point at". Both want the pointer out, so the page
            // does not need F3's permission -- and when the page closes, the
            // window goes back to whatever F3 last left mouseLook saying.
            const bool wantPointer = pointer_page_open(session) && !session.awaitingKey();
            const bool wantRelative = mouseLook && !wantPointer;
            if (wantPointer != pointerLive || wantRelative != relativeMouse) {
                pointerLive = wantPointer;
                relativeMouse = wantRelative;
                video.setRelativeMouse(relativeMouse);
            }
        }
        // WHAT A KEY DOES, in one place, whatever pressed it. Called from the
        // keyboard, the mouse and the pad, so a verb bound to PAD_A and a verb
        // bound to E go down exactly the same path and cannot drift apart.
        const auto pressed = [&](render::Key key) {
            if (key == render::Key::None) {
                return;
            }
            // THE OPTIONS PAGE EATS THE NEXT KEY WHEN IT IS LISTENING. Before
            // any binding lookup, because the whole point of "press a key" is
            // that the key's current meaning does not matter.
            if (session.awaitingKey()) {
                session.bindAwaited(key == render::Key::Escape ? render::Key::None : key);
                if (key != render::Key::Escape) {
                    (void)render::saveControls(session.controls(), controlsFile);
                }
                return;
            }
            // #85. THE QUICK WHEEL'S OWN DIRECTIONS, AHEAD OF THE ORDINARY
            // BINDING LOOKUP -- the same reason route_menu_key's up/downward/
            // etc. read raw keys as well as actions: while the wheel is open
            // the D-pad (or arrows, on a keyboard) IS the wheel, whatever
            // either one happens to be bound to otherwise. See controls.hpp's
            // own note on why this is a STEPPER and not a true radial: no
            // stick-angle primitive exists to build one off, and the D-pad is
            // the direction source that is already wired as four discrete
            // buttons.
            if (quickWheelOpen) {
                if (key == render::Key::PadUp || key == render::Key::Up ||
                    key == render::Key::PadRight || key == render::Key::Right) {
                    quickWheelStepped = true;
                    quickSlot = (quickSlot + 1) % 10;
                    session.selectQuickSlot(quickSlot);
                    return;
                }
                if (key == render::Key::PadDown || key == render::Key::Down ||
                    key == render::Key::PadLeft || key == render::Key::Left) {
                    quickWheelStepped = true;
                    quickSlot = (quickSlot + 9) % 10;
                    session.selectQuickSlot(quickSlot);
                    return;
                }
            }
            const render::Action action = session.controls().actionFor(key);
            switch (action) {
                // #85. ONE BUTTON, CONTEXT-RESOLVED. session.interact() is the
                // WHOLE rule (stance, then who/what is faced) -- see its own
                // header. Was Interact + Examine + Steal + Lift + Rest.
                case render::Action::Interact:
                    session.interact();
                    return;
                // COMBAT. Attack is a DOWN-EDGE now, not a one-shot. It arms the
                // self-guard and starts the sim's hold clock via
                // Session::attackDown(); the swing itself resolves on the
                // release edge (see released()). The tier -- tap (Subdue) or
                // hard (Intent::Harm) -- is measured room-side across the hold,
                // so the client resolves nothing here. Set the armed flag ONLY
                // on this world path so a page-consumed press fires no phantom
                // swing on its release.
                case render::Action::Attack:
                    attackHeld = true;
                    session.attackDown();
                    return;
                // #85. Was Jump + Traverse + DropDown. session.vertical() is
                // the rule: climb (mantle-or-leap) first, a drop if there is
                // a ledge to step off, an ordinary hop if neither.
                case render::Action::Vertical:
                    session.vertical();
                    return;
                case render::Action::Crouch:
                    crouch.press(stepClock);
                    session.setCrouched(crouch.active());
                    return;
                // #85. Was Sprint + Walk, one HoldToggle now -- see
                // controls.hpp's own note and the `held.sprint`/`held.walk`
                // derivation below, where the two are read back apart.
                case render::Action::Sprint:
                    sprint.press(stepClock);
                    return;
                // #85. ONE SCREEN, PAGES. Was Journal + Keys + Character +
                // Map + Letters + Options.
                case render::Action::Menu:
                    session.toggleMenu();
                    return;
                case render::Action::PagePrev:
                    session.menuPagePrev();
                    return;
                case render::Action::PageNext:
                    session.menuPageNext();
                    return;
                // #85. HELD. The D-pad/arrow interception that steps the
                // quick bar while this is down lives ABOVE this switch, in
                // `pressed`'s own early return -- see the comment there.
                case render::Action::QuickWheel:
                    quickWheelOpen = true;
                    // SPELLS BUILD: the strip comes up the moment the wheel
                    // does, and the release decides tap-or-hold -- see the
                    // trackers' own note above.
                    quickWheelDownAt = stepClock;
                    quickWheelStepped = false;
                    session.showQuickBar();
                    return;
                // S13. THE COMBAT PAIR. Cast is an ordinary press, exactly
                // like Attack; Block is HELD, the QuickWheel's plain-bool
                // shape rather than a HoldToggle -- a latched guard is a
                // footgun in a fight, and Session::setBlocking's own header
                // says where the hold actually becomes the room's fact.
                case render::Action::Cast:
                    session.castEquipped();
                    return;
                case render::Action::Block:
                    session.setBlocking(true);
                    return;
                // CORE ACTION #13. THE WARD MAP -- the owner's own ask: "a map
                // that they can press M to see... and select on controller".
                // The same key closes it; ESC closes it through the Pause
                // branch below (districtMapOpen is part of menuOpen()).
                case render::Action::Map:
                    session.toggleDistrictMap();
                    return;
                // UI-EA-SPEC sec. 4 violation #5: the two F-key pages, through
                // the table like everything else. The same action closes the
                // page it opened (toggleKeys/toggleOptions are toggles), which
                // is the enter/back law's "the key that opened a page closes
                // it" holding for these two by construction.
                case render::Action::KeysPage:
                    session.toggleKeys();
                    return;
                case render::Action::OptionsPage:
                    session.toggleOptions();
                    return;
                case render::Action::QuickNext:
                    quickSlot = (quickSlot + 1) % 10;
                    session.selectQuickSlot(quickSlot);
                    return;
                case render::Action::QuickPrev:
                    quickSlot = (quickSlot + 9) % 10;
                    session.selectQuickSlot(quickSlot);
                    return;
                case render::Action::Screenshot: {
                    const render::Framebuffer output =
                        render::upscaleNearest(frame, options.windowScale);
                    if (render::writePng(output, "granadad-screenshot.png")) {
                        std::printf("granadad: wrote granadad-screenshot.png\n");
                    } else {
                        std::printf("granadad: FAILED to write granadad-screenshot.png\n");
                    }
                    return;
                }
                // #85. RENAMED FROM Menu, unchanged body: the system panic/
                // save/quit screen, deliberately apart from the new Menu
                // above -- see controls.hpp's own note on why that reads as
                // one system and not two.
                case render::Action::Pause:
                    // ESCAPE BACKS OUT OF WHATEVER IS OPEN, and opens the pause
                    // menu when nothing is. IT USED TO QUIT THE GAME OUTRIGHT
                    // ON THAT SECOND BRANCH -- no confirmation, no way back if
                    // a finger slipped -- which is exactly the "eats your
                    // evening once and is never trusted again" this comment
                    // already warned about for every OTHER key. ESC gets the
                    // same warning applied to itself now: the pause menu is
                    // what opens, and only QUIT, chosen twice, closes anything.
                    // #85: session.menuOpen() replaces six named flags with
                    // the one predicate that also drives the Menu action now.
                    if (session.talking() || session.picking() || session.menuOpen() ||
                        session.pauseOpen() || session.waitOpen()) {
                        if (session.picking()) {
                            session.stopPicking();
                        } else {
                            session.closeConversation();
                        }
                        return;
                    }
                    session.togglePause();
                    return;
                default:
                    break;
            }
            // The quick slots, as one range rather than ten branches.
            const int slotBase = static_cast<int>(render::Action::QuickSlot1);
            const int slotIndex = static_cast<int>(action) - slotBase;
            if (slotIndex >= 0 && slotIndex < 10) {
                quickSlot = slotIndex;
                session.selectQuickSlot(quickSlot);
            }
            // UI-EA-SPEC sec. 2, contract (c): AN UNRECOGNIZED PRESS IS THE
            // REQUEST FOR HELP. A key that resolved to no action and was
            // wanted by no page reached the end of everything and did
            // nothing -- the one moment a quiet HUD earns its silence back
            // by raising the tutor bands. Session::noteTutorWake() is the
            // edge; HUD's band helpers hold and ease on their own clocks.
            if (action == render::Action::Count) {
                session.noteTutorWake();
            }
        };

        const auto released = [&](render::Key key) {
            const render::Action action = session.controls().actionFor(key);
            if (action == render::Action::Crouch) {
                crouch.release(stepClock);
                session.setCrouched(crouch.active());
            } else if (action == render::Action::Sprint) {
                sprint.release(stepClock);
            } else if (action == render::Action::QuickWheel) {
                // #85. CLOSES THE WHEEL. Nothing else to do: selectQuickSlot
                // already committed live as the D-pad stepped it, the same
                // way QuickNext/QuickPrev always applied immediately -- there
                // is no separate "confirm" beyond letting go.
                quickWheelOpen = false;
                // SPELLS BUILD: unless the press was a TAP -- down and up
                // inside HoldToggle's own kTapSteps with no slot stepped --
                // in which case it was never a wheel at all, and the Grimoire
                // page is what it asked for. toggleGrimoire() guards itself
                // (inert while talking or picking), so a tap mid-conversation
                // stays inert exactly like every other page toggle.
                if (!quickWheelStepped &&
                    stepClock - quickWheelDownAt <= render::HoldToggle::kTapSteps) {
                    session.toggleGrimoire();
                }
            } else if (action == render::Action::Attack) {
                // COMBAT. RESOLVES THE SWING on the release edge -- hard iff the
                // hold reached kHardSwingHoldSteps, measured room-side. GUARDED
                // by attackHeld, which the world pressed() Attack branch is the
                // only place to set: this released() is reached DIRECTLY on
                // every up edge (bypassing route_menu_key), so a press eaten by
                // the district-map fast-travel branch, a lockpick, or a
                // pointerLive-consumed MouseLeft never armed it, and its release
                // does nothing here. Disarm on the way out so the next press
                // starts clean.
                if (attackHeld) {
                    attackHeld = false;
                    session.attackUp();
                }
            } else if (action == render::Action::Block) {
                // S13. LOWERS THE GUARD. Held means blocking, up means not,
                // no latch -- see the press side's own note.
                session.setBlocking(false);
            }
        };

        // 3D BUILD: raylib's keys and pointer onto SDL's queue, ahead of the
        // drain -- see VideoBridge. The pad's own events are already there.
        bridge.pump(video, frame.width(), frame.height());
        while (SDL_PollEvent(&event)) {
            // THE DEMO OWNS THE INPUT, and this is the one place that has to
            // say so -- ahead of every handler, so nothing below can be reached
            // by accident. The close button and ESCAPE still work, because an
            // unattended route the watcher cannot stop is worse than no route.
            // CASE WATCH keeps the identical rule for the identical reason.
            if (demo != nullptr || watch != nullptr) {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                     event.key.scancode == SDL_SCANCODE_ESCAPE)) {
                    running = false;
                }
                continue;
            }
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                // HOT-PLUG. A pad Steam Input starts (or stops) translating
                // AFTER this window already has focus -- launch first, then
                // route the Steam Controller, or alt-tab to configure Steam --
                // arrives here exactly like a pad that was plugged in the
                // whole time, because the event pump runs every frame
                // unconditionally. Both branches print the SAME "gamepads
                // detected=" line the startup enumeration above does, so a
                // verifier watching stdout sees one consistent count that
                // moves, rather than a "connected" line with nothing to
                // compare it against.
                case SDL_EVENT_GAMEPAD_ADDED: {
                    int count = 0;
                    SDL_JoystickID* liveIds = SDL_GetGamepads(&count);
                    if (liveIds != nullptr) {
                        SDL_free(liveIds);
                    }
                    std::printf("granadad: gamepad ADDED, gamepads detected=%d\n", count);
                    if (pad == nullptr) {
                        pad = SDL_OpenGamepad(event.gdevice.which);
                        if (pad != nullptr) {
                            std::printf("granadad: gamepad '%s' connected\n",
                                        SDL_GetGamepadName(pad));
                        }
                    }
                    break;
                }
                case SDL_EVENT_GAMEPAD_REMOVED: {
                    if (pad != nullptr &&
                        SDL_GetGamepadID(pad) == event.gdevice.which) {
                        SDL_CloseGamepad(pad);
                        pad = nullptr;
                        sprint.clear();
                        crouch.clear();
                        quickWheelOpen = false;
                    }
                    int count = 0;
                    SDL_JoystickID* liveIds = SDL_GetGamepads(&count);
                    if (liveIds != nullptr) {
                        SDL_free(liveIds);
                    }
                    std::printf("granadad: gamepad REMOVED, gamepads detected=%d\n", count);
                    break;
                }
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                    // SHIP NOTE MOVE 3: a pad press flips every prompt into
                    // pad vocabulary, live -- see Session::promptDevice().
                    session.noteInputDevice(render::InputDevice::Pad);
                    // THE B SEAM, CLOSED AT THE EDGE. While a page owns the
                    // input, East IS Escape -- remapped ONCE, here, so the
                    // router and the fall-through pressed() read the same
                    // key and one press cannot both close the casebook and
                    // reach Crouch's binding (the CROUCHED-banner seam the
                    // ship note's drive found). Not while the options page
                    // is listening for a key: a rebinding must capture the
                    // real PadEast. See render::pageBackRemap and
                    // route_menu_key's own header.
                    const render::Key key = render::pageBackRemap(
                        key_of_pad_button(event.gbutton.button),
                        pointer_page_open(session) && !session.awaitingKey());
                    if (!route_menu_key(session, key)) {
                        pressed(key);
                    }
                    break;
                }
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    released(key_of_pad_button(event.gbutton.button));
                    break;
                case SDL_EVENT_KEY_DOWN: {
                    const render::Key key = key_of_scancode(event.key.scancode);
                    // SHIP NOTE MOVE 3 -- through noteInputKey, so an
                    // unmapped scancode (Key::None) is nobody, not the
                    // keyboard.
                    session.noteInputKey(key);
                    if (event.key.repeat) {
                        // A held key is not a stream of presses. The one place
                        // repeat is wanted is walking a long list, and the menu
                        // router below takes it.
                        (void)route_menu_key(session, key);
                        break;
                    }
                    // THE JOURNAL TOOK TAB AT #85 (it is on J now, the
                    // owner's own convention call), so freeing the mouse
                    // moved to F3 -- see the keys page. A player who wants
                    // their cursor back is almost always a player who wants
                    // to alt-tab, and alt-tab already works.
                    //
                    // AND IT YIELDS TO A BINDING. F3 is unbound by default, so
                    // this is free; the moment somebody binds a verb to it, the
                    // verb wins and the window keeps the mouse. A hard-coded key
                    // that quietly outranks the rebinding screen is the exact
                    // shape of bug the rest of this task was about.
                    if (key == render::Key::F3 &&
                        session.controls().actionFor(key) == render::Action::Count) {
                        // FLIPS THE INTENT, NOT THE WINDOW. The top of the
                        // frame owns the one SDL_SetWindowRelativeMouseMode
                        // call now, so F3 and an open page cannot each set the
                        // mode and disagree about which of them was last.
                        mouseLook = !mouseLook;
                        break;
                    }
                    // F1 AND F2 ARE ORDINARY ACTIONS NOW (UI-EA-SPEC sec. 4
                    // violation #5). They were advertised in --help, hard-
                    // coded right here with a yield-to-binding guard, and
                    // invisible to the very page F1 opens. Action::KeysPage
                    // and Action::OptionsPage carry them through the binding
                    // table instead -- they print on the keys page, the
                    // rebinding screen can move them, and the pressed()
                    // switch below dispatches them like every other verb.
                    // Nothing is left hard-coded here.
                    if (route_menu_key(session, key)) {
                        break;
                    }
                    pressed(key);
                    break;
                }
                case SDL_EVENT_KEY_UP:
                    released(key_of_scancode(event.key.scancode));
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    // SHIP NOTE MOVE 3: mouse and keyboard are one device.
                    session.noteInputDevice(render::InputDevice::KeyboardMouse);
                    // THE POINTER FIRST, WHEN THERE IS ONE. `pointerLive` is
                    // only ever true while a page is up (see its declaration),
                    // and while a page is up a left click is a click ON THE
                    // PAGE -- never Action::Attack, which is what it used to be
                    // and which meant clicking a lead in the casebook threw a
                    // punch at whoever was standing in front of you.
                    if (pointerLive && !session.awaitingKey()) {
                        if (event.button.button == SDL_BUTTON_RIGHT) {
                            // RIGHT-CLICK IS BACK, the same gesture and the
                            // same reason as run_creation_window()'s, and the
                            // same route the pad's B now takes.
                            if (!route_menu_key(session, render::Key::Escape)) {
                                pressed(render::Key::Escape);
                            }
                            break;
                        }
                        if (event.button.button != SDL_BUTTON_LEFT) {
                            break;
                        }
                        // Already in framebuffer pixels -- VideoBridge
                        // converted through the backend's placement.
                        if (session_pointer(session, frame.width(), frame.height(),
                                            static_cast<int>(event.button.x),
                                            static_cast<int>(event.button.y), true)) {
                            break;
                        }
                    }
                    const render::Key key = key_of_mouse_button(event.button.button);
                    if (!route_menu_key(session, key)) {
                        pressed(key);
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    released(key_of_mouse_button(event.button.button));
                    break;
                case SDL_EVENT_MOUSE_WHEEL: {
                    // The wheel walks the quick bar, and it walks a long list
                    // when one is open -- which is what a wheel is for.
                    if (event.wheel.y == 0) {
                        break;
                    }
                    session.noteInputDevice(render::InputDevice::KeyboardMouse);
                    const render::Key key =
                        event.wheel.y > 0 ? render::Key::WheelUp : render::Key::WheelDown;
                    if (!route_menu_key(session, key)) {
                        pressed(key);
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION:
                    if (pointerLive && !session.awaitingKey()) {
                        // HOVER MIRRORS THE CURSOR. Not a second highlight the
                        // pad cannot see -- see session_pointer's header.
                        //
                        // RECORDED HERE, RESOLVED ONCE PER FRAME, and that is
                        // not a refinement -- the first version hit-tested
                        // inside this case and FROZE THE GAME the moment the
                        // pointer crossed the ward map. A mouse produces
                        // motion events far faster than frames, and the map's
                        // hit-test has to rebuild DistrictMapState (which
                        // gathers every person standing in the selection) and
                        // re-compose the whole page to get the viewport the
                        // plan was drawn through. Dozens of those per frame is
                        // a hang, and it was one. The pointer can only be in
                        // one place when the frame is drawn, so only the last
                        // position of the batch can matter. (Framebuffer
                        // pixels already -- see VideoBridge.)
                        pointerX = static_cast<int>(event.motion.x);
                        pointerY = static_cast<int>(event.motion.y);
                        pointerMoved = true;
                        break;
                    }
                    if (mouseLook && !session.awaitingKey()) {
                        // THROUGH THE SETTINGS, AND RAW. Sensitivity and
                        // invert-Y are applied once, here, by
                        // render::MouseSettings; nothing smooths, filters or
                        // accelerates it on the way -- see MouseSettings and
                        // sim::PlayerBody::step. Into the PUMP rather than into
                        // a frame local, because a frame that runs no step must
                        // still keep the rotation.
                        const render::MouseSettings& mouse = session.controls().mouse;
                        pump.addLook(
                            mouse.yawFor(static_cast<std::int32_t>(event.motion.xrel)),
                            mouse.pitchFor(static_cast<std::int32_t>(event.motion.yrel)));
                    }
                    break;
                default:
                    break;
            }
        }

        // THE HOVER, ONCE, off the last position of whatever batch of motion
        // events this frame brought in. See the MOUSE_MOTION case.
        if (pointerMoved) {
            pointerMoved = false;
            if (pointerLive && !session.awaitingKey()) {
                (void)session_pointer(session, frame.width(), frame.height(), pointerX, pointerY,
                                      false);
            }
        }

        // UI-EA-SPEC sec. 2, contract (c): THE HAND CHANGED, WAKE THE TUTORS.
        // Once per frame, off the edge alone -- the labels already re-worded
        // themselves live (promptDevice is read at draw time); this is the
        // accompanying "here is what your new hand does" moment.
        if (session.promptDevice() != lastPromptDevice) {
            lastPromptDevice = session.promptDevice();
            session.noteTutorWake();
        }

        // Held keys move the body — unless somebody is talking to you, in which
        // case the movement keys are walking a list and must not also walk you
        // out of the room.
        //
        // #85 CLOSES A GAP THE SURVEY FOUND: this used to check talking() ||
        // picking() || optionsOpen() || pauseOpen() and NOT the casebook, the
        // keys page, the character sheet, the map or the letters -- so
        // holding W with any of those five open did not walk the topic
        // cursor a second time (route_menu_key already eats that discrete
        // press) but DID keep walking the player's own body underneath the
        // page, silently, every step. session.menuOpen() is the one
        // predicate that now answers "is a page currently eating the
        // keyboard" for all six of them, so this and route_menu_key's own
        // per-page checks and every toggle*()'s own exclusivity block read
        // off the same six flags instead of three hand-kept copies of the
        // list.
        // NOT WHILE session.firstRun() -- the brand-new-game opening casebook
        // (SessionConfig::openingPage) is the ONE exception: Session::step()
        // itself dismisses it on the first movement it sees ("FIRST STEP
        // CLOSES THE OPENING PAGE", its own comment), which needs the
        // movement KEY POLLED in the first place. Gating that read on
        // menuOpen() -- true here too, since the opening page IS the
        // casebook -- would mean the page can never be dismissed by walking
        // at all: `held` stays zero forever, step() never sees a non-zero
        // MoveInput, and firstRun_ never clears. firstRun() is a true
        // one-shot (every discrete verb press clears it, in dismissOverlays()
        // and in every toggle*()), so this only widens the gap for that exact
        // window and closes again the moment anything else happens.
        const bool listening =
            demo != nullptr || watch != nullptr || session.talking() || session.picking() ||
            (session.menuOpen() && !session.firstRun()) || session.pauseOpen() ||
            session.waitOpen();
        // 3D BUILD: the held state is the bridge's, kept off the same edges
        // the events came from -- SDL_GetKeyboardState/SDL_GetMouseState
        // answer for a keyboard SDL no longer reads.
        const bool* keys = listening ? nullptr : bridge.held.data();
        const Uint32 mouseButtons = listening ? 0U : bridge.mouseMask;
        SDL_Gamepad* livePad = listening ? nullptr : pad;
        const render::ControlSettings& controls_now = session.controls();
        if (keys != nullptr) {
            const auto down = [&](render::Action action) {
                return action_is_down(controls_now, action, keys, mouseButtons, livePad);
            };
            if (down(render::Action::Forward)) {
                held.forward += 1;
            }
            if (down(render::Action::Back)) {
                held.forward -= 1;
            }
            if (down(render::Action::StrafeRight)) {
                held.strafe += 1;
            }
            if (down(render::Action::StrafeLeft)) {
                held.strafe -= 1;
            }
            if (down(render::Action::TurnRight)) {
                held.turn += 1;
            }
            if (down(render::Action::TurnLeft)) {
                held.turn -= 1;
            }
            // #85. PACE, OFF ONE HoldToggle NOW -- was Sprint + Walk, two
            // separate actions. latched() is the tap-toggled WALK; active()
            // that is NOT latched is the momentary SPRINT -- and the two can
            // never both read true, by construction: HoldToggle::press()
            // cancels a latch the instant a new press starts (see its own
            // header), so the very press that would otherwise make both true
            // in the same step is the one press activeNow() reports as NOT
            // held for. See controls.hpp's Sprint entry for the fuller
            // version of this note.
            held.walk = sprint.latched();
            held.sprint = sprint.active() && !sprint.latched();
        }

        // --- the pad --------------------------------------------------------
        //
        // A REAL RADIAL DEADZONE and a cubed look curve, both in
        // render/controls.hpp with cases on them. The left stick steers and
        // picks a gait; the right stick looks.
        if (livePad != nullptr) {
            const render::PadSettings& padTuning = controls_now.pad;
            const render::Stick move = render::applyDeadzone(
                render::Stick{SDL_GetGamepadAxis(livePad, SDL_GAMEPAD_AXIS_LEFTX),
                              SDL_GetGamepadAxis(livePad, SDL_GAMEPAD_AXIS_LEFTY)},
                padTuning);
            if (move.x != 0 || move.y != 0) {
                held.strafe += render::stickIntent(move.x);
                held.forward -= render::stickIntent(move.y);  // stick +y is down
                const std::int64_t magnitude = render::stickMagnitude(move);
                const std::int64_t percent = magnitude * 100 / render::kStickMax;
                if (percent >= render::kAnalogueSprintPercent) {
                    held.sprint = true;
                } else if (percent < render::kAnalogueWalkPercent) {
                    held.walk = true;
                }
            }
            const render::Stick look = render::applyDeadzone(
                render::Stick{SDL_GetGamepadAxis(livePad, SDL_GAMEPAD_AXIS_RIGHTX),
                              SDL_GetGamepadAxis(livePad, SDL_GAMEPAD_AXIS_RIGHTY)},
                padTuning);
            if (look.x != 0 || look.y != 0) {
                pump.addLook(render::padLook(look.x, padTuning.lookBamPerSecond, 1),
                             render::padLook(controls_now.mouse.invertY ? look.y : -look.y,
                                             padTuning.lookBamPerSecond, 1));
            }
        }

        // --- the triggers ---------------------------------------------------
        //
        // S13. SDL reports LT/RT as AXES (SDL_GAMEPAD_AXIS_*_TRIGGER, 0..32767),
        // never as SDL_EVENT_GAMEPAD_BUTTON_DOWN -- so kPadTable can never
        // produce Key::PadLeftTrigger/PadRightTrigger and the two sat in the
        // Key enum with no way to ever fire. Polled here into synthesized
        // pressed()/released() edges on threshold crossings, which finally
        // gives PadSettings::triggerDeadzonePercent -- persisted, sanitised
        // and parsed since the pad-readiness pass, read by nothing until now
        // -- its one reader. An edge, not a level, so a held trigger behaves
        // exactly like any held key: Block=LT stays down for as long as the
        // finger does, Cast=RT fires once per pull. Runs OUTSIDE the
        // `livePad != nullptr` stick block on purpose -- a pad unplugged
        // mid-pull reads as an ordinary release edge instead of a stuck guard.
        //
        // THE PARITY PASS: `pad`, NOT `livePad`, AND A CAPTURE FOUND IT. This
        // block's own comment above says a trigger goes down "the same route a
        // physical button-down takes, menu router first" -- and it could not,
        // because `listening` nulls livePad the moment any page opens, so
        // every trigger read as RELEASED under exactly the pages the router
        // exists for. The ward map's zoom went on the triggers in this pass;
        // the first photograph of it came back at ZOOM 1/4 with both pulls in,
        // which is what sent me here. Reading `pad` restores the intent, and
        // costs nothing when no page is open: `livePad` IS `pad` then. A pad
        // that goes away still reads as a release, because SDL_GetGamepadAxis
        // is only consulted when the handle is non-null.
        {
            const std::int32_t threshold =
                controls_now.pad.triggerDeadzonePercent * render::kStickMax / 100;
            const auto triggerEdge = [&](bool& wasDown, SDL_GamepadAxis axis,
                                         render::Key key) {
                const bool isDown =
                    pad != nullptr && SDL_GetGamepadAxis(pad, axis) > threshold;
                if (isDown == wasDown) {
                    return;
                }
                wasDown = isDown;
                if (isDown) {
                    // The same route a physical button-down takes, menu
                    // router first -- a trigger bound to a verb must not
                    // outrank a page that is eating the keyboard.
                    if (!route_menu_key(session, key)) {
                        pressed(key);
                    }
                } else {
                    released(key);
                }
            };
            triggerEdge(leftTriggerDown, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
                        render::Key::PadLeftTrigger);
            triggerEdge(rightTriggerDown, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                        render::Key::PadRightTrigger);
        }

        // --- THE PARITY PASS: THE LEFT STICK WALKS A LIST -------------------
        //
        // WHAT WAS WRONG. `livePad` above is null for as long as a page owns
        // the input, which is right for the two analogue readers -- a leaned
        // stick must not walk the body around underneath an open map -- but it
        // also meant the stick was the one pad control that went completely
        // silent exactly when the player was most likely to be pushing it.
        // With the D-pad unbound as well (see route_menu_key), a pad could OPEN
        // the ward map and could FACE a building, and could not move the cursor
        // between them by any means at all.
        //
        // `pad`, NOT `livePad`, and gated on `listening` -- the mirror image of
        // the block above. Below a page the stick steers the body and this does
        // nothing; under one it steers the list and the body reader is the part
        // that is quiet. The two can never both be reading it.
        //
        // A LATCH, NOT A LEVEL, and the same two thresholds run_creation_window
        // already uses: one step when the push crosses kStickOn, and nothing
        // more until it comes back under kStickOff. An axis produces no press
        // event, so without the latch a leaned stick scrolls a list at the
        // frame rate, which is unusable -- the character screen learned that
        // first and this is the same lesson, not a second guess at it.
        //
        // THROUGH route_menu_key, AS THE ARROW KEY IT STANDS FOR. Not through
        // a parallel set of Session calls: a verb that exists for the stick
        // and not for the arrows is exactly the drift this whole pass is
        // about. ARROWS, NOT THE D-PAD KEYS, since violation #3: a raw arrow
        // is pure list movement on every page and can never resolve through a
        // binding, while a synthesized PadUp would hit the Menu surface's
        // toggle-reserved rule and a leaned stick would close the page it was
        // trying to scroll. The D-pad's own physical presses still arrive as
        // themselves.
        if (pad != nullptr && listening) {
            constexpr Sint16 kNavStickOn = 18000;
            constexpr Sint16 kNavStickOff = 9000;
            const auto stickNav = [&](bool& latched, SDL_GamepadAxis axis, render::Key negative,
                                      render::Key positive) {
                const int value = SDL_GetGamepadAxis(pad, axis);
                const int magnitude = value < 0 ? -std::max(value, -32767) : value;
                if (!latched && magnitude >= kNavStickOn) {
                    latched = true;
                    (void)route_menu_key(session, value < 0 ? negative : positive);
                } else if (latched && magnitude <= kNavStickOff) {
                    latched = false;
                }
            };
            stickNav(navStickVertical, SDL_GAMEPAD_AXIS_LEFTY, render::Key::Up,
                     render::Key::Down);
            stickNav(navStickHorizontal, SDL_GAMEPAD_AXIS_LEFTX, render::Key::Left,
                     render::Key::Right);
        } else {
            // CLEARED THE MOMENT THE PAGE CLOSES, so a stick still leaned when
            // the map goes away does not arrive at the next page already
            // latched and swallow its first push.
            navStickVertical = false;
            navStickHorizontal = false;
        }

        held.crouch = crouch.active();

        // THE ONLY THING THAT ACTUALLY QUITS. Session never touches SDL, so a
        // QUIT confirmed on the pause menu sets a flag and this is the one
        // place that reads it -- one frame after the confirming press, so the
        // menu's own last frame still draws before the window goes.
        if (session.quitRequested()) {
            running = false;
        }

        // THE DEMO'S OWN TICK, ahead of the step so the input it hands back is
        // the input this frame is stepped with.
        if (demo != nullptr) {
            const render::DemoDirector::Tick tick = demo->advance(session);
            held = tick.move;
            if (!tick.running) {
                // THE ROUTE ENDS THE RUN, and it ends it on the end card's own
                // last frame rather than on a street corner -- which is the
                // difference between a demo finishing and a demo stopping.
                running = false;
            }
        }
        // THE WATCH'S OWN TICK, at the demo's station and under the demo's
        // contract -- with one difference the tape forces: a frame may step
        // the simulation ZERO times (a hold showing a plate, a card) or once
        // (a recorded step), never more, so the replayed session sees exactly
        // the drive's steps and nothing the watcher's eye was given costs a
        // byte of simulation.
        std::int32_t watchSteps = 0;
        if (watch != nullptr) {
            const render::CaseWatchDirector::Tick tick = watch->advance(session);
            held = tick.move;
            watchSteps = tick.steps;
            if (!tick.running) {
                running = false;
            }
        }

        const Clock::time_point now = Clock::now();
        const double frameSeconds = std::chrono::duration<double>(now - last).count();
        last = now;
        // ONE STEP PER FRAME WHILE THE DEMO IS UP. See the director's
        // declaration: this is the whole of the demo's determinism, and it
        // costs the ordinary game nothing because the branch is never taken.
        // The watch takes the same gate with its 0-or-1 count, above.
        const std::int32_t steps = demo != nullptr    ? 1
                                   : watch != nullptr ? watchSteps
                                                      : pump.advance(frameSeconds);
        for (std::int32_t i = 0; i < steps; ++i) {
            ++stepClock;
            session.step(demo != nullptr || watch != nullptr ? held
                                                             : pump.nextStepInput(held));
        }

        // AUDIO, PER FRAME, per the plan: the clock for the beds' day/night
        // layer gains, then one update() of wall-clock dt for the crossfades,
        // sparse one-shot timers and the footstep cadence (dt is clamped
        // internally against pauses and hitches). After the step loop, so
        // the hour the beds shape to is the hour the steps just reached.
        if (audio != nullptr) {
            audio->setTimeOfDay(session.timeOfDay());
            audio->update(static_cast<float>(frameSeconds));
        }

        // THE CARD OWNS THE FRAME. Asked BEFORE drawFrame, because the card is
        // drawn after it and the HUD it would sit under is drawn inside it --
        // see Session::setHudStandDown and DemoDirector::cardOwnsFrame. Without
        // this the end card, which is the last thing a viewer of the demo ever
        // sees, carried a compass, a clock, a case trail with a green bar and a
        // building name hanging off its right border.
        if (demo != nullptr) {
            session.setHudStandDown(demo->cardOwnsFrame());
        }
        if (watch != nullptr) {
            session.setHudStandDown(watch->cardOwnsFrame());
            // THE WATCHER'S EYE, after the step and before the draw: the
            // tape's raw yaw whips compass-to-compass step to step (the
            // drive's walker steers by trying facings), which the replay's
            // one-step-per-frame cadence turned into a 60 Hz strobe through
            // the near wall -- the owner's "teleporting through walls ...
            // double vision". The sim has already stepped under the tape's
            // own yaw; this eases only what is about to be DRAWN. See
            // CaseWatchDirector::composeView.
            watch->composeView(session);
        }
        // 3D BUILD: with the world in the backend's own 3D pass, this frame
        // is the OVERLAY -- drawn without the software world pass, on a
        // transparent ground; --2d keeps the raycaster's world in it.
        session.drawFrame(frame, render::Session::FramePasses{.world = !options.video3d});
        // THE CARD AND THE CAPTION GO ON LAST, over the finished frame, and
        // the shutter goes after them -- so what a capture holds is exactly
        // what the window presented, furniture included.
        if (demo != nullptr) {
            demo->drawOverlay(frame, session);
            demo->shutter(frame);
        }
        if (watch != nullptr) {
            watch->drawOverlay(frame, session);
            watch->shutter(frame);
        }
        // THE FRAME DUMP, LAST -- after every overlay and both shutters, so
        // what lands on disk is exactly what the window is about to present.
        // UI-EA-SPEC ship checklist (FLOW): NO LONGER SCRIPTED-RUNS-ONLY.
        // The transition grammar's evidence -- the boot veil rising, a page
        // easing open, a back-to-opener swap, the commit beat -- lives in
        // the ORDINARY windowed session, which the demo and the watch never
        // drive. The flag is still explicit opt-in, still VERIFICATION ONLY
        // (an hour of play would write two hundred thousand PNGs -- point it
        // at a scratch dir and keep the run short), and a run that never
        // passes it is byte-for-byte untouched.
        if (!options.frameDumpDir.empty()) {
            std::error_code frameDumpEc;
            std::filesystem::create_directories(options.frameDumpDir, frameDumpEc);
            char frameName[16];
            (void)std::snprintf(frameName, sizeof(frameName), "f%05lld.png",
                                static_cast<long long>(frames));
            (void)render::writePng(frame, (options.frameDumpDir / frameName).string());
        }
        ++frames;

        // 3D BUILD: the frame goes out through the backend -- the 3D pass
        // under --3d, then this frame as the overlay, then the swap.
        (void)present_frame(video, options, rig, session, frame, nullptr);

        // PACED FOR AN EYE, NOT FOR A BENCHMARK. VSync alone is whatever the
        // monitor happens to be, so a 144 Hz panel would run the route at 2.4x
        // and a capture with no window at several hundred. One step per frame
        // plus this floor is what makes the demo play at the same speed
        // everywhere -- and it is the ONLY place wall clock touches the demo,
        // which is why it cannot change a pixel.
        //
        // A DEADLINE, NOT A PER-FRAME SLEEP, and the difference was measured:
        // the first version slept `budget - spent` each frame and the route
        // came out at about 71 frames a second, because SDL_DelayNS on Windows
        // returns early and every frame kept its own error. Accumulating the
        // deadline makes the error self-correcting, and the short loop absorbs
        // an undersleep instead of banking it. A frame that genuinely overran
        // resets the deadline rather than trying to claw the time back, which
        // is what stops a hitch turning into a sprint.
        if (demo != nullptr || watch != nullptr) {
            constexpr Uint64 kBudgetNs = 1'000'000'000ULL / 60ULL;
            const Uint64 nowNs = SDL_GetTicksNS();
            demoDeadlineNs = demoDeadlineNs == 0 || nowNs > demoDeadlineNs + kBudgetNs
                                 ? nowNs + kBudgetNs
                                 : demoDeadlineNs + kBudgetNs;
            for (Uint64 t = SDL_GetTicksNS(); t < demoDeadlineNs; t = SDL_GetTicksNS()) {
                SDL_DelayNS(demoDeadlineNs - t);
            }
        }

        if (!padDriver.advance(frame)) {
            running = false;
        }
    }

    // THE BORROW ENDS BEFORE THE LENDER DOES. `audio` (declared after
    // `session`) destructs first on the way out of this function, so the
    // session's borrowed pointer is detached here, while both are still
    // alive -- nothing below this line steps or toggles the session, but a
    // dangling pointer that is merely never used is still a dangling pointer.
    session.setAudio(nullptr);

    // A REBINDING SURVIVES THE PROCESS. Written on the way out as well as at
    // the moment it is made, so a slider moved on the options page is still
    // moved tomorrow.
    (void)render::saveControls(session.controls(), controlsFile);

    std::printf("granadad: %lld frame(s), body ended at (%d,%d,z%d)\n",
                static_cast<long long>(frames), session.body().tileX(), session.body().tileY(),
                session.body().band());

    // THE WATCH'S VERDICT, while the session still stands. The summary line
    // carries the same fields the --case harness prints, read off the REPLAYED
    // session; the twin line says whether the replay ended exactly where the
    // recorded drive did -- and a divergence is an exit 1, because a watch
    // that shows something other than what --case proves is not a watch.
    int watchExit = 0;
    if (watch != nullptr) {
        if (watch->finished()) {
            std::printf("granadad: WATCH -- %s\n", watch->endSummary(session).c_str());
            if (watch->twinMatched(session)) {
                std::printf("granadad: WATCH -- replay matched the drive, step for step\n");
            } else {
                std::printf(
                    "granadad: WATCH -- REPLAY DIVERGED FROM THE DRIVE -- what was watched is "
                    "not what --case proves\n");
                watchExit = 1;
            }
        } else {
            std::printf("granadad: WATCH -- stopped early (ESC or close), nothing owed\n");
        }
    }

    if (pad != nullptr) {
        SDL_CloseGamepad(pad);
    }
    padDriver.detach();
    // The window itself is main()'s and closes after this returns.

    // THE ENGINE GOES BEFORE SDL DOES, and this is the contract setAudio()'s
    // own header in session.hpp states -- "main.cpp detaches (setAudio(nullptr))
    // before its engine goes away." It never did. `audio` is a function-scope
    // unique_ptr, so its destructor used to run on `return`, which is AFTER
    // SDL_Quit() has already torn down the audio subsystem and the callback
    // thread the engine's stream belongs to. Closing a stream SDL has freed is
    // a use-after-free, and it announced itself exactly the way one does: the
    // windowed client exited 0xC0000374 (heap corruption) or 0xC0000005
    // (access violation) -- never during play, always on the way out.
    //
    // It was invisible for as long as nothing exercised a full windowed
    // session that ended by itself: --smoke and every test are headless and
    // never open a device, and a human closing the window got the same crash
    // after the window had already gone, where it reads as Windows tidying up.
    // `--demo` is what made it reproducible, because it plays to the end and
    // then quits on its own.
    //
    // Detach first so the Session cannot touch a dead engine, then drop the
    // engine, then let SDL go. Ordering only -- no hook, no sound and no sim
    // state changes, which is why the world hash cannot feel this.
    session.setAudio(nullptr);
    audio.reset();
    SDL_Quit();
    return watchExit;
}


}  // namespace

int main(int argc, char** argv) {
    print_build_banner();

    bool stop = false;
    int exitCode = 0;
    const Options options = parse(argc, argv, stop, exitCode);
    if (stop) {
        return exitCode;
    }

    try {
        if (options.wantsCreation) {
            return run_creation_capture(options);
        }
        // CASE WATCH, ahead of the smoke branch on purpose: `--case-watch` on
        // its own never sets wantsSmoke, and a command line carrying both
        // flags plainly wants the watchable one. Straight into the client with
        // an unconfirmed CreationResult -- the watch drives the HARNESS'S
        // session (default sheet, the scripted hour), so the creation window
        // and the chargen application are both deliberately skipped; run_client
        // guards every chargen touch behind !caseWatch for exactly this.
        // 3D BUILD. ONE WINDOW FOR THE WHOLE LAUNCH, opened here and handed
        // to the creation screen and the world in turn -- see VideoBridge
        // and run_creation_window's header. Opened lazily, after the
        // headless branches above, so a capture or a report still opens
        // nothing.
        const auto open_video = [&options]() {
            render3d::BackendConfig config;
            config.width = options.smoke.session.width;
            config.height = options.smoke.session.height;
            config.windowScale = options.windowScale;
            config.modelDir = rig_model_dir();
            config.weaponDir = weapon_model_dir();
            std::unique_ptr<render3d::Backend> video = render3d::Backend::open(config);
            if (video == nullptr) {
                std::printf("granadad: could not open the window -- closing.\n");
            }
            return video;
        };
        if (options.caseWatch) {
            std::unique_ptr<render3d::Backend> video = open_video();
            if (video == nullptr) {
                return 1;
            }
            return run_client(options, render::CreationResult{}, *video);
        }
        if (options.wantsSmoke) {
            render::SmokeRunConfig smoke = options.smoke;
            if (!smoke.screenshot.empty()) {
                // 3D BUILD: the capture is composited through the backend the
                // window uses, so the PNG is the picture the window shows.
                smoke.shutter = [&options](const render::Session& session,
                                           const render::Framebuffer& software) {
                    return shutter_through_backend(options, session, software);
                };
            }
            const render::SmokeRunResult result = render::runSmoke(smoke);
            std::printf("granadad: %s\n", result.summary.c_str());
            if (!options.smoke.screenshot.empty()) {
                // The PNG and the RUN are two different verdicts, and S5 keeps
                // them apart. A scripted line that fell short still writes its
                // frame -- that frame is evidence OF the shortfall -- and the
                // process still exits non-zero. Printing "FAILED to write" for
                // a file that WAS written sends the next reader after the wrong
                // bug, and it sent me after one.
                std::printf("granadad: wrote %s\n",
                            options.smoke.screenshot.string().c_str());
            }
            if (!options.smoke.refocus.empty()) {
                // PLANNING SPRINT (item #1). THE NUMBER BESIDE THE PICTURE.
                // See SmokeRunResult::characterFocusAtCapture's own header --
                // a PNG proves a border is SOME shade of the accent colour; this
                // prints which shade, so a review does not have to eyeball a
                // fraction off a screenshot.
                std::printf(
                    "granadad: focus at capture -- character=%.3f map=%.3f letters=%.3f"
                    " journal=%.3f\n",
                    static_cast<double>(result.characterFocusAtCapture),
                    static_cast<double>(result.mapFocusAtCapture),
                    static_cast<double>(result.lettersFocusAtCapture),
                    static_cast<double>(result.journalFocusAtCapture));
            }
            if (result.scriptFellShort()) {
                std::printf(
                    "granadad: scripted run landed %d of %d beats -- this frame is NOT a"
                    " picture of what was asked for\n",
                    static_cast<int>(result.scriptedLanded),
                    static_cast<int>(result.scriptedWanted));
            }
            return result.ok ? 0 : 1;
        }
        // #80. A NEW GAME OPENS ON THE ORIGIN SCREEN, NOT ON THE WORLD.
        // Every scripted/headless path above returns before reaching this
        // line -- a capture or a test wants a frame of the Docks (or, now,
        // of the creation flow via --creation), never a frame of one menu
        // blocking another.
        std::unique_ptr<render3d::Backend> video = open_video();
        if (video == nullptr) {
            return 1;
        }
        const render::CreationResult chosen = run_creation_window(options, *video);
        if (!chosen.confirmed) {
            std::printf("granadad: no character was made -- closing.\n");
            return 0;
        }
        std::printf("granadad: playing as %s (%s)\n", chosen.name.c_str(),
                    chosen.originId.c_str());
        // #84 CLOSED THE SEAM #80 LEFT HERE. `chosen` used to stop being read
        // the moment this function returned -- see run_client()'s own header
        // for where the sheet is actually applied now, and why the attribute
        // bonus pool still is not.
        return run_client(options, chosen, *video);
    } catch (const std::exception& error) {
        std::printf("granadad: %s\n", error.what());
        std::printf("granadad: content directory is %s (set %s to move it)\n",
                    granadad::content::contentDir().string().c_str(),
                    granadad::content::kContentDirEnvVar);
        return 1;
    }
}
