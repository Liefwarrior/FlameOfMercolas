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
        "  --time=HH            hour of the day, 0-23 (default 20, dusk)\n"
        "  --fov=DEG            horizontal field of view (default 90)\n"
        "  --spawn=X,Y,Z        spawn tile (default the authored Tarwalk spawn)\n"
        "  --yaw=DEG            spawn facing, 0 = north (default 0)\n"
        "  --sensitivity=N      mouse look, BAM per count (default 14)\n"
        "  --clock=N            simulated seconds per real second (default 1)\n"
        "  --hold               do not walk during --smoke; let the world move\n"
        "  --world=NAME         baked world to load (default docks_surface)\n"
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
                case SDL_EVENT_KEY_DOWN:
                    // A CONVERSATION TAKES THE KEYBOARD. Everything below is
                    // still Session's — the client owns no game logic — but
                    // while somebody is talking to you, the arrows pick topics
                    // rather than turning your head, and Escape ends the
                    // conversation rather than the game.
                    if (session.talking() && !event.key.repeat) {
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
                            session.chooseTopic(
                                static_cast<std::size_t>(event.key.key - SDLK_1));
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
        const bool* keys = session.talking() ? nullptr : SDL_GetKeyboardState(nullptr);
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
                std::printf("granadad: %s %s\n", result.ok ? "wrote" : "FAILED to write",
                            options.smoke.screenshot.string().c_str());
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
