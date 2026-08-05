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

# bash, not the default /bin/sh. Debian's sh is dash, and dash has no
# `set -o pipefail` — it exits 2 on the attempt. The build's one un-duplicated
# piped run (the relocated-content-dir content suite) needs pipefail to be able
# to go red at all, so the shell that gets it is the shell that has it.
SHELL ["/bin/bash", "-c"]

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

# The custom tile pack, 43 KB, for the same reason: the renderer's tests load
# the owner's actual sheet and draw an actual frame of the Docks. Without it
# they would only ever exercise the procedural fallback, which is the one thing
# that never ships.
COPY content/art/custom /src/content/art/custom

# The actor sprites, 30 KB, for the same reason (#78). The ward's six hundred
# people are drawn out of the owner's own sheet, and ActorSheet::load is
# deliberately silent about a missing pack -- so without this in the context
# every case about how they look would pass against the procedural fallback.
COPY content/art/sprites /src/content/art/sprites

# The spell raws, 11 KB. S2's priest of the Flame teaches out of the owner's
# actual spells.json rather than out of a table in a .cpp — see .dockerignore.
COPY content/raws/spells /src/content/raws/spells

# The raws the ward SPEAKS out of, 100 KB (S3). The conversation layer writes
# no dialogue: barks.json is every line anybody says, notables.json is who the
# Forty are, histories.json is the fifteen stories between them, rumors.json is
# who may repeat which one, and skills.json owns the vocabulary a haggle is
# fought in. All four loaders are deliberately silent about a missing file so a
# content edit cannot stop the game booting -- which is exactly why these have
# to be in the context: without them the suite would pass against empty tables.
COPY content/raws/barks /src/content/raws/barks
COPY content/raws/names /src/content/raws/names
COPY content/raws/rumors /src/content/raws/rumors
COPY content/raws/skills /src/content/raws/skills

# The raws the ward's GUILDS come out of, 12 KB (S4). factions.json is the
# owner's five factions and the jobs that belong to each; ranks.json is the
# ladder S4 hangs off every one of them; quests/ carries both the owner's
# vanished-clerk line, which this build skips BY SHAPE, and the Priest of the
# Flame line, which it runs end to end.
#
# quests/ is copied whole rather than by filename, because QuestBook::load reads
# the directory and tells the two schemas apart itself. Copying one file and not
# the other would make the case that proves it skips the owner's file pass for
# entirely the wrong reason.
COPY content/raws/factions /src/content/raws/factions
COPY content/raws/quests /src/content/raws/quests

# S6: the radiant contract board. Every broker, patron and source in it is a
# cross-reference into notables.json and is refused at load if that file does
# not have the id -- so without this in the context the board loads EMPTY and
# every case about it would pass against nothing.
COPY content/raws/contracts /src/content/raws/contracts

