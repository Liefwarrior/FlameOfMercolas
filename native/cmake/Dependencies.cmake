# Third-party dependencies, fetched by CMake FetchContent.
#
# Every dependency is pinned to a FULL COMMIT SHA, not a tag. Tags are mutable —
# a retagged upstream would silently change what "reproducible build" means. The
# human-readable release each SHA corresponds to is in the comment above it.
#
# Keep this list short and portable. Everything here must cross-compile to
# Windows via mingw-w64 and build on a console toolchain.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# --- SDL3 — window, input, audio. Rendering only; never linked into sim-core.
# release-3.4.12
#
# Skipped entirely when GRANADAD_BUILD_CLIENT=OFF. SDL is by far the heaviest
# thing here, and a sim-only build (tests, headless soak, the determinism lint)
# has no business paying for it.
if(GRANADAD_BUILD_CLIENT)
set(SDL_SHARED         OFF CACHE BOOL "" FORCE)
set(SDL_STATIC         ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY   OFF CACHE BOOL "" FORCE)
set(SDL_TESTS          OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        f87239e71e42da91ca317a12eefb82cfbf3393eb
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
endif()

# --- miniz — zlib inflate for the TROJSAV sections.
# 3.1.2
#
# The option names are UNPREFIXED, which is upstream's choice and not a typo
# here. At pin 77d0dce8, miniz/CMakeLists.txt declares:
#
#     option(BUILD_EXAMPLES    "Build examples"  ${MINIZ_STANDALONE_PROJECT})
#     option(BUILD_TESTS       "Build tests"     ${MINIZ_STANDALONE_PROJECT})
#     option(BUILD_HEADER_ONLY "Build a header-only version" OFF)
#     option(INSTALL_PROJECT   "Install project" ${MINIZ_STANDALONE_PROJECT})
#
# This file used to set MINIZ_BUILD_TESTS / MINIZ_BUILD_EXAMPLES /
# MINIZ_BUILD_HEADER_ONLY. Those names match nothing upstream: they were three
# inert cache entries that read, to anyone scanning this file, like they were
# holding the build down. Fixed in #74 by using the real names.
#
# Belt and braces rather than strictly necessary — MINIZ_STANDALONE_PROJECT is
# OFF whenever PROJECT_NAME is already defined, which it always is by the time
# FetchContent adds miniz — but a default we do not control is not the same as
# a decision we made, and stating it costs three lines.
#
# The generic names are a shared namespace, so the collision risk is real and
# worth naming: SDL3 reads SDL_TESTS/SDL_EXAMPLES, nlohmann reads
# JSON_BuildTests, doctest reads DOCTEST_WITH_TESTS. None of them looks at
# BUILD_TESTS or BUILD_EXAMPLES, so nothing else in this file is affected. Check
# again before adding a dependency that does.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(BUILD_HEADER_ONLY OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS     OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        77d0dce8627735138c51770d1799a1ef48f2117d
    GIT_SHALLOW    TRUE)

# --- nlohmann/json — the raws loader.
# v3.12.0
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        55f93686c01528224f448c19128836e7df245f72
    GIT_SHALLOW    TRUE)

# --- doctest — tests.
# v2.5.3
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        2d0a9359a60c51affe2a9bebb1be1dca47868151
    GIT_SHALLOW    TRUE)

# --- stb — PNG decode and PNG encode. No tags upstream, no CMake; pinned by
# commit.
#
# NOT shallow, and that is deliberate. Every other dependency here is pinned to
# a commit that upstream also tagged, so a `--depth 1` clone lands on it. stb
# has no tags at all, so this pin is an ordinary commit in the middle of master
# — and the moment upstream pushes past it, `git clone --depth 1` fetches only
# the new tip and the checkout fails with
#
#     fatal: reference is not a tree: 31c1ad374564...
#
# which is what this build started doing. The choice was re-pin to whatever
# master says today, or clone the history. Re-pinning to a moving target to fix
# a reproducibility mechanism would be the wrong way round: the pin is the
# point. stb is a handful of header files, so the full clone costs a second.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4)

set(GRANADAD_DEPS miniz nlohmann_json doctest stb)
if(GRANADAD_BUILD_CLIENT)
    list(INSERT GRANADAD_DEPS 0 SDL3)
endif()
FetchContent_MakeAvailable(${GRANADAD_DEPS})

# stb ships headers and nothing else — wrap it ourselves.
add_library(stb_stb INTERFACE)
target_include_directories(stb_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
add_library(stb::stb ALIAS stb_stb)

# miniz's exported target name has moved between releases; normalise it so the
# rest of the build only ever says miniz::miniz.
if(NOT TARGET miniz::miniz)
    if(TARGET miniz)
        add_library(miniz::miniz ALIAS miniz)
    else()
        message(FATAL_ERROR "miniz built but exposed no usable target")
    endif()
endif()

# miniz is the zlib inflate behind every TROJSAV section, so the bytes it
# returns are world state and it compiles with the determinism flags. The full
# argument — including which of those flags are provably inert for integer
# DEFLATE and why they are applied anyway — is in Determinism.cmake.
granadad_apply_determinism_deps(miniz::miniz)

# Third-party headers must not be judged by our -Werror. miniz in particular
# hands us 19 -Wunused-function warnings through a plain -I include; stb above
# has always used SYSTEM, and now so does everything else.
foreach(dep IN ITEMS miniz::miniz doctest::doctest nlohmann_json::nlohmann_json)
    granadad_mark_headers_system(${dep})
endforeach()

# Same for SDL: we ask for static, but do not assume we got it.
if(GRANADAD_BUILD_CLIENT)
    if(TARGET SDL3::SDL3-static)
        set(GRANADAD_SDL_TARGET SDL3::SDL3-static)
    elseif(TARGET SDL3::SDL3-shared)
        set(GRANADAD_SDL_TARGET SDL3::SDL3-shared)
    elseif(TARGET SDL3::SDL3)
        set(GRANADAD_SDL_TARGET SDL3::SDL3)
    else()
        message(FATAL_ERROR "SDL3 built but exposed no usable target")
    endif()
    message(STATUS "granadad: linking SDL via ${GRANADAD_SDL_TARGET}")
    # Headers only. SDL deliberately does NOT get the determinism codegen flags:
    # it is renderer-side, never linked into granadad-sim, and nothing it
    # produces is allowed to reach simulation state. See Determinism.cmake.
    granadad_mark_headers_system(${GRANADAD_SDL_TARGET})
endif()
