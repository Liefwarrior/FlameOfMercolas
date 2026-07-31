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

function(granadad_apply_determinism target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /fp:strict          # no reassociation, no contraction
            /fp:except-         # but do not pay for FP exception state
            /permissive-        # conformance; catches MSVC-only accidents early
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
            -Wall -Wextra
            -Wconversion        # narrowing is how fixed-point silently rots
            -Wdouble-promotion  # a float sneaking up to double is a smell
        )
    endif()

    # Reproducible output: bake no absolute build paths into the binary, so a
    # build in /work and a build in C:/repositories produce identical bytes.
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE
            "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
            "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build")
    endif()
endfunction()
