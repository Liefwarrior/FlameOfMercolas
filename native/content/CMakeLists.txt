# granadad-content — the TROJSAV / world-format reader.
#
# This module builds two ways:
#
#   * STANDALONE. Configure this directory directly and it fetches its own
#     miniz + doctest and defines its own determinism flags. That is how it is
#     developed and how CI can test the format reader without pulling SDL3.
#
#   * EMBEDDED. add_subdirectory() it from native/CMakeLists.txt and it reuses
#     the spine's miniz::miniz, doctest::doctest and granadad_apply_determinism.
#
# Nothing here reaches up into the parent directory, so wiring it into the spine
# is a one-line add_subdirectory(content) and no edits in here.

cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(GRANADAD_CONTENT_STANDALONE TRUE)
    project(granadad_content
        VERSION 0.1.0
        DESCRIPTION "Granadad: The Darkstreets — TROJSAV / world-format reader"
        LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    option(GRANADAD_BUILD_TESTS "Build the doctest suite" ON)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    include(ContentDependencies)
else()
    set(GRANADAD_CONTENT_STANDALONE FALSE)
endif()

# The spine supplies granadad_apply_determinism. Standalone needs its own, and
# it must stay in step with native/cmake/Determinism.cmake.
if(NOT COMMAND granadad_apply_determinism)
    function(granadad_apply_determinism target)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fp:strict /fp:except- /permissive- /W4)
        else()
            target_compile_options(${target} PRIVATE
                -fwrapv               # signed overflow wraps, like Java
                -fno-fast-math
                -ffp-contract=off
                -fno-strict-aliasing
                -Wall -Wextra
                -Wconversion
                -Wdouble-promotion)
        endif()
    endfunction()
endif()

if(NOT TARGET miniz::miniz)
    message(FATAL_ERROR
        "granadad-content needs target miniz::miniz (the spine's Dependencies.cmake provides it)")
endif()

add_library(granadad-content STATIC
    src/byte_reader.cpp
    src/chunk_codec.cpp
    src/coords.cpp
    src/crc32c.cpp
    src/lanes.cpp
    src/trojsav.cpp
    src/world.cpp
    src/world_reader.cpp
)
add_library(granadad::content ALIAS granadad-content)

target_include_directories(granadad-content PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(granadad-content PRIVATE miniz::miniz)
granadad_apply_determinism(granadad-content)

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_TESTS)
    if(GRANADAD_CONTENT_STANDALONE)
        enable_testing()
    endif()

    # The tests read the real baked worlds. content/ is the owner's canon and is
    # READ-ONLY: nothing in this build ever writes into it.
    #
    # Overridable so the module can be configured from a copied or relocated
    # source tree (mutation testing, vendoring) rather than only from its home
    # in the repo.
    get_filename_component(GRANADAD_DEFAULT_CONTENT_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../content" ABSOLUTE)
    set(GRANADAD_CONTENT_DIR "${GRANADAD_DEFAULT_CONTENT_DIR}" CACHE PATH
        "Repo content/ directory holding the baked .trojsav worlds the tests read")
    if(NOT EXISTS "${GRANADAD_CONTENT_DIR}/maps/baked/tavern_fixture.trojsav")
        message(FATAL_ERROR
            "granadad-content tests need the baked worlds at "
            "${GRANADAD_CONTENT_DIR}/maps/baked — pass -DGRANADAD_CONTENT_DIR=<repo>/content")
    endif()

    add_executable(granadad-content-tests
        tests/test_main.cpp
        tests/test_crc32c.cpp
        tests/test_coords.cpp
        tests/test_chunk_codec.cpp
        tests/test_trojsav.cpp
        tests/test_world_reader.cpp
    )
    target_link_libraries(granadad-content-tests PRIVATE granadad-content doctest::doctest)
    target_compile_definitions(granadad-content-tests PRIVATE
        "GRANADAD_CONTENT_DIR=\"${GRANADAD_CONTENT_DIR}\"")
    granadad_apply_determinism(granadad-content-tests)

    # Per-test-case ctest entries when doctest ships its discovery module,
    # otherwise one entry for the whole binary. Either way `ctest` runs them.
    if(EXISTS "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
        include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
        doctest_discover_tests(granadad-content-tests)
    else()
        add_test(NAME granadad-content-tests COMMAND granadad-content-tests)
    endif()
endif()
