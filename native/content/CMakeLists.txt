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
else()
    set(GRANADAD_CONTENT_STANDALONE FALSE)
endif()

# The spine supplies granadad_apply_determinism, granadad_apply_determinism_deps
# and granadad_mark_headers_system. Standalone needs its own, and they must stay
# in step with native/cmake/Determinism.cmake — which is where the reasoning
# behind every one of these flags lives. Do not re-derive it here; go read it.
#
# These are defined BEFORE include(ContentDependencies) on purpose: that file
# has to flag miniz the moment it materialises it.
#
# VERIFICATION GAP (M0): this module has been configured, built and tested on
# Linux / GCC 12 only. The machine it was written on has no C++ toolchain at
# all — Visual Studio is installed without the C++ workload and there is no
# MSVC, clang or mingw — so the build ran in a debian:bookworm-slim container.
# The MSVC branch below and every /-style flag in it are UNCOMPILED. Nothing
# here is Linux-specific by design (the one platform #if is the _wfopen path in
# trojsav.cpp), but "should port" is not "did port".
# VERIFICATION GAP (#74): /utf-8, /W4 and /WX are part of that — added for the
# same reasons as the GCC side, never once run through MSVC.
if(NOT COMMAND granadad_apply_determinism)
    option(GRANADAD_WERROR "Treat compiler warnings as errors" ON)

    function(granadad_determinism_codegen target)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fp:strict /fp:except- /utf-8)
        else()
            target_compile_options(${target} PRIVATE
                -fwrapv               # signed overflow wraps, like Java
                -fno-fast-math
                -ffp-contract=off
                -fno-strict-aliasing)
            target_compile_options(${target} PRIVATE
                "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
                "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build")
            if(FETCHCONTENT_BASE_DIR)
                target_compile_options(${target} PRIVATE
                    "-ffile-prefix-map=${FETCHCONTENT_BASE_DIR}=./deps")
            endif()
        endif()
    endfunction()

    function(granadad_apply_determinism target)
        granadad_determinism_codegen(${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /permissive- /W4)
            if(GRANADAD_WERROR)
                target_compile_options(${target} PRIVATE /WX)
            endif()
        else()
            target_compile_options(${target} PRIVATE
                -Wall -Wextra
                -Wconversion
                -Wdouble-promotion)
            if(GRANADAD_WERROR)
                target_compile_options(${target} PRIVATE -Werror)
            endif()
        endif()
    endfunction()

    # Third-party source in the sim data path: codegen flags, no warnings.
    function(granadad_apply_determinism_deps target)
        if(NOT TARGET ${target})
            message(FATAL_ERROR
                "granadad_apply_determinism_deps(${target}): no such target.")
        endif()
        get_target_property(_granadad_aliased ${target} ALIASED_TARGET)
        if(_granadad_aliased)
            set(target ${_granadad_aliased})
        endif()
        get_target_property(_granadad_type ${target} TYPE)
        if(_granadad_type STREQUAL "INTERFACE_LIBRARY")
            return()
        endif()
        granadad_determinism_codegen(${target})
    endfunction()

    # Third-party headers must not be judged by our -Werror.
    function(granadad_mark_headers_system target)
        if(NOT TARGET ${target})
            return()
        endif()
        get_target_property(_granadad_aliased ${target} ALIASED_TARGET)
        if(_granadad_aliased)
            set(target ${_granadad_aliased})
        endif()
        get_target_property(_granadad_dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_granadad_dirs)
            set_property(TARGET ${target} PROPERTY
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_granadad_dirs}")
        endif()
    endfunction()
endif()

if(GRANADAD_CONTENT_STANDALONE)
    include(ContentDependencies)
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
    # Hard failure, on purpose. The tempting alternative — skip the tests when
    # the data is absent — is how a build gate quietly shrinks to nothing: drop
    # content/maps/baked out of .dockerignore and every one of these cases
    # vanishes while the build still prints green. Missing data is a broken
    # build context, not a reason to test less.
    if(NOT EXISTS "${GRANADAD_CONTENT_DIR}/maps/baked/tavern_fixture.trojsav")
        message(FATAL_ERROR
            "granadad-content tests need the baked worlds at "
            "${GRANADAD_CONTENT_DIR}/maps/baked — pass -DGRANADAD_CONTENT_DIR=<repo>/content. "
            "In the docker build this means .dockerignore stopped re-admitting "
            "content/maps/baked/** into the build context.")
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
    # Only defined when embedded in the native/ spine. Without it a mingw build
    # of this binary imports libstdc++-6.dll and dies before main().
    if(COMMAND granadad_static_runtime)
        granadad_static_runtime(granadad-content-tests)
    endif()

    # Per-test-case ctest entries when doctest ships its discovery module,
    # otherwise one entry for the whole binary. Either way `ctest` runs them.
    #
    # Discovery works by RUNNING the freshly linked binary with --list-test-cases
    # at build time. That is impossible when cross-compiling — the Windows .exe
    # will not execute on the Linux build host — and the attempt fails the build
    # rather than the test. Cross builds therefore register the single-binary
    # entry; the host build, which is the one that actually runs ctest, gets the
    # per-case breakdown.
    #
    # Not a precaution taken on faith: with the CMAKE_CROSSCOMPILING clause
    # removed, the docker build fails at
    #   FAILED: bin/granadad-content-tests.exe ..._tests-<hash>.cmake
    #   CMake Error at .../doctest-src/scripts/cmake/doctestAddTests.cmake:45
    # because ninja links the .exe and immediately tries to execute it.
    if(NOT CMAKE_CROSSCOMPILING AND EXISTS "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
        include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
        doctest_discover_tests(granadad-content-tests)
    else()
        add_test(NAME granadad-content-tests COMMAND granadad-content-tests)
    endif()
endif()
