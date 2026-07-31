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
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(MINIZ_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MINIZ_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIZ_BUILD_HEADER_ONLY OFF CACHE BOOL "" FORCE)
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

# --- stb — PNG decode. No tags upstream, no CMake; pinned by commit.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
    GIT_SHALLOW    TRUE)

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
endif()
