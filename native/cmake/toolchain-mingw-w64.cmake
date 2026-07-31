# Cross-compile from Linux to a native Windows x86-64 .exe with mingw-w64.
#
# This is how the docker build produces something the owner can double-click on
# Windows. Docker is the BUILD surface only — the container never runs the game
# and never opens a window.
#
# Note the -posix compiler suffix. Debian ships two mingw flavours: the win32
# threading model has no std::thread, std::mutex or std::condition_variable at
# all, so a build that looks fine until the first threaded file suddenly does
# not compile. Always the posix variant.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}-ar)
set(CMAKE_RANLIB       ${TOOLCHAIN_PREFIX}-ranlib)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

# Look for programs on the host, but headers/libraries only in the target root —
# otherwise CMake happily finds Linux libraries and links them into a PE.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
