# THE GPU ONLY DRAWS; IT NEVER DECIDES GAMEPLAY.
#
# docs/design/OBLIVION-PLAN.md section 6.3, ruling 2 of section 1. Line of
# sight, detection, stealth and everything else the simulation depends on is
# computed in the simulation from integers and is NEVER read back from a frame,
# a depth buffer or a query. The one place a frame is allowed to leave the GPU
# is the capture path (render3d/rl/capture.cpp), which feeds --screenshot and
# the frame tests and nothing else.
#
# This is enforced the way the ban on std::unordered_map is: by grep, because a
# review cannot be trusted to notice one call. Any GL or raylib readback call in
# native/ outside capture.cpp fails the build. A comment may name the call (this
# file's own reason for existing does), a line of code may not.
#
# Run by ctest as `granadad-no-gpu-readback`; the docker gate asserts by name
# that it is registered.

if(NOT DEFINED GRANADAD_NATIVE_DIR)
    message(FATAL_ERROR "GRANADAD_NATIVE_DIR is not set")
endif()

set(banned
    "ReadPixels"
    "rlReadScreenPixels"
    "LoadImageFromScreen"
    "LoadImageFromTexture"
    "rlReadTexturePixels"
    "rlCopyFramebuffer"
    "glGetQuery"
    "glGetBufferSubData"
    "glRead")

file(GLOB_RECURSE sources
    "${GRANADAD_NATIVE_DIR}/*.cpp" "${GRANADAD_NATIVE_DIR}/*.hpp" "${GRANADAD_NATIVE_DIR}/*.h")

set(hits "")
set(scanned 0)
foreach(file IN LISTS sources)
    # The cmake scripts are not scanned (they are not in the globbed
    # extensions), and neither is the one allowed file.
    if(file MATCHES "/render3d/rl/capture\\.cpp$")
        continue()
    endif()
    if(file MATCHES "/build[^/]*/")
        continue()
    endif()
    math(EXPR scanned "${scanned} + 1")
    file(STRINGS "${file}" lines)
    set(n 0)
    foreach(line IN LISTS lines)
        math(EXPR n "${n} + 1")
        # Drop a whole-line comment; a trailing comment is not special-cased,
        # on purpose: name the call in a comment on its own line.
        if(line MATCHES "^[ \t]*(//|\\*|/\\*)")
            continue()
        endif()
        foreach(word IN LISTS banned)
            if(line MATCHES "${word}")
                list(APPEND hits "${file}:${n}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(scanned LESS 50)
    message(FATAL_ERROR
        "granadad-no-gpu-readback scanned only ${scanned} files under ${GRANADAD_NATIVE_DIR}: "
        "the glob is wrong, and a check that scans nothing proves nothing.")
endif()

if(hits)
    string(REPLACE ";" "\n" shown "${hits}")
    message(FATAL_ERROR
        "A GPU readback call is outside render3d/rl/capture.cpp:\n${shown}\n"
        "The GPU only draws. Nothing in sim/, render/ or render3d-core may read a "
        "pixel, a depth value or a query result; a frame leaves the GPU only through "
        "Backend::endFrame's capture.")
endif()
message(STATUS "granadad-no-gpu-readback: ${scanned} files scanned, no readback outside capture.cpp")
