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
#include <limits>
#include <string>

#include "granadad/content/content_dir.hpp"
#include "granadad/sim/compound.hpp"
#include "granadad/render/capture.hpp"
#include "granadad/render/framebuffer.hpp"
#include "granadad/render/session.hpp"
#include "granadad/render/step_pump.hpp"
#include "granadad/sim/angle.hpp"
#include "granadad/sim/build_info.hpp"
#include "granadad/sim/docks.hpp"
#include "granadad/sim/fixed.hpp"
#include "granadad/sim/player.hpp"

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

// ---------------------------------------------------------------------------
// command line
// ---------------------------------------------------------------------------

struct Options {
    render::SmokeRunConfig smoke;
    bool wantsSmoke = false;
    bool wantsWindow = true;
    int windowScale = 2;
    /// Mouse look sensitivity, BAM per mouse count.
    int sensitivity = 14;
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
        "  --scale=N            window / capture upscale, nearest neighbour (default 2)\n"
        "  --time=HH            hour of the day, 0-23 (default 20, dusk). A\n"
        "                       scripted line sets its own hour when this is not\n"
        "                       given -- --skyrun wants 22, when Finch is in\n"
        "                       the snug -- and never overrides one that is\n"
        "  --fov=DEG            horizontal field of view (default 90)\n"
        "  --spawn=X,Y,Z        spawn tile (default the authored Tarwalk spawn)\n"
        "  --yaw=DEG            spawn facing, 0 = north (default 0)\n"
        "  --sensitivity=N      mouse look, BAM per count (default 14)\n"
        "  --clock=N            simulated seconds per real second (default 1)\n"
        "  --hold               do not walk during --smoke; let the world move\n"
        "  --talk               open a conversation before the shutter goes\n"
        "  --topic=N[,N...]     pick these topics once it is open (1-based, as\n"
        "                       the numbers printed beside them on screen)\n"
        "  --offer=N            name this number across a counter\n"
        "  --cursor=N           put the topic cursor on row N without picking\n"
        "                       it, so a frame can be taken OF a long label\n"
        "  --again              close the conversation and open it again\n"
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
        "  --nemesis[=WHERE]    lose a fist fight to a named labourer three\n"
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
        "                       and empty it. WHERE is box, taproom or street\n"
        "  --world=NAME         baked world to load (default docks_surface)\n"
        "  --ward[=DAYS]        run the ward's compounds -- courtyard farms,\n"
        "                       ground rents, bonds and the priest's hearings\n"
        "                       -- for DAYS (default 730) and print what the\n"
        "                       land gave, what the mouths took and who went\n"
        "                       hungry. No window\n"
        "  --selftest           deterministic primitives only, no window\n"
        "  --version            print the build banner and exit\n");
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
        if (std::strcmp(arg, "--selftest") == 0) {
            stop = true;
            exitCode = run_selftest();
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
            options.wantsWindow = false;
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
            options.sensitivity = std::clamp(std::atoi(value), 1, 200);
        } else if (starts_with(arg, "--clock=", &value)) {
            options.smoke.session.clockScale = std::clamp(std::atoi(value), 1, 3600);
        } else if (std::strcmp(arg, "--hold") == 0) {
            options.smoke.walk = false;
        } else if (std::strcmp(arg, "--talk") == 0) {
            options.smoke.talk = true;
        } else if (std::strcmp(arg, "--again") == 0) {
            options.smoke.talk = true;
            options.smoke.again = true;
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
// the window
// ---------------------------------------------------------------------------

int run_client(const Options& options) {
    render::Session session(options.smoke.session);
    if (!session.body().spawnedLegally()) {
        std::printf("granadad: spawn tile is not standable -- check --spawn\n");
        return 1;
    }
    std::printf("granadad: %s loaded, %zu lamp(s), art=%s\n",
                options.smoke.session.world.c_str(), session.lampCount(),
                session.atlas().fromAuthoredArt() ? "content/art/custom" : "procedural fallback");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    const int windowW = options.smoke.session.width * options.windowScale;
    const int windowH = options.smoke.session.height * options.windowScale;
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

    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                          options.smoke.session.width, options.smoke.session.height);
    if (texture != nullptr) {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }

    render::Framebuffer frame(options.smoke.session.width, options.smoke.session.height);

    bool mouseLook = true;
    SDL_SetWindowRelativeMouseMode(window, true);

    // The body advances on a fixed 60 Hz cadence whatever the frame rate does,
    // so what the simulation sees is a whole number of identical steps and a
    // slow machine plays the same game as a fast one. StepPump owns that, and
    // owns the mouse-look carry that a frame producing zero steps used to drop
    // on the floor — see granadad/render/step_pump.hpp.
    render::StepPump pump;
    using Clock = std::chrono::steady_clock;
    Clock::time_point last = Clock::now();

    bool running = true;
    std::int64_t frames = 0;
    while (running) {
        sim::MoveInput held;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                // VERIFICATION GAP (S3): NOTHING TESTS THIS SWITCH. Every branch
                // below calls a Session method the suite drives directly, so the
                // behaviour is covered and the BINDING is not -- a key wired to
                // the wrong verb, or a conversation that fails to capture the
                // keyboard, would ship green. It is the same gap S1 and S2 have
                // in this file and it needs an input-layer harness, not another
                // case.
                case SDL_EVENT_KEY_DOWN:
                    // A CONVERSATION TAKES THE KEYBOARD. Everything below is
                    // still Session's — the client owns no game logic — but
                    // while somebody is talking to you, the arrows pick topics
                    // rather than turning your head, and Escape ends the
                    // conversation rather than the game.
                    if (session.talking() && !event.key.repeat) {
                        if (session.forging()) {
                            // The workbench takes the keyboard the way the
                            // counter does. UP/DOWN walks the five fields,
                            // LEFT/RIGHT changes the one under the cursor,
                            // ENTER asks for it, ESC puts the tools down.
                            if (event.key.key == SDLK_UP || event.key.key == SDLK_W) {
                                session.moveForgeField(-1);
                                break;
                            }
                            if (event.key.key == SDLK_DOWN || event.key.key == SDLK_S) {
                                session.moveForgeField(1);
                                break;
                            }
                            if (event.key.key == SDLK_LEFT || event.key.key == SDLK_A) {
                                session.adjustForge(-1);
                                break;
                            }
                            if (event.key.key == SDLK_RIGHT || event.key.key == SDLK_D) {
                                session.adjustForge(1);
                                break;
                            }
                            if (event.key.key == SDLK_RETURN || event.key.key == SDLK_E) {
                                session.commitForge();
                                break;
                            }
                            if (event.key.key == SDLK_ESCAPE) {
                                session.endForge();
                                break;
                            }
                            break;
                        }
                        if (session.haggling()) {
                            const int stride =
                                (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 5 : 1;
                            if (event.key.key == SDLK_LEFT || event.key.key == SDLK_DOWN) {
                                session.adjustOffer(-stride);
                                break;
                            }
                            if (event.key.key == SDLK_RIGHT || event.key.key == SDLK_UP) {
                                session.adjustOffer(stride);
                                break;
                            }
                            if (event.key.key == SDLK_RETURN || event.key.key == SDLK_E) {
                                session.makeOffer();
                                break;
                            }
                            if (event.key.key == SDLK_T) {
                                session.takeAskingPrice();
                                break;
                            }
                            if (event.key.key == SDLK_ESCAPE) {
                                session.closeConversation();
                                break;
                            }
                            break;
                        }
                        if (event.key.key == SDLK_ESCAPE) {
                            session.closeConversation();
                            break;
                        }
                        if (event.key.key == SDLK_UP || event.key.key == SDLK_W) {
                            session.moveTopicCursor(-1);
                            break;
                        }
                        if (event.key.key == SDLK_DOWN || event.key.key == SDLK_S) {
                            session.moveTopicCursor(1);
                            break;
                        }
                        if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                            // The number printed BESIDE the topic, which is a
                            // slot on the visible page and not an index into
                            // the whole list. On page two, 1 is the tenth
                            // topic. See kTopicPageSize.
                            session.chooseVisibleTopic(
                                static_cast<int>(event.key.key - SDLK_1));
                            break;
                        }
                        if (event.key.key == SDLK_0) {
                            // The "0 MORE (2/3)" row. Every topic is reachable
                            // by a printed key, however long the list gets.
                            session.nextTopicPage();
                            break;
                        }
                        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_E) {
                            session.interact();
                            break;
                        }
                        if (event.key.key == SDLK_F) {
                            // Still legal, and it ends the conversation the way
                            // a punch always ends a conversation.
                            session.punch();
                            break;
                        }
                        break;
                    }
                    // S9. THE WIRE OWNS THE KEYBOARD WHILE IT IS IN. Same
                    // shape the conversation and the workbench already use: a
                    // mode the SIMULATION is in, which the client reads and
                    // routes for. W and S move the pick, SPACE probes, F puts a
                    // shoulder to the lid and ESC takes the wire out.
                    if (session.picking()) {
                        if (event.key.key == SDLK_UP || event.key.key == SDLK_W) {
                            session.movePick(1);
                            break;
                        }
                        if (event.key.key == SDLK_DOWN || event.key.key == SDLK_S) {
                            session.movePick(-1);
                            break;
                        }
                        if (event.key.key == SDLK_SPACE && !event.key.repeat) {
                            session.probeLock();
                            break;
                        }
                        if (event.key.key == SDLK_F && !event.key.repeat) {
                            session.forceLock();
                            break;
                        }
                        if (event.key.key == SDLK_ESCAPE) {
                            session.stopPicking();
                            break;
                        }
                        break;
                    }
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    } else if (event.key.key == SDLK_TAB) {
                        mouseLook = !mouseLook;
                        SDL_SetWindowRelativeMouseMode(window, mouseLook);
                    } else if (event.key.key == SDLK_E && !event.key.repeat) {
                        // Talk. What used to be one sentence and a silent
                        // purchase is now a topic list — see Session::interact.
                        session.interact();
                    } else if (event.key.key == SDLK_F && !event.key.repeat) {
                        session.punch();
                    } else if (event.key.key == SDLK_R && !event.key.repeat) {
                        session.restHere();
                    } else if (event.key.key == SDLK_SPACE && !event.key.repeat) {
                        // UP. One key for the two answers to "get me over
                        // that": a mantle onto the ledge you are facing, or a
                        // leap across the gap in front of you. Which one the
                        // geometry wants is not the player's problem.
                        session.climb();
                    } else if (event.key.key == SDLK_X && !event.key.repeat) {
                        session.dropDown();
                    } else if (event.key.key == SDLK_G && !event.key.repeat) {
                        // Hands on whatever is here -- a guest's strongbox, the
                        // bale in the snug, a rat on the floor, or the wire
                        // Finch sells his own. S9: a LOCKED box puts the wire in
                        // rather than opening itself.
                        session.steal();
                    } else if (event.key.key == SDLK_C && !event.key.repeat) {
                        // S9. Down on your haunches. Half speed, and worth more
                        // than twenty levels of skill.
                        session.toggleCrouch();
                    } else if (event.key.key == SDLK_T && !event.key.repeat) {
                        // S9. A hand in the coat of whoever is at your elbow,
                        // with no conversation open and nobody looking at you.
                        session.lift();
                    } else if (event.key.key == SDLK_F12) {
                        render::SmokeRunConfig shot = options.smoke;
                        shot.screenshot = "granadad-screenshot.png";
                        const render::Framebuffer output =
                            render::upscaleNearest(frame, options.windowScale);
                        if (render::writePng(output, shot.screenshot)) {
                            std::printf("granadad: wrote %s\n", shot.screenshot.string().c_str());
                        }
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (mouseLook) {
                        // Into the pump, not into a frame local. A frame that
                        // runs no step must still keep the rotation.
                        pump.addLook(static_cast<std::int32_t>(event.motion.xrel) *
                                         options.sensitivity,
                                     -static_cast<std::int32_t>(event.motion.yrel) *
                                         options.sensitivity);
                    }
                    break;
                default:
                    break;
            }
        }

        // Held keys move the body — unless somebody is talking to you, in which
        // case W and S are walking the topic list and must not also walk you
        // out of the room.
        const bool* keys =
            (session.talking() || session.picking()) ? nullptr : SDL_GetKeyboardState(nullptr);
        if (keys != nullptr) {
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
                held.forward += 1;
            }
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
                held.forward -= 1;
            }
            if (keys[SDL_SCANCODE_D]) {
                held.strafe += 1;
            }
            if (keys[SDL_SCANCODE_A]) {
                held.strafe -= 1;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                held.turn += 1;
            }
            if (keys[SDL_SCANCODE_LEFT]) {
                held.turn -= 1;
            }
            held.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        }

        const Clock::time_point now = Clock::now();
        const double frameSeconds = std::chrono::duration<double>(now - last).count();
        last = now;
        const std::int32_t steps = pump.advance(frameSeconds);
        for (std::int32_t i = 0; i < steps; ++i) {
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

    std::printf("granadad: %lld frame(s), body ended at (%d,%d,z%d)\n",
                static_cast<long long>(frames), session.body().tileX(), session.body().tileY(),
                session.body().band());

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
            if (result.scriptFellShort()) {
                std::printf(
                    "granadad: scripted run landed %d of %d beats -- this frame is NOT a"
                    " picture of what was asked for\n",
                    static_cast<int>(result.scriptedLanded),
                    static_cast<int>(result.scriptedWanted));
            }
            return result.ok ? 0 : 1;
        }
        return run_client(options);
    } catch (const std::exception& error) {
        std::printf("granadad: %s\n", error.what());
        std::printf("granadad: content directory is %s (set %s to move it)\n",
                    granadad::content::contentDir().string().c_str(),
                    granadad::content::kContentDirEnvVar);
        return 1;
    }
}
