// Granadad: The Darkstreets — client entry point.
//
// Everything that draws lives in granadad-render, which knows nothing about
// SDL. This file is the window, the keyboard, the mouse and the fixed-timestep
// loop that turns wall-clock into a whole number of MOVEMENT STEPS — and
// nothing else. That split is what lets `--screenshot` work with no window at
// all, and it is why the docker gate can render a frame of the Docks on every
// build.
//
// Floats are legal in this file and its neighbours under src/client. They are
// not legal anywhere under src/sim.

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
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

#include "granadad/content/content_dir.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/sim/compound.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/controls.hpp"
#include "granadad/render/creation.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/step_pump.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/build_info.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/engine.hpp"
#include "granadad/sim/fixed.hpp"
#include "granadad/sim/player.hpp"
#include "granadad/sim/tile_query.hpp"
#include "granadad/sim/ward_actors.hpp"

namespace {

namespace render = granadad::render;
namespace sim = granadad::sim;

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

// ---------------------------------------------------------------------------
// command line
// ---------------------------------------------------------------------------

struct Options {
    render::SmokeRunConfig smoke;
    bool wantsSmoke = false;
    int windowScale = 2;
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
        "  --screenshot=PATH    write the captured frame as a PNG (implies no window)\n"
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
        "  --punch              VERIFICATION ONLY: retry the punch key until\n"
        "                       one lands, before the shutter goes\n"
        "  --creation[=STEP]    capture the origin-select/customize flow with\n"
        "                       no window and no world. STEP is origin\n"
        "                       (default), customize (CUSTOM, a few points\n"
        "                       spent), devin or gabri (their own fixed sheet)\n"
        "  --settle             run a scripted overlay's open animation to\n"
        "                       completion before the shutter, instead of\n"
        "                       capturing the frame it opened on (this is\n"
        "                       now the DEFAULT for a screenshot -- see\n"
        "                       --no-settle)\n"
        "  --no-settle          capture the overlay's opening bump instead\n"
        "                       of its settled, fully-open frame -- only\n"
        "                       useful for proving the transition itself\n"
        "                       does not flash on its first drawn frame\n"
        "  --settle-steps=N     VERIFICATION ONLY: run exactly N zero-input\n"
        "                       steps before the shutter instead of 16-or-0,\n"
        "                       so a mid-transition frame of the panel/menu\n"
        "                       eases can actually be photographed\n"
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
        "  --nemesis[=WHERE]    lose a fist fight to a named laborer three\n"
        "                       times and watch him rise: a rung, a guild\n"
        "                       with members in it, a permanent cut of the\n"
        "                       ward's prices and his name on the compound\n"
        "                       roll as a Den Duke. WHERE is talk or away\n"
        "  --contract[=WHERE]   play the ward's own bounty -- take it off the\n"
        "                       Watch, get the Flame's mark, hunt the taproom\n"
        "                       and get paid. WHERE is talk or away\n"
        "  --burgle[=WHERE]     rob the Gilded Gull at four in the morning --\n"
        "                       crouch, cross a dark taproom unseen, lift a\n"
        "                       purse, up the stair, wire into a guest's box\n"
        "                       and empty it. WHERE is box, lock (the wire in\n"
        "                       the next box), taproom or street\n"
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
        "                       lead is read), or keys (the in-game controls\n"
        "                       page)\n"
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
        "  --version            print the build banner and exit\n"
        "\n"
        "IN THE GAME: WASD moves, the mouse looks, SHIFT sprints, CTRL\n"
        "crouches (both HOLD and TAP), SPACE jumps, E talks, TAB opens your\n"
        "casebook, F1 lists every key and F2 rebinds them.\n"
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
        } else if (std::strcmp(arg, "--punch") == 0) {
            // VERIFICATION ONLY. See SmokeRunConfig::punch's own header.
            options.smoke.punch = true;
            options.wantsSmoke = true;
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

    const render::Action action = session.controls().actionFor(key);
    // The five list movements, in the vocabulary of intent. Arrows always work
    // as well, bound or not, because a list is the one place arrow keys are
    // unambiguous.
    const bool up = key == render::Key::Up || action == render::Action::Forward ||
                    action == render::Action::QuickPrev;
    const bool downward = key == render::Key::Down || action == render::Action::Back ||
                          action == render::Action::QuickNext;
    const bool leftward = key == render::Key::Left || action == render::Action::StrafeLeft;
    const bool rightward = key == render::Key::Right || action == render::Action::StrafeRight;
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
            const int stride = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 5 : 1;
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

    if (session.casebookOpen() || session.keysOpen() || session.characterOpen() ||
        session.mapOpen() || session.lettersOpen()) {
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
    if (options.creationStep == "customize") {
        flow.moveOriginCursor(2);  // CUSTOM
        flow.chooseOrigin();
        for (int i = 0; i < 3; ++i) {
            flow.moveCustomizeCursor(1);
            flow.adjustCustomizeRow(1);
        }
    } else if (options.creationStep == "devin") {
        flow.chooseOrigin();
    } else if (options.creationStep == "gabri") {
        flow.moveOriginCursor(1);
        flow.chooseOrigin();
    } else if (options.creationStep != "origin") {
        std::printf("granadad: --creation wants origin, customize, devin or gabri\n");
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
// #80: the origin-select/customize flow, in its own small window
// ---------------------------------------------------------------------------
//
// ITS OWN WINDOW RATHER THAN A RESTRUCTURED run_client(). No Session and no
// world exist yet at this point in the boot sequence -- this screen has to
// run BEFORE either -- and the alternative (hoisting run_client's own forty
// lines of SDL setup above the Session construction they currently follow)
// touches code every other page in this build was proven against. A second,
// self-contained SDL_Init/window/renderer/texture that tears itself down
// before run_client's own setup runs is a few lines longer and a great deal
// safer to review, and it costs nothing at runtime a player would notice:
// SDL_QuitSubSystem below balances the SDL_Init here, so run_client's own
// SDL_Init(VIDEO | GAMEPAD) right after this returns is an ordinary fresh
// init and not a double-init of anything.
//
// Returns an UNCONFIRMED CreationResult if the player closed the window --
// main() treats that exactly like closing the game, not like starting one.
render::CreationResult run_creation_window(const Options& options) {
    render::CreationFlow flow(granadad::content::contentDir());

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init (creation) failed: %s\n", SDL_GetError());
        return {};
    }
    // DON'T STEAL FOCUS ON LAUNCH. SDL's own default is to activate a window
    // the moment SDL_ShowWindow (which SDL_CreateWindow calls internally)
    // shows it -- which is exactly the OS's ordinary activate-on-show
    // behaviour for a foreground-launched process, and exactly what Eli asked
    // not to have happen. This is the FIRST window a real launch opens (see
    // main()'s own call order), so the hint has to be set here too, not only
    // in run_client() below -- setting it on only the second window would
    // leave THIS one still stealing focus the moment it appears. Set to "0"
    // BEFORE SDL_CreateWindow, per SDL's own documented contract; it does
    // nothing to ordinary click-to-focus, which the OS still grants normally.
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    const int width = options.smoke.session.width;
    const int height = options.smoke.session.height;
    SDL_Window* window =
        SDL_CreateWindow("Granadad: The Darkstreets", width * options.windowScale,
                         height * options.windowScale, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::printf("SDL_CreateWindow (creation) failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return {};
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::printf("SDL_CreateRenderer (creation) failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return {};
    }
    SDL_SetRenderLogicalPresentation(renderer, width, height,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(renderer, 1);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STREAMING, width, height);
    if (texture != nullptr) {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }

    render::Framebuffer frame(width, height);
    bool cancelled = false;

    while (!flow.done() && !cancelled) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                cancelled = true;
                continue;
            }
            if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
                continue;
            }
            const render::Key key = key_of_scancode(event.key.scancode);
            if (flow.step() == render::CreationStep::Origin) {
                if (key == render::Key::Up || key == render::Key::W) {
                    flow.moveOriginCursor(-1);
                } else if (key == render::Key::Down || key == render::Key::S) {
                    flow.moveOriginCursor(1);
                } else if (key == render::Key::Enter) {
                    flow.chooseOrigin();
                } else if (key == render::Key::Escape) {
                    cancelled = true;
                }
                continue;
            }
            // Customize.
            if (flow.editingName()) {
                if (key == render::Key::Enter) {
                    flow.chooseCustomizeRow();
                } else if (key == render::Key::Escape) {
                    flow.backToOrigin();
                } else if (key == render::Key::Backspace) {
                    flow.backspaceName();
                } else {
                    const char typed = name_char_of_key(key);
                    if (typed != '\0') {
                        flow.typeNameChar(typed);
                    }
                }
                continue;
            }
            if (key == render::Key::Up || key == render::Key::W) {
                flow.moveCustomizeCursor(-1);
            } else if (key == render::Key::Down || key == render::Key::S) {
                flow.moveCustomizeCursor(1);
            } else if (key == render::Key::Left || key == render::Key::A) {
                flow.adjustCustomizeRow(-1);
            } else if (key == render::Key::Right || key == render::Key::D) {
                flow.adjustCustomizeRow(1);
            } else if (key == render::Key::Enter) {
                flow.chooseCustomizeRow();
            } else if (key == render::Key::Escape) {
                flow.backToOrigin();
            }
        }

        flow.advance();
        render::drawCreation(frame, flow);
        if (texture != nullptr) {
            SDL_UpdateTexture(texture, nullptr, frame.pixels().data(), width * 4);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }
    }

    const render::CreationResult result = flow.done() ? flow.result() : render::CreationResult{};

    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return result;
}

// ---------------------------------------------------------------------------
// the window
// ---------------------------------------------------------------------------

int run_client(const Options& options, const render::CreationResult& chosen) {
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
    start.openingPage = true;
    if (!start.timeOfDayGiven) {
        start.timeOfDay = 8 * 3600;
    }
    render::Session session(start);
    if (!session.body().spawnedLegally()) {
        std::printf("granadad: spawn tile is not standable -- check --spawn\n");
        return 1;
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
    // ATTRIBUTES ARE DELIBERATELY LEFT UNCROSSED. chargen.hpp's own header
    // states the attribute bonus pool has no runtime reader yet -- no skill
    // check in this build weighs an AttributeId, only a level -- so wiring
    // AttributeBlock into a mechanic that does not exist would be inventing
    // one, not closing a seam. Chargen::apply() is [[nodiscard]] for exactly
    // this reason and its answer is discarded here on purpose, not silently
    // dropped.
    {
        sim::SkillTrack& playerSkills = session.tavern().dialogue().skills();
        if (chosen.companion.loaded()) {
            const std::int32_t matched = chosen.companion.applyStartingSkills(playerSkills);
            std::printf("granadad: %s's sheet set %d skill(s)\n", chosen.companion.name().c_str(),
                        static_cast<int>(matched));
        } else {
            (void)chosen.chargen.apply(playerSkills);
            std::printf("granadad: custom sheet set %d skill(s)\n",
                        static_cast<int>(chosen.chargen.picks().size()));
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

    std::printf("granadad: %s loaded, %zu lamp(s), art=%s\n",
                options.smoke.session.world.c_str(), session.lampCount(),
                session.atlas().fromAuthoredArt() ? "content/art/custom" : "procedural fallback");
    std::printf("granadad: controls from %s\n", controlsFile.string().c_str());

    // SDL_INIT_GAMEPAD as well as VIDEO. A pad that is plugged in should just
    // work; asking a player to turn one on in a menu is a 2006 courtesy.
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    // DON'T STEAL FOCUS ON LAUNCH. See the identical call and comment in
    // run_creation_window() above -- this is the SECOND window a real launch
    // opens. Set again here rather than trusted to carry over: SDL does not
    // document SDL_SetHint as scoped to a subsystem's lifetime, but this file
    // does not lean on that either way -- run_creation_window() fully quits
    // SDL_INIT_VIDEO and this function calls SDL_Init from scratch, so
    // stating the hint again at the one call site that actually needs it is
    // one line and removes the question entirely.
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    const int windowW = start.width * options.windowScale;
    const int windowH = start.height * options.windowScale;
    SDL_Window* window =
        SDL_CreateWindow("Granadad: The Darkstreets", windowW, windowH, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // Nearest neighbour, always. The chunkiness is the art direction.
    SDL_SetRenderLogicalPresentation(renderer, options.smoke.session.width,
                                     options.smoke.session.height,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    // VSYNC ON. Without it the renderer free-runs, which burns a core to draw
    // frames the monitor throws away AND -- the part that matters for feel --
    // hands the compositor torn frames. The step pump already decouples the
    // simulation from the frame rate, so this costs nothing in latency that the
    // display was not going to cost anyway.
    SDL_SetRenderVSync(renderer, 1);

    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                          options.smoke.session.width, options.smoke.session.height);
    if (texture != nullptr) {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }

    render::Framebuffer frame(options.smoke.session.width, options.smoke.session.height);

    bool mouseLook = true;
    SDL_SetWindowRelativeMouseMode(window, true);

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
    bool quickWheelOpen = false;

    bool running = true;
    std::int64_t frames = 0;
    while (running) {
        sim::MoveInput held;
        SDL_Event event;
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
                    quickSlot = (quickSlot + 1) % 10;
                    session.selectQuickSlot(quickSlot);
                    return;
                }
                if (key == render::Key::PadDown || key == render::Key::Down ||
                    key == render::Key::PadLeft || key == render::Key::Left) {
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
                // #85. Was Punch, renamed to say what it will still be doing
                // once armed combat exists: swinging whatever is in the hand.
                case render::Action::Attack:
                    session.punch();
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
                        session.pauseOpen()) {
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
            }
        };

        while (SDL_PollEvent(&event)) {
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
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    if (!route_menu_key(session, key_of_pad_button(event.gbutton.button))) {
                        pressed(key_of_pad_button(event.gbutton.button));
                    }
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    released(key_of_pad_button(event.gbutton.button));
                    break;
                case SDL_EVENT_KEY_DOWN: {
                    const render::Key key = key_of_scancode(event.key.scancode);
                    if (event.key.repeat) {
                        // A held key is not a stream of presses. The one place
                        // repeat is wanted is walking a long list, and the menu
                        // router below takes it.
                        (void)route_menu_key(session, key);
                        break;
                    }
                    // TAB IS THE JOURNAL NOW. Freeing the mouse moved to F3 --
                    // see the keys page. A player who wants their cursor back
                    // is almost always a player who wants to alt-tab, and
                    // alt-tab already works.
                    //
                    // AND IT YIELDS TO A BINDING. F3 is unbound by default, so
                    // this is free; the moment somebody binds a verb to it, the
                    // verb wins and the window keeps the mouse. A hard-coded key
                    // that quietly outranks the rebinding screen is the exact
                    // shape of bug the rest of this task was about.
                    if (key == render::Key::F3 &&
                        session.controls().actionFor(key) == render::Action::Count) {
                        mouseLook = !mouseLook;
                        SDL_SetWindowRelativeMouseMode(window, mouseLook);
                        break;
                    }
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
                    const render::Key key =
                        event.wheel.y > 0 ? render::Key::WheelUp : render::Key::WheelDown;
                    if (!route_menu_key(session, key)) {
                        pressed(key);
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION:
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
            session.talking() || session.picking() ||
            (session.menuOpen() && !session.firstRun()) || session.pauseOpen();
        const bool* keys = listening ? nullptr : SDL_GetKeyboardState(nullptr);
        const Uint32 mouseButtons = listening ? 0U : SDL_GetMouseState(nullptr, nullptr);
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

        held.crouch = crouch.active();

        // THE ONLY THING THAT ACTUALLY QUITS. Session never touches SDL, so a
        // QUIT confirmed on the pause menu sets a flag and this is the one
        // place that reads it -- one frame after the confirming press, so the
        // menu's own last frame still draws before the window goes.
        if (session.quitRequested()) {
            running = false;
        }

        const Clock::time_point now = Clock::now();
        const double frameSeconds = std::chrono::duration<double>(now - last).count();
        last = now;
        const std::int32_t steps = pump.advance(frameSeconds);
        for (std::int32_t i = 0; i < steps; ++i) {
            ++stepClock;
            session.step(pump.nextStepInput(held));
        }

        session.drawFrame(frame);
        ++frames;

        if (texture != nullptr) {
            SDL_UpdateTexture(texture, nullptr, frame.pixels().data(),
                              options.smoke.session.width * 4);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }
    }

    // A REBINDING SURVIVES THE PROCESS. Written on the way out as well as at
    // the moment it is made, so a slider moved on the options page is still
    // moved tomorrow.
    (void)render::saveControls(session.controls(), controlsFile);

    std::printf("granadad: %lld frame(s), body ended at (%d,%d,z%d)\n",
                static_cast<long long>(frames), session.body().tileX(), session.body().tileY(),
                session.body().band());

    if (pad != nullptr) {
        SDL_CloseGamepad(pad);
    }
    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
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
        if (options.wantsSmoke) {
            const render::SmokeRunResult result = render::runSmoke(options.smoke);
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
        const render::CreationResult chosen = run_creation_window(options);
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
        return run_client(options, chosen);
    } catch (const std::exception& error) {
        std::printf("granadad: %s\n", error.what());
        std::printf("granadad: content directory is %s (set %s to move it)\n",
                    granadad::content::contentDir().string().c_str(),
                    granadad::content::kContentDirEnvVar);
        return 1;
    }
}
