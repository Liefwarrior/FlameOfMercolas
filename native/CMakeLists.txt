cmake_minimum_required(VERSION 3.24)

# Granadad: The Darkstreets — native C++ rewrite.
#
# Console toolchains lag, so this targets C++20 and nothing newer. See
# native/README.md for both build paths (docker for reproducible, CMake preset
# for fast local iteration).
#
# VERIFICATION GAP (M0): the docker path is verified end to end — configure,
# cross-compile, host ctest, and the resulting .exe run natively on Windows.
# The CMakePresets paths (local, local-debug, sim-only, mingw-cross) are NOT
# verified: the machine this was authored on has no cmake, ninja or C++
# compiler installed, so nothing outside the container was ever executed. The
# preset definitions are believed correct but untested.

project(granadad
    # S10 ships the playable demo. The version is the ONE place the build says
    # how far along it is: the capture stamp, the title card and --help all read
    # it out of build_info() now rather than each carrying their own literal.
    # S9 shipped "GRANADAD S6" burnt into the corner of every frame, three
    # sprints stale, and the review found it in a screenshot.
    VERSION 0.10.0
    DESCRIPTION "Granadad: The Darkstreets — deterministic simulation of the Docks district of Trojia"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(GRANADAD_BUILD_TESTS  "Build the doctest suite" ON)
option(GRANADAD_BUILD_CLIENT "Build the SDL3 observer client (OFF = sim only, no SDL fetch)" ON)

# Stamped by the docker build so a binary can name the commit it came from.
set(GRANADAD_REVISION "local" CACHE STRING "Git revision baked into the binary")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Determinism)
include(StaticRuntime)
include(Dependencies)

