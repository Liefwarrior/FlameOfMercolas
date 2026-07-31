cmake_minimum_required(VERSION 3.24)

# Granadad: The Darkstreets — native C++ rewrite.
#
# Console toolchains lag, so this targets C++20 and nothing newer. See
# native/README.md for both build paths (docker for reproducible, CMake preset
# for fast local iteration).

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
include(Dependencies)

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
# client-observer — the SDL3 window. Floats are legal HERE and only here.
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_CLIENT)
    add_executable(granadad src/client/main.cpp src/client/stb_impl.cpp)
    target_link_libraries(granadad PRIVATE granadad-sim ${GRANADAD_SDL_TARGET} stb::stb)
    granadad_apply_determinism(granadad)

    if(MINGW)
        # Ship one self-contained .exe: fold libgcc/libstdc++/winpthread in
        # rather than making the owner carry three MinGW runtime DLLs next to
        # the game. Console subsystem stays on — the sim prints, and we want
        # stdout to work when it is launched from a terminal on Windows.
        target_link_options(granadad PRIVATE
            -static-libgcc -static-libstdc++
            -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive -Wl,-Bdynamic)
    endif()
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_TESTS)
    enable_testing()
    add_executable(granadad-tests tests/test_fixed.cpp)
    target_link_libraries(granadad-tests PRIVATE granadad-sim doctest::doctest)
    granadad_apply_determinism(granadad-tests)
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
endif()
