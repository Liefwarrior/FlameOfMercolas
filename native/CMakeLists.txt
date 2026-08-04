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

# Set here as well as in content/CMakeLists.txt, with the same name and the same
# default, so whichever directory reaches the cache first wins and the other is
# a no-op — and content/ standalone still computes its own with no reference
# upward. The DEFINE that carries it to the compiler is granadad-content's, and
# only granadad-content's.
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
    src/sim/angle.cpp
    src/sim/tile_query.cpp
    src/sim/player.cpp
    src/sim/region_path.cpp
    src/sim/brawl.cpp
    src/sim/actor.cpp
    src/sim/spellbook.cpp
    # S3 -- the social layer. barks/notables read the owner's raws and write
    # nothing; social/barter/dialogue are the state and the rules over them.
    src/sim/barks.cpp
    src/sim/notables.cpp
    src/sim/social.cpp
    src/sim/barter.cpp
    src/sim/dialogue.cpp
    # S4 -- the guilds. faction reads the owner's registry and the S4 ladders
    # hung off it; questline reads any stages-shaped quest file; spellforge is
    # canon's own cost model plus the composition verb the modular effect
    # vocabulary was authored for.
    src/sim/faction.cpp
    src/sim/questline.cpp
    src/sim/spellforge.cpp
    src/sim/tavern.cpp
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
# granadad-gate — the twin-run determinism gate.
#
# A library plus a thin executable, rather than one main.cpp, so the comparators
# and the workload are reachable from the doctest suite. A gate whose own logic
# is untested is a gate you find out about on the day it should have gone red.
# ---------------------------------------------------------------------------
add_library(granadad-gate STATIC
    src/gate/workload.cpp
    src/gate/twin_run.cpp
    src/gate/world_hash_report.cpp
)
target_include_directories(granadad-gate PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# GRANADAD_CONTENT_DIR_DEFAULT arrives through granadad-content, which owns the
# resolver as of S2. Same runtime-first / compile-time-default contract: the
# path baked in is the build container's and does not exist on the machine the
# .exe ships to, so $GRANADAD_CONTENT_DIR and then the executable's own tree
# outrank it. See granadad/content/content_dir.hpp for both incidents that made
# that necessary.
target_link_libraries(granadad-gate PUBLIC granadad-sim)
granadad_apply_determinism(granadad-gate)

add_executable(granadad-twin-gate src/gate/main.cpp)
target_link_libraries(granadad-twin-gate PRIVATE granadad-gate)
granadad_apply_determinism(granadad-twin-gate)
granadad_static_runtime(granadad-twin-gate)

# ---------------------------------------------------------------------------
# granadad-render — the software first-person renderer. Floats are legal here.
#
# Built ALWAYS, including in the sim-only configuration the docker host check
# uses, and deliberately linked against no SDL. That is the whole design:
# rendering a frame needs a world, an art pack and a camera, and none of those
# needs a window. Because this library stands alone, `granadad --screenshot`
# opens no window, and the test suite can render the Docks and assert on the
# pixels on every build -- which is how each of the nine sprints after this one
# gets to prove visually that it still works.
# ---------------------------------------------------------------------------
add_library(granadad-render STATIC
    src/render/framebuffer.cpp
    src/render/atlas.cpp
    src/render/lamps.cpp
    src/render/lighting.cpp
    src/render/world_renderer.cpp
    src/render/hud.cpp
    src/render/dialogue_view.cpp
    src/render/capture.cpp
    src/render/session.cpp
    src/render/step_pump.cpp
    src/render/stb_impl.cpp
)
target_include_directories(granadad-render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# GRANADAD_CONTENT_DIR_DEFAULT arrives through granadad-content.
target_link_libraries(granadad-render PUBLIC granadad-sim stb::stb)
granadad_apply_determinism(granadad-render)

# The lamp baker. Turns a fixture's authored light_source markers into the
# sidecar the game reads, so the 27 lamps of the Docks are derived from canon
# rather than transcribed. Run by hand; its output is committed.
add_executable(granadad-bake-lamps src/tools/bake_lamps.cpp)
target_link_libraries(granadad-bake-lamps PRIVATE granadad-render)
granadad_apply_determinism(granadad-bake-lamps)
granadad_static_runtime(granadad-bake-lamps)

# ---------------------------------------------------------------------------
# client-observer — the SDL3 window and nothing else.
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_CLIENT)
    add_executable(granadad src/client/main.cpp)
    target_link_libraries(granadad PRIVATE granadad-render ${GRANADAD_SDL_TARGET})
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
        tests/test_twin_gate.cpp
        tests/test_angle.cpp
        tests/test_tile_query.cpp
        tests/test_player.cpp
        tests/test_tavern.cpp
        tests/test_lamps.cpp
        tests/test_atlas.cpp
        tests/test_render.cpp
        tests/test_tavern_render.cpp
        tests/test_social.cpp
        tests/test_dialogue.cpp
        tests/test_faction.cpp
        tests/test_spellforge.cpp
        tests/test_questline.cpp
        tests/test_step_pump.cpp
        # S5 -- the roofs, and the trade that runs on them.
        tests/test_roofrun.cpp
    )
    target_link_libraries(granadad-tests PRIVATE granadad-gate granadad-render doctest::doctest)

    # GRANADAD_CONTENT_DIR_DEFAULT and _CRT_SECURE_NO_WARNINGS arrive through
    # granadad-gate's PUBLIC compile definitions. tests/test_world_hash.cpp
    # loads the real baked worlds and compares the C++ hash against what a JVM
    # produced from the same files, so it needs the content directory —
    # resolved through granadad/content/content_dir.hpp, which is the ONE
    # implementation of the runtime-env-first, compile-time-default-second
    # order the docker build actually proves.

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

    # The twin-run gate as a ctest entry. It is the only check in this build that
    # can catch nondeterminism rather than incorrectness, and a gate that is not
    # wired into `ctest` is a gate somebody has to remember to run.
    #
    # Cross builds skip it for the same reason as above: the .exe does not run
    # here. scripts/verify-windows.ps1 runs it natively on the other side.
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME granadad-twin-run-gate COMMAND granadad-twin-gate)
        add_test(NAME granadad-world-hash-fingerprint
            COMMAND granadad-twin-gate --fingerprint
                    "${CMAKE_CURRENT_BINARY_DIR}/world-hash.txt")
        # S2: the same gate, over the district, with the Gilded Gull registered
        # and its movement clock driven. This is the only check in the build
        # that can catch NONDETERMINISM in the tavern rather than incorrectness
        # — fourteen actors, breadth-first routes, drink orders and a door
        # policy, run twice from one seed and required to agree bit for bit.
        #
        # A separate entry rather than a change to the default workload, so the
        # published cross-toolchain report stays frozen: it is compared byte for
        # byte between Linux and Windows, and a sprint that changed its contents
        # would be a sprint that regenerated the thing the comparison rests on.
        add_test(NAME granadad-twin-run-gate-tavern
            COMMAND granadad-twin-gate --tavern --ticks 900)
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

# The twin-run gate ships whether or not tests are built: it is not a test, it
# is the determinism proof, and its --fingerprint report is the sim-side half of
# the cross-toolchain comparison scripts/verify-windows.ps1 completes.
install(TARGETS granadad-twin-gate RUNTIME DESTINATION .)

# The lamp baker ships for one reason: content/maps/src is not in the build
# context, so the only place the bake can actually be RE-DERIVED from the
# authored Tiled source is on the machine that has the repo. Shipping it means
# `granadad-bake-lamps.exe content/maps/src/docks_surface.tmx <out>` and a diff
# is the whole verification, rather than a claim about a file nobody can
# regenerate.
install(TARGETS granadad-bake-lamps RUNTIME DESTINATION .)