# enable_testing() has to happen in the top-level directory BEFORE any
# add_subdirectory() that registers tests, or the subdirectory's add_test()
# calls are silently dropped from the ctest run. That silence is exactly the
# failure this build is trying not to have.
if(GRANADAD_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# granadad-content — the TROJSAV / world-format reader, and the tests that read
# the owner's real baked worlds.
#
# Added BEFORE granadad-sim, because as of M1 sim links it. See the seam note on
# granadad-sim below.
#
# This is deliberately part of the default build rather than a standalone
# side-project. Left out, `ctest` reported "100% tests passed, 1 tests out of 1"
# — that one test being fixed.hpp — while the 57 cases and ~900k assertions
# that prove the format reader against the actual shipped files ran nowhere.
# A green badge that narrow is worse than no badge.
#
# Configure fails outright if the baked worlds are missing (see
# content/CMakeLists.txt), which is what keeps someone from quietly dropping
# them out of the docker context again.
#
# Closed in #75: the content suite now runs on BOTH toolchains. fixtures.hpp
# resolves the content directory from $GRANADAD_CONTENT_DIR at run time, so the
# cross-built granadad-content-tests.exe is installed into dist/ and executed
# natively on Windows against the repo's own content/. Pass/fail is the weak
# half of that; the strong half is `--fingerprint`, which writes a report of the
# decoded world state (section CRCs, per-lane CRC32C, per-form / per-material /
# per-flags / per-fluid histograms over every tile) that is compared byte for
# byte between the two platforms. See docker/build.Dockerfile and
# scripts/verify-windows.ps1.
# ---------------------------------------------------------------------------

# Set here as well as in content/CMakeLists.txt, with the same name and the same
# default, so whichever directory reaches the cache first wins and the other is
# a no-op — and content/ standalone still computes its own with no reference
# upward. The DEFINE that carries it to the compiler is granadad-content's, and
# only granadad-content's.
get_filename_component(GRANADAD_DEFAULT_CONTENT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../content" ABSOLUTE)
set(GRANADAD_CONTENT_DIR "${GRANADAD_DEFAULT_CONTENT_DIR}" CACHE PATH
    "Repo content/ directory holding the baked .trojsav worlds")

add_subdirectory(content)

# ---------------------------------------------------------------------------
# sim-core — the deterministic simulation. No SDL, no rendering, no floats.
# ---------------------------------------------------------------------------
add_library(granadad-sim STATIC
    src/sim/fixed.cpp
    src/sim/build_info.cpp
    src/sim/system_id.cpp
    src/sim/world_hash.cpp
    src/sim/engine.cpp
    src/sim/angle.cpp
    src/sim/tile_query.cpp
    src/sim/player.cpp
    src/sim/region_path.cpp
    src/sim/brawl.cpp
    src/sim/actor.cpp
    src/sim/spellbook.cpp
    # S3 -- the social layer. barks/notables read the owner's raws and write
    # nothing; social/barter/dialogue are the state and the rules over them.
    src/sim/barks.cpp
    src/sim/notables.cpp
    src/sim/social.cpp
    src/sim/barter.cpp
    src/sim/dialogue.cpp
    # Attribute/skill allocation mechanics -- attributes.cpp gives
    # content/raws/skills/skills.json's governingAttribute/aptitudeTier
    # columns (parsed by social.cpp's SkillTrack::load) an actual value a
    # character can hold; chargen.cpp is the Daggerfall-inspired
    # Primary/Major/Minor skill sheet and attribute bonus-point pool that
    # writes starting levels into a real SkillTrack. Listed after social.cpp
    # because both depend on SkillTrack's own definition.
    src/sim/attributes.cpp
    src/sim/chargen.cpp
    # S4 -- the guilds. faction reads the owner's registry and the S4 ladders
    # hung off it; questline reads any stages-shaped quest file; spellforge is
    # canon's own cost model plus the composition verb the modular effect
    # vocabulary was authored for.
    src/sim/faction.cpp
    src/sim/questline.cpp
    src/sim/spellforge.cpp
    # S5 -- the trade the roofs run on.
    src/sim/crime.cpp
    # S6 -- the goods, the law that takes them off you, and the radiant work
    # that is the reason to be carrying them in the first place.
    src/sim/contraband.cpp
    src/sim/watch.cpp
    src/sim/contract.cpp
    # S9 -- being unseen, and the lock. stealth.cpp is the first integer light
    # model on this side of the line; lockpick.cpp is a lock whose pins are a
    # pure function of the seed and never re-rolled. Both listed BEFORE
    # tavern.cpp because the taproom is what owns a stance and four strongboxes.
    src/sim/stealth.cpp
    src/sim/lockpick.cpp
    src/sim/tavern.cpp
    # S7 -- the compounds. The roll (who holds what), the courtyard farms, and
    # the leasehold that moves labour between them. The first system in this
    # build whose unit of time is a DAY rather than a second.
    src/sim/compound.cpp
    # S8 -- the nemesis. Putting the player on the floor is a promotion event:
    # a rung off the owner's own ladders, a trade house founded out of
    # content/raws/factions/chapters.json with real members and a permanent
    # toll on the ward's prices, and a vacant charge taken on the compound
    # roll. Listed AFTER compound.cpp because it reaches into the roll.
    src/sim/nemesis.cpp
    # S10 -- the reason to be in the district. casebook.cpp is the bloodletter
    # trail: authored leads at the map's own script_anchor markers, a look verb,
    # and two dead ends that are the point rather than a defect. legend.cpp is
    # the long game -- five tracks the ward remembers you by, each rung of which
    # buys something a player can feel.
    src/sim/casebook.cpp
    src/sim/legend.cpp
    # #82 -- THE BLOODLETTER TRAIL, AS REAL LETTERS. letters.cpp is a second,
    # standalone raws loader (content/raws/quests/bloodletter_letters.json)
    # cross-referenced to casebook.json's own lead ids by string, not merged
    # into casebook.hpp's schema and not touching casebook.json itself. See
    # letters.hpp's own header for why a second file and what is and is not
    # claimed.
    src/sim/letters.cpp
    # #78 -- THE WARD, WITH PEOPLE IN IT. path_finder.cpp is the bounded
    # deterministic A* a district-sized walk needs (region_path.cpp's exhaustive
    # frontier is right for a room and useless across the Docks);
    # ward_actors.cpp is the needs/jobs/schedule/occupancy machine;
    # ward_roster.cpp is who is actually here, at the map's own authored
    # anchors. Listed AFTER tavern.cpp and compound.cpp because the population
    # extends both and spawns nobody inside the Gilded Gull.
    src/sim/path_finder.cpp
    src/sim/ward_actors.cpp
    src/sim/ward_roster.cpp
    # #79 -- THE WARD CAN BE SPOKEN TO. Not a second dialogue system: it builds
    # the flat Speaker sim/dialogue.hpp already runs a conversation from, out of
    # the owner's own name pools and the twenty-nine of the Forty the roster was
    # already standing at their authored sites. Listed AFTER ward_roster.cpp
    # because it implements that file's last bake step.
    src/sim/ward_voice.cpp
    # #80 -- WHAT A PLAYER LOOKS LIKE, IN THE WARD'S OWN VOCABULARY. Eleven of
    # WardType's sixteen values, named as ids a content file or a screen can
    # hold -- see appearance.hpp's own header on the five deliberately left
    # off. companions.cpp reads content/raws/companions/*.json (the DEVIN and
    # GABRI origin templates), checks every startingSkills id against the
    # real SkillTrack, and can write them through SkillTrack::setLevel the
    # same way any other caller does. Listed after ward_voice.cpp because
    # companions.cpp depends on SkillTrack's own definition, same as
    # attributes.cpp/chargen.cpp above.
    src/sim/appearance.cpp
    src/sim/companions.cpp
)
target_include_directories(granadad-sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# THE SEAM, resolved deliberately in M1: sim depends on content, and content
# never depends on sim.
#
# Before this line no built binary could load a world and tick it. The world
# hasher has to walk a World's decoded lanes, and World, Coords and the lane
# registry all live in granadad::content -- which was written standalone on
# purpose so the format reader could be developed and tested without pulling in
# SDL or a simulation.
#
# Three ways out were available: sim links content, content's world types move
# up into a third module, or a new module sits between them. This is the first,
# and the direction matters more than the choice: content still links nothing of
# ours, so `cmake -S native/content` still configures, builds and tests on its
# own exactly as its header comment promises. The arrow only ever points one
# way, and the day it points both is the day neither module can be reasoned
# about alone.
#
# PUBLIC, not PRIVATE: granadad/sim/world_hash.hpp includes
# granadad/content/world.hpp in its interface, so anything linking sim needs
# content's headers too.
target_link_libraries(granadad-sim
    PUBLIC granadad-content nlohmann_json::nlohmann_json miniz::miniz)
target_compile_definitions(granadad-sim PRIVATE
    GRANADAD_VERSION="${PROJECT_VERSION}"
    GRANADAD_REVISION="${GRANADAD_REVISION}")
granadad_apply_determinism(granadad-sim)

# sim-core must never see SDL. Keeping the link surface honest here is cheaper
# than discovering a rendering type leaked into simulation state later.

# ---------------------------------------------------------------------------
# granadad-gate — the twin-run determinism gate.
#
# A library plus a thin executable, rather than one main.cpp, so the comparators
# and the workload are reachable from the doctest suite. A gate whose own logic
# is untested is a gate you find out about on the day it should have gone red.
# ---------------------------------------------------------------------------
add_library(granadad-gate STATIC
    src/gate/workload.cpp
    src/gate/twin_run.cpp
    src/gate/world_hash_report.cpp
)
target_include_directories(granadad-gate PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# GRANADAD_CONTENT_DIR_DEFAULT arrives through granadad-content, which owns the
# resolver as of S2. Same runtime-first / compile-time-default contract: the
# path baked in is the build container's and does not exist on the machine the
# .exe ships to, so $GRANADAD_CONTENT_DIR and then the executable's own tree
# outrank it. See granadad/content/content_dir.hpp for both incidents that made
# that necessary.
target_link_libraries(granadad-gate PUBLIC granadad-sim)
granadad_apply_determinism(granadad-gate)

add_executable(granadad-twin-gate src/gate/main.cpp)
target_link_libraries(granadad-twin-gate PRIVATE granadad-gate)
granadad_apply_determinism(granadad-twin-gate)
granadad_static_runtime(granadad-twin-gate)

# ---------------------------------------------------------------------------
# granadad-render — the software first-person renderer. Floats are legal here.
#
# Built ALWAYS, including in the sim-only configuration the docker host check
# uses, and deliberately linked against no SDL. That is the whole design:
# rendering a frame needs a world, an art pack and a camera, and none of those
# needs a window. Because this library stands alone, `granadad --screenshot`
# opens no window, and the test suite can render the Docks and assert on the
# pixels on every build -- which is how each of the nine sprints after this one
# gets to prove visually that it still works.
# ---------------------------------------------------------------------------
add_library(granadad-render STATIC
    src/render/framebuffer.cpp
    src/render/atlas.cpp
    src/render/lamps.cpp
    src/render/lighting.cpp
    src/render/world_renderer.cpp
    src/render/controls.cpp
    src/render/hud.cpp
    src/render/dialogue_view.cpp
    src/render/capture.cpp
    src/render/session.cpp
    src/render/step_pump.cpp
    src/render/stb_impl.cpp
    # Task #83 -- the steps-based ease every panel's open/close and the HUD's
    # own alert row now animate through, instead of popping. See the header.
    src/render/anim.cpp
    # #78 -- the people, as pixels the owner drew. content/art/sprites is
    # twenty-five 16x16 actor figures in the MERCOLAS-24 palette with a tag
    # vocabulary already authored against the Java actor types; this reads the
    # ART and none of the Java code that indexed it.
    src/render/actor_sheet.cpp
    # #80 -- the origin-select and customize flow: a new game's first two
    # screens, BG3/DOS2-shaped, drawn with no Session and no world underneath
    # them -- see the file's own header on why.
    src/render/creation.cpp
)
target_include_directories(granadad-render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# GRANADAD_CONTENT_DIR_DEFAULT arrives through granadad-content.
target_link_libraries(granadad-render PUBLIC granadad-sim stb::stb)
granadad_apply_determinism(granadad-render)

# The lamp baker. Turns a fixture's authored light_source markers into the
# sidecar the game reads, so the 27 lamps of the Docks are derived from canon
# rather than transcribed. Run by hand; its output is committed.
add_executable(granadad-bake-lamps src/tools/bake_lamps.cpp)
target_link_libraries(granadad-bake-lamps PRIVATE granadad-render)
granadad_apply_determinism(granadad-bake-lamps)
granadad_static_runtime(granadad-bake-lamps)

# ---------------------------------------------------------------------------
# client-observer — the SDL3 window and nothing else.
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_CLIENT)
    add_executable(granadad src/client/main.cpp)
    target_link_libraries(granadad PRIVATE granadad-render ${GRANADAD_SDL_TARGET})
    granadad_apply_determinism(granadad)
    # Console subsystem stays on — the sim prints, and we want stdout to work
    # when the game is launched from a terminal on Windows.
    granadad_static_runtime(granadad)
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_TESTS)
    # THE SUITE'S SOURCES, IN ONE LIST, AND THE LIST IS LOAD-BEARING.
    #
    # ctest registers ONE ENTRY PER FILE in this list — see the registration
    # block below for why, and cmake/CheckTestPartition.cmake for the check that
    # stops a file dropping out of it. Adding a .cpp to the suite means adding it
    # HERE and nowhere else: the executable and the ctest entries are both
    # generated from this variable, so the two cannot drift.
    set(GRANADAD_TEST_SOURCES
        tests/test_fixed.cpp
        tests/test_wrap.cpp
        tests/test_rng.cpp
        tests/test_world_hash.cpp
        tests/test_engine.cpp
        tests/test_twin_gate.cpp
        tests/test_angle.cpp
        tests/test_tile_query.cpp
        tests/test_player.cpp
        tests/test_tavern.cpp
        tests/test_lamps.cpp
        tests/test_atlas.cpp
        tests/test_render.cpp
        tests/test_tavern_render.cpp
        tests/test_social.cpp
        # Attribute/skill allocation mechanics: the Daggerfall-inspired
        # Primary/Major/Minor skill sheet and the attribute bonus-point pool,
        # proved against the real content/raws/skills/skills.json SkillTrack
        # rather than a rival list.
        tests/test_chargen.cpp
        tests/test_dialogue.cpp
        tests/test_faction.cpp
        tests/test_spellforge.cpp
        tests/test_questline.cpp
        tests/test_step_pump.cpp
        tests/test_controls.cpp
        # Task #83 -- the steps-based ease every panel's open/close and the
        # HUD's own alert row animate through.
        tests/test_anim.cpp
        # S5 -- the roofs, and the trade that runs on them.
        tests/test_roofrun.cpp
        tests/test_crime.cpp
        # S6 -- the goods, the radiant work over them, and the law that takes
        # both away.
        tests/test_contract.cpp
        # S7 -- the compounds: the roll, the courtyard farms, the leasehold,
        # and the soak that proves the ward feeds itself.
        tests/test_compound.cpp
        # S8 -- the nemesis: the man who put the player down, the rung he
        # climbed for it, the house he founded, the charge he took on the roll,
        # and the rematch that takes none of it back.
        tests/test_nemesis.cpp
        # S9 -- being unseen and getting in: the integer light field, the notice
        # rule every clause of which can go red on its own, and a lock whose
        # pins are a pure function of the seed.
        tests/test_stealth.cpp
        tests/test_lockpick.cpp
        # S10 -- the bloodletter trail and the long game. The leads, the two
        # dead ends, the coordinates checked against the baked world rather
        # than against a comment, and the five tracks the ward remembers you by.
        tests/test_casebook.cpp
        # #82 -- the bloodletter trail's own leads, read as the letters
        # somebody actually wrote: that the file loads, that every `lead`
        # it names is a real casebook.json lead, and that the Wielder's own
        # closing dispatch never claims a discovery only the unbuilt
        # undercellar delve would have made.
        tests/test_letters.cpp
        tests/test_firstrun.cpp
        # The pause menu ESC opens when nothing else has already claimed the
        # keyboard -- RESUME, SETTINGS and a QUIT that asks twice, replacing
        # the unconfirmed `running = false` that key carried since #77.
        tests/test_pause.cpp
        # The character sheet: the five Legend tracks, the four skills this
        # build actually levels, and what the ward and the purse currently
        # say -- reached from the same read-only reference-list surface the
        # casebook and the keys page already prove, and drawn from counters
        # that were being computed and thrown away every frame before it.
        tests/test_character.cpp
        # #82 -- THE DISTRICT MAP. Daggerfall's own travel map, scaled to one
        # district: known ground, open leads (each with a bearing and a range
        # from where the player is standing) and who among the named will
        # actually talk -- all three read off the same Casebook state the
        # casebook itself reads, growing by the identical act (hearing a
        # lead), on the same read-only reference-list surface the keys page
        # and the character sheet already prove.
        tests/test_map.cpp
        # And every scripted line the demo advertises, accepted -- see the file
        # header for the sprint --skyrun spent landing 2 of 9 beats with nothing
        # in the suite to notice.
        tests/test_scripted_lines.cpp
        # And every word the player can read: that the 4x6 font can draw all of
        # it, and that none of it is a diagnostic identifier that reached the
        # screen because a sentence was wanted and an enum name was to hand.
        tests/test_copy.cpp
        # #78 -- THE WARD, AND WHETHER IT IS ACTUALLY ALIVE. The roll, the
        # per-type counts, the Java build's own behavioural bars (serf
        # starvation under 5%, no guard pile-ups, exact per-kind conservation),
        # the off-shift branch, and a small set of expectations each naming a
        # place, an hour and a reason.
        tests/test_ward_actors.cpp
        # #79 -- AND WHETHER THE WARD CAN BE SPOKEN TO. Who the roster is
        # standing at the owner's authored sites, that the name over a
        # conversation came out of the owner's pools, that three trades do not
        # say the same sentence, that a starving body says so, and that a cat is
        # not offered the vanished clerk.
        tests/test_ward_voice.cpp
        # #80 -- AND WHETHER ANYBODY LIVES ON THE ROOFS. The climb verb, the
        # roof-slum tenants canon puts on a deck, the Skyrunners in their own
        # territory, and above all the round trip: a bed you can climb TO and
        # not FROM is a tenant standing there until it starves, and nothing
        # about a frame or a census would say so.
        tests/test_ward_roofs.cpp
        # #80 -- AND WHETHER ANYTHING EATS THE RATS. The prey range that makes
        # the hunt cheap, the scrap clamp without which no predator was ever
        # hungry enough to hunt at all, the chase budget the Java build spent a
        # sprint arriving at, and a soak that watches the mouse count actually
        # fall and come back.
        tests/test_ward_hunt.cpp
        # #80 -- THE ORIGIN-SELECT AND CUSTOMIZE FLOW. Three cards, a name
        # field, and a real sim::Chargen sheet hosted rather than reinvented
        # -- see creation.hpp's own header on why this reuses
        # render::drawDialogue instead of a new widget.
        tests/test_creation.cpp
        # #80 -- THE FIXED SHEETS. content/raws/companions/devin.json and
        # gabri.json proved against the real SkillTrack and, independently,
        # against a from-the-doc reimplementation of PROGRESSION-SPEC.md
        # section 5's attribute formula -- see companions.hpp's own header.
        tests/test_companions.cpp
    )

    add_executable(granadad-tests tests/test_main.cpp ${GRANADAD_TEST_SOURCES})
    target_link_libraries(granadad-tests PRIVATE granadad-gate granadad-render doctest::doctest)

    # GRANADAD_CONTENT_DIR_DEFAULT and _CRT_SECURE_NO_WARNINGS arrive through
    # granadad-gate's PUBLIC compile definitions. tests/test_world_hash.cpp
    # loads the real baked worlds and compares the C++ hash against what a JVM
    # produced from the same files, so it needs the content directory —
    # resolved through granadad/content/content_dir.hpp, which is the ONE
    # implementation of the runtime-env-first, compile-time-default-second
    # order the docker build actually proves.

    granadad_apply_determinism(granadad-tests)
    granadad_static_runtime(granadad-tests)

    # ONE ctest ENTRY PER TEST FILE. Not per case, and not one for the binary.
    #
    # WHAT THIS REPLACED AND WHY. Until #81 this was doctest_discover_tests(),
    # which registers every TEST_CASE as its own ctest entry — and ctest runs an
    # entry by LAUNCHING THE BINARY AGAIN with --test-case=. Four hundred and
    # sixty-three cases meant four hundred and sixty-three processes, each of
    # which re-did from scratch every expensive thing the suite has: reading the
    # baked world, parsing the raws, baking a six-hundred-body ward.
    #
    # That is not a startup cost you can shrug at. It is the reason a
    # file-static fixture DID NOT WORK: tests/test_ward_hunt.cpp measured it and
    # wrote it down — five cases reading one ten-hour soak paid for that soak
    # five times, 307 + 270 + 262 + 260 seconds, and the answer at the time was
    # to fold five named claims into one case to stop paying for it. That is a
    # test suite being deformed by its runner.
    #
    # A file is the unit because it is the unit a fixture can be shared inside.
    # tests/support/ward_fixture.hpp loads the district ONCE per process and
    # bakes a ward ONCE per hour; the cases in a file share both, and no case in
    # one file can see anything another file did. Crash isolation is kept at file
    # granularity: a segfault in test_render.cpp takes down that entry and
    # nothing else.
    #
    # THE FLOOR MOVED WITH IT, and got stronger rather than weaker. It used to
    # count ctest ENTRIES; it now counts DOCTEST CASES, read out of the binary
    # itself with --list-test-cases (see docker/build.Dockerfile). An entry count
    # can be inflated by registering entries; a case count cannot.
    #
    # AND THE PARTITION IS CHECKED. A per-file entry whose filter matches
    # nothing is a file that silently stops running — doctest exits 0 having run
    # no cases. granadad-test-partition-is-complete below adds up the per-file
    # counts and requires them to equal the binary's own total, so a case can
    # neither vanish nor be counted twice.
    #
    # The CMAKE_CROSSCOMPILING guard is kept for the reason recorded in
    # content/CMakeLists.txt: nothing here can run a Windows .exe on the Linux
    # build host. Cross builds register the single-binary entry, which is what
    # scripts/verify-windows.ps1 runs natively anyway.
    if(NOT CMAKE_CROSSCOMPILING)
        foreach(granadad_test_src IN LISTS GRANADAD_TEST_SOURCES)
            get_filename_component(granadad_test_file "${granadad_test_src}" NAME)
            # --order-by=file, so the order cases run in is the order they are
            # written in and is the same on every machine. A shared fixture and
            # an unstated order together are how you get a flake nobody can
            # reproduce, and this build cannot afford one.
            add_test(NAME "suite: ${granadad_test_file}"
                     COMMAND granadad-tests
                             "--source-file=*${granadad_test_file}"
                             --order-by=file)
        endforeach()
        # Pipe-separated, not semicolon-separated: a semicolon inside a -D value
        # is a CMake list separator and would arrive as N arguments instead of
        # one. The script splits on the pipe.
        string(REPLACE ";" "|" GRANADAD_TEST_SOURCES_PIPED "${GRANADAD_TEST_SOURCES}")
        add_test(NAME granadad-test-partition-is-complete
                 COMMAND ${CMAKE_COMMAND}
                         "-DGRANADAD_TEST_EXE=$<TARGET_FILE:granadad-tests>"
                         "-DGRANADAD_TEST_SOURCES=${GRANADAD_TEST_SOURCES_PIPED}"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckTestPartition.cmake")
    else()
        add_test(NAME granadad-tests COMMAND granadad-tests)
    endif()

    # The twin-run gate as a ctest entry. It is the only check in this build that
    # can catch nondeterminism rather than incorrectness, and a gate that is not
    # wired into `ctest` is a gate somebody has to remember to run.
    #
    # Cross builds skip it for the same reason as above: the .exe does not run
    # here. scripts/verify-windows.ps1 runs it natively on the other side.
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME granadad-twin-run-gate COMMAND granadad-twin-gate)
        add_test(NAME granadad-world-hash-fingerprint
            COMMAND granadad-twin-gate --fingerprint
                    "${CMAKE_CURRENT_BINARY_DIR}/world-hash.txt")
        # S2: the same gate, over the district, with the Gilded Gull registered
        # and its movement clock driven. This is the only check in the build
        # that can catch NONDETERMINISM in the tavern rather than incorrectness
        # — fourteen actors, breadth-first routes, drink orders and a door
        # policy, run twice from one seed and required to agree bit for bit.
        #
        # A separate entry rather than a change to the default workload, so the
        # published cross-toolchain report stays frozen: it is compared byte for
        # byte between Linux and Windows, and a sprint that changed its contents
        # would be a sprint that regenerated the thing the comparison rests on.
        add_test(NAME granadad-twin-run-gate-tavern
            COMMAND granadad-twin-gate --tavern --ticks 900)
        # S7: the same gate over the ward's compounds -- 122 households, five
        # courtyards, the roll, the bonds and the priest's hearings, run twice
        # from one seed and required to agree bit for bit.
        #
        # 90,000 TICKS AND NOT 900, and the number is load-bearing. A day is
        # the ward's unit: at 86,400 ticks to the day a shorter run would tick
        # a system that had not yet done a day's work and would compare an
        # untouched roll twice, which is a green that proves nothing. This
        # crosses one day boundary and the twin run therefore compares a ward
        # that has grown, eaten, bought and been paid.
        add_test(NAME granadad-twin-run-gate-ward
            COMMAND granadad-twin-gate --ward --ticks 90000)
        # S7: THE BALANCE BAR, ENFORCED BY THE BUILD. Two years of the
        # compounds, and the run exits non-zero if the ward starves past
        # kStarvationBarPermille (the Java build's own 5%) OR if it drowns in
        # surplus with nothing scarce in it. A balance claim nobody can fail is
        # a balance claim nobody has made.
        add_test(NAME granadad-ward-soak COMMAND granadad-twin-gate --ward-soak 730)
        # #78: THE SAME GATE OVER THE DISTRICT'S OWN PEOPLE, and this is the
        # real determinism test of the port.
        #
        # The tavern's fourteen and the compound roll are both small and both
        # mostly bookkeeping. The population is six hundred and seventy-eight
        # bodies running a bounded A* with per-actor jitter, an open-addressing
        # occupancy index with backward-shift deletion, a shove that draws
        # against a named stream, and a policy stack whose ties are broken by
        # position. Every one of those is a place where an iteration order or a
        # hash bucket could leak into state, and none of them can be caught by
        # anything except running the whole thing twice and comparing.
        #
        # STARTED AT FOUR IN THE AFTERNOON AND RUN FOR SIX HOURS OF WARD TIME,
        # which is the number that matters. A run inside one shift compares a
        # district that stood at the same posts throughout; this one crosses the
        # end of the day trades at six, the night roster coming on at six, the
        # ward going home, eating, and the thieves' window opening at ten.
        # 7,200 TICKS, WHICH IS TWO HOURS OF WARD TIME, and the number is a
        # trade the gate has to make out loud. The Docker host check compiles
        # -DCMAKE_BUILD_TYPE=Debug, and this runs six hundred and seventy-eight
        # bodies TWICE; a whole day here would be minutes of every build, which
        # is how a gate stops being a gate somebody runs. Two hours starting at
        # four in the afternoon still crosses the thing that matters: the day
        # trades end at six, the night roster comes on at six, and the ward
        # walks home in between.
        add_test(NAME granadad-twin-run-gate-population
            COMMAND granadad-twin-gate --population --population-hour 16 --ticks 7200)
    endif()
endif()

# ---------------------------------------------------------------------------
# install — this is what the docker build harvests into dist/
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_CLIENT)
    install(TARGETS granadad RUNTIME DESTINATION .)
    # If SDL ended up shared (it should not — we ask for static) carry the DLL.
    if(TARGET SDL3::SDL3-shared)
        install(FILES $<TARGET_FILE:SDL3::SDL3-shared> DESTINATION .)
    endif()
endif()
if(GRANADAD_BUILD_TESTS)
    install(TARGETS granadad-tests RUNTIME DESTINATION .)
    # Shipped so the format reader can be tested on the toolchain that actually
    # ships it. Before #75 this binary was compiled, linked and thrown away,
    # which meant the cross-platform determinism claim rested on one platform.
    install(TARGETS granadad-content-tests RUNTIME DESTINATION .)
endif()

# The twin-run gate ships whether or not tests are built: it is not a test, it
# is the determinism proof, and its --fingerprint report is the sim-side half of
# the cross-toolchain comparison scripts/verify-windows.ps1 completes.
install(TARGETS granadad-twin-gate RUNTIME DESTINATION .)

# The lamp baker ships for one reason: content/maps/src is not in the build
# context, so the only place the bake can actually be RE-DERIVED from the
# authored Tiled source is on the machine that has the repo. Shipping it means
# `granadad-bake-lamps.exe content/maps/src/docks_surface.tmx <out>` and a diff
# is the whole verification, rather than a claim about a file nobody can
# regenerate.
install(TARGETS granadad-bake-lamps RUNTIME DESTINATION .)
