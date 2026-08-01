# Compiler flags that protect the cross-platform world hash.
#
# Two of the project's binding constraints are enforced here rather than by
# discipline alone:
#
#   * Signed overflow is UNDEFINED in C++ but well-defined wraparound in Java.
#     The Java build relies on int wrap in places. -fwrapv makes the C++ agree
#     with Java instead of letting the optimiser assume overflow is impossible
#     and delete the very branches that handle it.
#
#   * Floats are banned from simulation state, but they still exist in the
#     renderer. -ffp-contract=off stops the compiler fusing a*b+c into an FMA,
#     which rounds differently between machines. Fast-math is off for the same
#     reason.
#
# These flags are necessary but NOT sufficient. They do not stop anyone typing
# `double` into a sim struct — that is the lint pass's job.
#
# ---------------------------------------------------------------------------
# THREE FUNCTIONS, AND THE LINE BETWEEN THEM
# ---------------------------------------------------------------------------
#   granadad_determinism_codegen(t)   flags that change what the compiler EMITS
#   granadad_apply_determinism(t)     the above + our warning set + -Werror.
#                                     For code in this repo. Nothing else.
#   granadad_apply_determinism_deps(t) the codegen flags ONLY, for third-party
#                                     source we compile but do not own.
#
# The split exists because the two halves have opposite audiences. Codegen flags
# have to reach every object whose bytes can influence simulation state,
# including code we did not write. Warning flags must NOT: -Werror -Wconversion
# pointed at somebody else's C90 makes every upstream bump a build break, and
# the predictable end of that is somebody switching -Werror off for everyone.
#
# ---------------------------------------------------------------------------
# WHICH DEPENDENCIES GET granadad_apply_determinism_deps, AND WHY
# ---------------------------------------------------------------------------
# The rule: a dependency gets the codegen flags when its OUTPUT can end up in
# simulation state. Not "when it is a dependency".
#
#   miniz          YES. It is the zlib inflate behind every TROJSAV section
#                  (content/src/trojsav.cpp -> mz_uncompress2). The bytes it
#                  returns ARE the world: tiles, lanes, chunk payloads. If miniz
#                  ever decoded differently between two machines, the world hash
#                  would differ before a single tick had run. See below for what
#                  each flag is actually worth there.
#
#   SDL3           NO. Renderer only. It is deliberately never linked into
#                  granadad-sim, and nothing it produces is allowed to reach
#                  simulation state. Floats are legal in SDL's world; that is
#                  the whole point of keeping it on the far side of the line.
#
#   nlohmann/json  N/A. Header-only — it compiles inside OUR translation units
#                  and therefore already carries our flags, not its own.
#
#   doctest        N/A, same reason: header-only, compiled into our test TUs.
#
#   stb            N/A. Header-only, and pulled in only by src/client.
#
# ---------------------------------------------------------------------------
# THE MINIZ QUESTION, ANSWERED RATHER THAN ASSUMED
# ---------------------------------------------------------------------------
# Until #74 these flags reached none of the above: granadad_apply_determinism
# was only ever called on our own targets, and miniz — sitting directly in the
# save/load path — compiled with upstream's flags and nothing of ours. Taking
# each flag in turn, at pin 77d0dce8 (miniz 3.1.2):
#
#   -fwrapv              EARNS ITS KEEP. miniz is mostly unsigned, but "mostly"
#                        is not a guarantee we can check on every bump, and
#                        signed overflow is exactly the UB that behaves one way
#                        at -O0 and another at -O2. Free to apply; expensive to
#                        be wrong about.
#
#   -fno-strict-aliasing EARNS ITS KEEP. miniz reads multi-byte integers through
#                        casts in its fast paths. Whether every one of those is
#                        strictly conforming is upstream's problem, and it is
#                        not one we want to discover as a wrong tile.
#
#   -ffp-contract=off    PROVABLY INERT HERE, and applied anyway. DEFLATE is
#   -fno-fast-math       integer and bit work end to end: at this pin,
#                        `grep -nE '\b(float|double|long double)\b' miniz*.c
#                        miniz*.h` returns nothing at all. There is no FP for
#                        these flags to constrain. They stay because the cost is
#                        zero and because the day someone bumps miniz and it
#                        grows a float, nobody will re-run that grep.
#
#   -ffile-prefix-map    EARNS ITS KEEP. Debug info from a dependency bakes
#                        absolute source paths just as ours does, and
#                        byte-identical rebuilds is a claim about the whole
#                        binary, not the parts we typed.
#
# So: NOT "miniz does not need them". Two of them it needs, two are free
# insurance, one is about reproducibility rather than determinism.

