# Dependencies for the STANDALONE granadad-content build.
#
# When this module is built inside the native/ spine these are already present
# and this file is never included. The pins below are the same commit SHAs as
# native/cmake/Dependencies.cmake so the two builds resolve identical sources —
# keep them in step.
#
# Pinned to FULL COMMIT SHAs, not tags. Tags are mutable; a retagged upstream
# would silently change what "reproducible build" means.

include(FetchContent)

# --- miniz — zlib inflate for the TROJSAV sections. NOT used for CRC: miniz's
# mz_crc32 is IEEE, and TROJSAV carries Castagnoli. See crc32c.hpp.
# 3.1.2
#
# UNPREFIXED option names, upstream's choice, not a typo here — at pin
# 77d0dce8 miniz declares option(BUILD_TESTS ...), option(BUILD_EXAMPLES ...),
# option(BUILD_HEADER_ONLY ...). This file used to say MINIZ_BUILD_TESTS and
# friends, which match nothing and did nothing while looking load-bearing.
# Same fix and the same collision note as native/cmake/Dependencies.cmake.
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

# --- doctest — tests.
# v2.5.3
set(DOCTEST_NO_INSTALL  ON  CACHE BOOL "" FORCE)
set(DOCTEST_WITH_TESTS  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        2d0a9359a60c51affe2a9bebb1be1dca47868151
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(miniz doctest)

# miniz's exported target name has moved between releases; normalise it so the
# rest of the build only ever says miniz::miniz.
if(NOT TARGET miniz::miniz)
    if(TARGET miniz)
        add_library(miniz::miniz ALIAS miniz)
    else()
        message(FATAL_ERROR "miniz built but exposed no usable target")
    endif()
endif()

# miniz inflates every TROJSAV section, so its output is world state and it
# compiles with the determinism codegen flags — same as in the spine. The
# argument for each flag lives in native/cmake/Determinism.cmake.
granadad_apply_determinism_deps(miniz::miniz)

# ...and its headers stay out of reach of our -Werror. Without this, the 19
# -Wunused-function warnings miniz.h emits become 19 build errors.
foreach(dep IN ITEMS miniz::miniz doctest::doctest)
    granadad_mark_headers_system(${dep})
endforeach()
