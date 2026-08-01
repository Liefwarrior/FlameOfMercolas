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
# granadad-content — the TROJSAV / world-format reader, and the tests that read
# the owner's real baked worlds.
#
# Added BEFORE granadad-sim, because as of M1 sim links it. See the seam note on
# granadad-sim below.
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

# Hoisted out of content/CMakeLists.txt so it exists whether or not tests are
# built: granadad-twin-gate needs it too, and it is not a test target. Set with
# the same name and the same default, so whichever directory reaches the cache
# first wins and the other is a no-op — and content/ standalone still computes
# its own with no reference upward.
get_filename_component(GRANADAD_DEFAULT_CONTENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../content" ABSOLUTE)
set(GRANADAD_CONTENT_DIR "${GRANADAD_DEFAULT_CONTENT_DIR}" CACHE PATH
    "Repo content/ directory holding the baked .trojsav worlds")

add_subdirectory(content)

# ---------------------------------------------------------------------------
# sim-core — the deterministic simulation. No SDL, no rendering, no floats.
# ---------------------------------------------------------------------------
add_library(granadad-sim STATIC
    src/sim/fixed.cpp
    src/sim/build_info.cpp
    src/sim/system_id.cpp
    src/sim/world_hash.cpp
    src/sim/engine.cpp
)
target_include_directories(granadad-sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# THE SEAM, resolved deliberately in M1: sim depends on content, and content
# never depends on sim.
#
# Before this line no built binary could load a world and tick it. The world
# hasher has to walk a World's decoded lanes, and World, Coords and the lane
# registry all live in granadad::content -- which was written standalone on
# purpose so the format reader could be developed and tested without pulling in
# SDL or a simulation.
#
# Three ways out were available: sim links content, content's world types move
# up into a third module, or a new module sits between them. This is the first,
# and the direction matters more than the choice: content still links nothing of
# ours, so `cmake -S native/content` still configures, builds and tests on its
# own exactly as its header comment promises. The arrow only ever points one
# way, and the day it points both is the day neither module can be reasoned
# about alone.
#
# PUBLIC, not PRIVATE: granadad/sim/world_hash.hpp includes
# granadad/content/world.hpp in its interface, so anything linking sim needs
# content's headers too.
target_link_libraries(granadad-sim
    PUBLIC granadad-content nlohmann_json::nlohmann_json miniz::miniz)
target_compile_definitions(granadad-sim PRIVATE
    GRANADAD_VERSION="${PROJECT_VERSION}"
    GRANADAD_REVISION="${GRANADAD_REVISION}")
granadad_apply_determinism(granadad-sim)

# sim-core must never see SDL. Keeping the link surface honest here is cheaper
# than discovering a rendering type leaked into simulation state later.

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
    add_executable(granadad-tests
        tests/test_main.cpp
        tests/test_fixed.cpp
        tests/test_wrap.cpp
        tests/test_rng.cpp
        tests/test_world_hash.cpp
        tests/test_engine.cpp
    )
    target_link_libraries(granadad-tests PRIVATE granadad-sim doctest::doctest)

    # tests/test_world_hash.cpp loads the real baked worlds and compares the C++
    # hash against what a JVM produced from the same files, so it needs the
    # content directory — resolved through granadad/content/content_dir.hpp,
    # the ONE implementation of the runtime-env-first,
    # compile-time-default-second order the docker build actually proves.
    target_compile_definitions(granadad-tests PRIVATE
        "GRANADAD_CONTENT_DIR_DEFAULT=\"${GRANADAD_CONTENT_DIR}\"")
    if(MSVC)
        # VERIFICATION GAP (M1): std::getenv under /WX, never compiled here.
        target_compile_definitions(granadad-tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    granadad_apply_determinism(granadad-tests)
    granadad_static_runtime(granadad-tests)

    # Per-CASE ctest entries, not one entry for the binary.
    #
    # This block is why M1's tests are visible to the docker build's test-count
    # floor at all. granadad-tests was registered with a plain add_test(), so it
    # counted as exactly ONE test no matter how many cases it held — adding
    # thirty would have moved the floor by zero and a future collapse of the sim
    # suite to a single case would have gone unnoticed.
    #
    # The CMAKE_CROSSCOMPILING guard is copied from content/CMakeLists.txt for
    # the reason recorded there: discovery works by RUNNING the freshly linked
    # binary, which is impossible for a Windows .exe on the Linux build host and
    # fails the build rather than the test.
    if(NOT CMAKE_CROSSCOMPILING AND EXISTS "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
        include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
        doctest_discover_tests(granadad-tests)
    else()
        add_test(NAME granadad-tests COMMAND granadad-tests)
    endif()

endif()
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
