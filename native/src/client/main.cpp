// Granadad: The Darkstreets — client entry point.
//
// SCAFFOLD. This is the build-surface skeleton: it exists so the toolchain,
// the dependency pins and the docker build can be proven end to end before the
// simulation lands. It links SDL3, stb and the sim library and reports what it
// is. The observer's real render loop replaces the body of run_client().
//
// Floats are legal in THIS file and its neighbours under src/client. They are
// not legal anywhere under src/sim.

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

#include "granadad/sim/build_info.hpp"
#include "granadad/sim/fixed.hpp"

namespace {

void print_build_banner() {
    const granadad::sim::BuildInfo info = granadad::sim::build_info();
    std::printf("Granadad: The Darkstreets %.*s (%.*s) [%.*s, %.*s]\n",
                static_cast<int>(info.version.size()), info.version.data(),
                static_cast<int>(info.revision.size()), info.revision.data(),
                static_cast<int>(info.target.size()), info.target.data(),
                static_cast<int>(info.compiler.size()), info.compiler.data());

    int linked = SDL_GetVersion();
    std::printf("SDL runtime %d.%d.%d\n", SDL_VERSIONNUM_MAJOR(linked),
                SDL_VERSIONNUM_MINOR(linked), SDL_VERSIONNUM_MICRO(linked));
}

// Exercises the deterministic primitives without touching SDL, so it can be run
// on a machine with no display. `granadad.exe --selftest` is the cheapest
// possible "did the cross-compile actually produce a working binary" check.
int run_selftest() {
    using namespace granadad::sim;

    struct Case {
        const char* name;
        bool ok;
    };
    const Case cases[] = {
        {"wrap_add overflow", wrap_add(2147483647, 1) == -2147483648},
        {"wrap_sub underflow", wrap_sub(-2147483648, 1) == 2147483647},
        {"q16 identity", q16_mul(Q16_ONE, Q16_ONE) == Q16_ONE},
        {"floor_div negative", floor_div(-1, 32) == -1},
        {"floor_mod negative", floor_mod(-1, 32) == 31},
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

int run_client() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Granadad: The Darkstreets", 1280, 720, 0);
    if (window == nullptr) {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        if (renderer != nullptr) {
            SDL_SetRenderDrawColor(renderer, 12, 12, 16, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
        }
    }

    if (renderer != nullptr) {
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    print_build_banner();

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) {
            return run_selftest();
        }
        if (std::strcmp(argv[i], "--version") == 0) {
            return 0;  // banner already printed
        }
    }

    return run_client();
}
