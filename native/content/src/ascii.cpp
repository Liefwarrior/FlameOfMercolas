#include "granadad/content/ascii.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace granadad::content {

bool writeTextFile(const std::string& path, const std::string& text, std::string* error) {
    const std::filesystem::path target(path);
    std::FILE* handle = nullptr;
#if defined(_WIN32)
    // "wb", and it matters: text mode on Windows would turn every '\n' into
    // "\r\n" and the byte comparison against the Linux report would fail for a
    // reason that has nothing to do with the simulation.
    handle = _wfopen(target.c_str(), L"wb");
#else
    handle = std::fopen(target.c_str(), "wb");
#endif
    if (handle == nullptr) {
        if (error != nullptr) {
            *error = "cannot open for writing: " + path;
        }
        return false;
    }
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), handle);
    const bool flushed = std::fflush(handle) == 0;
    const bool closed = std::fclose(handle) == 0;
    if (written != text.size() || !flushed || !closed) {
        if (error != nullptr) {
            *error = "short write to " + path;
        }
        return false;
    }
    return true;
}

}  // namespace granadad::content
