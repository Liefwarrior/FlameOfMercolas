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
    # FATIGUE BUILD -- the one-bar-everything pool (ELDER-SCROLLS-REFERENCE
    # section 1.2 adapted to MGT/AGI/VIG/WIT and integer Q8) plus the four
    # attributes' runtime readers. After attributes.cpp: the pool is sized
    # off an AttributeBlock.
    src/sim/fatigue.cpp
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
    # #92 -- THE DAGGERFALL FLOW'S CONTENT. chargen_raws.cpp reads
    # content/raws/chargen/*.json (the nine callings, the ward's ten
    # questions, the twelve biography questions) against the SAME SkillTrack,
    # FactionRegistry and NotableRegistry every other system reads, refuses
    # loudly on anything they do not know, and owns the pure quiz tally and
    # the closed chargen effect vocabulary. Listed after companions.cpp
    # because it generalizes that file's chargenPreset idea and shares its
    # dependencies.
    src/sim/chargen_raws.cpp
    # #81 -- THE RADIANT QUEST GENERATOR CORE. Not a second ContractBoard:
    # contract.cpp's patrons and sources are the Forty, bound to a site field
    # read once out of notables.json at load. This draws its giver and target
    # from the WHOLE live ward roster ward_roster.cpp bakes and ward_voice.cpp
    # names, and resolves where each of them is standing off docks.hpp's own
    # place table AT GENERATION TIME rather than off an authored site. Listed
    # last because it depends on ward_actors.hpp's WardPopulation, the last
    # thing this library builds.
    src/sim/radiant_quest.cpp
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
    # Every Docks building's door and every named street, as a legible
    # floating nameplate when the player is close enough to read it. See the
    # file's own header for why it reuses the world pass's camera math and
    # depth buffer instead of a second projection/visibility system.
    src/render/signage_renderer.cpp
    src/render/controls.cpp
    src/render/hud.cpp
    src/render/dialogue_view.cpp
    # PANES PASS -- the terminal-panel drawing vocabulary every later UI phase
    # composes its screens out of: the +~-~- rules and alternating |/! edges,
    # the breadcrumb header, the tab row, the numbered option list with
    # inverted-fill selection, the master/detail split, aligned key/value rows
    # and the bullet hierarchy. Width-aware from the first line; NOT modular.
    # See the header, and docs/design/UI-REFERENCE-TERMINAL.md for the grammar.
    src/render/panel.cpp
    # ...and the first real surface drawn with it: the controls page, converted
    # off the conversation widget's four-page 3x4 topic grid.
    src/render/keys_page.cpp
    # Morrowind round -- the tiled Menu (Character/Map/Letters top, Journal
    # full-width bottom, all four drawn at once). See the file's own header.
    src/render/menu_view.cpp
    # Core action #13 -- THE WARD MAP: the full-screen top-down district plan
    # the M key opens, with the authored sign names decluttered onto it. See
    # the file's own header on why it is navigation, not the Chart tile.
    src/render/map_view.cpp
    src/render/capture.cpp
    # THE DEMO PASS -- the curated, self-running route through the ward: a
    # table of beats and the playhead that walks it, driving the session
    # through the same public calls a keyboard makes. See the header on why it
    # is here and not another --smoke flag.
    src/render/case_watch.cpp
    src/render/demo.cpp
    src/render/session.cpp
    src/render/step_pump.cpp
    src/render/stb_impl.cpp
    # Task #83 -- the steps-based ease every panel's open/close and the HUD's
    # own alert row now animate through, instead of popping. See the header.
    src/render/anim.cpp
    # 3D BUILD, V LANE -- the viewmodel machine: what the player's own hands
    # are doing, stepped by Session once per movement step off the combat
    # sim's getters. Lives here (not in render3d) so a --screenshot after a
    # scripted punch photographs the same frame of the swing everywhere;
    # render3d/viewmodel.hpp draws it. See the header.
    src/render/viewmodel_machine.cpp
    # #78 -- the people, as pixels the owner drew. content/art/sprites is
    # twenty-five 16x16 actor figures in the MERCOLAS-24 palette with a tag
    # vocabulary already authored against the Java actor types; this reads the
    # ART and none of the Java code that indexed it.
    src/render/actor_sheet.cpp
    # #80 -- the origin-select and customize flow: a new game's first two
    # screens, BG3/DOS2-shaped, drawn with no Session and no world underneath
    # them -- see the file's own header on why.
    src/render/creation.cpp
    src/render/creation_page.cpp
    # THE CASEBOOK PASS -- the investigation's own book, converted off the tiled
    # Menu's bottom strip into the master/detail frame the spec names it for.
    # This is the surface the owner's playthrough stalled on: three leads opened
    # at the Weighhouse and nothing on the screen said so. See the header.
    src/render/casebook_page.cpp
)
target_include_directories(granadad-render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# GRANADAD_CONTENT_DIR_DEFAULT arrives through granadad-content.
# PRIVATE granadad-audio: the wiring pass (audio_engine.hpp's own plan) put
# the event hooks in session.cpp, so render calls the CORE audio engine --
# never granadad-audio-sdl, which stays the client's alone. PRIVATE because
# session.hpp only forward-declares AudioEngine; no audio header is in
# render's interface. The determinism argument is unchanged: granadad-sim
# and the gate targets still link no audio, and nothing under src/sim can
# include it.
target_link_libraries(granadad-render
    PUBLIC granadad-sim stb::stb
    PRIVATE granadad-audio)
granadad_apply_determinism(granadad-render)

# The lamp baker. Turns a fixture's authored light_source markers into the
# sidecar the game reads, so the 27 lamps of the Docks are derived from canon
# rather than transcribed. Run by hand; its output is committed.
add_executable(granadad-bake-lamps src/tools/bake_lamps.cpp)
target_link_libraries(granadad-bake-lamps PRIVATE granadad-render)
granadad_apply_determinism(granadad-bake-lamps)
granadad_static_runtime(granadad-bake-lamps)

# ---------------------------------------------------------------------------
# granadad-audio — beds, footsteps, UI and combat sound. CLIENT-SIDE ONLY.
#
# The core library links NO SDL and NO granadad-sim, and nothing under
# src/sim/ or the gate targets links it back — that structure IS the
# determinism argument: the twin-run gate and the world hash cannot be
# affected by code they cannot reach. The one file that touches SDL
# (backend_sdl.cpp) compiles into its own wrapper library below, client
# builds only, so the sim-only docker host check builds and tests the whole
# engine headless on the null backend. stb::stb is here for stb_vorbis: the
# vendored Kenney audio is OGG end to end and SDL3 has no Vorbis decoder.
# granadad-content provides contentDir() so the sound bank resolves the same
# way every other asset does.
#
# The client links granadad-audio-sdl (below) as of the wiring pass —
# createSdlAudioEngine() in main.cpp, event hooks in session.cpp; see
# audio_engine.hpp's wiring plan, now made.
# ---------------------------------------------------------------------------
add_library(granadad-audio STATIC
    src/audio/mixer.cpp
    src/audio/decode_vorbis.cpp
    src/audio/sound_bank.cpp
    src/audio/audio_engine.cpp
    src/audio/backend_null.cpp
)
target_include_directories(granadad-audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# GRANADAD_CONTENT_DIR_DEFAULT arrives through granadad-content.
target_link_libraries(granadad-audio PUBLIC granadad-content stb::stb)
granadad_apply_determinism(granadad-audio)

# The audio smoke probe — bake_lamps' pattern: a dev tool, run by hand.
# Offline-renders a scripted couple of seconds of the Docks (harbour bed, a
# walk across three surfaces into water, a punch, a click) to a WAV, inspects
# the samples programmatically, and exits non-zero on silence or clipping.
add_executable(granadad-audio-probe src/tools/audio_probe.cpp)
target_link_libraries(granadad-audio-probe PRIVATE granadad-audio)
granadad_apply_determinism(granadad-audio-probe)
granadad_static_runtime(granadad-audio-probe)

# The content packer — the build-time half of granadad-standalone.exe. Same
# standing as the probe: a dev tool, run by the docker build. It links
# granadad-audio so the audio subset it packs IS the compiled soundPaths()
# manifest — the 2 MB the game opens, never the 1.2 GB kenney dump — and it
# cannot drift from the code because it is generated from it. The pack
# writer/extractor pair lives in granadad-content (via granadad-audio's
# PUBLIC link) so the round trip is proved in that module's own suite.
add_executable(granadad-pack-content src/tools/pack_content.cpp)
target_link_libraries(granadad-pack-content PRIVATE granadad-audio)
granadad_apply_determinism(granadad-pack-content)
granadad_static_runtime(granadad-pack-content)

if(GRANADAD_BUILD_CLIENT)
    # The ONLY audio target that sees SDL. The client links THIS at wiring
    # time, never granadad-audio directly.
    add_library(granadad-audio-sdl STATIC src/audio/backend_sdl.cpp)
    target_link_libraries(granadad-audio-sdl
        PUBLIC granadad-audio PRIVATE ${GRANADAD_SDL_TARGET})
    granadad_apply_determinism(granadad-audio-sdl)
endif()

# ---------------------------------------------------------------------------
# granadad-render3d-core — the 3D scene, as pure C++. Floats are legal here.
#
# 3D BUILD. Everything the 3D renderer draws comes through ONE struct, the
# SceneDescription (include/granadad/render3d/scene.hpp): meshes as plain
# vertex arrays, textures as plain RGBA bytes, instances as (mesh, transform,
# tint), a camera and a clear colour. This library BUILDS that struct as a
# pure function of Session state -- the chunk mesher, the actor instancer,
# the viewmodel state machine, the starter scene -- and hashes it (FNV-1a
# over its canonical byte image), and it never sees raylib.h. That is the
# renderer's determinism claim: a GPU frame is never hashed, the description
# is, and two sessions driven by the same script must produce identical scene
# bytes. Built and tested in the sim-only host check like everything else.
#
# Links granadad-render (PUBLIC: render::Camera, the day curve, bandSurface)
# and through it the sim -- read-only. Nothing here writes to a sim struct.
# ---------------------------------------------------------------------------
add_library(granadad-render3d-core STATIC
    # The description, its hash, and the camera adapter.
    src/render3d/scene.cpp
    # A lit ground plane and a cube: the first thing the 3D path drew, the
    # reference the frame tests render, and the worked example of how a lane
    # fills a SceneDescription.
    src/render3d/starter_scene.cpp
    # The three lanes' contracts, each shipped as a stub with the ids, the
    # coordinate mapping and the version rules fixed so the lanes can build
    # against them in parallel: chunk meshes (W), actor instances (A), the
    # viewmodel machine (V).
    src/render3d/chunk_mesher.cpp
    # The world lane: the Docks as a scene -- chunk geometries built once,
    # recoloured on the minute, instanced near the eye, the sky dome, the
    # camera from the body.
    src/render3d/world_scene.cpp
    src/render3d/actor_instances.cpp
    src/render3d/viewmodel.cpp
)
target_include_directories(granadad-render3d-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(granadad-render3d-core PUBLIC granadad-render)
granadad_apply_determinism(granadad-render3d-core)

# ---------------------------------------------------------------------------
# granadad-render3d-rl — the raylib adapter. EXACTLY ONE translation unit in
# the whole project includes raylib.h, and it is this library's one source.
#
# raylib is PRIVATE: its headers, its global-namespace names (Rectangle,
# Camera, CloseWindow...) and its PUBLIC platform defines stop here. The
# client and the tests see include/granadad/render3d/backend.hpp and nothing
# of raylib's. Built in BOTH configures -- on the Memory platform with the
# software rasterizer for the host check, on Desktop/GL 3.3 for the .exe --
# so the frame tests can draw a real 3D frame headless on every build. The
# HUD keeps drawing into its render::Framebuffer; this composites it over the
# 3D frame as a point-filtered, integer-scaled overlay texture, which is what
# keeps the eighteen pixel-exact HUD/page test files byte-identical.
# ---------------------------------------------------------------------------
add_library(granadad-render3d-rl STATIC src/render3d/rl_backend.cpp)
target_link_libraries(granadad-render3d-rl
    PUBLIC granadad-render3d-core
    PRIVATE raylib)
granadad_apply_determinism(granadad-render3d-rl)

# ---------------------------------------------------------------------------
# client — the window. 3D BUILD: raylib owns the window, the GL context, the
# 3D pass and the composite (granadad-render3d-rl); SDL stays for the audio
# device and the gamepad, and is never initialised with VIDEO any more.
# ---------------------------------------------------------------------------
if(GRANADAD_BUILD_CLIENT)
    add_executable(granadad src/client/main.cpp)
    # granadad-audio-sdl per audio_engine.hpp's wiring plan: the client is the
    # ONE target that reaches a real sound device. granadad-render3d-rl for
    # video; ${GRANADAD_SDL_TARGET} for the gamepad and the event queue the
    # input routing still reads (see VideoBridge in main.cpp).
    target_link_libraries(granadad
        PRIVATE granadad-render granadad-render3d-rl granadad-audio-sdl ${GRANADAD_SDL_TARGET})
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
        tests/test_fatigue.cpp
        tests/test_tavern.cpp
        # ACTION-COMBAT BUILD -- the swing state machine as a table, the
        # sightline raycast on-line table, the charge tiers, lethal death and
        # the corpse conventions, the murder hook, intent-by-verb, and the
        # Watch's deference to a presented Wielder.
        tests/test_combat_action.cpp
        # STANCE & ROOM BUILD -- fighting mode as a table (every raise and
        # lower rule), the one-press tap-from-hands-down, the 600-step lull at
        # its exact edge, the LOWER HANDS slot of the interact walk proved
        # against its own prompt, the FISTS UP row, and the twin-run of a
        # scripted raise/lower/re-raise on the tavern hash.
        tests/test_stance.cpp
        # WATCH & RHYTHM BUILD -- WatchCause::Violence as a table (seen
        # lethal fight -> Closing + halt + arrest; unseen -> nothing; a brawl
        # -> nothing; a presented Wielder -> never), the blow on the closing
        # watchman (Kill intent, murder), the wind-up / stagger / recoil /
        # block-stagger timings, the guard and hard bands same-roll off the
        # NPC's pending roll, stand-back, bouncers refusing steel, the rout,
        # and a scripted fight twin-run.
        tests/test_watch_rhythm.cpp
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
        tests/test_chargen_raws.cpp
        tests/test_dialogue.cpp
        tests/test_faction.cpp
        tests/test_spellforge.cpp
        tests/test_questline.cpp
        tests/test_step_pump.cpp
        tests/test_controls.cpp
        # PANES PASS -- the frame primitives and the composition rules: the
        # cell grid against the font's own, the span splits, the |/! alternation
        # through interior dividers, the option list re-columning by width, the
        # common value column, panes holding their height, and the controls page
        # composed at every window size the game runs at.
        tests/test_panel.cpp
        # UI-EA-SPEC -- the word diet, pinned: the census's own token rule
        # (whitespace-delimited, alphanumeric-bearing, keycap sentinels count
        # zero) run over each dieted page's at-rest model, each surface held
        # under its ceiling so a stray sentence cannot creep back in unseen.
        tests/test_word_diet.cpp
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
        # THE DEMO PASS -- the scripted route: its shape, the ground under
        # every coordinate in it, that it finishes by its own hand, and that
        # two runs of it end in the same place at the same second.
        tests/test_demo.cpp
        # #82 -- the bloodletter trail's own leads, read as the letters
        # somebody actually wrote: that the file loads, that every `lead`
        # it names is a real casebook.json lead, and that the Wielder's own
        # closing dispatch never claims a discovery only the unbuilt
        # undercellar delve would have made.
        tests/test_letters.cpp
        # COURIER CASE -- THE QUIET TENANT, the owner's second authored case.
        # The raws' own invariants (zero starts, one close, reachable-from-the-
        # courier's-first-lead rather than from a start flag, sites standable in
        # the baked Docks, the handed sheet cross-referenced to its lead), and
        # the errand driven end to end through the same verbs a keypress makes,
        # twice, ending in byte-identical books -- the twin-run discipline the
        # sim's blood oath keeps, applied to a session-scripted case.
        tests/test_mission_sheet.cpp
        tests/test_case_line.cpp
        # EVICTION -- the owner's third authored case. The raws' own invariants
        # (zero starts, TWO closes for participate/disrupt, the four
        # investigation leads reachable from the hire and the two closes from
        # their scripted beats, sites standable in the baked Docks, the gate on
        # C2's own ground, the lease agreement rendered in the roll's
        # vocabulary and never as a dwelling-lease), and both paths driven end
        # to end through the same verbs a keypress makes, twice, to
        # byte-identical books -- with the open-hand hire gate proved to refuse
        # below its bar and pass at it.
        tests/test_eviction.cpp
        tests/test_eviction_line.cpp
        # CASE WATCH -- the errand made watchable: the recorded drive is the
        # harness's own route (twice, to the op), the replay reproduces the
        # drive's end state exactly with zero extra simulation steps, and the
        # planned runtime sits in the demo's neighbourhood.
        tests/test_case_watch.cpp
        # FAST TRAVEL -- the ward map's second commit verb. The cost math
        # (route length at the shipped walking pace, in integers), each
        # refusal predicate (carrying the courier's man, the Watch closing,
        # the wait page's own safe-spot list), a travel taken advancing the
        # clock by exactly its restated minutes, and two identical travels
        # ending byte-identical -- the twin-run discipline the sim's blood
        # oath keeps, applied to a session-scripted feature.
        tests/test_travel.cpp
        tests/test_firstrun.cpp
        # The pause menu ESC opens when nothing else has already claimed the
        # keyboard -- RESUME, SETTINGS and a QUIT that asks twice, replacing
        # the unconfirmed `running = false` that key carried since #77.
        tests/test_pause.cpp
        # SPELLS BUILD -- the Grimoire page and the quick bar it loads: the
        # bind/select/equip round-trip that closes VERIFICATION GAP #77 and
        # gives S13's equipSpellAt its callers.
        tests/test_grimoire.cpp
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
        # Core action #13 -- THE WARD MAP. The plan's cell classification
        # against the real baked Docks, the palette derived from the atlas's
        # own tones, and the label declutter proved deterministic, overlap-
        # free, and -- the acceptance sentence -- able to place the Mission
        # of the Flame from the authored spawn.
        tests/test_map_view.cpp
        # THE CASEBOOK PASS -- the book as the master/detail frame, the moment
        # leads open, and the route from a lead to where it is. The defect
        # these cases exist for is the one the owner's own playthrough found:
        # three leads opened at the Weighhouse and nothing on the frame said
        # so. See the file header.
        tests/test_casebook_page.cpp
        # DISTRICT PHASE D -- THE THRESHOLD MOMENT. Crossing into a named place
        # announces it, once, on a plate over the compass ribbon: the edge that
        # fires it (and the four ways it must NOT fire -- a spawn, a seam
        # between two rects of one road, a band change on the same street, a
        # body standing still), the stand-down every other overlay obeys, and
        # the geometry asserted in pixels, because this HUD's two shipped
        # collisions were both found in a PNG by a person.
        tests/test_place_plate.cpp
        # UI-EA (LANE HUD) -- THE LAW OF EARNED TEXT. The at-rest street HUD's
        # word budget pinned by counting tokens (the census's own law), each
        # reference row's wake edge and 2.5s sleep, the rest state a fresh
        # session actually settles into, and the Q-hold tutor toast's
        # two-showings-then-retired life. The diet cannot regress silently.
        tests/test_hud_diet.cpp
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
        # STREET PANIC BUILD (feel/build, 9a) -- AND WHETHER THE STREET RUNS
        # FROM A KNIFE. The alarm against the three-clause witness rule,
        # computed independently (same band, in range, line of sight); the
        # walled, the other-band, the far and the Watch all untouched; the
        # panic lasting as long as its severity says and no longer; the
        # frightened alone paying the panic rate; and the population's
        # twin-run byte-identical after an alarm -- the one declared
        # population baseline move of the Oblivion-feel program.
        tests/test_street_panic.cpp
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
        # #81 -- THE RADIANT QUEST GENERATOR CORE. That a malformed or
        # nonexistent-body template is refused at load exactly like a
        # contract's patron/source; that the same day of the same world
        # offers the same board and a different day does not; and that every
        # generated brief substitutes a real WardIdentity name and a place
        # read off the actor's own live tile, with no token left unfilled.
        tests/test_radiant_quest.cpp
        # #81 -- THE ANTI-REPETITION CHECK. Hundreds of days of boards,
        # bucketed by template and by the giver's own faction, proving the
        # generator does not collapse into the same sentence with one noun
        # swapped: mostly-distinct briefs, no single brief dominating, and
        # more than one body ever drawn into the giver or target slot.
        tests/test_radiant_variety.cpp
        # #85 -- THE CONSOLIDATED CONTROL SCHEME. Interact resolved by stance
        # and by what is faced (talk/pickpocket, examine/take, rest, pick-a-
        # lock), proved as the actual feature rather than only "the action
        # fires" -- see the file's own header.
        tests/test_interact_context.cpp
        # #85 -- Vertical's own orchestration (climb, then a drop, then an
        # ordinary jump) and the Menu router's page cycle, kept apart from
        # Pause.
        tests/test_vertical_menu.cpp
        # The tiled Menu's own drawing, converted to the terminal register:
        # the one-frame/four-pane layout, the pane-derived page size (the
        # ship note's "ten rows into a forty-row pane" defect, pinned dead),
        # the nine-key window's printed digits staying truthful to Session's
        # direct-select, letter paging clamped to the pane, and the empty
        # states surviving the reframe. Pure layout + framebuffer cases; the
        # Menu's Session behaviour stays in test_vertical_menu.cpp above.
        tests/test_menu_view.cpp
        # The Docks signage pass: the generated 83-sign table matches the
        # survey's own count, docks::kPlaces wins the priority order over a
        # sign's own tmx name where both exist, and a label actually draws
        # in view and stops drawing behind you or past the legibility
        # radius.
        tests/test_signage.cpp
        # The audio engine, headless on the null backend: mixer math (pan
        # law, bus gains, soft clip, voice reaping), the procedural harbour
        # layers, bed crossfades proved monotone, the footstep cadence gate
        # and the 22-row material->surface table pinned against
        # render::materialIds(), the manifest checked file-by-file against
        # the vendored Kenney tree wherever it exists, and graceful silence
        # where it does not.
        tests/test_audio_engine.cpp
        # 3D BUILD -- THE SCENE DESCRIPTION AS THE THING THAT GETS HASHED.
        # Identical state hashes identically, any byte moved is a different
        # hash, the frame convention every lane builds against (north -Z,
        # east +X, up +Y), the mesh id spaces that keep the lanes apart, and
        # the viewmodel machine's integer clock. No raylib, no window, no
        # licensed asset: the starter scene is procedural.
        tests/test_scene3d.cpp
        # 3D BUILD -- THE 3D FRAME, ASSERTED ON ITS PIXELS, HEADLESS. A real
        # frame of a lit plane and a cube drawn through raylib's software
        # rasterizer inside the test suite -- no window, no GPU, no display
        # server -- with the pixels checked: non-trivial, the sky where the
        # sky is, the ground where the ground is, the lit face brighter than
        # the shaded one, and the same description drawn twice byte-identical.
        # Under a GPU build (the .exe's own test binary) the frame cases skip
        # rather than open a window; the overlay-alpha cases run everywhere.
        tests/test_render3d.cpp
        # 3D BUILD -- THE WORLD LANE. The chunk mesher over the real Docks:
        # the mesh is a deterministic function of the tile map (built twice,
        # same bytes, same scene hash), a wall cell emits its four sides and
        # a floor in a street emits none, shared faces between neighbours
        # are dropped, the light recolours without moving a vertex, and the
        # world scene instances only the chunks near the eye. Procedural
        # atlas: no art tree required.
        tests/test_chunk_mesher.cpp
        # 3D BUILD -- THE VIEWMODEL + HUD LANE. The player's own hands follow
        # the combat sim's state through Session (charge, hard hold, the
        # swing at the resolved tier, the guard, the cast, the flinch), the
        # posed parts are described and hashed, the terminal HUD composites
        # over the 3D frame BYTE-IDENTICALLY where it is opaque (rlsw), the
        # raised fists draw over the Docks and in front of a wall the body
        # stands against, and the washes are overlay pixels. Placeholder
        # arms: no licensed glb required.
        tests/test_viewmodel.cpp
    )

    add_executable(granadad-tests tests/test_main.cpp ${GRANADAD_TEST_SOURCES})
    target_link_libraries(granadad-tests
        PRIVATE granadad-gate granadad-render granadad-render3d-rl granadad-audio doctest::doctest)

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
        # RADIANT BUILD: the taproom AND the district's people on one engine,
        # wired together the way the client wires them (Tavern::attachPeople),
        # which is the ONLY live twin run in the build whose radiant board is
        # populated and moving -- the driver takes an errand off it every six
        # simulated minutes, and both runs must agree bit for bit about a
        # board that was generated off six hundred live bodies. 900 ticks,
        # the tavern entry's own number: the population's determinism has its
        # own two-hour entry above; this one exists to compare radiant STATE,
        # posted at attach and moved by the driver, not to re-prove the crowd.
        add_test(NAME granadad-twin-run-gate-radiant
            COMMAND granadad-twin-gate --tavern --population --population-hour 16 --ticks 900)
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

# The audio probe ships for the same reason the lamp baker does: the docker
# context has no Kenney audio, so the only machine that can render the REAL
# sound bank to an inspectable WAV is the one with the repo.
install(TARGETS granadad-audio-probe RUNTIME DESTINATION .)

# The content packer ships so that PACKAGING NEVER NEEDS A BUILD. The owner
# directive behind it: producing granadad-standalone.exe must not depend on
# compiling the game or running its test cycle. With this tool in dist/ (and
# granadad-base-stripped.exe beside it, published by docker/build.Dockerfile),
# scripts/package-standalone.ps1 re-packs a content tweak on the host in
# seconds: run the packer over content/, concatenate onto the stripped base,
# done. The docker build still packs its own standalone every green run --
# that is the CI proof of the mechanism -- but it is no longer the only path.
install(TARGETS granadad-pack-content RUNTIME DESTINATION .)
