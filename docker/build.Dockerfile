# syntax=docker/dockerfile:1.7
#
# Granadad: The Darkstreets — reproducible build of the native C++ game.
#
# WHAT THIS IS: a BUILD surface. The container compiles a Windows .exe and hands
# it back through a bind mount. It never runs the game, never opens a window,
# never needs a GPU. The owner runs the resulting binary natively on Windows.
#
# The cross-compile is Linux -> Windows x86-64 via mingw-w64, so the artifact is
# a real PE executable you can double-click, not a Linux ELF.
#
# ---------------------------------------------------------------------------
# On reproducibility, honestly
# ---------------------------------------------------------------------------
# Pinned hard:
#   * the base image, by sha256 digest (not the :bookworm-slim tag, which moves)
#   * every third-party dependency, by full commit SHA (see native/cmake/Dependencies.cmake)
#   * build paths, via -ffile-prefix-map, so /work and C:/repositories produce identical bytes
#
# NOT pinned: the exact Debian package versions of gcc/cmake/ninja. Pinning them
# breaks the build outright the moment Debian ships a security update and drops
# the old version from the mirror, which is a bad trade for a command the owner
# is supposed to be able to run at any time. The versions this was built and
# verified against are printed by the build itself and recorded in the artifact
# manifest, so drift is visible rather than silent.
#   verified against: cmake 3.25.1-1, ninja 1.11.1-2~deb12u1,
#                     g++-mingw-w64-x86-64-posix 12.2.0-14+deb12u1+25.2+b1

ARG DEBIAN_DIGEST=sha256:7b140f374b289a7c2befc338f42ebe6441b7ea838a042bbd5acbfca6ec875818