# ---------------------------------------------------------------------------
# Codegen. Applied to our code AND to third-party code in the sim data path.
# Nothing in here is a diagnostic; everything in here changes emitted bytes.
# ---------------------------------------------------------------------------
function(granadad_determinism_codegen target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /fp:strict          # no reassociation, no contraction
            /fp:except-         # but do not pay for FP exception state
            # Source AND execution charset pinned to UTF-8. Without this MSVC
            # decodes source using the machine's ANSI codepage, so the same .cpp
            # compiles to different string bytes on a Windows box set to 1252
            # and one set to 932. This project's entire point is a hash that
            # matches across machines, and string literals are hashed content.
            /utf-8
        )
        # MSVC has no /fwrapv. It wraps signed overflow in practice on x86-64
        # but does not promise to. Do not lean on it; use the wrapping helpers
        # in <granadad/sim/fixed.hpp> for anything that must wrap.
    else()
        target_compile_options(${target} PRIVATE
            -fwrapv             # signed overflow wraps, like Java
            -fno-fast-math
            -ffp-contract=off   # never fuse mul+add
            -fno-strict-aliasing
        )
        # Reproducible output: bake no absolute build paths into the binary, so
        # a build in /work and a build in C:/repositories produce identical
        # bytes.
        target_compile_options(${target} PRIVATE
            "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
            "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build")
        # FetchContent normally lands under CMAKE_BINARY_DIR and is covered by
        # the line above, but FETCHCONTENT_BASE_DIR is overridable and the
        # docker build has moved it before. Map it too when it is somewhere else.
        if(FETCHCONTENT_BASE_DIR)
            target_compile_options(${target} PRIVATE
                "-ffile-prefix-map=${FETCHCONTENT_BASE_DIR}=./deps")
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Our code. Codegen + every warning we care about + -Werror.
# ---------------------------------------------------------------------------
#
# -Werror is ON by default and the docker gate passes it explicitly, so a stale
# cache cannot switch it off behind anyone's back. The option exists for exactly
# one situation: a compiler this project has never seen inventing a new warning
# in a header, where the alternative to a temporary -DGRANADAD_WERROR=OFF is a
# developer who cannot build at all. It is not for making a red build green.
option(GRANADAD_WERROR "Treat compiler warnings as errors" ON)

function(granadad_apply_determinism target)
    granadad_determinism_codegen(${target})

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /permissive-        # conformance; catches MSVC-only accidents early
            /W4
        )
        if(GRANADAD_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra
            -Wconversion        # narrowing is how fixed-point silently rots
            -Wdouble-promotion  # a float sneaking up to double is a smell
        )
        if(GRANADAD_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Third-party code in the simulation data path. Codegen only.
# ---------------------------------------------------------------------------
function(granadad_apply_determinism_deps target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR
            "granadad_apply_determinism_deps(${target}): no such target. A "
            "dependency in the sim data path must not silently go unflagged.")
    endif()
    # ALIAS targets cannot carry compile options; resolve to the real one.
    get_target_property(_granadad_aliased ${target} ALIASED_TARGET)
    if(_granadad_aliased)
        set(target ${_granadad_aliased})
    endif()
    # INTERFACE libraries have no objects of their own — a header-only
    # dependency compiles inside our TUs and already has our flags.
    get_target_property(_granadad_type ${target} TYPE)
    if(_granadad_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()
    granadad_determinism_codegen(${target})
endfunction()

# ---------------------------------------------------------------------------
# Make a dependency's headers invisible to our warnings.
# ---------------------------------------------------------------------------
#
# Prerequisite for -Werror, not a nicety. miniz publishes its include directory
# with a plain target_include_directories(... PUBLIC ...), so it arrives as -I
# and every one of our translation units that includes <miniz.h> inherits 19
# -Wunused-function warnings from it. With -Werror that is not 19 warnings, it
# is a build that cannot compile. Contrast the stb wrapper in Dependencies.cmake,
# which has always used the SYSTEM keyword.
#
# Copying INTERFACE_INCLUDE_DIRECTORIES into INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
# is the portable way to say this about a target somebody else declared: it
# works on every CMake we support, unlike the SYSTEM target property (3.25) or
# the SYSTEM keyword (only available where we own the add_subdirectory call).
#
# This suppresses warnings from their headers. It does not suppress warnings
# from OUR code that happens to be in a macro from their header, and it does not
# make their code correct. It draws the -Werror boundary at "source we wrote".
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
