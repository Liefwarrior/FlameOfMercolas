#pragma once

// Where the baked worlds are, resolved at RUN TIME.
//
// Three sources, in this order, and the order is the whole point:
//
//   1. $GRANADAD_CONTENT_DIR       an explicit override always wins
//   2. the executable's own tree   search upward from argv[0]'s real directory
//                                  for a `content/maps/baked`
//   3. the configure-time default  the last resort, and on a cross build it is
//                                  a path inside the build container
//
// WHY 1 IS FIRST. Binaries here are cross-compiled for Windows inside a Linux
// container, so the path baked in at configure time is the CONTAINER's
// /src/content -- a path that does not exist on the machine the .exe runs on.
// While the directory was a compile-time constant the Windows build could be
// linked but never executed, which meant the format reader was proven on
// GCC/Linux and completely unproven on the toolchain that ships. See task #75.
// The docker build asserts that this variable is actually read (same worlds
// under a different path -> byte-identical report; bogus path -> non-zero
// exit), and that assertion only means something while the variable outranks
// everything else.
//
// WHY 2 EXISTS AT ALL (S2). It shipped broken without it. `dist\granadad.exe`
// -- the command README.md documents as "the game" -- died on the owner's own
// machine with
//
//     granadad: cannot open TROJSAV: /src/content\maps\baked\docks_surface.trojsav
//
// because nothing sets the environment variable for the GAME. Only
// scripts/verify-windows.ps1 sets it, and only for the test binaries, so the
// build gate was structurally blind to it. The exe knows where it is; a game
// that has to be told where its own data lives is a game that does not start.
// dist/granadad.exe finds <repo>/content by walking up one level. An installed
// build finds content/ beside the executable on the first iteration.
//
// ONE implementation, deliberately. The content fingerprint, the sim test
// suite, the twin-run gate and the client all resolve the directory here.
// Three copies of this logic would mean the docker build's assertion covered
// one of them.

#include <filesystem>
#include <string>

#ifndef GRANADAD_CONTENT_DIR_DEFAULT
#error "Define GRANADAD_CONTENT_DIR_DEFAULT for any target including content_dir.hpp"
#endif

namespace granadad::content {

/// The environment variable that names the content/ directory at run time.
inline constexpr const char* kContentDirEnvVar = "GRANADAD_CONTENT_DIR";

/// How many directory levels above the executable are searched for a content
/// tree. Two is enough for `<install>/bin/granadad.exe` and for the repo's own
/// `<repo>/dist/granadad.exe`; more than that and a stray content/ somewhere up
/// the filesystem could be picked up by accident.
inline constexpr int kContentDirSearchLevels = 3;

/// The directory the running executable lives in, or an empty path when the
/// platform will not say. Windows asks the loader; Linux reads /proc/self/exe.
[[nodiscard]] std::filesystem::path executableDir();

/// Searches `start` and up to kContentDirSearchLevels of its parents for a
/// directory holding `content/maps/baked`, and returns that `content`. Returns
/// an empty path when there is none.
///
/// Split out from contentDir() so it can be tested against a synthetic tree
/// rather than against wherever the test binary happens to have been built.
[[nodiscard]] std::filesystem::path searchForContentDir(const std::filesystem::path& start);

/// The repo's content/ directory. See the header comment for the search order.
[[nodiscard]] std::filesystem::path contentDir();

/// content/maps/baked, where the .trojsav worlds live.
inline std::filesystem::path bakedDir() {
    return contentDir() / "maps" / "baked";
}

inline std::filesystem::path bakedMap(const std::string& name) {
    return bakedDir() / (name + ".trojsav");
}

}  // namespace granadad::content