# S7: the roll, and the households on it.
#
# compounds.json is the five plots of DOCKS-GAZETTEER section 2.8 and it is
# refused at load, plot by plot, unless notables.json has the Den Duke, the
# creditor and the charter priest it names -- the same gate the contract board
# above passes through, and without the file in the context that refusal would
# be proved by an absence.
#
# household.json is the canon household-size distribution the ward's population
# is DERIVED from ({1:20,2:35,3:25,4:15,5:5}, the same numbers section 2.5
# cites when it derives the ward). The loader falls back to a copy of those
# weights compiled into the code, deliberately, so the game still boots while
# somebody is editing a raw -- and that fallback is exactly why the real file
# has to be here, or the claim that the population comes out of the owner's own
# numbers would never be tested.
COPY content/raws/compounds /src/content/raws/compounds
COPY content/raws/actors /src/content/raws/actors

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
    # pipefail, added in S2. Without it a pipeline's status is the LAST
    # command's, so `granadad-content-tests | tail -3` reports success no matter
    # what the suite did. One run in here is not duplicated by ctest -- the
    # relocated-content-dir run below -- and it was the one being swallowed.
    set -o pipefail; \
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
    echo "=== the art pack must be in the context ==="; \
    # 43 KB, and the renderer's tests bind every material to a real region of
    # it. Without it they silently fall back to procedural tiles and the gate
    # stops covering the art path entirely.
    for asset in art-mapping.json tiles.png; do \
        test -f "/src/content/art/custom/$asset" \
            || { echo "FATAL: /src/content/art/custom/$asset is missing from the"; \
                 echo "       build context. .dockerignore must re-admit"; \
                 echo "       content/art/custom/** or the renderer is only ever"; \
                 echo "       tested against its procedural fallback."; \
                 exit 1; }; \
    done; \
    \
    echo "=== the actor sprites must be in the context (#78) ==="; \
    # 30 KB, and the same trap as the tile pack: ActorSheet::load falls back to
    # procedural silhouettes so a content edit cannot stop the game booting,
    # which means a build without these files renders a ward of grey lozenges
    # and every case about how the district LOOKS passes against the fallback.
    for asset in sprite-index.json sprites.png; do \
        test -f "/src/content/art/sprites/$asset" \
            || { echo "FATAL: /src/content/art/sprites/$asset is missing from the"; \
                 echo "       build context. .dockerignore must re-admit"; \
                 echo "       content/art/sprites/** or the ward's six hundred"; \
                 echo "       people are only ever drawn with the fallback."; \
                 exit 1; }; \
    done; \
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
    test -f /src/content/maps/baked/docks_surface.lamps.json \
        || { echo "FATAL: the baked lamp sidecar is missing. The renderer has no"; \
             echo "       light sources without it and the district goes dark."; \
             echo "       Re-derive it with granadad-bake-lamps."; exit 1; }; \
    ls -l /src/content/maps/baked; \
    \
    echo "=== the spell raws must be in the context (S2) ==="; \
    # The priest of the Flame teaches from these. Without the file the
    # Spellbook falls back to an empty book ON PURPOSE -- a missing raws
    # directory must not stop the game booting -- so a test that only checked
    # "he taught nothing" would pass here and prove nothing. Say it plainly.
    test -f /src/content/raws/spells/spells.json \
        || { echo "FATAL: /src/content/raws/spells/spells.json is missing from the"; \
             echo "       build context. .dockerignore must re-admit"; \
             echo "       content/raws/spells/** or the priest teaches nothing"; \
             echo "       and the case that proves he teaches from CANON is"; \
             echo "       proving it against the empty fallback."; exit 1; }; \
    \
    echo "=== the raws the ward SPEAKS out of must be in the context (S3) ==="; \
    # Same trap as the spell raws, and S3's first build fell straight into it:
    # BarkTables, NotableRegistry and SkillTrack are all deliberately SILENT
    # about a missing file, because a content edit must not stop the game
    # booting. Silent means a build with these files absent produces a game
    # where nobody says anything and a suite that either fails confusingly or
    # -- worse -- passes against the empty-content fallback. Say it plainly,
    # here, where it is one line instead of nineteen red cases.
    for raw in barks/barks.json names/notables.json names/histories.json \
               names/names.json rumors/rumors.json skills/skills.json \
               barks/flame_barks.json factions/factions.json factions/ranks.json \
               quests/quests.json quests/flame_disciple.json \
               barks/roof_barks.json quests/skyrunner_tenant.json \
               contracts/contracts.json barks/contract_barks.json \
               factions/chapters.json barks/nemesis_barks.json; do \
        test -f "/src/content/raws/$raw" \
            || { echo "FATAL: /src/content/raws/$raw is missing from the build"; \
                 echo "       context. .dockerignore must re-admit it, or the"; \
                 echo "       conversation layer has no words in it and the"; \
                 echo "       cases that prove it speaks from CANON are proving"; \
                 echo "       it against an empty table."; exit 1; }; \
    done; \
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
    # ----------------------------------------------------------------------
    # No unordered containers. Checked by grep, and that is not laziness.
    # ----------------------------------------------------------------------
    # std::unordered_map's iteration order is a function of the keys, the
    # insertion sequence and the standard library's bucket policy. Two runs of
    # the same binary in the same process therefore AGREE -- which means the
    # twin-run gate, the one check that catches nondeterminism, is structurally
    # blind to this. It only diverges against another machine, another libstdc++
    # or another toolchain, which is exactly when it is most expensive to find.
    #
    # So it is banned outright and the ban is enforced here, at the only place
    # that can see it. Sorted or dense-index containers, everywhere, tests
    # included. The one hit allowed is the word appearing in a comment that
    # explains this rule.
    echo "=== no unordered containers anywhere in native/ ==="; \
    if grep -rn --include=*.cpp --include=*.hpp \
         'std::unordered_\(map\|set\|multimap\|multiset\)' /src/native \
         | grep -v '^\s*//' | grep -v '// *.*unordered' | grep -v '^[^:]*:[0-9]*: *\*' \
         | grep -v '^[^:]*:[0-9]*:\s*//'; then \
        echo "FATAL: an unordered container reached native/. Iteration order is"; \
        echo "       then a property of the standard library, and the twin-run"; \
        echo "       gate CANNOT see it: both runs share a process and agree"; \
        echo "       with each other while disagreeing with every other machine."; \
        echo "       Use std::map, a sorted vector, or a dense index."; \
        exit 1; \
    fi; \
    echo "ok: no std::unordered_* in native/"; \
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
    #
    # M1: 59 -> 128. The jump is mostly granadad-tests, which used to be ONE
    # ctest entry for the whole binary no matter how many cases it held --
    # doctest_discover_tests now registers it per case, so the sim suite is
    # visible to this floor for the first time. The remainder is the twin-run
    # gate and the world-hash fingerprint, one entry each.
    #
    # S1: 128 -> 183. Movement (angles, tile queries, the body), the lamp bake,
    # the tile atlas, and the renderer -- which draws real frames of the real
    # Docks in here, on every build, because the renderer is software and needs
    # no window.
    #
    # S2: 183 -> 228. The Gilded Gull -- its geometry re-read from the baked
    # bytes, the brawl/lethal rule as a table, the pathfinder, the door policy
    # end to end -- plus the content-directory resolver that decides whether the
    # shipped game starts at all, the client's fixed-timestep loop, and a second
    # twin-run gate entry with the tavern registered.
    #
    # S3: 228 -> 270. The conversation layer -- the owner's 210 bark tables
    # and the key vocabulary this code builds against them, the 42 notables
    # with their 15 micro-histories and the rumor domains that gate who may
    # repeat which one, the ledger that remembers what the player did (with
    # its byte encoding round-tripped), use-XP skills over the skills raws,
    # haggling as an argument, and the conversation surface proved to leave
    # the centre of the screen alone. Plus the four S2 review findings that
    # were closed with a case rather than a comment: headroom on a
    # purpose-built world, the Q8 read caught mid-stride, actor pixels
    # counted apart from candle pixels, and the Gull's furniture derived
    # from the baked bytes.
    #
    # S4: 270 -> 311. The guilds -- the owner's five factions, the ladders hung
    # off them, the mirror between the Watch and the roofs, the byte codec, and
    # what a rung does to the price of a mug across a real counter; the six-stage
    # Priest of the Flame line played end to end through the same calls a
    # keypress makes; canon's own spell cost model and the pairing table it
    # refuses by, run against the owner's own eleven authored craftings; the
    # composition bench. Plus the six S3 review findings closed with cases that
    # can go red rather than comments that cannot: the witness radius pinned from
    # BOTH sides, the same-floor clause, the line-of-sight clause, and the topic
    # list proved completely addressable from the keyboard at any length.
    #
    # S7: 371 -> 394. Two halves. The four S6 review findings closed with cases
    # that can go red -- the watchman's eye tested AT THE CALL SITE and not
    # only in the arithmetic, the condemned-man amnesty closed, the authored
    # object given an existence, and the boat made to land what the board asked
    # for -- plus the two the shipped frames themselves proved: no HUD line
    # drawn off the edge, and the picked topic spelled out in full under the
    # grid. Then the compounds: the roll refused against the owner's own
    # notables, courtyard crops that grow and FAIL, the bond that moves labour
    # between compounds, the priest's six outcomes, the player's
    # lease-buy-let-collect arc, and TWO gate entries -- a twin run over the
    # ward and a two-year soak whose exit code IS the balance bar.
    #
    # S8: 394 -> 413. The nemesis -- the man who put the player on the
    # floor, the rung he climbs off the owner's own ladders for it, the
    # trade house he founds out of the new chapters.json with real members
    # and a permanent toll on the ward's prices, the vacant charge he takes
    # on the compound roll, the memory that changes how he greets you and
    # what is in his hands, and the arc played end to end through a real
    # taproom brawl. Plus the four S7 review findings closed with cases
    # that can go red: the HUD alert off the topic grid with a PIXEL
    # assertion behind it, the lodgers' eviction stated as an invariant
    # instead of a tautology, the abatement made undeletable, and the
    # bond-pipe precondition constructed instead of tested for.
    #
    # S10: 442 -> 469. The demo. The bloodletter trail -- twelve authored
    # leads at the map's own clue anchors, checked against the BAKED world
    # rather than against a comment, walked end to end on foot by the
    # district's own router; the two dead ends, asserted dead; the five
    # legend tracks; the first-run page, the in-game key list, and the
    # centre of the screen staying clear with each of them up. Plus the S9
    # review's findings closed with cases that can go red: a lock opened by
    # a hand that only has what a player has, the dark room asserting the
    # claim in its own name, and the burglary's stealth beat needing
    # somebody awake in the room to miss it.
    echo "=== the gate must cover more than one test ==="; \
    # #79: 490 -> 502. The suite is at 525 with the ward's voice in it (the
    # opening shot, the names, the moods, and the key that reaches the street),
    # and the floor moves with it. It is a FLOOR and not an equality on purpose
    # -- a sprint that adds cases must not have to edit this line -- but a floor
    # that never moves stops being able to notice a module falling out.
    # #80: 502 -> 526. The suite is at 541 with the roofs and the food chain in
    # it -- the climb verb, the roof beds proved both ways, the Skyrunners in
    # their own territory, the scrap clamp and the soak that watches the mouse
    # count fall and come back -- and the floor moves with the last number the
    # gate actually measured.
    GRANADAD_MIN_TESTS=526; \
    # Listed ONCE into a variable, and grepped from there. `ctest -N | grep -q`
    # is racy under `set -o pipefail`: grep -q exits the moment it matches, ctest
    # dies of SIGPIPE, and the pipeline reports failure for a check that PASSED.
    # It went red exactly that way the first time pipefail was turned on.
    ctest_list="$(ctest --test-dir /build-cache/hostcheck -N)"; \
    test_count="$(printf '%s\n' "$ctest_list" | sed -n 's/^Total Tests: *//p')"; \
    echo "ctest knows about ${test_count} tests (floor: ${GRANADAD_MIN_TESTS})"; \
    if [ -z "$test_count" ] || [ "$test_count" -lt "$GRANADAD_MIN_TESTS" ]; then \
        echo "FATAL: the test gate has shrunk to ${test_count:-0} tests, below the"; \
        echo "       floor of ${GRANADAD_MIN_TESTS}. Something stopped being built."; \
        echo "       Check add_subdirectory(content) in native/CMakeLists.txt and"; \
        echo "       that content/maps/baked reached the build context."; \
        exit 1; \
    fi; \
    # WHY A SHELL `case` AND NOT `printf | grep -qF`. S9 found this the hard
    # way: every one of the checks below used to be a pipeline, and `grep -q`
    # EXITS THE MOMENT IT MATCHES. With `set -o pipefail` on and a test list
    # that has grown to four hundred lines, printf is still writing when grep
    # goes away, takes SIGPIPE, and the pipeline reports failure FOR A CASE
    # THAT IS REGISTERED. It bit "the stealth line is one row on an edge",
    # sitting at #265 of 442, and it would have bitten a different case every
    # time the suite grew. A glob match against the variable spawns no process,
    # cannot race, and is what these were always trying to say.
    case "$ctest_list" in *"docks_surface loads completely"*) ;; *) false;; esac \
        || { echo "FATAL: the TROJSAV cases that load the real baked worlds are not"; \
             echo "       registered. The gate would pass without ever opening a"; \
             echo "       .trojsav file."; exit 1; }; \
    case "$ctest_list" in *"granadad-twin-run-gate"*) ;; *) false;; esac \
        || { echo "FATAL: the twin-run gate is not registered with ctest. It is the"; \
             echo "       only check here that can catch NONDETERMINISM rather than"; \
             echo "       incorrectness, and every other guarantee rests on it."; \
             exit 1; }; \
    case "$ctest_list" in *"every shipped world hashes to exactly what the JVM said"*) ;; *) false;; esac \
        || { echo "FATAL: the case that compares the C++ world hash against the"; \
             echo "       JVM's is not registered. Without it the hasher is only"; \
             echo "       being compared to itself."; exit 1; }; \
    # S1's two by name. The renderer is software precisely so that a frame of
    # the real district can be drawn and checked HERE, with no window and no
    # GPU; and the art case must load the owner's actual sheet rather than the
    # procedural fallback, which is the one thing that never ships.
    case "$ctest_list" in *"the Docks render to a frame with a world in it"*) ;; *) false;; esac \
        || { echo "FATAL: the case that renders the Docks in first person is not"; \
             echo "       registered. Every sprint after S1 proves itself with a"; \
             echo "       captured frame, and this is what keeps that path alive."; \
             exit 1; }; \
    case "$ctest_list" in *"the owner's art pack loads when it is there"*) ;; *) false;; esac \
        || { echo "FATAL: the case that loads content/art/custom is not"; \
             echo "       registered, so the renderer is only ever being tested"; \
             echo "       against its own procedural fallback."; exit 1; }; \
    \
    # S2's four, by name. Each of these is a claim the sprint is judged on, and
    # a claim whose test has quietly stopped being registered is a claim
    # nobody is checking.
    for case in \
        "a brawler gets warned and then physically put out of the door" \
        "a fist fight is a brawl and a knife fight is not" \
        "the room is lit and full at nine, dark and empty at five" \
        "the executable finds a content tree one level above itself" \
        "a frame that runs no step keeps its mouse look for the next one" \
        "granadad-twin-run-gate-tavern"; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S2 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S2's named cases are all registered"; \
    \
    # S3's, by name. The first five are the sprint's own claims; the last
    # four are S2 review findings that were closed with a case that can go
    # red rather than with a comment that cannot.
    for case in \
        "an actor's disposition changes what that actor DOES" \
        "who can tell you what is decided by the raws, not by a dice roll" \
        "standing is AUDIBLE: the same person greets you out of a different table" \
        "the conversation surface leaves the centre of the screen alone" \
        "a night's sleep does not make anybody forget" \
        "headroom refuses the world's own ceiling" \
        "an actor is drawn where the simulation says the actor is" \
        "sprite pixels and people are counted apart" \
        "the Gull's lights come out of the baked bytes, not out of the renderer"; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S3 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S3's named cases are all registered"; \
    \
    # S4's, by name. The first eight are the sprint's own claims -- a faction
    # you can join, a ladder you climb, influence that reaches the price of a
    # mug, a questline finished, and spellcrafting priced by canon's own model.
    # The last five are S3 review findings, each closed with a case that goes
    # red when the rule is removed rather than a comment that cannot.
    for case in \
        "a player can take the oath, climb the Mission's ladder and finish the priest's line" \
        "the five factions come out of the owner's file and the ladders hang off it" \
        "every rung is earned, including the first" \
        "the mirror ledger: what the Watch gains the roofs lose" \
        "a rung on the Row is worth real coin across a real counter" \
        "the priest's authored voice never says there are seven" \
        "the owner's eleven authored craftings all pass the rules this build enforces" \
        "a quest file is read for its shape, and the owner's is left alone" \
        "a deed carries eight tiles and no further" \
        "a robbery on the guest floor is not witnessed by the taproom below" \
        "nobody witnesses anything through a wall" \
        "every topic is reachable by a number printed beside it" \
        "the workbench draws where a conversation is allowed to be"; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S4 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S4's named cases are all registered"; \
    \
    # S5: the roofs, the trade that runs on them, and the drawing path a
    # mutation walked straight through in S4.
    for case in \
        "the roof moves open two thirds again of the district" \
        "the roof-slum plane was completely unreachable and is not any more" \
        "the whole roof of the Gilded Gull can be stood on" \
        "a mantle grips a wall face and comes up on top of it" \
        "a mantle refuses open air -- there is nothing to put your hands on" \
        "a leap crosses the alley between two roofs and lands on the far one" \
        "the roof moves do not open a trapdoor into the unbuilt dungeon" \
        "the Gull's guest floor has never been reachable on foot, and now is" \
        "a crime moves the tally, the heat and BOTH sides of the mirror at once" \
        "a warrant is issued high and lapses low, so one cooled point cannot flicker it" \
        "the ward forgets at one rate whether it is watched or slept through" \
        "a cutpurse is not a fence: the second rung is what makes somebody buy" \
        "nobody hands a stranger a bale, and carrying one out past the law is what pays" \
        "a watchman gets longer to finish his drink and a wanted man gets none" \
        "the Skyrunner line is authored against a vocabulary that can finish it" \
        "a player can sign on with the roofs and finish the Skyrunner line" \
        "page two of a long list DRAWS nine numbered rows, not none" \
        "a counted stage refuses to be turned in until it has been done" \
        "the gate's workload actually moves a faction number" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S5 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S5's named cases are all registered"; \
    \
    # S6: the goods, the radiant work over them, and the law that takes both \
    # away. The two named last are the sprint's ACCEPTANCE -- a contract \
    # accepted, performed and paid, and a separate run that ends in the \
    # impound with the job dead beside the goods. \
    for case in \
        "a generated job can only ever name somebody the owner's file has" \
        "a broker, patron or source the registry does not have is refused at load" \
        "what a sack holds is measured in weight, and that is what gets you caught" \
        "a watchman notices a load, not a count, and never a man with nothing on him" \
        "the sentence is canon's: a night for anybody, the hand and then the rope for the roofs" \
        "an arrest empties the sack, tears up the paper and remembers the hand" \
        "the same night of the same world offers the same work, and the next night does not" \
        "a bounty is not paid without the Flame's mark, and pay is the ward's own economy" \
        "a contract taken, performed and paid: the ward's bounty, end to end" \
        "caught: a load, a warrant, and a job that dies in the impound" \
        "a warrant alone is enough, given long enough in front of the wrong man" \
        "a leap is armed by the key and flown by the pump, and lands ONCE" \
        "the room charges a landing: the craft, the fall, the roof and the tally" \
        "a topic label too long for its column stops at a word, not mid-word" \
        "a scripted line sets the clock it needs, and never one that was asked for" \
        "the heat clock survives its own codec past thirty-two bits" \
        "the gate's workload actually takes a contract off the board" \
        "the sack and the job are one line each, on the edge, and empty when there is nothing" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S6 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S6's named cases are all registered"; \
    \
    # S7, part one: the four S6 review findings, and the two its own shipped \
    # frames proved. Every one of these is a claim that used to be a comment. \
    for case in \
        "a watchman's eye is on the load AT THE CALL SITE, not only in the arithmetic" \
        "the rope is not an amnesty: a condemned man is the one face the ward knows" \
        "a recovery job is settled by the piece it named, not by a count in a sack" \
        "a boat lands what the ward ordered, so the night's board can be filled at all" \
        "no HUD line is ever drawn off the edge of the frame it is in" \
        "the picked topic is spelled out in full under the grid, however long it is" \
        "two jobs on one board are told apart by the first word, not the last" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the S6 findings S7 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S7's carry-forward cases are all registered"; \
    \
    # S7, part two: the compounds. The last three named are the sprint's \
    # ACCEPTANCE -- two years of the ward feeding itself at or under the bar \
    # the Java build held, the bridge that says a day off the engine's clock \
    # is the same day the soak's shortcut runs, and the soak itself as a ctest \
    # entry whose EXIT CODE is the balance bar. \
    for case in \
        "the roll names nobody the owner's own file does not have" \
        "a compound is dwelling units inside one wall, not a street of houses" \
        "a courtyard bed grows, is cut, and feeds the compound it stands in" \
        "a bed nobody turns over comes up worth nothing" \
        "buy the paper on a compound's hands and its courtyard comes up thin" \
        "the bond is the pipe: leased labour turns up in the bondholder's yard" \
        "the ground penny falls on the earth, never on the dwelling" \
        "a Den Duke cannot turn a family out, and only one of the six answers is eviction" \
        "an offering is an offering and not a fee: it does not buy the verdict" \
        "the player leases space, buys a house, becomes a landlord, and collects" \
        "leasing yourself in lieu of the penny is a debt relation, not a caste" \
        "two years of the ward: the compounds feed themselves, and nothing drowns" \
        "a day off the engine's clock is the same day as a day off endOfDay" \
        "granadad-ward-soak" \
        "granadad-twin-run-gate-ward" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S7 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S7's compound cases are all registered"; \
    \
    # S8, part one: the four S7 review findings. Every one of these was a
    # case that could not go red, and three of them were proved dead by a
    # mutation that shipped green.
    for case in \
        "a warning shouted mid-conversation does not land on the topic grid" \
        "a Den Duke cannot turn a family out, and only one of the six answers is eviction" \
        "the abatement is the sharpest instrument in the ward, and it is not dead code" \
        "the bond is the pipe: leased labour turns up in the bondholder's yard" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the S7 findings S8 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S8's carry-forward cases are all registered"; \
    \
    # S8, part two: the nemesis. The last named is the sprint's ACCEPTANCE
    # -- a named labourer puts the player down in an ordinary fist fight,
    # the player gets up, and the labourer is not who he was.
    for case in \
        "a trade house can only ever rise inside a faction the owner's file has" \
        "a guild is a guild OF something: the trade picks the house, not the faction" \
        "a labourer who puts the player down rises on the ward's own ladder" \
        "what one guild gains in the ward, its declared rival loses" \
        "the second win founds a house with real members, and a mug never costs what it did" \
        "the third win puts his name on the ward's roll" \
        "a rise that cannot reach the roll still ranks and still founds" \
        "he remembers, and he does not greet you the way he did" \
        "he comes prepared, and a man with a blade is not a brawl any more" \
        "past the grudge he stops keeping his own hours" \
        "you can win the rematch, and you still cannot un-found his guild" \
        "the book survives its own codec, which is the seam a save file uses" \
        "the ward's roll is in the windowed game, and a rival can take ground on it" \
        "the man who put you down is one line on an edge, and the centre stays empty" \
        "the gate's workload actually loses a fight and hashes what it cost" \
        "the scripted nemesis arc is played, not staged, and the HUD says who he is" \
        "beaten by a named labourer in a fist fight, and he is not who he was" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S8 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S8's nemesis cases are all registered"; \
    \
    # S9, part one: the S8 review's findings, closed with cases rather than
    # comments. The first is S7's finding #8 finally shut -- the ward's day
    # turning inside the windowed game -- and the last two are the nemesis
    # book's two lookups that were quietly wrong.
    for case in \
        "the ward's roll is in the windowed game, and a rival can take ground on it" \
        "a guild is a guild OF something: the trade picks the house, not the faction" \
        "the book survives its own codec, which is the seam a save file uses" \
        "a rival keeps his record when the roster hands him a different id" \
        "a labourer who puts the player down rises on the ward's own ladder" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the S8 findings S9 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S9's carry-forward cases are all registered"; \
    \
    # S9, part two: stealth, thievery and lockpicking. The last named is the
    # sprint's ACCEPTANCE -- a burglary played from the keys, in the dark,
    # against a lock that can beat you.
    for case in \
        "light is an integer field with the renderer's own shape" \
        "the sky is committed dark, and a roof takes three quarters of it" \
        "noise is what you are doing, and the loudest thing wins" \
        "every clause of the notice rule moves the answer, and none of them alone decides it" \
        "crouching, the dark and the skill are what a player does about it" \
        "the Gull's own lamps light the law, not only the eye" \
        "the same crime is witnessed in a lit taproom and missed in a dark one" \
        "a hand in a coat is refused when the mark can see you and taken when they cannot" \
        "a trained sneak lifts in a loud room what the same hands cannot lift standing up" \
        "skyrunning and cracksmanship both rise from a night's work" \
        "crouching is the room's own state, and the body pays for it in speed" \
        "the stealth line is one row on an edge, and it says what it is looking at" \
        "a lock's pins are a pure function of the seed and the lock, and never re-rolled" \
        "skill buys information and forgiveness, and never buys success" \
        "a wrong probe strains the wire, and enough of them snap it" \
        "the last pick snapping jams the lock, and only force opens it then" \
        "the feel tells a trained hand which way it was wrong, and an apprentice nothing" \
        "the box above the stair is locked, and cracksmanship is what opens it" \
        "forcing a lock always works, is the loudest thing in the house, and costs half" \
        "a jammed lock is permanent, and the room says so with the box still shut" \
        "your own rented room is not a lock to pick" \
        "Finch sells wire to his own and to nobody else" \
        "the lock row draws the whole minigame, and the centre of the screen stays empty" \
        "a burglary is played from the keys: crouch, cross, lift, climb, pick, empty" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S9 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S9's stealth, thievery and lockpicking cases are all registered"; \
    \
    # S10. The demo's own claims, by name. A trail whose cases silently stopped
    # being built would leave a green build with no investigation in the game --
    # and the two that close S9's findings are exactly the ones a future sprint
    # would be tempted to delete when they went red.
    for case in \
        "the casebook loads the owner's own trail, and every lead is reachable from the hook" \
        "every lead stands somewhere a body can stand in the baked Docks" \
        "the trail is walked across the real district, on foot, by the router" \
        "you cannot read a clue nobody has pointed you at" \
        "the trail is walked end to end, and the dead ends cost a walk and pay a clue" \
        "the look key finds the body, and the district's other corners stay quiet" \
        "the casebook opens in the conversation's own bands and leaves the middle alone" \
        "working the trail is what the Flame's track is made of, and it pays a rung" \
        "the five tracks are five different people, and the titles never collide" \
        "a new game opens on the case, not on a systems demo" \
        "the keys are in the game, and every verb the client binds is on the list" \
        "the notes, the keys and the world never fight over the middle of the screen" \
        "a lock opens to a hand that only has what a player has" \
        "the burglar's second box is opened by hands the first one taught" \
        "the Skyrunner line lands all nine of its stages" \
        "the Priest of the Flame line lands all six of its stages" \
        "the nemesis arc lands all seven of its beats" \
        "the roof line gets onto the lead and back down again, all three ways" \
        ; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things S10 is judged on."; \
                 exit 1; }; \
    done; \
    echo "ok: S10's trail, legend and first-run cases are all registered"; \
    \
    for case in \
        "the ward has a roll, and it is the size the Java build's was" \
        "every kind of person the owner named is actually in the ward" \
        "everybody in the ward is standing somewhere a body can stand" \
        "one body per square, and it holds while six hundred of them walk" \
        "no guard pile-ups: a watchman never shoves a watchman on duty" \
        "per-kind item conservation is exact, tick after tick" \
        "the ward feeds itself: nobody is on the road to starving after a day" \
        "the ward keeps its hours: everybody is somewhere for a reason" \
        "a rostered guard on the night beat does not oscillate on its own bunk" \
        "the ward's needs come out of the owner's raws, not out of a table here" \
        "no job in the ward can outscore going to bed" \
        "a route never cuts a solid corner, and never comes back partial" \
        "two actors asking the same question walk it differently" \
        "the population is registered in the windowed game and keeps the hour" \
        "a person is a figure somebody drew, not an egg with a head on it" \
        "granadad-twin-run-gate-population"; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is one of the things #78 is judged on -- the ward"; \
                 echo "       having people in it, at the right places at the right"; \
                 echo "       hours, drawn as figures somebody actually drew."; \
                 exit 1; }; \
    done; \
    echo "ok: #78's population cases are all registered"; \
    \
    for case in \
        "the owner's notables are the bodies keeping their own authored sites" \
        "every name in the ward is a row of the owner's own raws" \
        "the ward's poor go by one name, and its trades carry two" \
        "a dockhand, a watchman and a priest greet you with three different sentences" \
        "the same trade at four in the morning is not the same trade at noon" \
        "a starving body says so, and says it in its own trade's voice" \
        "the mood a body is in replaces the greeting it would have given" \
        "a cat answers, and is not asked about the vanished clerk" \
        "a ward speaker cannot be confused with one of the Gull's" \
        "the ward's trades talk shop about their own trade" \
        "pressing the talk key on a street corner reaches the body standing on it" \
        "the street line reaches three trades and gets three different voices" \
        "a hand in a ward purse takes the coin off a real body"; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is what #79 is judged on -- the whole ward being"; \
                 echo "       addressable, in the owner's own authored voice, with"; \
                 echo "       a name over it that came out of his own raws."; \
                 exit 1; }; \
    done; \
    echo "ok: #79's ward-voice cases are all registered"; \
    \
    # #80. THE TWO GAPS THE POPULATION ROUND FLAGGED AND DID NOT CLOSE, both of
    # which were stated in-code rather than papered over -- the roof slum with
    # nobody in it, and a district of rats nothing ate.
    #
    # THE FIRST THREE ARE THE ONES TO WATCH. "the roof slum has tenants" is the
    # headline and would be satisfied by a body dumped on a deck; "a roof bed is
    # a bed you can get out of" is the one that says the tenant is not stranded;
    # and "the ward's cats actually get hungry now" is the one that says the
    # hunt is not dead code. All three would be tempting to delete on the day
    # they go red, and all three are exactly the claim being made.
    for case in \
        "the roof slum has tenants, and they are the people canon puts up there" \
        "a roof bed is a bed you can get out of, and it is proved both ways" \
        "the Skyrunners live on a deck, not in a ground condo" \
        "climbing is a verb the poor have and the Watch does not" \
        "a walker cannot reach the roof-slum plane, and a climber can" \
        "a climb costs what a climb costs, and open ground is priced the same" \
        "the roof empties when its tenants go out to work, and fills when they are back" \
        "a roof tenant that went out to work climbs home again on its own legs" \
        "nobody is homed on ground they cannot reach, climber or not" \
        "the mice are a contiguous id range, which is what makes the hunt cheap" \
        "a scrap is not a meal: the ward's cats actually get hungry now" \
        "the food chain runs: mice are taken, and the den puts more out" \
        "a caught mouse holds no tile and is drawn nowhere" \
        "a chase that cannot land is abandoned, and the beast goes back to wandering" \
        "the ward's loaf ledger does not move when a cat eats a rat"; do \
        case "$ctest_list" in *"$case"*) ;; *) false;; esac \
            || { echo "FATAL: the case \"$case\" is not registered."; \
                 echo "       It is what #80 is judged on -- somebody actually"; \
                 echo "       living on the roofs and able to get down again, and"; \
                 echo "       a food chain that actually bites."; \
                 exit 1; }; \
    done; \
    echo "ok: #80's roof and food-chain cases are all registered"; \
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
    # ----------------------------------------------------------------------
    # The twin-run gate, at length, with its output in the build log.
    # ----------------------------------------------------------------------
    # ctest already ran it -- and ctest prints "Passed", which is the same three
    # letters whether the gate compared two runs or compared nothing. This runs
    # it long and prints what it actually compared, so the log states the claim
    # rather than asserting it.
    echo "=== the twin-run determinism gate ==="; \
    /build-cache/hostcheck/bin/granadad-twin-gate --ticks 2000 --walkers 128; \
    \
    # A gate that has never been observed going red has not been shown to work.
    # Its two comparators are unit-tested against synthetic divergence in
    # native/tests/test_twin_gate.cpp; the whole-gate mutation proof is a manual
    # run against a scratch copy of the tree and is recorded in the M1 report,
    # not here -- deliberately introducing nondeterminism inside the build that
    # is supposed to reject it would be a hard thing to ever remove safely.
    \
    # ----------------------------------------------------------------------
    # Half of the cross-toolchain comparison. The other half only Windows can
    # run — see scripts/verify-windows.ps1.
    # ----------------------------------------------------------------------
    # sim-core bans float/double so the decode is identical on every toolchain.
    # Until #75 that was tested on exactly one toolchain: the cross build
    # compiled granadad-content-tests.exe and threw it away, because
    # GRANADAD_CONTENT_DIR was a compile-time constant naming a path inside
    # this container.
    #
    # Now it is read from the environment at run time. Proved here, twice,
    # because only the pair is worth anything:
    #   1. same worlds under a DIFFERENT path -> byte-identical report.
    #      Alone this proves nothing; the compiled-in default would pass it.
    #   2. a bogus path -> non-zero exit. This is the one that shows the
    #      variable is actually being read.
    echo "=== \$GRANADAD_CONTENT_DIR is read at run time ==="; \
    mkdir -p /out /relocated/maps; \
    cp -a /src/content/maps/baked /relocated/maps/baked; \
    /build-cache/hostcheck/bin/granadad-content-tests \
        --fingerprint /out/content-fingerprint-linux-gcc.txt; \
    GRANADAD_CONTENT_DIR=/relocated \
        /build-cache/hostcheck/bin/granadad-content-tests \
        --fingerprint /tmp/relocated-fingerprint.txt; \
    cmp /out/content-fingerprint-linux-gcc.txt /tmp/relocated-fingerprint.txt \
        || { echo "FATAL: the same worlds read from a different directory produced"; \
             echo "       a different report. The report is supposed to describe"; \
             echo "       world state and nothing whatsoever about where it was"; \
             echo "       read from — otherwise the Windows comparison would fail"; \
             echo "       for reasons that have nothing to do with determinism."; \
             exit 1; }; \
    if GRANADAD_CONTENT_DIR=/definitely-not-a-directory \
       /build-cache/hostcheck/bin/granadad-content-tests \
       --fingerprint /tmp/should-not-exist.txt >/dev/null 2>&1; then \
        echo "FATAL: a bogus GRANADAD_CONTENT_DIR still produced a report, so the"; \
        echo "       variable is being ignored and fixtures.hpp::contentDir() has"; \
        echo "       gone back to the compiled-in constant. The Windows .exe would"; \
        echo "       then silently test nothing on the machine it ships to."; \
        exit 1; \
    fi; \
    GRANADAD_CONTENT_DIR=/relocated /build-cache/hostcheck/bin/granadad-content-tests | tail -3; \
    echo "ok: the env var selects the worlds, and the report ignores the path"; \
    \
    # ----------------------------------------------------------------------
    # The SHIPPED binary must find its own worlds with NOTHING set. (S2)
    # ----------------------------------------------------------------------
    # S1 published a dist\granadad.exe that died on the owner's machine with
    #   granadad: cannot open TROJSAV: /src/content\maps\baked\docks_surface.trojsav
    # — the build container's own path, baked in at configure time. Nothing sets
    # GRANADAD_CONTENT_DIR for the GAME; scripts/verify-windows.ps1 sets it for
    # the test binaries only, so this gate could not see it. README.md documents
    # that exact command as the way to play.
    #
    # The check has to be non-vacuous, which takes three things at once:
    #   * the environment variable UNSET,
    #   * the configure-time default MOVED OUT OF THE WAY, and
    #   * a content tree one level above the executable, which is dist/ exactly.
    # With any of those missing the run would pass on the old behaviour.
    echo "=== the shipped binary finds its own worlds with nothing set (S2) ==="; \
    mkdir -p /fakeinstall/dist; \
    cp -a /src/content /fakeinstall/content; \
    cp /build-cache/hostcheck/bin/granadad-twin-gate /fakeinstall/dist/; \
    mv /src/content /src/content.hidden; \
    if env -u GRANADAD_CONTENT_DIR /fakeinstall/dist/granadad-twin-gate \
         --fingerprint /tmp/self-found.txt; then \
        echo "ok: it walked up from dist/ and found content/maps/baked"; \
    else \
        mv /src/content.hidden /src/content; \
        echo "FATAL: with GRANADAD_CONTENT_DIR unset and the configure-time"; \
        echo "       default gone, the binary could not find the content tree"; \
        echo "       one directory above itself. That is the state dist/ ships"; \
        echo "       in, so the game does not start on the owner's machine."; \
        echo "       See granadad::content::searchForContentDir."; \
        exit 1; \
    fi; \
    # ...and the negative, or the run above proves only that /src/content was
    # still readable somehow. Nowhere above /tmp/orphan holds a content tree, so
    # this MUST fail.
    mkdir -p /tmp/orphan; \
    cp /build-cache/hostcheck/bin/granadad-twin-gate /tmp/orphan/; \
    if env -u GRANADAD_CONTENT_DIR /tmp/orphan/granadad-twin-gate \
         --fingerprint /tmp/should-not-exist.txt >/dev/null 2>&1; then \
        mv /src/content.hidden /src/content; \
        echo "FATAL: a binary with no content tree anywhere near it still"; \
        echo "       produced a report, so the search is not what found the"; \
        echo "       worlds a moment ago and the check above proved nothing."; \
        exit 1; \
    fi; \
    mv /src/content.hidden /src/content; \
    rm -rf /fakeinstall /tmp/orphan; \
    echo "ok: found beside the exe, and NOT found when there is nothing to find"; \
    \
    # ----------------------------------------------------------------------
    # The same treatment for the SIMULATION half, added in M1.
    # ----------------------------------------------------------------------
    # The content fingerprint proves both toolchains DECODE the shipped worlds
    # to the same bytes. It says nothing about whether those bytes then HASH the
    # same, or whether a run over them lands in the same place -- and those are
    # the two claims M1 adds. So the world-hash report gets the identical
    # treatment: relocation-invariant, bogus-path-fatal, published for Windows
    # to match byte for byte.
    echo "=== the world hash and a run over it, on this toolchain ==="; \
    /build-cache/hostcheck/bin/granadad-twin-gate \
        --fingerprint /out/world-hash-linux-gcc.txt; \
    GRANADAD_CONTENT_DIR=/relocated \
        /build-cache/hostcheck/bin/granadad-twin-gate \
        --fingerprint /tmp/relocated-world-hash.txt; \
    cmp /out/world-hash-linux-gcc.txt /tmp/relocated-world-hash.txt \
        || { echo "FATAL: the same worlds read from a different directory hashed"; \
             echo "       differently. The report describes world state and must"; \
             echo "       say nothing about where it was read from."; exit 1; }; \
    if GRANADAD_CONTENT_DIR=/definitely-not-a-directory \
       /build-cache/hostcheck/bin/granadad-twin-gate \
       --fingerprint /tmp/should-not-exist.txt >/dev/null 2>&1; then \
        echo "FATAL: a bogus GRANADAD_CONTENT_DIR still produced a world-hash"; \
        echo "       report, so the variable is being ignored and the Windows"; \
        echo "       .exe would silently compare nothing."; \
        exit 1; \
    fi; \
    \
    echo "=== linux/gcc side of the comparison ==="; \
    for report in content-fingerprint-linux-gcc.txt world-hash-linux-gcc.txt; do \
        echo "--- $report"; \
        wc -c < "/out/$report" | xargs echo "    report bytes:"; \
        sha256sum "/out/$report"; \
    done; \
    head -8 /out/content-fingerprint-linux-gcc.txt; \
    echo "        [...]"; \
    grep -E 'hash\.(wrld|combined) |run\.(wrld|combined) ' /out/world-hash-linux-gcc.txt; \
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
    # The Windows half of the comparison needs two things in dist/, and neither
    # is something a compile error would catch: an install() rule can be dropped
    # in a refactor and the build stays green while the cross-toolchain claim
    # quietly reverts to one toolchain. That is exactly how this gap was born.
    echo "=== the windows half must actually ship ==="; \
    for required in granadad-content-tests.exe content-fingerprint-linux-gcc.txt \
                    granadad-tests.exe granadad-twin-gate.exe world-hash-linux-gcc.txt; do \
        test -f "/out/$required" \
            || { echo "FATAL: /out/$required is missing. Without it the"; \
                 echo "       cross-toolchain comparison cannot be run on Windows"; \
                 echo "       and the determinism claim rests on Linux/GCC alone."; \
                 echo "       See install(TARGETS granadad-content-tests) in"; \
                 echo "       native/CMakeLists.txt and scripts/verify-windows.ps1."; \
                 exit 1; }; \
    done; \
    echo "ok: dist/ carries the Windows content suite and the Linux report"; \
    # VERIFICATION GAP (#75): this container CANNOT finish the job. Running the
    # PE binary here would need wine, which would be a third toolchain emulating
    # the second — proof about wine, not about Windows. The comparison is
    # completed by scripts/verify-windows.ps1 on the host, which publish.sh
    # prints as the next command. Nothing forces the owner to type it.
    \
    # ----------------------------------------------------------------------
    # THE GATE STAMPS ITSELF. S4, closing the S3 review's sixth finding.
    # ----------------------------------------------------------------------
    # This whole layer is one cached RUN. Re-run the documented build command on
    # an unchanged tree and docker reuses it: the command exits 0 having
    # executed nothing, and the S3 review got exactly that green and correctly
    # refused to trust it.
    #
    # The cache key IS the source tree, so a reused layer does mean these tests
    # passed against THIS tree. What it does not mean is that anything ran just
    # now. So the gate records what it verified and when, publish.sh prints it,
    # and a reader can tell the two apart instead of guessing.
    #
    # The digest is over native/ alone, sorted with LC_ALL=C so the ordering is
    # the bytes' and not the locale's. It is the answer to "which tree did this
    # green come from", and it is printed rather than compared: comparing it to
    # a host-side recomputation would need the two to agree about line endings
    # and path separators, which is a claim this build has not proved and will
    # not assert.
    echo "=== the gate stamps itself ==="; \
    # NORMALISED, since S8, so A HOST CAN RECOMPUTE IT.
    #
    # The S7 review's sixth finding was that this stamp could not be used as
    # evidence the gate ran on the committed tree: it hashed raw bytes at
    # container paths, and this file said out loud that it would not try to
    # make the two sides agree about line endings. They agree now.
    #
    # A scratch copy of native/ has its CRLF line endings folded to LF, and
    # the digest is sha256 over `sha256sum` output for every file, sorted
    # LC_ALL=C by its ./-relative path. scripts/verify-windows.ps1 computes
    # exactly that from the repo and FAILS on a mismatch -- so a green that
    # belongs to other bytes (a stale COPY layer, an uncommitted edit, a
    # second worktree) says which instead of being quietly trusted.
    #
    # It is a COPY and not the tree itself because the build has already
    # happened against those exact bytes and nothing after this line may
    # touch them.
    rm -rf /tmp/stamp; \
    cp -a /src/native /tmp/stamp; \
    find /tmp/stamp -type f -exec sed -i 's/\r$//' {} +; \
    tree_digest="$(cd /tmp/stamp && find . -type f -print0 \
        | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"; \
    stamp_files="$(cd /tmp/stamp && find . -type f | wc -l)"; \
    rm -rf /tmp/stamp; \
    { \
      echo "gate executed:  $(date -u '+%Y-%m-%dT%H:%M:%SZ') UTC"; \
      echo "revision:       ${GRANADAD_REVISION}"; \
      echo "native/ digest: ${tree_digest}"; \
      echo "ctest cases:    ${test_count} (floor ${GRANADAD_MIN_TESTS})"; \
      echo "native/ files:  ${stamp_files}"; \
    } > /out/GATE-STAMP.txt; \
    cat /out/GATE-STAMP.txt; \
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