# ---------------------------------------------------------------------------
# Stage 1: the toolchain. Changes rarely, so it caches for the life of the repo.
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim@${DEBIAN_DIGEST} AS toolchain

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        file \
        xz-utils \
        gcc-mingw-w64-x86-64-posix \
        g++-mingw-w64-x86-64-posix \
    && rm -rf /var/lib/apt/lists/*

# Debian ships two mingw threading models under the same unsuffixed name. The
# win32 one has no std::thread, std::mutex or std::condition_variable at all —
# a build looks fine until the first threaded file and then does not compile.
# Force the posix variant to be what the plain name resolves to, so anything
# that ignores our toolchain file still gets the working compiler.
RUN update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
 && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix

# SOURCE_DATE_EPOCH is honoured by gcc for __DATE__/__TIME__ and by ar for
# archive member timestamps. Fixed value = byte-identical rebuilds.
ENV SOURCE_DATE_EPOCH=1700000000

# ---------------------------------------------------------------------------
# Stage 2: the build.
# ---------------------------------------------------------------------------
FROM toolchain AS build

ARG GRANADAD_REVISION=docker
ARG BUILD_TYPE=RelWithDebInfo

WORKDIR /src

# Only native/ is copied. content/ (1.2 GB of art) and .claude/worktrees
# (1.6 GB of parallel checkouts) are excluded by .dockerignore — the compiler
# has no use for either, and content is read at runtime straight from the repo.
COPY native /src/native

# FetchContent lands here. A cache mount keeps SDL3 from being re-cloned and
# rebuilt on every source edit; the pins are commit SHAs, so a warm cache and a
# cold one produce the same bytes.
ENV FETCHCONTENT_BASE_DIR=/deps

RUN --mount=type=cache,target=/deps,sharing=locked \
    --mount=type=cache,target=/build-cache,sharing=locked \
    set -eux; \
    echo "=== toolchain ==="; \
    cmake --version | head -1; \
    ninja --version; \
    x86_64-w64-mingw32-g++ --version | head -1; \
    \
    echo "=== host check: build sim + tests for Linux and actually run them ==="; \
    # A cross-compiled .exe cannot be executed here, so correctness is proven on
    # a host build of the same sources. The client is skipped: no SDL needed to
    # test deterministic integer math, and skipping it keeps this pass quick.
    cmake -S /src/native -B /build-cache/hostcheck -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DGRANADAD_BUILD_CLIENT=OFF \
        -DGRANADAD_BUILD_TESTS=ON \
        -DGRANADAD_REVISION="${GRANADAD_REVISION}"; \
    cmake --build /build-cache/hostcheck; \
    ctest --test-dir /build-cache/hostcheck --output-on-failure; \
    \
    echo "=== cross-compile: Windows x86-64 .exe ==="; \
    cmake -S /src/native -B /build-cache/win -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/src/native/cmake/toolchain-mingw-w64.cmake \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_INSTALL_PREFIX=/out \
        -DGRANADAD_BUILD_CLIENT=ON \
        -DGRANADAD_BUILD_TESTS=ON \
        -DGRANADAD_REVISION="${GRANADAD_REVISION}"; \
    cmake --build /build-cache/win; \
    cmake --install /build-cache/win; \
    \
    echo "=== verify every artifact is a self-contained Windows binary ==="; \
    # Two failure modes worth failing the build over, both of which produce an
    # artifact that looks fine sitting in dist/ and dies on the owner's machine:
    #
    #   1. The toolchain file gets ignored and we cheerfully ship an ELF
    #      named .exe.
    #   2. The exe links the MinGW runtime DLLs (libstdc++-6, libgcc_s_seh-1,
    #      libwinpthread-1), which do not exist on a stock Windows box. The
    #      binary then dies with 0xC0000139 STATUS_ENTRYPOINT_NOT_FOUND before
    #      main() runs. This is not hypothetical — granadad-tests.exe shipped
    #      exactly this way once, because the static-runtime link options were
    #      applied to the game target and not to the test target.
    #
    # Checking imports catches (2), which `file` alone cannot see.
    for exe in /out/*.exe; do \
        echo "--- $exe"; \
        file "$exe"; \
        file "$exe" | grep -q "PE32+ executable" \
            || { echo "FATAL: $exe is not a Windows PE binary"; exit 1; }; \
        if x86_64-w64-mingw32-objdump -p "$exe" \
             | grep -iE "DLL Name: +(libstdc\+\+|libgcc|libwinpthread)"; then \
            echo "FATAL: $exe imports a MinGW runtime DLL that will not exist"; \
            echo "       on the target machine. Apply granadad_static_runtime()"; \
            echo "       to this target in native/CMakeLists.txt."; \
            exit 1; \
        fi; \
        echo "    imports: $(x86_64-w64-mingw32-objdump -p "$exe" | grep -c 'DLL Name:') system DLL(s), no MinGW runtime"; \
    done; \
    \
    echo "=== manifest ==="; \
    { \
      echo "Granadad: The Darkstreets — build manifest"; \
      echo "revision:    ${GRANADAD_REVISION}"; \
      echo "build type:  ${BUILD_TYPE}"; \
      echo "target:      windows-x86_64 (PE32+), cross-compiled from Debian bookworm"; \
      echo "cmake:       $(cmake --version | head -1)"; \
      echo "compiler:    $(x86_64-w64-mingw32-g++ --version | head -1)"; \
      echo ""; \
      echo "sha256:"; \
      cd /out && sha256sum *; \
    } > /out/BUILD-MANIFEST.txt; \
    cat /out/BUILD-MANIFEST.txt

# ---------------------------------------------------------------------------
# Stage 3: artifacts only. Tiny, and holds nothing but what gets published.
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim@${DEBIAN_DIGEST} AS artifacts

COPY --from=build /out /artifacts
COPY docker/publish.sh /usr/local/bin/publish.sh

# `docker compose up` runs this: copy the built artifacts onto the bind mount at
# the repo's dist/ and print what landed there.
ENTRYPOINT ["/usr/local/bin/publish.sh"]
