#pragma once

#include <string_view>

namespace granadad::sim {

// Identifies the binary. The docker build stamps the git revision in; a local
// preset build reports "local". Printed on startup so a bug report says which
// binary produced it.
struct BuildInfo {
    std::string_view version;
    std::string_view revision;
    std::string_view target;   // "windows-x86_64" | "linux-x86_64" | ...
    std::string_view compiler;
};

[[nodiscard]] BuildInfo build_info() noexcept;

}  // namespace granadad::sim
