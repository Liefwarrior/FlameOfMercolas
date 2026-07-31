#include "granadad/sim/build_info.hpp"

#ifndef GRANADAD_VERSION
#define GRANADAD_VERSION "0.0.0-dev"
#endif

// Stamped by the docker build (-DGRANADAD_REVISION=...). A local preset build
// leaves it as "local", which is exactly the signal you want when a bug report
// arrives with a binary nobody can reproduce.
#ifndef GRANADAD_REVISION
#define GRANADAD_REVISION "local"
#endif

namespace granadad::sim {

BuildInfo build_info() noexcept {
    return BuildInfo{
        GRANADAD_VERSION,
        GRANADAD_REVISION,
#if defined(_WIN32)
        "windows-x86_64",
#elif defined(__linux__)
        "linux-x86_64",
#else
        "unknown",
#endif
#if defined(__MINGW32__)
        "mingw-w64-gcc",
#elif defined(__clang__)
        "clang",
#elif defined(_MSC_VER)
        "msvc",
#elif defined(__GNUC__)
        "gcc",
#else
        "unknown",
#endif
    };
}

}  // namespace granadad::sim
