#pragma once

// Where the baked worlds are, resolved at RUN TIME.
//
// Runtime first, configure-time second, and that order is the whole point.
// Binaries here are cross-compiled for Windows inside a Linux container, so the
// path baked in at configure time is the CONTAINER's /src/content -- a path
// that does not exist on the machine the .exe runs on. While the directory was
// a compile-time constant the Windows build could be linked but never executed,
// which meant the format reader was proven on GCC/Linux and completely unproven
// on the toolchain that ships. See task #75.
//
// The compile-time fallback stays because the Linux host build inside the
// container has no environment set and should keep working with no ceremony.
//
// ONE implementation, deliberately. The content fingerprint, the sim test suite
// and the twin-run gate all resolve the directory, and the docker build asserts
// the env var actually wins (same worlds under a different path -> byte
// identical report; bogus path -> non-zero exit). Three copies of this logic
// would mean that assertion covered one of them.

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef GRANADAD_CONTENT_DIR_DEFAULT
#error "Define GRANADAD_CONTENT_DIR_DEFAULT for any target including content_dir.hpp"
#endif

namespace granadad::content {

/// The environment variable that names the content/ directory at run time.
inline constexpr const char* kContentDirEnvVar = "GRANADAD_CONTENT_DIR";

/// The repo's content/ directory.
inline std::filesystem::path contentDir() {
    // getenv, not _dupenv_s: this reads a path the owner set one line earlier
    // in their own shell, and the C standard function is the one that exists on
    // every toolchain here. MSVC's C4996 for it is silenced per target in
    // CMakeLists rather than by writing two code paths.
    const char* fromEnv = std::getenv(kContentDirEnvVar);
    if (fromEnv != nullptr && *fromEnv != '\0') {
        return std::filesystem::path(fromEnv);
    }
    return std::filesystem::path(GRANADAD_CONTENT_DIR_DEFAULT);
}

/// content/maps/baked, where the .trojsav worlds live.
inline std::filesystem::path bakedDir() {
    return contentDir() / "maps" / "baked";
}

inline std::filesystem::path bakedMap(const std::string& name) {
    return bakedDir() / (name + ".trojsav");
}

}  // namespace granadad::content
