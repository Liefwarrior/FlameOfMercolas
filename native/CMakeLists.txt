cmake_minimum_required(VERSION 3.24)

# Granadad: The Darkstreets — native C++ rewrite.
#
# Console toolchains lag, so this targets C++20 and nothing newer. See
# native/README.md for both build paths (docker for reproducible, CMake preset
# for fast local iteration).
#
# VERIFICATION GAP (M0): the docker path is verified end to end — configure,
# cross-compile, host ctest, and the resulting .exe run natively on Windows.
# The CMakePresets paths (local, local-debug, sim-only, mingw-cross) are NOT
# verified: the machine this was authored on has no cmake, ninja or C++
# compiler installed, so nothing outside the container was ever executed. The
# preset definitions are believed correct but untested.

project(granadad
    VERSION 0.1.0
    DESCRIPTION "Granadad: The Darkstreets — deterministic simulation of the Docks district of Trojia"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(GRANADAD_BUILD_TESTS  "Build the doctest suite" ON)
option(GRANADAD_BUILD_CLIENT "Build the SDL3 observer client (OFF = sim only, no SDL fetch)" ON)

# Stamped by the docker build so a binary can name the commit it came from.
set(GRANADAD_REVISION "local" CACHE STRING "Git revision baked into the binary")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Determinism)
include(StaticRuntime)
include(Dependencies)

# enable_testing() has to happen in the top-level directory BEFORE any
# add_subdirectory() that registers tests, or the subdirectory's add_test()
# calls are silently dropped from the ctest run. That silence is exactly the
# failure this build is trying not to have.
if(GRANADAD_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# sim-core — the deterministic simulation. No SDL, no rendering, no floats.
# ---------------------------------------------------------------------------
add_library(granadad-sim STATIC
    src/sim/fixed.cpp
    src/sim/build_info.cpp
)
target_include_directories(granadad-sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(granadad-sim PUBLIC nlohmann_json::nlohmann_json miniz::miniz)
target_compile_definitions(granadad-sim PRIVATE
    GRANADAD_VERSION="${PROJECT_VERSION}"
    GRANADAD_REVISION="${GRANADAD_REVISION}")
granadad_apply_determinism(granadad-sim)

# sim-core must never see SDL. Keeping the link surface honest here is cheaper
# than discovering a rendering type leaked into simulation state later.

# ---------------------------------------------------------------------------
# granadad-content — the TROJSAV / world-format reader, and the tests that read
# the owner's real baked worlds.
#
# This is deliberately part of the default build rather than a standalone
# side-project. Left out, `ctest` reported "100% tests passed, 1 tests out of 1"
# — that one test being fixed.hpp — while the 57 cases and ~900k assertions
# that prove the format reader against the actual shipped files ran nowhere.
# A green badge that narrow is worse than no badge.
#
# Configure fails outright if the baked worlds are missing (see
# content/CMakeLists.txt), which is what keeps someone from quietly dropping
# them out of the docker context again.
#
# Closed in #75: the content suite now runs on BOTH toolchains. fixtures.hpp
# resolves the content directory from $GRANADAD_CONTENT_DIR at run time, so the
# cross-built granadad-content-tests.exe is installed into dist/ and executed
# natively on Windows against the repo's own content/. Pass/fail is the weak
# half of that; the strong half is `--fingerprint`, which writes a report of the
# decoded world state (section CRCs, per-lane CRC32C, per-form / per-material /
# per-flags / per-fluid histograms over every tile) that is compared byte for
# byte between the two platforms. See docker/build.Dockerfile and
# scripts/verify-windows.ps1.
# ---------------------------------------------------------------------------
add_subdirectory(content)

# ---------------------------------------------------------------------------
# client-observer — the SDL3 window. Floats are legal HERE and only here.
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_CLIENT)
    add_executable(granadad src/client/main.cpp src/client/stb_impl.cpp)
    target_link_libraries(granadad PRIVATE granadad-sim ${GRANADAD_SDL_TARGET} stb::stb)
    granadad_apply_determinism(granadad)
    # Console subsystem stays on — the sim prints, and we want stdout to work
    # when the game is launched from a terminal on Windows.
    granadad_static_runtime(granadad)
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_TESTS)
    add_executable(granadad-tests tests/test_fixed.cpp)
    target_link_libraries(granadad-tests PRIVATE granadad-sim doctest::doctest)
    granadad_apply_determinism(granadad-tests)
    granadad_static_runtime(granadad-tests)
    add_test(NAME granadad-tests COMMAND granadad-tests)
endif()

# ---------------------------------------------------------------------------
# install — this is what the docker build harvests into dist/
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_CLIENT)
    install(TARGETS granadad RUNTIME DESTINATION .)
    # If SDL ended up shared (it should not — we ask for static) carry the DLL.
    if(TARGET SDL3::SDL3-shared)
        install(FILES $<TARGET_FILE:SDL3::SDL3-shared> DESTINATION .)
    endif()
endif()
if(GRANADAD_BUILD_TESTS)
    install(TARGETS granadad-tests RUNTIME DESTINATION .)
    # Shipped so the format reader can be tested on the toolchain that actually
    # ships it. Before #75 this binary was compiled, linked and thrown away,
    # which meant the cross-platform determinism claim rested on one platform.
    install(TARGETS granadad-content-tests RUNTIME DESTINATION .)
endif()
