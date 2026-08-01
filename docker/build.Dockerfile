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

# The baked worlds — 21 KB, three files. They come in FIRST because they change
# far less often than the source, so an edit to a .cpp does not invalidate this
# layer. The content tests open these actual files; without them the test gate
# collapses to "fixed.hpp compiles". Everything else under content/ (1.2 GB of
# art) stays out — see .dockerignore.
#
# The path must land at /src/content/maps/baked, because that is what
# native/content/CMakeLists.txt resolves ../../content to from /src/native.
COPY content/maps/baked /src/content/maps/baked

# Only native/ is copied besides that. content/art and .claude/worktrees
# (1.6 GB of parallel checkouts) are excluded by .dockerignore — the compiler
# has no use for either, and the rest of content is read at runtime straight
# from the repo.
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
    echo "=== invalidate every cached object built from other bytes ==="; \
    # Ninja decides what to recompile by comparing mtimes, and /build-cache
    # survives between builds — including builds of a DIFFERENT source tree at
    # the same paths (a scratch copy used for mutation testing, a second
    # worktree). COPY stamps each file with the mtime it had in the build
    # context, which can be OLDER than a cached object compiled from different
    # bytes. Ninja then prints "no work to do" and ctest reports a verdict on
    # code that is not in this tree.
    #
    # Not hypothetical. This build reported `CHECK(floor_mod(-1,32) == 999)`
    # failing — an assertion that exists nowhere in the repo, served whole from
    # a stale object left behind by an earlier mutation test.
    #
    # Stamping the copied sources to now makes every cached object of OURS
    # unconditionally out of date, so our code is always recompiled from the
    # bytes in this context. Third-party objects live under /deps (FetchContent
    # puts each dependency's binary dir there) and are pinned by commit SHA, so
    # they stay cached and the build stays quick. This must run inside this RUN
    # rather than as its own layer: a separate layer would itself be cached,
    # and would hand back mtimes older than the poisoned objects again.
    find /src -exec touch {} +; \
    \
    echo "=== the baked worlds must be in the context ==="; \
    # The content tests read these. If .dockerignore stops re-admitting
    # content/maps/baked/** the cmake configure below fails anyway, but it
    # fails 200 lines into a FetchContent log; say it plainly here instead.
    for w in compound_block docks_surface tavern_fixture; do \
        test -f "/src/content/maps/baked/$w.trojsav" \
            || { echo "FATAL: /src/content/maps/baked/$w.trojsav is missing from the"; \
                 echo "       build context. .dockerignore must re-admit"; \
                 echo "       content/maps/baked/** or the TROJSAV tests cannot run."; \
                 exit 1; }; \
    done; \
    ls -l /src/content/maps/baked; \
    \
    echo "=== host check: build sim + tests for Linux and actually run them ==="; \
    # A cross-compiled .exe cannot be executed here, so correctness is proven on
    # a host build of the same sources. The client is skipped: no SDL needed to
    # test deterministic integer math, and skipping it keeps this pass quick.
    cmake -S /src/native -B /build-cache/hostcheck -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DGRANADAD_BUILD_CLIENT=OFF \
        -DGRANADAD_BUILD_TESTS=ON \
        -DGRANADAD_WERROR=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DGRANADAD_REVISION="${GRANADAD_REVISION}"; \
    cmake --build /build-cache/hostcheck; \
    \
    # ----------------------------------------------------------------------
    # The flags each target compiles with, asserted rather than assumed.
    # ----------------------------------------------------------------------
    # Both halves of this were real. granadad_apply_determinism() is called only
    # on OUR targets, so miniz — the zlib inflate behind every TROJSAV section,
    # i.e. the code that turns bytes on disk into world state — compiled with
    # none of it. And -Werror is worth nothing if it silently stops reaching a
    # target after a refactor.
    #
    # So check the actual command lines cmake generated, not the CMake we hoped
    # we wrote. Printing them too, because "the build says it checked" is the
    # kind of claim this project does not accept on faith.
    echo "=== flags: what each target actually compiles with ==="; \
    cc_json=/build-cache/hostcheck/compile_commands.json; \
    test -f "$cc_json" || { echo "FATAL: no compile_commands.json to check"; exit 1; }; \
    ours="$(grep '"command"' "$cc_json" | grep 'content/src/trojsav\.cpp' | head -1)"; \
    theirs="$(grep '"command"' "$cc_json" | grep 'miniz-src/miniz_tinfl\.c' | head -1)"; \
    test -n "$ours"   || { echo "FATAL: trojsav.cpp not in compile_commands.json"; exit 1; }; \
    test -n "$theirs" || { echo "FATAL: miniz_tinfl.c not in compile_commands.json — is miniz still built from source?"; exit 1; }; \
    echo "--- ours   (content/src/trojsav.cpp)"; \
    echo "$ours" | tr ' ' '\n' | grep -E '^-(f|W|std)' | sort | tr '\n' ' '; echo; \
    echo "    includes: $(echo "$ours" | grep -oE '\-isystem' | wc -l) -isystem, $(echo "$ours" | grep -oE ' \-I[^ ]+' | wc -l) -I"; \
    echo "--- theirs (miniz_tinfl.c)"; \
    echo "$theirs" | tr ' ' '\n' | grep -E '^-(f|W|std)' | sort | tr '\n' ' '; echo; \
    for flag in -fwrapv -ffp-contract=off -fno-fast-math -fno-strict-aliasing; do \
        case "$theirs" in *"$flag"*) ;; \
            *) echo "FATAL: miniz compiles without $flag. It inflates every"; \
               echo "       TROJSAV section, so its output IS world state."; \
               echo "       Call granadad_apply_determinism_deps(miniz::miniz)."; \
               exit 1;; \
        esac; \
    done; \
    case "$ours" in *-Werror*) ;; \
        *) echo "FATAL: our own code compiles without -Werror."; exit 1;; esac; \
    case "$theirs" in *-Werror*) \
            echo "FATAL: -Werror reached third-party source. That makes every"; \
            echo "       upstream bump a build break, and it ends with somebody"; \
            echo "       switching -Werror off for everyone."; exit 1;; \
        *) ;; esac; \
    case "$ours" in *-isystem*) ;; \
        *) echo "FATAL: no -isystem on our compile line — dependency headers are"; \
           echo "       being judged by -Werror. See granadad_mark_headers_system."; \
           exit 1;; esac; \
    echo "ok: determinism codegen reaches miniz; -Werror reaches only our code"; \
    \
    # A gate is only worth what it covers. Before this check the suite was one
    # test — fixed.hpp — while ctest cheerfully printed "100% tests passed,
    # 1 tests out of 1" and everyone read the word "passed". The 57 TROJSAV
    # cases that open the owner's real baked worlds ran nowhere.
    #
    # So: assert a floor on the count, not just on the verdict. If the content
    # module falls out of the build, or doctest's per-case discovery quietly
    # collapses to a single entry, this fails instead of shrinking in silence.
    # Raise the floor when the suite grows; never lower it to make a build pass.
    echo "=== the gate must cover more than one test ==="; \
    GRANADAD_MIN_TESTS=50; \
    test_count="$(ctest --test-dir /build-cache/hostcheck -N \
        | sed -n 's/^Total Tests: *//p')"; \
    echo "ctest knows about ${test_count} tests (floor: ${GRANADAD_MIN_TESTS})"; \
    if [ -z "$test_count" ] || [ "$test_count" -lt "$GRANADAD_MIN_TESTS" ]; then \
        echo "FATAL: the test gate has shrunk to ${test_count:-0} tests, below the"; \
        echo "       floor of ${GRANADAD_MIN_TESTS}. Something stopped being built."; \
        echo "       Check add_subdirectory(content) in native/CMakeLists.txt and"; \
        echo "       that content/maps/baked reached the build context."; \
        exit 1; \
    fi; \
    ctest --test-dir /build-cache/hostcheck -N | grep -q "docks_surface loads completely" \
        || { echo "FATAL: the TROJSAV cases that load the real baked worlds are not"; \
             echo "       registered. The gate would pass without ever opening a"; \
             echo "       .trojsav file."; exit 1; }; \
    \
    ctest --test-dir /build-cache/hostcheck --output-on-failure; \
    \
    # ctest reports per-case pass/fail; it never says how many assertions were
    # behind them. Run the two suites once more, directly, so the build log
    # states plainly what the green badge is worth. Half a second, and it means
    # nobody has to take "tests passed" on faith.
    echo "=== what the gate actually proved ==="; \
    /build-cache/hostcheck/bin/granadad-tests | tail -3; \
    /build-cache/hostcheck/bin/granadad-content-tests | tail -3; \
    \
    echo "=== cross-compile: Windows x86-64 .exe ==="; \
    cmake -S /src/native -B /build-cache/win -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/src/native/cmake/toolchain-mingw-w64.cmake \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_INSTALL_PREFIX=/out \
        -DGRANADAD_BUILD_CLIENT=ON \
        -DGRANADAD_BUILD_TESTS=ON \
        -DGRANADAD_WERROR=ON \
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

# `docker compose run --rm --build build` runs this: copy the built artifacts
# onto the bind mount at the repo's dist/ and print what landed there. If it
# fails, its non-zero exit reaches the caller — which is the whole reason the
# documented command is `run` and not `up`. See docker-compose.yml.
ENTRYPOINT ["/usr/local/bin/publish.sh"]
