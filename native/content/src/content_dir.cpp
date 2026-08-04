#include "granadad/content/content_dir.hpp"

#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
// Only here, never in a header: <windows.h> drags in a few thousand macros and
// two of them (near enough) are called things this codebase uses as ordinary
// identifiers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>
#endif

namespace granadad::content {

namespace {

/// Whether `candidate` is a content tree -- i.e. holds the baked worlds. Tested
/// on maps/baked and not on the directory's NAME, so a build that lays its data
/// out differently fails here rather than silently loading half a world.
[[nodiscard]] bool holdsBakedWorlds(const std::filesystem::path& candidate) {
    std::error_code error;
    return std::filesystem::is_directory(candidate / "maps" / "baked", error);
}

}  // namespace

std::filesystem::path executableDir() {
#if defined(_WIN32)
    // GetModuleFileNameW truncates and does NOT null-terminate on overflow in
    // the ANSI-era contract, so the length is checked rather than trusted.
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD written =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), written)).parent_path();
        }
        if (buffer.size() >= 32768) {
            return {};  // longer than any legal Windows path; give up rather than loop
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    std::error_code error;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return {};
    }
    return self.parent_path();
#else
    // VERIFICATION GAP (S2): every platform this project builds for is covered
    // above. Anything else falls back to $GRANADAD_CONTENT_DIR and the
    // configure-time default, which is exactly the pre-S2 behaviour.
    return {};
#endif
}

std::filesystem::path searchForContentDir(const std::filesystem::path& start) {
    if (start.empty()) {
        return {};
    }
    std::error_code error;
    std::filesystem::path directory = std::filesystem::weakly_canonical(start, error);
    if (error) {
        directory = start;
    }
    for (int level = 0; level <= kContentDirSearchLevels; ++level) {
        const std::filesystem::path candidate = directory / "content";
        if (holdsBakedWorlds(candidate)) {
            return candidate;
        }
        // An install that puts the worlds directly beside the binary: the
        // executable's own directory IS the content tree.
        if (level == 0 && holdsBakedWorlds(directory)) {
            return directory;
        }
        const std::filesystem::path parent = directory.parent_path();
        if (parent.empty() || parent == directory) {
            break;
        }
        directory = parent;
    }
    return {};
}

std::filesystem::path contentDir() {
    // getenv, not _dupenv_s: this reads a path the owner set one line earlier
    // in their own shell, and the C standard function is the one that exists on
    // every toolchain here. MSVC's C4996 for it is silenced per target in
    // CMakeLists rather than by writing two code paths.
    const char* fromEnv = std::getenv(kContentDirEnvVar);
    if (fromEnv != nullptr && *fromEnv != '\0') {
        return std::filesystem::path(fromEnv);
    }
    const std::filesystem::path beside = searchForContentDir(executableDir());
    if (!beside.empty()) {
        return beside;
    }
    return std::filesystem::path(GRANADAD_CONTENT_DIR_DEFAULT);
}

}  // namespace granadad::content
