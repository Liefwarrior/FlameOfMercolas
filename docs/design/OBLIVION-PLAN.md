# OBLIVION-PLAN — a 2006-Oblivion-shaped Granadad, and the loop that makes it a game

**Status: PLAN ONLY. Nothing in this document is implemented, and nothing may be
until the owner approves it on Uncbox issue #19.** This PR adds this file and
changes nothing else: no code, no `content/`, no edit to `DECISIONS.md` (the
amendment is drafted in section 2 and lands in phase 3).

Author: the planning agent for Liefwarrior/Uncbox#19, from the owner's brief of
2026-11-12 and its same-day update ("where they differ, the update wins").
Repo state audited: `main` at `364006b3` (2026-10-14). Nothing was built or run for
this plan; where something was executed, section 12 says exactly what.

---

## 0. Read this first

**What the owner asked for.** Granadad should play and look roughly like the original
2006 *Oblivion*: free first-person walking with real mouse-look, solid 3D streets and
buildings built from the ward's tile data, per-pixel lighting with torches and
lanterns, shadows, a day/night sky, distance fog, light post-processing, simple
low-poly people, and a complete, saved gameplay loop: inventory, shops, equipment,
spells, Morrowind-style dialogue, a journal and topic index, save/load. Fifteen phases
are already filed as Uncbox issues #21-#35.

**What the audit found, in six sentences.**

1. **A real 3D renderer already exists.** `native/src/render3d/` (about 11k lines plus a
   2.1k-line raylib adapter) has been the default view since 2026-09-11: depth buffer,
   textured chunk meshes cut from the tile data, true pitch, a sky dome, actors,
   viewmodel hands. `README.md`, `native/README.md` and `ARCHITECTURE.md` still
   describe the older software voxel-column renderer, and the binding row in
   `DECISIONS.md` still says real 3D was rejected. The docs and the code have been
   contradicting each other for two months (section 3, section 2).
2. **What the 3D path lacks is exactly the owner's list:** per-pixel lighting (light is
   baked into vertex colours), real distance fog (the clear day has none), shadows,
   water shading, post-processing, animated people (placeholder boxes), and art that
   looks like 2006 and not like a 1-bit tile atlas. The reason is historical: the
   headless proof used raylib's software rasteriser, which runs no shaders, so every
   effect was forced into vertex colour. **Swapping that headless path for Mesa
   llvmpipe is the one change that unlocks the rest** (section 6.2).
3. **Most loop systems exist in the simulation** (items and a worn-slot kit, shops,
   barter, spells, topic dialogue, casebook, letters) but each lacks the piece that
   makes it a *loop*: no use or consumables, one hand slot and no off-hand, three
   placeholder shops, no ranged or mind spells, no keywords and no learned topics, a
   journal that is a casebook rather than an index, **no save/load at all** (nothing
   survives closing the window), **no title screen, and no ending** (section 3.2).
4. **`session.cpp` is 20,416 lines and 980 KB**, of which about 7,900 lines are scripted
   `--flag` capture drivers, not game logic. It splits cleanly along its own seams
   (section 5).
5. **The cleanup is smaller and safer than "delete the Java" sounds.** About 14 MB of
   orphaned pre-3D screenshots, a handful of dead functions and some rotted docs are
   provably removable now. The Java tree is not referenced by any build, Docker or
   script, but it is still the only writer of the `.trojsav` format and the source of
   the golden vectors, so removing most of it needs an owner ruling (section 4).
6. **The phase order in #21-#35 mostly stands.** I propose four adjustments, each
   argued in section 9 and listed in section 11 for you to accept or refuse.

**Decisions I need from you** are collected in section 11; none blocks approving the
rest. The biggest two: whether the Java parity reference may go, and the audio gap
(a clean checkout is silent because the audio files are gitignored).

---

## 1. Rulings in force

From the owner's brief and its update. Quoted rules are binding on every phase.

1. **Real 3D, GPU-accelerated, built on the existing `native/src/render3d/` path and its
   single raylib adapter**, not a new renderer. Use the GPU properly: vertex and
   fragment shaders, depth buffer, per-pixel lighting, shadows, fog, post-processing.
   Thin layers (raylib, SDL3, OpenGL) are fine. Third-party engines (Unity, Unreal,
   Godot, Ogre, bgfx) are not. The old 2D-trickery world renderer is retired once
   render3d covers what it does. The old 3D rejection in `DECISIONS.md` is marked
   **SUPERSEDED**, not deleted.
2. **The GPU only draws; it never decides gameplay.** Line of sight, detection,
   stealth and everything else the simulation depends on stays computed in the
   simulation with integers and is never read back from the GPU. Headless
   `--screenshot` runs the same renderer on Mesa llvmpipe in the Linux build;
   screenshot comparisons use a small tolerance.
3. **The complete loop**, working together and saved: inventory, shops, weapons and
   equipment, simple spells, Morrowind-style dialogue, journal and topic index, save
   and load.
4. **README.md's non-negotiables still hold:** no `float`/`double` in simulation
   state or any math that affects it; the player's fixed-point position `(tile << 8) |
   sub` lives in the sim; no `std::unordered_map` (or friends) anywhere in `native/`;
   `content/` is never modified (new files may be added).
5. **Progression keeps the design north star: no levels.** The code agrees (section
   3.2, row 8c): there is no character level or XP-to-level anywhere. Skills improve
   by use, attributes are fixed after creation, standing is faction rungs and the
   five legend tracks. This plan proposes no change.
6. **Planning only.** The next phase starts when the owner comments `@liefwarrior-bot
   go` on its issue, as the issues already say.

---

## 2. The DECISIONS.md amendment (draft; appended in phase 3)

This PR does not edit `docs/design/DECISIONS.md`. In phase 3 the amendment below is
appended, and the clauses it overturns are struck through in place with the date, in
the file's own convention (struck text stays, so history is not lost).

**Why an amendment is needed although a 3D renderer already exists.** The binding row
*First-person EXPLORATION camera (added 2026-07-29)* says, in its own words, "real 3D
was rejected because visibility would live in the GPU" and lists "no true pitch ... and
affine texture mapping across a quad" as accepted costs. The September 3D build
(raylib 6.0, the scene description, rlsw for headless frames; commit `feat(3d/toolchain)`
2026-09-11) was never recorded as a ruling. So the file that is called binding
describes a renderer that has not been the default for two months.

> | — | Real 3D renderer (added 2026-11-12, owner ruling) | **The ruling that rejected real 3D is SUPERSEDED.** Liefwarrior, owner, 2026-11-12: Granadad is to play and look roughly in the way of the original 2006 *Oblivion* -- not AAA, not modern: free first-person walking with true mouse-look, solid 3D streets and buildings built from the ward's tile data, per-pixel lighting with torches and lanterns, shadows, a day/night sky, distance fog, light post-processing, simple low-poly people and a complete, saved gameplay loop. **Struck through, kept for history:** ~~"real 3D was rejected because visibility would live in the GPU"~~ and ~~"no true pitch (looking up is a horizon shear), affine texture mapping across a quad"~~ (2026-07-29, the first-person exploration row); ~~"Daggerfall's graphics ... billboarded sprites for everything alive"~~ as the *render* target (2026-07-28 presentation row; billboards stay for flames and halos); ~~"Barony-grade, sharper"~~ (README.md visual target). **Mechanism:** `native/src/render3d/` is the renderer; the GPU adapter is raylib on OpenGL 3.3 core with hand-written GLSL; the depth buffer, perspective-correct textures and true pitch are in force. **No third-party engine** (Unity, Unreal, Godot, Ogre, bgfx); thin layers are permitted (raylib, SDL3, OpenGL, single-purpose libraries pinned by commit). **The GPU only draws:** line of sight, detection, stealth, light as the sim reads it, and every other thing the simulation depends on is computed in the simulation from integers and is never read back from a frame, a depth buffer or a shader. **Headless proof is mandatory:** `--screenshot` works with no display and no physical GPU (Mesa llvmpipe in the Linux build); screenshot comparisons use a stated tolerance, and the scene-description hash stays the exact cross-machine proof. **Unchanged and still binding:** no float or double in simulation state or any math that affects it; the player's position is sub-tile fixed point in the sim; the tile simulation is the model and every camera is a view over it (2026-07-28); no `std::unordered_map` in `native/`; `content/` is never modified (new files may be added); progression has no levels (3a). **Determinism:** a GPU frame is never hashed across machines; rendering never writes sim state, so no baseline in `docs/BASELINE-WORLD-HASH.md` moves in a renderer-only phase. |

Phase 3 also fixes the text that has drifted: the README "Design north star" paragraph
(Barony-grade becomes the Oblivion-2006 target; the north-star quote about levels
stays untouched), the renderer rows of `README.md` and `native/README.md`, and
`ARCHITECTURE.md` §2, §3 and §11.1 (marked SUPERSEDED, pointing here).

---

## 3. What exists today

BUILT = implemented and covered by a doctest case or a scripted `--flag`. PARTIAL =
exists, but a player hits a wall. MISSING = not in the code. Paths are under
`native/` unless they start with `content/` or `docs/`. This was read from the code,
not played; nothing was built for this plan.

### 3.1 The two renderers

**Renderer A, the old software voxel-column raycaster** (`src/render/world_renderer.cpp`
815 lines, `world_renderer.hpp`, `voxel_classify.*`). Still alive, not dead code: it
is built by every `Session` (`session.cpp:294`), drawn by `--2d`
(`client/main.cpp:1301`), by the workbench's game pane (`workbench/workbench.cpp:1455`),
by `runSmoke` before the 3D shutter (wasted work), and by `test_render.cpp`,
`test_signage.cpp`, `test_tavern_render.cpp`, `test_kit_page.cpp`. render3d also
borrows its shared types (`Camera`, `SpriteInstance`, `LampGlow`) from
`world_renderer.hpp`. It cannot be deleted before those four uses are moved (phase 2
extracts the shared types, phase 7 does the rest).

**Renderer B, render3d** (default since 2026-09-11; `Options::video3d = true`,
`client/main.cpp:365`). `--screenshot` goes through it too (`runSmoke` →
`shutter_through_backend`, `main.cpp:3980`).

| Piece | Status | Where | Note |
|---|---|---|---|
| Scene description, FNV-1a scene hash | BUILT | `render3d/scene.cpp`, `scene.hpp`; `test_scene3d.cpp` | The determinism proof for the renderer. Keep. |
| Tile data to chunk meshes | BUILT | `chunk_mesher.cpp`, `door_jambs.cpp`; `test_chunk_mesher.cpp` (41 cases) | 16x16 chunks, shared face culling, door gaps are real, water at tide height. |
| Ramps and stairs as wedges | PARTIAL | `chunk_mesher.cpp` | Thin slabs, "its own stated gap". |
| Roofs with pitch, eaves, trim | MISSING | | Flat tops only. |
| Textures | PARTIAL | `render/atlas.cpp`, `content/art/custom/tiles.png` | One atlas of the owner's pixel tiles; faces stretch the tile over the whole face height. |
| True pitch, depth buffer, perspective-correct textures | BUILT | `sim/player.cpp` `kMaxPitch`; the GL pipeline | The brief's "real mouse-look" already works. |
| Lighting | PARTIAL | `render/lighting.cpp`, `chunk_mesher.cpp` `colourChunk()` | Vertex colour: day-curve ambient plus a baked 27-lamp field plus a few dynamic lamps. No normals, no per-pixel term, no shadows. |
| Sky | PARTIAL | `world_scene.cpp` `buildSkyDome()` | Vertex-coloured dome. No sun or moon disc, stars, or clouds. |
| Distance fog | MISSING (clear day), PARTIAL (weather) | `world_scene.cpp` `buildVeil()` | The header says the clear district "reads sharp to its far end". Weather adds 19 banded translucent shells to 56 tiles. |
| Water | PARTIAL | `chunk_mesher.cpp` | A tinted top face. |
| Shadows, post-processing | MISSING | | |
| Static building kit (Synty glTF) | PARTIAL | `static_pieces.cpp` (4.7k lines), `content/raws/world3d/docks-pieces.json` | The `.gltf` files are in `/content/art/lot-3d/`, **gitignored and absent from a fresh checkout**. No file, nothing draws; the chunk mesh is the placeholder. |
| People | PARTIAL | `actor_instances.cpp` | Rigid placeholder boxes (208-301 per street frame), not animated; the 8 clips only play on a licensed `.glb` rig that is not in the repo. |
| First-person hands and weapon | PARTIAL | `viewmodel.cpp`, `viewmodel_machine.cpp` | State machine and poses built; the hands are box fists; a sword is a placeholder. |
| GPU adapter | BUILT | `render3d/rl_backend.cpp` (2,124 lines; the only file that includes `raylib.h`) | raylib 6.0: window, GL 3.3, one small blend shader; headless builds use raylib's software rasteriser rlsw (`PLATFORM=Memory`), which has **no shader support**. |
| Headless `--screenshot` | BUILT on rlsw | `rl_backend.cpp`, `Dockerfile` host check | Byte-exact frame tests (`test_render3d.cpp`, 11 cases) against rlsw. Must become llvmpipe plus tolerance (phase 3). |
| HUD and pages | BUILT | `render/hud.cpp`, `panel.cpp`, `dialogue_view.cpp`, ... | Drawn on the CPU into a `render::Framebuffer`, composited over the 3D frame; 18 pixel-exact test files rely on this staying so. |

### 3.2 The gameplay loop, end to end

| # | Step | Status | Evidence | What is missing |
|---|---|---|---|---|
| 1a | Title / menu | **MISSING** | `main()` goes straight to character creation (`client/main.cpp` ~5819-5961). | A title with New, Continue, Settings, Quit. |
| 1b | Character creation | BUILT | Devin / Gabri / custom, 9 callings, a quiz and a biography; `--creation=...`; `test_creation`, `test_chargen*`. | The attribute point shop awaits sign-off (PLAY.md §2). |
| 1c | New game / first run | BUILT | Spawn on the Tarwalk at 08:00; `test_firstrun`; the rope loops back to creation ("A NEW MAN"). | New game only by relaunching or being hanged. |
| 2a | Free movement | BUILT | `sim/player.cpp` (Q8 body, sprint, crouch, jump, mantle, leap, drop); `test_player`, `test_roofrun`. | None. |
| 2b | The ward | BUILT world, PARTIAL play | One baked Docks world (`content/maps/baked/docks_surface.trojsav`, about 17k tiles walkable from spawn, 25k with roofs); 661 ward actors plus the Gull's staff with jobs and schedules; `test_ward_actors`. | A full street with little to do in it. |
| 2c | Doors and interiors | PARTIAL | Doorways are open gaps; threshold plate names the place; `render3d/door_jambs.cpp`. `SoundId::DoorOpen/Close` have no call site. | No door objects, no open/close, no locked houses; locks and strongboxes exist only in the Gull. |
| 2d | Day / night | BUILT | `render/lighting.cpp` `skyAt()`, lamp bake, weather; `test_weather`, `test_lamps`. | The default clock is 1 sim-second per real second (`main.cpp:1305`): a day takes 24 real hours, so time moves in practice only by WAIT, sleep and arrest skips. |
| 3a | Casebook / leads | BUILT | `sim/casebook.cpp`, `content/raws/quests/casebook.json` (12 leads, 2 dead ends); `test_casebook` walks the trail to `closed()`. | Two leads sit one band down on the Beaching Strand; the scripted `--trail` skips them and no test walks them as a player would. |
| 3b | Journal, letters, map | BUILT | `J` notes ring, `M` ward map; `render/casebook_page.cpp`, `map_view.cpp`; `test_letters`. | It is a casebook, not an index: no topics, people or places as entries (row 13 below). |
| 3c | Radiant quests | BUILT (word errands), PARTIAL (fetch) | `sim/radiant_quest.cpp`, 21 templates. | Fetch targets keep goods that ward bodies never yield; an owner ruling is owed (`docs/SESSION-2026-10-09.md` item 1). |
| 3d | Faction lines | BUILT | Flame (6 stages), Skyrunner (9), courier, eviction; `sim/questline.cpp`, `sim/faction.cpp`. | Several unlock tokens still have no reader. |
| 4a | Talk | BUILT | `E` on the body in front; topic tree, tone dial; 210 authored bark tables; `sim/dialogue.cpp` (2.5k lines), `test_dialogue`, `test_ward_voice`. | Keywords, learned topics: row 12 below. |
| 4b | Barter | BUILT | `sim/barter.cpp`, haggle view. | Covers ale and rooms only. |
| 5a | Fight | BUILT | Real-time swing, guard, charge, stagger (`sim/tavern.cpp`); `test_combat_action`, `test_stance`, `test_fatigue`. | Opponents are people, rats and cats; no ranged weapons. |
| 5b | NPC AI | BUILT | Pathing, Watch halt and arrest, street panic; `test_street_panic`. | A deferred route-replan oscillation (DECISIONS 2026-09-26). |
| 5c | Defeat, rope, nemesis | BUILT | `applyDefeat`, `sim/justice.cpp`, `sim/nemesis.cpp`; `test_court`, `test_nemesis`. | None. |
| 7 | Rest | PARTIAL | Sleep and rent only in the Gull (`Tavern::rentRoom`, `sleepUntil`); WAIT is global. | No other bed; no player hunger or thirst; healing only by sleep, revive or spell. |
| 8 | Progress | BUILT | 20 use-trained skills (`SkillTrack::use`, `sim/social.cpp`), fixed attributes, 5 legend tracks x 4 rungs (`sim/legend.cpp`). **No character level exists.** | None (ruling 5). |
| 9 | **Save / load** | **MISSING** | The client's only file write is the controls file; `main.cpp:5573`: "There is no save." Byte `encode`/`decode` codecs exist for seven subsystems (social ledger, factions, nemesis, crime ledger, contraband sack, kit, ground items) and nothing writes them. The ward population, the Gull, the casebook, the clock and `SkillTrack` have no encoder. Java `WorldSaver` is still the only `.trojsav` writer. | A whole-state save (section 8.7). |
| 10a | **Ending** | PARTIAL | The last lead `drowned-hold` sets `closed_` (`sim/casebook.cpp:358`): a CLOSED row, a stinger, +30 Flame legend. | No epilogue, credits, win state; play just continues. |
| 10b | Game over | BUILT | The rope; "A NEW MAN". | None. |
| 10c | "The trail goes down" | MISSING | The last lead points into an undercellar; three sealed dungeon seams in the map, nothing behind them. | A short dungeon, or an honest end. |

The eight systems the update names, with file references:

| # | System | Status | What exists | What is missing |
|---|---|---|---|---|
| 11a | **Inventory** (carry, inspect, drop, use) | PARTIAL | `sim/items.cpp`, `items.hpp`: the registry off `content/raws/items/items.json` (23 items), the `Kit` (dense counts, hashed, byte-codec), ground list, load budget in drams (`loadCapDrams`); Character tile rows (`Session::kitRows`, `characterRows`); take (`takeNearestItem`), drop (`dropHighlightedKitRow`), search a corpse; `test_kit`, `test_kit_page`. | An inspect pane, categories and sort, **use** (there are no consumables: no food or potions in the registry), stacking rules, containers. The screen is a row list on the Character tile. |
| 11b | **Shops** | PARTIAL | `sim/shops.cpp`, `content/raws/shops/shops.json`, 3 counters (Brann's, Slop-Chest, Fenner's); `Tavern::buyFromCounter` / `sellToCounter` (`tavern.cpp` ~5286-5311); the haggle view in `dialogue_view`. | Placeholder stock and rates, **unlimited stock** (not from the simulation), a pawnbroker who asks no questions (`SESSION-2026-10-09` item 2), no trade screen with both inventories, no restock or price drift. |
| 11c | **Weapons and equipment** | PARTIAL | `ItemSlot { Hand, Body, Head, Feet, Trinket }`; worn rows and flat DR (`stepBrawl`: "COAT TURNS 2"); weapon class, damage and reach in items.json; `Kit::wear / bare`; viewmodel kinds fists, club, dagger, sword (`render3d/viewmodel.cpp`). | **One Hand slot, no off-hand**; armour has no per-piece coverage (COMBAT-SPEC's coverage-weighted AC is the named v2 ceiling); worn gear does not change a person's appearance; the first-person weapon is a placeholder per kind, not per item. |
| 11d | **Spells** | PARTIAL | `sim/spellbook.cpp`, `sim/spellforge.cpp`, `content/raws/spells/spells.json` (11 spells, targets SELF or TOUCH only; effects TEMPERATURE, VITALITY, ATTRIBUTE); `C` casts the readied one (`Session::castEquipped`); grimoire page; cooldown and held effects; `test_spellforge`, `test_grimoire`. | **No ranged effect or projectile, no light, no calm or frenzy** (no MORALE or LIGHT effect kinds); warmth spells are refused by design ("nothing in the ward reads heat on a body"); no magicka-style resource other than the wind pool; effects barely visible in the world. |
| 12 | **Morrowind-style dialogue** | PARTIAL | `sim/dialogue.cpp`: topic kinds (`Personal`, `WardTalk`, `History`, `Quest`, `Mastery`, `Trade`, ...), a TELL ME ABOUT tree, a tone dial POLITE/NORMAL/BLUNT, paged topic lists (nine to a page), 210 authored bark tables, disposition and faction memory in `SocialLedger`; `render/dialogue_view.cpp`, `menu_view.cpp`. | **No highlighted keywords in replies and no per-player learned-topic list** (a grep for keyword/learned state in `sim/dialogue.hpp` and `social.hpp` finds none); topics come from a person's own role and authored tables, not from what the player has heard; reply text is a flat block. |
| 13 | **Journal and topic index** | PARTIAL | `Session::journalPanelView`, `lettersPanelView`, `casebook_page.cpp`; five bloodletter letters; `J` ring tiles Character, Chart, Letters, Journal, Ward map, Grimoire. | No index, no cross-links between entries, no search, no record of where a fact was learned. |
| 14 | **Save and load** | **MISSING** | See row 9. | Everything; includes a title screen with Continue. |
| 15 | Menus and settings, audio | PARTIAL | Pause: RESUME, WAIT, CONTROLS, SETTINGS, QUIT. Options: four sliders and key rebinding. `native/src/audio/` is complete (SDL3 backend, mixer, Ogg decode, about 60 sound ids, music director). | No volume, graphics or difficulty setting. **The audio files are not in git** (`SoundBank` reads `content/art/kenney-all-in-1/Audio` and `content/art/lot/audio`, both ignored), so a fresh checkout is silent. |

**Doc and code disagree on the keys.** `README.md` and `PLAY.md` say `F` punches, `R`
sleeps and `Q` reads a lead. The defaults in `render/controls.cpp` are `E` use, talk or
look, `MOUSE1` swing, `MOUSE2` guard, `C` cast, `T` wait, `J` notes, `M` map, and no
default for `Q`, `F` or `R`. The code is right; the docs are stale.

**Not verified in this audit:** whether a human on foot can read the two band-18 leads;
which offensive spells exist beyond the ids in `spells.json` (none ranged, from the
targets); how many of the 42 authored notables are bound to live actors; the README's
"twenty-eight of the thirty-five K-sites" against the map's 25.

**Authored but not consumed by any C++:** `content/raws/jobs/jobs.json` (25 jobs),
`factions/leanings.json`, `fluids/water.json`, `reactions/`, `treatments/` and
`materials/*.json` (Java-era thermal and fluid raws; the native sim has no such system).
Not a cleanup item (never touch `content/`), but relevant to the art plan: the
materials files name the surfaces the procedural texture set will cover.

---

## 4. Cleanup audit (phase 1)

Every candidate below has evidence and a verdict. **SAFE** = removable now. **AFTER** =
removable once the named prerequisite holds. **KEEP** = looked at and left. Dates are
`git log -1 --format=%cs`. Static checks were throw-away scripts over `git ls-files`
(no build). **`content/` is never touched: where something in `content/` becomes an
orphan, it is listed as left alone.**

### 4.1 Java leftovers: `sim-core/`, `tools/`, Gradle

**Nothing automated runs Java.** `.dockerignore:15` is a deny-all (`**`) re-admitting
only `native/**`, `docker/**` and chosen `content/` folders; `docker/build.Dockerfile`
invokes no gradle, javac or JDK (the only hits are comments such as "the Java build ...",
lines 800, 958, 1282); no script calls gradle; there is no `.github/` or other CI folder.

**Three things still need Java:**

1. `tools/golden/generate.ps1` runs plain `javac` over all of `sim-core/src/main/java`
   and `tools/golden/GoldenVectors.java`, regenerating `native/tests/golden_java_vectors.hpp`
   (used by `test_world_hash.cpp:108,209,269`, `test_rng.cpp`, `test_engine.cpp`; header
   last regenerated 2026-08-19). It needs only 47 sim-core files (4.7k lines), including
   `actor/ActorRngStream` and `actor/NamedDraws`.
2. `ToolsLauncher import-map` (`tools/src/.../ToolsLauncher.java:70,103`) calls
   `TiledWorldImporter.java:143` → `new WorldSaver(...)`. The chain
   `tools/scripts/gen_docks_surface.py` → `docks_surface.tmx` → `import-map` →
   `.trojsav` is how the owner's worlds are baked (`docs/BASELINE-WORLD-HASH.md:285,328,350`).
3. Four `.claude/skills/*/SKILL.md` tell agents to run `gradlew.bat`.

**The README's three claims check out:** there is no C++ `.trojsav` writer (zero
hits for `WorldSaver|serialize|write.*trojsav` under `native/`; `native/content/src/`
is the reader plus `pack_write.cpp`, which writes content packs); there is no C++
Tiled importer (C++ only scrapes `.tmx` light markers, `src/tools/bake_lamps.cpp`);
the golden generator exists but lives in `tools/golden/`, outside the Gradle `tools`
module.

| Bucket | Size | Needed by | Verdict |
|---|---|---|---|
| sim-core slice: `world` (30), `world/io` (8, with WorldSaver/Loader/Hasher), `world/change` (3), `random` (2), `engine/SystemId`, `actor/ActorRngStream` + `NamedDraws`, `json` (11), `material` (10), `fluid` (3) | about 70 files, 7.7k lines | `import-map`, `tools/golden` | **KEEP** until a C++ world writer and importer exist |
| tools importer (3), tmx (18), validate (19), `ToolsLauncher` | about 3.9k lines main | `import-map` | **KEEP** (same) |
| `tools/golden/` | 2 files | golden header | **KEEP** |
| `tools/scripts/*.py` (6) | 7.4k lines | `gen_docks_signs.py` writes live `native/include/granadad/sim/docks_signs_generated.hpp`; `gen_docks_surface.py` writes the `.tmx`; the others write `content/art/{custom,sprites,faces}`, which native loads (`render/atlas.cpp`, `actor_sheet.cpp`, Dockerfile `COPY` lines 94, 100) | **KEEP** |
| `tools/lot-pipeline/` (24 files) | 5.9k lines | stages licensed art into gitignored `content/art/lot*`; the only record of what that tree holds | **KEEP**; its `unity/` subfolder is "blocked on the Unity licence" per its own README → **AFTER** the owner kills that lane |
| `tools/palette/` (11 files) | 655 lines + 293 test | `gen-palette` only; `content/maps/src/materials.tsx` is already committed | **AFTER**: owner confirms the palette is never regenerated |
| sim-core `actor/**` except the two golden files (about 82 files; with `type`, `job`, `spell`, `quest`, `faction` about 13k + 7.8k lines) | | Nothing automated. Referenced as "the behavioural reference" in C++ comments (`ward_actors.hpp:16,104,399`, `compound.hpp:165,504,757`), `native/README.md:5`, the four skills, ARCHITECTURE.md | **AFTER**: (1) the owner rules the Java build is no longer a parity reference; (2) the two actor files `GoldenVectors` imports move next to it or stay |
| sim-core `event/` (24), `progression/` (18), `engine/` except SystemId (12), `bark`, `bubble`, `world/{Walkability,ActiveSet,site/*}` | 62 files, 3.8k lines | nothing outside sim-core | **AFTER** (same) |
| sim-core tests | 132 files, 21.5k lines | Gradle only; no CI runs them | **AFTER** (same) |
| Gradle plumbing: `gradlew`, `gradlew.bat`, `gradle/` (wrapper jar 60 KB), `build.gradle.kts`, `settings.gradle.kts`, `gradle.properties` | small | skills, ARCHITECTURE §2, README | **AFTER** the Java tree goes |
| `.claude/skills/` (4 files, 44 KB) | | every one says "Run `./gradlew.bat build`" and edits `sim-core`; not referenced by Docker (only `.claude/worktrees` is, in `.gitignore:34`, `.dockerignore:5`, Dockerfile:182) | **AFTER** with the Java tree; keep the `.gitignore` worktrees line |

Totals: sim-core main 259 files / 32.5k lines, tests 132 / 21.5k, `tools/src` 68 / 7.3k.
About 25k lines (189 files) of sim-core main and all its tests are unreachable from the
two live consumers; about 7.7k lines remain required. `settings.gradle.kts` also
includes `content/build.gradle.kts`, which lives inside `content/` and stays as an inert
orphan (never touched).

### 4.2 Stale screenshots

`docs/frames/` is 766 tracked files, 47.9 MB. The software renderer drew every world
until "3D by default" (`f27ef510`, 2026-09-11), so frames dated before that show the
retired world pass (the HUD in them is still today's). Method: for each entry, grep
every tracked non-image, non-`content/` file outside the folder for `frames/<name>`.

| Bucket | What | Size | Verdict |
|---|---|---|---|
| 18 pre-3D folders nothing mentions | `copy-pass, empty, menu-tiles, p78-population, p79-ward, p81-questions, p84-gabri-skyrunner, parallel3, parallel4, parity, s85-controls, ship-travel, ship3, ship4, sprint1, sprint2, travel1, watch` (2026-08-05 to 09-10) | 9.3 MB, 158 files | **SAFE** |
| 63 loose pre-3D PNGs nothing mentions | of 91 `s4-...s16-*.png`, `district-{a,b,c,d}-*.png` | 4.7 MB | **SAFE** |
| 6 pre-3D folders only a script writes | `cut-seam, p77-controls, p80-roofs, parallel2, polish-1, polish` (written by `capture-p80.ps1`, `capture-parallel2.ps1`, `drive-windowed.ps1`, `hud-real-estate.py`) | 4.2 MB | **AFTER** the matching one-off scripts go (4.4), else they regenerate |
| 14 pre-3D folders cited by docs | `casebook, chargen, conversation, crosshair, demo, eviction1, map, p84-playthrough, panes, placement, ship, ship2, ship-eviction, voice-bio`, cited by HUD-REAL-ESTATE, SHIP-NOTE, DEMO, PLAY, CHARGEN-DAGGERFALL-DRAFT | 10.5 MB | **AFTER** the citing doc is archived (4.3) |
| 28 loose PNGs cited | PLAY.md §7, S5-S9 docs, HUD-REAL-ESTATE | 2.1 MB | **AFTER** (same) |
| current-renderer evidence | `3d-world-synty` (4.5 MB), `3d-slice-one` (2.1 MB), each with a README | 6.6 MB | **KEEP**; they are the *before* baseline for phases 3-6 |
| post-3D folders cited or script-written | `controls, rung, errands, justice, kit, shops, street, tone, voice, workbench` | 7.7 MB | **KEEP** |
| `combat`, `pull` | post-3D, unreferenced | 1.4 MB | **AFTER** owner glance |

Pre-3D is about 31.6 MB (66%); about 14 MB is provably orphaned (the SAFE rows),
another 14.7 MB goes with the docs that cite it. Git history keeps all of it.
**Broken now:** `PLAY.md:68,76,83,90,98,105,132` link seven images
`docs/frames/s14-chargen-*.png` that were never tracked (7 of PLAY.md's 15 image links
are dead; the real chargen shots are `docs/frames/chargen/after-*.png`).

### 4.3 Stale docs

| Doc | Evidence | Verdict |
|---|---|---|
| `docs/PLAN-v0.md` | The Java "god-view observer"; cites `.tmj`/`.tregion` formats that do not exist (`:58-59`); ARCHITECTURE.md:26-29,1170 already says the code wins. Inbound: ARCHITECTURE.md (3), TILE-ART-SPEC.md:26, COMBAT-SCREEN-SPEC.md:421 | **AFTER**: move to `docs/archive/`, fix those 5 references |
| `docs/S5-SKYRUNNERS.md` .. `S9-DARKSTREETS.md`, `S10-DEMO.md` | Per-sprint patch notes with gate counts (349..442) and software-renderer frames; S10-DEMO says PLAY.md supersedes it | **AFTER**: archive (they are the changelog) |
| `docs/SHIP-NOTE.md` | "Read this first" for the pre-3D eviction build; zero inbound references | **AFTER**: archive, its frames follow |
| `docs/design/COMBAT-SCREEN-SPEC.md`, `COMBAT-SPEC.md` | The JRPG combat screen, scrapped 2026-09-02 (DECISIONS:63); banner already at `COMBAT-SCREEN-SPEC.md:21-23` | **AFTER**: archive once the live parts of COMBAT-SPEC are folded into COMBAT-ACTION-SPEC |
| `docs/design/PLAY-MODE-SPEC.md`, `PROGRESSION-SPEC.md` | Java-era specs (`actAs`/`Persona`, `BehaviorPolicy`); native implements the use-XP, aptitude and attribute parts only | **KEEP** with a banner "Java reference, read ARCHITECTURE.md §0 for what is built" |
| `docs/DEMO.md` | `--demo` still exists (`main.cpp:1470`) | **KEEP**, link from README |
| `ARCHITECTURE.md` | §0 and §0.2 are honest; §2 (`:451-480`, five modules incl. `headless`, `client-observer`, "STATUS: BUILT"), §3 client and headless blocks (`:624,631`), §11.1 (`:1061-1086`, `./gradlew :client-observer:twinRunGate`) describe modules deleted 2026-08-09 | **KEEP**; **AFTER**-trim: banner, regenerate §2, §3, §11.1 against `native/src/gate/` |
| `docs/design/FACES-SPEC.md`, `FACEGEN-RESEARCH.md`, `docs/art/TILE-ART-SPEC.md` | Superseded in place; still describe the live generators | **KEEP** |
| `ELDER-SCROLLS-REFERENCE`, `UI-EA-SPEC`, `DEVIN-TEMPLATE`, `FLOW-SHOT-LIST` | No inbound links; design inputs (UI-EA-SPEC binds the HUD word budget, see 8.2) | **KEEP**, link from `SPEC-INDEX.md` |
| `Lore/` (1.3 MB: novel `.docx` and `.txt`, three `.html` including two `SECRET-*`, two scripts) | Cited by `docs/lore/*`, DOCKS-GAZETTEER, COMBAT-SPEC; not by any build file. `README.md:121` says lore lives only in the sibling repo, which commit `108823cd` (2026-10-14) made false | **KEEP** (owner canon); fix the README line; flag to the owner that the novel text and two SECRET files are in git |
| `steam/controller_config/` | Its own README says no Steamworks SDK is linked | **KEEP** (forward-looking) |

**Stale statements to correct** (these are text edits, done in phases 1 and 3, not
deletions): `README.md:92` ("software first-person renderer, SDL3 client": the window and
3D world are raylib; SDL3 is audio and gamepad only), `README.md:115` ("chunky readable
sprites"), `README.md:20,25` ("`--screenshot` ... no window": the shipped build opens a
window for one frame, only the headless build opens none, `main.cpp:3974-3978`),
`README.md:121` (Lore location), `native/README.md:5` ("Java build stays as the
behavioural reference"), `:40,330` ("renderer is software ... no GPU"), `:327` ("ctest total
349": the gate stamp says 1,367 cases), `:579,581,675` (`src/render3d/` is missing from the
layout list), `PLAY.md:7,14,34-37,447` (commit `dc47aab`, 679 cases, pre-3D frames, 7 dead
links), `docs/BASELINE-WORLD-HASH.md:272` ("see CLAUDE.md": no such file), and the key table
in `README.md` and `PLAY.md` (section 3.2).

### 4.4 Dead native code, scripts

| Candidate | Evidence | Verdict |
|---|---|---|
| `.cpp` files in no CMakeLists; headers included by nothing; tests not registered | Scripts over `git ls-files native`: 0 unlisted `.cpp` (no `file(GLOB)`), 0 of 123 headers unreferenced, every test file named in CMake | none |
| About nine functions defined, never called, even by tests | `Session::setFov` (`session.hpp:1317`, `session.cpp:1574`), `drawPickHighlight` (`dialogue_view.hpp:487`), `Workbench::clampCursor` (`workbench.cpp:439`), `render::actorSheetQuery` (`actor_sheet.cpp:161`), `ActorSheet::variantsFor` (`:338`), `CreationFlow::centreLines` / `pagePastLines` / `pageSlotCost` (`creation.cpp:632,2052,2124`); each re-checked with `grep -w` | **SAFE** (the phase re-greps before removing) |
| `granadad-audio-probe` | Referenced only by `CMakeLists.txt:452-455,1179` and `src/tools/audio_probe.cpp` | **AFTER** owner OK (a hand diagnostic) |
| `granadad-bake-lamps`, `granadad-pack-content`, `granadad-twin-gate` | Run by the Dockerfile, `package-standalone.ps1`, `verify-standalone.ps1` | **KEEP** |
| Software world pass (`WorldRenderer::renderFrame`, `--2d`, `FramePasses::world`) | Section 3.1 | **AFTER**: phase 2 extracts the shared types; phase 7 moves the workbench pane and three test files, then deletes it (about 1.1k lines) |
| `capture-p80.ps1`, `capture-parallel2.ps1` | No inbound references; one-off lane captures into folders in 4.2 | **SAFE** together with those folders |
| `hud-real-estate.py` | Only HUD-REAL-ESTATE.md cites it; "nothing in the gate runs it" | **AFTER** that doc is archived |
| `gate.ps1/.sh`, `verify-windows.ps1`, `package-standalone.ps1`, `verify-standalone.ps1`, `drive-windowed.ps1`, `shoot-{court,kit,controls,rung,street,3d-bodies-hands}.ps1`, `docker/`, `docker-compose.yml` | Referenced by README, compose, Dockerfile, docs | **KEEP** |

### 4.5 Phase 1's PR, as implied

Phase 1 (#21) is one PR containing only the SAFE rows: the 18 folders and 63 loose PNGs
of orphaned screenshots (about 14 MB), the two capture scripts, the nine dead
functions, the seven dead PLAY.md links, and the text corrections of 4.3 that do not
depend on later phases. Everything marked AFTER is a later, smaller PR the owner
can release one by one; the Java removal in particular waits for your ruling
(section 11, decision 1). No behaviour changes; the proof is the existing tests plus
the twin-run gate, unchanged.

---

## 5. Splitting `session.cpp` (phase 2)

**The problem, measured.** `native/src/render/session.cpp` is 20,416 lines and 980,037
bytes (the next largest file in `src/render/` is `hud.cpp`, 118 KB); `session.hpp` is
4,636 lines. The file holds 262 `Session::` member definitions, about 12 anonymous
namespaces and, from line 12,528 to the end, about 7,900 lines that are **not game logic
at all**: the scripted capture drivers behind `--trail`, `--flame`, `--roofs`,
`--skyrun`, `--burgle`, `--street*`, `--radiant`, `--shop`, `--threshold`, `--petition`,
and `runSmoke` itself (helpers such as `walkToTile`, `runStreetLine` at 17,433,
`runStreetAssault` at 17,785, `runRadiantLine` at 18,275, `runShopLine` at 18,703,
`runSmoke` at 18,917). `Session::drawFrame` alone is about 840 lines (11,690-12,520).

**The approach: keep one `Session` class, define its members across several
translation units.** C++ allows a class's member functions to live in as many `.cpp`
files as you like, so this is a pure move with no interface change: no caller, no test,
no header include changes. Free helpers that sit in anonymous namespaces today and are
shared by two or more of the new files move to one internal header,
`render/session_internal.hpp`; helpers used by one file move with it. A later,
separate step (not in this phase) may turn clusters of members into component structs
that `Session` owns; the phase does not attempt it, because that is a design change and
this one must be provably behaviour-neutral.

**The files** (line numbers are today's, from the audit; the PR re-derives them):

| New file in `src/render/` | Moves | About lines |
|---|---|---|
| `session.cpp` (stays) | construction, setters (`setAudio`, `setControls`), `camera()`, `sky()`, `weather()`, `tavernLights()`, `simHash` | 600 |
| `session_step.cpp` | `step` (4,956-5,935, the 980-line tick), `stepMany`, clock skips, `syncTavernToBody`, `syncWardToCalendar`, ground-plot sync | 1,300 |
| `session_movement.cpp` | `tryClimb`, `climb`, `tryDropDown`, `dropDown`, `vertical`, `jump`, crouch, `settleLanding`, `flyOutLeap` | 450 |
| `session_interact.cpp` | `interact`, `resolveInteract` (289 lines), `examine`, ground-item and corpse targeting, take and search, lift and steal, the prompt | 1,500 |
| `session_dialogue.cpp` | `talkToWard`, topic cursor and paging, `chooseTopic`, `chooseWardTopic`, `closeConversation`, forge, haggle and offer, `dialogueView` | 1,700 |
| `session_panels.cpp` | the J-ring views: `characterPanelView`, `mapPanelView`, `lettersPanelView`, `journalPanelView`, `stripCard`, `characterRows`, `kitRows` and kit verbs, `grimoireRows` and its cursor | 1,800 |
| `session_casebook.cpp` | casebook cursor and tabs, leads, pull, rung plate, case plates and fades | 1,500 |
| `session_cases.cpp` | courier sheet and eviction case steps | 450 |
| `session_combat.cpp` | `attackDown/Up`, `punch`, `castEquipped`, blocking, defeat and death ceremony, watch hooks | 700 |
| `session_court.cpp` | `serveSentence`, the court and hearing page state, the rope ceremony | 1,250 |
| `session_menus.cpp` | pause, options, keys page, quick bar, wait page | 1,400 |
| `session_map.cpp` | district map, travel plan, `placeBodyAt` | 750 |
| `session_hud.cpp` | the `*Line` HUD readouts, `syncPanelAnim`, `drawFrame` | 1,500 |
| `render/scripted/*.cpp` + `scripted.hpp` | the capture drivers, one file per line: `trail`, `flame`, `roofs_skyrun`, `burgle`, `street`, `radiant`, `shop`, `threshold_petition`, `smoke` | 7,900 |

The result is no file over about 2,000 lines, and the 7,900 lines of drivers stop
shipping inside the game's central class. The CMake source list in
`native/CMakeLists.txt` (explicit, no globs) gains the new files; the test-partition
check (`CheckTestPartition.cmake`) and the `native/` digest in the gate stamp change,
which is expected.

**Behaviour neutrality is proved, not asserted:**

1. The 1,367-case suite passes with no edit to any test file.
2. The twin-run determinism gate prints the same tavern and population baseline
   numbers (`docs/BASELINE-WORLD-HASH.md`; they are re-read at the start of the PR).
3. A fixed set of `--screenshot` captures (one per scripted line in `--help`, at
   `--time=8`) is taken on `main` and on the branch and compared **byte for byte**,
   HUD and world (the software HUD and the scene hash are exact; the 3D frame is
   compared on the same build configuration both times, so rlsw is still byte-exact at
   this phase).
4. `git diff --color-moved=dimmed-zebra` shows only moved blocks plus the CMake and
   new-header lines; each new file is its own commit so a reviewer checks a move at a
   time.
5. Phase 2 also takes the **shared-type extraction** that phase 7 needs: `Camera`,
   `SpriteInstance` and `LampGlow` move out of `world_renderer.hpp` into a neutral
   header (a rename plus include fixes, no logic change), so phase 7 is only a
   deletion. This is the first of the order adjustments in section 11.

---

## 6. Renderer architecture (phases 3-7)

### 6.1 Principle

Keep the layering that already works and change what is on each side of it:

```
granadad-sim              ints and fixed point only                       (unchanged)
granadad-render           HUD, pages, Session, day curve, lamp field      (unchanged)
granadad-render3d-core    pure C++ SceneDescription builders, hashed      (extended)
granadad-render3d-rl      the raylib adapter: shaders, passes, readback   (rewritten inside)
client                    window, input, audio, gamepad                   (unchanged shape)
```

The scene description is the contract: a pure function of tiles, sim state and clock,
hashed exactly. The adapter only draws it. Extending the description (normals, light
list, material ids, shadow casters) is how every new effect arrives, so the hash
keeps proving determinism and a test can pin each addition without a GPU.

`rl_backend.cpp` is 2,124 lines and by rule the only file that includes `raylib.h`.
Phase 3 splits it into `render3d/rl/*.cpp` (context, resources, passes, shaders,
models, capture, input); the rule becomes "only files under `render3d/rl/`", which
keeps raylib's global names (`Rectangle`, `Camera`, `CloseWindow`) out of everything
else exactly as before.

### 6.2 The key change: headless on llvmpipe instead of rlsw

Today headless frames come from raylib's own software rasteriser, `rlsw`
(`PLATFORM=Memory`, `OPENGL_VERSION=Software`). It runs **no shaders**; the code says so
over and over ("rlsw has not got a shader", "no shader, so rlsw and the GPU agree"),
and that is the whole reason lighting is vertex colour, fog is stacked translucent
shells and a flame's halo is a vertex-coloured fan. Per-pixel lighting, shadows,
post-processing and a fog shader are impossible until the headless path can run GLSL.
So phase 3 retires rlsw and runs the **same GL 3.3 renderer** headless on Mesa
llvmpipe, as the update requires.

**What I verified on the planning machine** (WSL Ubuntu, no display server, Mesa
26.0.8 runtime present): a 15-line C program that `dlopen`s `libEGL.so.1`, requests the
surfaceless platform (`EGL_PLATFORM_SURFACELESS_MESA`) and makes a core context current
returns `4.5 (Core Profile) Mesa 26.0.8`, renderer `llvmpipe (LLVM 21.1.8, 256 bits)`.
So a GL 3.3+ core context with no display and no GPU is available from Mesa. A
lavapipe (Vulkan) ICD is also installed; it is not needed for this route.

**What I did not verify** is how raylib gets such a context, and this is phase 3's
first task (3a, below), time-boxed, before any shader work. raylib 6.0 offers these
platform back ends in `src/platforms/`: GLFW (what the Windows exe uses), **SDL**
(`rcore_desktop_sdl.c`, which accepts an SDL3 CMake subdirectory target; this repo
already pins SDL3 `release-3.4.12`), RGFW, DRM, Win32 and Memory. Candidates, in order
of preference:

| Route | How | Risk |
|---|---|---|
| **A. raylib SDL platform on SDL's `offscreen` video driver** (`SDL_VIDEODRIVER=offscreen`) | One platform for Linux host and tests, no display server | Unverified whether SDL3's offscreen driver can create a *desktop GL 3.3 core* context (it is built for EGL, aimed at GLES). If not, A is dead. |
| **B. Xvfb** (a virtual framebuffer X server) plus raylib's Desktop (GLFW) or SDL platform, Mesa GLX on llvmpipe | Boring and widely used for CI; no GPU and no monitor | Needs `xvfb` in the docker image and the harness to start it; it is a display *server*, though a virtual one, and the update's wording is "headless ... on Mesa llvmpipe"; it is within it. |
| **C. Our own headless platform file** that creates the EGL surfaceless context (the verified code above) and hands raylib's rlgl its loader (`rlLoadExtensions`) | Truly displayless and deterministic | Patches third-party code or forks a platform file; raylib's `CORE.Window` state is assumed by many calls. Last resort. |

If A fails the spike in an afternoon, B is the answer and costs only a docker package.
Whichever route wins is recorded in `native/README.md` and in the phase-3 PR. The
Windows `.exe` is unaffected by this choice: it stays raylib Desktop (GLFW) on the
player's own GL 3.3 driver; it was never rlsw.

**What changes in the gate:** the docker host check stops compiling raylib with
`PLATFORM=Memory`, installs the Mesa runtime (`libegl1`, `libgl1-mesa-dri`,
`libegl-mesa0`, and `xvfb` if route B), and the compile-line assertions in the
Dockerfile (lines about 397-410, which assert `-DPLATFORM_MEMORY`) are rewritten.
**Frame tests become tolerance tests:** the eleven byte-exact `rlsw` cases in
`test_render3d.cpp` compare against committed golden PNGs with a per-channel maximum
difference and a mean-error bound, because Mesa and LLVM versions move low bits and an
exact hash across versions would be a flaky gate. The 18 pixel-exact HUD and page test
files are unaffected (the CPU framebuffer never touches the GPU). The scene hash stays
exact. A test that cannot create a context **fails loudly**, never silently skips, and
the build asserts by name that the frame cases ran (the same pattern the gate already
uses for the first-person case).

### 6.3 "The GPU only draws": how it is enforced

The simulation already has its own integer light field (`sim/stealth.cpp`) over the
same authored lamps on the same falloff curve, and every question of who saw whom goes
through one integer rule. That stays the only authority.

- **Nothing in `sim/` or `render3d-core` may read a pixel, depth value or GL query
  result.** The build greps for `ReadPixels`, `rlReadScreenPixels`, `LoadImageFromScreen`,
  `glGetQuery` and `glRead` outside `render3d/rl/capture.cpp`, as it already greps for
  `std::unordered_map`; a hit fails the gate.
- **The dependency arrow stays** `client → render3d-rl → render3d-core → render → sim →
  content`. No arrow points down from sim to render.
- **One mirrored curve.** The shader's lamp falloff is `P * (1 - (d/R)^2)^2`, the same
  function the sim's integer field uses (`render/lighting.hpp`). A test evaluates the
  C++ reference of the shader function against the sim field at a grid of points and
  pins the agreement, so what the player sees lit and what stealth reads stay one idea.
- **Shadows are cosmetic.** A sun shadow does not hide the player from the Watch; the
  sim's light field knows nothing of it. This is a design consequence the owner should
  accept explicitly (section 11, decision 4); the plan does not make shadows a stealth
  input.
- Projectiles, spell effects and light spells (phase 11) are decided in the sim; the
  renderer receives the resulting positions in the next description.

### 6.4 Frame pipeline

One `SceneDescription` per rendered frame, then, all through raylib's rlgl and
`LoadShaderFromMemory` GLSL 330, drawn into an offscreen render texture at the internal
resolution:

1. **Shadow pass**: depth-only render of chunk, static and actor casters from the sun
   or moon into a 2,048-pixel depth texture (`rlLoadTextureDepth` plus
   `rlFramebufferAttach`, both in raylib 6.0's rlgl). The light frustum is fitted to the
   near 48 tiles around the eye and snapped to texel size so shadows do not shimmer.
2. **Sky pass**: a full-screen triangle at far depth. Fragment shader: horizon-to-zenith
   gradient from the day curve, sun and moon discs along the clock's direction, stars
   faded in by darkness, one scrolling cloud-noise layer. Replaces the dome and the
   clear-colour trick.
3. **Opaque pass**: chunks, static pieces, actors, ground items; depth test and write,
   back-face culling, mip-mapped textures. Lighting, shadow lookup and fog in the
   fragment shader (6.6-6.7).
4. **Transparent pass**: water (6.8), glass, lamp halos and flame billboards, far to
   near, depth test on, depth write off.
5. **Viewmodel pass**: depth cleared, own near plane and fixed FOV, same lighting.
   Contract unchanged (`ViewmodelInstance`).
6. **Post pass**: tone curve and colour grade from the day curve, a light bloom
   (threshold, one blur at quarter resolution, add), a vignette. No SSAO, no motion
   blur, no depth of field: 2006 *Oblivion* shipped HDR bloom, not those.
7. **Overlay**: the CPU HUD `Framebuffer` uploaded as a straight-alpha texture,
   point-filtered, integer-scaled; unchanged, which is what keeps the 18 pixel-exact
   test files exact.
8. **Present, or read back** to the capture buffer for `--screenshot`: the same code
   path either way.

Internal resolution is a design knob (today's frames are 640x360 upscaled 2x, which
gives the chunky 2006 look cheaply, and llvmpipe renders it in a few hundred
milliseconds). The plan does not choose it; `--width/--height/--scale` keep working.

### 6.5 Tile data to meshes

Kept from today: chunking, the shared cell classifier, face culling, real door gaps; the
tile mesh *is* the building the sim collides with. Changed:

- **Vertex data** gains **normals** and a baked ambient-occlusion value (cheap corner
  darkening from neighbouring cells), because per-pixel lighting needs normals and
  streets look flat-lit without AO. Light stops being baked into vertex colour (the
  colour stage `colourChunk()` shrinks to the AO and wetness terms). Indices stay 16-bit
  because raylib's mesh uses `unsigned short`; a 16x16 chunk is nowhere near 65,535
  vertices, and the mesher asserts it.
- **Materials**: the mesher already knows each face's material and form. It emits a
  material cell and **world-metre UVs**, so a texture repeats every metre instead of
  stretching one tile over a whole storey (the cause of today's pixel-soup walls). The
  shader tiles inside an atlas cell with a gutter. A GL 2D texture array would be
  cleaner but raylib's API does not expose one; if the atlas shows bleeding at the
  gutter, phase 4 reaches for rlgl directly.
- **Ramps and stairs** become real wedges and stepped boxes (the mesher's listed gap).
  The classifier change is shared with the old raycaster, so it lands behind a flag
  until phase 7 deletes the raycaster.
- **Roofs, eaves, chimneys**: the data has roof cells and wall tops but no pitch. A
  deterministic roof pass over enclosed footprints adds gable or hip roofs, overhangs
  and chimneys as extra triangles on top of the tile mesh, never changing what the sim
  collides with. This is the biggest silhouette win.
- **Trim**: window and door frames, string courses, plinths as extruded quads by rule,
  following `content/raws/world3d/docks-pieces.json`, which already encodes where they go.
- **Interiors**: ceilings already exist (bottom faces of slabs). Phase 4 adds floor
  materials by room use, beams, and a light cap so a room is lit by its own lamps and
  not the street's.
- **Quay and harbour**: planks, bollards, pilings, hulls as the catalogue's kit when
  the glTF files exist and procedural props when they do not (section 7).
- **Distance**: the district is 256x192 tiles, small enough to draw whole under fog.
  Chunks beyond about 48 tiles use a simplified mesh (tops and sides, no trim or AO),
  chosen per chunk with hysteresis. A fogged skyline ring (the Saltgate, the Mission
  turret, the hills) comes from landmark heights in `DOCKS-GAZETTEER.md`.

Meshes remain a pure function of tile bytes, lamps and clock; `test_chunk_mesher.cpp`
keeps pinning that. The scene hash changes on purpose in these phases; the sim hashes do
not.

### 6.6 Lighting

Per-pixel, forward, one lit pass, no deferred buffers. This is the 2006 model.

- **Sun and moon**: one directional light from the clock, with colour and intensity from
  `SkyState`; Lambert diffuse; a hemisphere ambient (sky colour above, ground bounce below)
  replaces the flat multiplier.
- **Point lights**: lanterns, torches, braziers, hearths, door lamps. The source of
  truth is unchanged: the 27 baked lamps (`*.lamps.json`) and `Session::tavernLights()`.
  Each chunk gets the lights within reach, capped at the 8 nearest, uploaded as a
  uniform array (raylib 6.0's `SetShaderValueV`); the fragment shader loops them.
  Flicker is a hashed function of lamp id and a render clock, never of sim state.
- **Specular**: Blinn-Phong with a per-material gloss, for wet cobbles and water only.
- **Emissive**: lamp glass, lit windows, flames (the existing `pane` and halo rules).
- **Shadows**: the sun or moon, from 6.4 step 1, PCF 3x3. Lamps cast none in the first
  pass (8 shadowing point lights is not 2006 either); a single shadowing lantern near
  the player is a later option, not planned.
- **Player-carried torch**: ships as a render-only light in phase 5. If it is ever to
  change who can see the player, the sim must grow a light source, which moves hashed
  state and needs a declared baseline move (`docs/BASELINE-WORLD-HASH.md`); that is a
  gameplay decision for the owner (section 11, decision 5), not part of this plan.

### 6.7 Sky and fog

- **Distance fog** in the fragment shader: `f = 1 - exp(-d * density)`, mixed to the sky
  colour *in that view direction*, so fog at the horizon matches the sky exactly.
  Density and colour come from `SkyState` (`fogDistance`, 26 tiles today) plus the
  weather veil value. The banded veil shells and the no-fog-on-a-clear-day workaround
  are deleted. The weather rules (clear, overcast, fog, wind; a pure function of seed,
  day and clock; the sim never reads them) carry over unchanged.
- **Day and night**: one clock drives sun direction, sky, ambient, lamp mix and star
  visibility from the existing day curve; sunrise and sunset come from the same curve.

### 6.8 Water

Harbour and basins are already top faces at tide height. They get a shader: two
scrolling procedural normal layers, a Fresnel mix of sky colour and a depth-based dark
tint, sun, moon and lamp glints, shore foam from a per-vertex shore distance the mesher
computes. No refraction, no planar reflection; a sky-only reflection is the 2006
appropriate step. A large water plane runs to the fogged horizon.

### 6.9 Characters and the first-person view (phase 6)

The repo has about 690 people and a placeholder box figure. The art plan is in section
7; the renderer side is:

- **Skeletal animation, hand-written**: a 15-bone hierarchy with the eight existing
  `ActorClip`s (idle, walk, punch L/R, block, hit, recover, death) plus a ninth, **talk**
  (clip index 10; 8 and 9 are reserved by the asset contract, which says nothing may be
  inserted in the middle). Evaluated on the CPU for near actors and drawn through
  raylib's animated-mesh path (`UpdateModelAnimation`, present in 6.0); far actors use a
  rest pose, as now. `clipFrame` stays a pure function of sim integers.
- **Appearance varies per person** through a parameter set (build, height, skin, hair,
  garment colours by `WardType` and faction, worn gear from the Kit), a pure function of
  the actor id and sim state, part of the scene hash.
- **First-person view**: hands plus the *equipped item* (phase 9 supplies the mapping from
  item to viewmodel model), pose by the existing viewmodel state machine, lit by the
  light at the player's tile.
- **Licensed rigs** (the Synty and Malbers `.glb` files) still load through raylib's own
  glTF support when present, so the owner's asset lane is not thrown away.

### 6.10 Headless proof, as a ladder

1. **Scene hash**: exact, cross-platform, no renderer needed.
2. **Frame tests on llvmpipe**: golden PNGs with a stated tolerance (6.2).
3. **Stats**: draw calls, triangles and light count per frame, asserted.
4. **Before and after captures in every PR**, produced by the same `--screenshot`
   command line on the base commit and on the branch.
5. **Windows**: the player's GPU path is checked by the owner (section 10).

### 6.11 Phase 7: retiring the old renderer

After phase 2 has moved the shared types, retiring renderer A is the deletion of
`WorldRenderer::renderFrame`, `voxel_classify` users that only it needs, `--2d`,
`FramePasses::world` and `runSmoke`'s redundant software frame, plus: the workbench
game pane moves onto the render3d backend (`workbench.cpp:1455`); the three test files
that read the software world (`test_render.cpp`, `test_signage.cpp`,
`test_tavern_render.cpp`, and `test_kit_page.cpp:462`) are rewritten against the 3D
path or against the HUD alone; every scripted `--screenshot` line in `--help` is run
before and after and shown to produce its capture. Then `README.md`, `PLAY.md` and
`ARCHITECTURE.md` describe the new renderer. About 1.1k lines go.

---

## 7. Art plan (`content/` holds only 2D sprites)

**Checked.** Tracked `content/art/` is the Kenney 1-bit packs and CC0 UI prompts, the
owner's `custom` pixel tiles, face busts and light masks, plus the `raws/`, `maps/` and
`world3d` catalogue. There is **no tracked 3D model and no painted or photographic
texture.** The Synty and Malbers glTF exports sit under `content/art/lot-3d/` and
`lot/`, which are gitignored (licensed; `docs/asset-manifest-lot.md`). A fresh checkout
therefore draws the chunk meshes and box people, as the current frames show.

So the art plan is **procedural first, licensed optional**, and every new file is a new
file under a new folder (`content/art/oblivion/`); nothing existing is edited.

1. **Materials.** A committed generator (C++ under `native/src/tools/`, so it builds
   with the project and is deterministic; the Python generators under `tools/scripts/`
   are the precedent) writes small tileable PNGs, 128x128 or 256x256, from value noise
   and Voronoi: harbour cobble, packed earth, plank deck, wet wood, plaster, brick,
   granite, reman concrete, thatch, slate and clay roof, iron, rope, cloth, plus a water
   normal pair. Colours come from the authored material raws
   (`content/raws/materials/*.json`) so the look follows the owner's canon of what each
   surface is. Outputs are checked in (a few hundred KB) and regenerable; the PR shows
   the generator and a contact sheet.
2. **Buildings.** The tile mesh plus the roof, trim and window rules of 6.5. No model
   files are needed.
3. **Props.** Barrels, crates, lanterns, bollards, nets, rope coils, signs, benches and
   the Gull's furniture as small parametric meshes (lathe and extrusion) from a prop table
   in C++, used wherever `docks-pieces.json` names a piece whose glTF file is absent. With
   the licensed files present, those draw instead; the catalogue and its placement rules
   already do this.
4. **People and hands.** A procedural low-poly humanoid, about 400-700 triangles (torso,
   pelvis, head with a small painted face texture, limbs, hands and feet as bevelled
   boxes), built from a parameter set, plus equipment meshes (blade, club, dagger, coat,
   cap) built the same way. This is "simple low-poly people" with no licence question.
5. **Audio.** Not a renderer item, but the same hole: the files are gitignored. The plan
   proposes a small CC0 or self-generated set under `content/audio/` (footsteps, doors,
   hits, ambience beds, a few stingers) so a clean checkout is not silent. Third-party
   audio needs the owner's provenance call (section 11, decision 3).
6. **Optional.** If the owner supplies or licenses real assets later they slot in through
   the existing catalogue and raylib's glTF loader.

**Look target, stated so it can be argued with:** low internal resolution; muddy,
saturated palette; strong warm point lights against a cool ambient; heavy harbour fog;
bloom and a tone curve and nothing else in post. "Roughly Oblivion 2006, not beyond it."

Anything that is not original or CC0 has its source and licence recorded in a
provenance note next to the file, as `docs/asset-manifest-lot.md` does for the licensed
packs.

---

## 8. The gameplay systems: design and UI (phases 8-15)

### 8.1 Rules every system phase obeys

1. **State lives in the simulation as integers** (the existing pattern: `Kit` counts, `SocialLedger`,
   `CrimeLedger`). Render and UI read it; they never own it.
2. **Each phase ships its own save codec** (`encode`/`decode`, versioned) and a round-trip
   test for the state it adds, instead of leaving all of it to phase 14. This is the
   second order adjustment in section 11, and the reason is concrete: the audit found
   seven codecs that already exist and nothing that writes them, and a save phase that has
   to retrofit six new systems at once is the one most likely to slip.
3. **Baseline policy.** A phase that adds hashed sim state declares in its PR whether the
   tavern or population twin-run baseline moves, and follows the re-bless procedure in
   `docs/BASELINE-WORLD-HASH.md` (one declared move per build, run A equals run B, both
   toolchains). The current numbers are re-read from that file at the start of each
   phase; the last recorded in `DECISIONS.md` are tavern `0x63F354D02A6B600F` and
   population `0x499B2D62A49840B5`. A phase that does not touch the workload says so and
   shows the numbers unchanged.
4. **UI is the existing terminal-panel register.** The HUD is drawn on the CPU into a
   `render::Framebuffer` and composited over the 3D frame (`render/panel.cpp`,
   `menu_view.cpp`, `dialogue_view.cpp`, `casebook_page.cpp`), and that is kept. The
   registers' rules bind: master/detail strip cards; the centre of the screen stays
   empty under every open page (`test_hud_diet` enforces it by pixel diff); every
   visible choice has a key; the J notes ring pages on the bumpers and brackets.
5. **The word budget is a conflict to resolve, not ignore.** `docs/design/UI-EA-SPEC.md`
   binds a 50% word cut (global cap 1,785 words across the census surfaces) and the
   owner's brief now asks for wiki-like, polished dialogue text. These are compatible
   only if the budget is read as a limit on *chrome* (labels, instructions, hints) and
   not on authored prose. The plan assumes that reading and asks the owner to confirm it
   (section 11, decision 6). Authored prose goes in the detail pane, never in labels.
6. **Every screen works on keyboard and pad**, with the existing action vocabulary
   (`render/controls.cpp`), and is photographed by a scripted `--flag` through the real
   verbs, like the existing ones.

### 8.2 Inventory (phase 8)

*Today*: 11a. `Kit` rows with weight in drams, worth in royals, wear and quick-slot
marks; take, drop, search corpse; load budget. *Sim work*: an item `use` verb with
effects expressed in the existing integer effect vocabulary; **consumables** (bread,
ale, bandage, a lantern oil flask; new rows in a **new** file
`content/raws/items/items_consumables.json`, since `items.json` is never edited) with
stack counts; an inspect record (description, weight, worth, damage or DR, owner, theft
flag); a container idea only where a Gull strongbox already has one. Weight by drams
stays (it fits the design and the load speed rule `loadSpeedScaleQ8`); slots would need
a rule the sim lacks.

*Screen*: the Character tile of the notes ring, as today, gains a master/detail split.

```
+---------------------------+-----------------------------+
| IN HAND   CUDGEL     +    |  CUDGEL            40 dr  6 |
| BODY      COAT       +    |  OAK, HANDLE-WORN. EVERY    |
| HEAD      -               |  HOUSE KEEPS ONE UNDER...   |
| ...                       |  BLUNT  7-9   REACH 2-8     |
| CARRIED  (sorted)         |                             |
|  ROPE   x1        48 dr   |  [E] USE  [X] DROP  [ENT] EQ|
|  BREAD  x3         9 dr   |                             |
| LOAD 151 / 240 DRAMS      |  THEIRS (theft)  /  HOT     |
+---------------------------+-----------------------------+
```

Left: the list, grouped WORN then CARRIED, with the load line at the foot. Right: the
inspect pane for the highlighted row. Verbs on keys, no sub-menus: `Enter` wear or
bare, `E` use, `X` drop, left and right walk the quick slot (existing grammar). Screenshots:
full list, an item with a use verb, a stolen item showing THEIRS, the overload row.

### 8.3 Weapons and equipment (phase 9)

*Today*: 11c. *Sim work*: insert an **off-hand slot**. `ItemSlot` is an insert-only enum
(DECISIONS "the second clean break of the insert-only enum rule"), so the PR names the
break and updates every switch, the codec version and the tests that pin the 24 and 25
character rows; one-handed, two-handed and shield classes in a new
`content/raws/items/items_gear.json`; armour pieces for head, body, hands, feet with
per-piece DR; reach and speed per weapon class feeding the existing integer swing
machine. Coverage-weighted AC and per-part wear (COMBAT-SPEC) stay the named v2
ceiling; this phase uses flat per-piece DR summed as `stepBrawl` already does.
*Appearance*: a pure function of the Kit's worn rows reaches the scene description (the
cap on the head, the coat colour, the blade in hand). *First person*: `ViewmodelKind` is
keyed by item, not by four weapon classes. *Screens*: the same Character tile with
slot rows (HAND, OFF-HAND, HEAD, BODY, HANDS, FEET, TRINKET) and a comparison line in the
inspect pane (`DR 2 -> 4`, `DMG 7-9 -> 6-10`). Screenshots: three loadouts on the same
street, first-person views of each, and the taproom figure wearing what the sim says.

### 8.4 Shops and trading (phase 10)

*Today*: 11b. *Sim work*: stock **from the simulation**: each counter holds a hashed
stock list, restocks on the day clock from the ward's own production (the compound
farms and the K-site jobs already model it), and prices follow the existing barter and
ward-influence rules (`sim/barter.cpp`, faction influence on mugs) rather than fixed
rates; the player's sale reduces stock and raises the shopkeeper's purse; the
pawnbroker's questions follow the crime ledger (heat). A haggle uses the existing
skill-against-skill rule and extends past ale and rooms. New counters beyond the three
placeholders come from the authored K-sites (a chandler, a rope-walk, an apothecary) in
`content/raws/shops/shops_extra.json` (a new file, with provenance and canon notes).
*Screen*: a two-column trade page (the shopkeeper's stock left, the player's carried
rows right), price and disposition in the detail strip, `Enter` buy or sell, `Tab`
swaps column, a running total at the foot, the haggle counter as the existing sub-mode.
Screenshots of a complete buy and a complete sell, and a refused sale.

### 8.5 Spells (phase 11)

*Today*: 11d. *The effect vocabulary is the constraint.* The spellforge prices
compositions out of effects `TEMPERATURE`, `VITALITY`, `ATTRIBUTE` and targets `SELF`
and `TOUCH`. A "small set (light, heal, a ranged attack, calm or frenzy)" needs three
**new effect kinds**: `LIGHT`, `MORALE` (calm or frenzy) and a ranged delivery
(`BOLT`). Each is added to the cost model, the content schema and the tests as a
declared, additive change; existing spell files are unchanged and new spells go in
`content/raws/spells/spells_core.json` (new file, canon anchor and provenance on each
row, the way `spells.json` does it).

- **Light**: a sim-side `LIGHT` hold, so it can change who sees the player (an integer
  radius in the stealth field); the renderer shows a light source at the same place.
  This is the one spell that interacts with detection, which is exactly why it is sim.
- **Heal**: `knit_the_skin` and `close_the_cut` already exist; the set picks one as
  the player's starter.
- **Ranged attack**: a projectile in the sim, fixed-point position and velocity,
  advanced on the tick, hit resolution against bodies on the sightline machinery the
  melee swing already uses; the renderer draws the bolt from the sim's position. This is
  the largest sim addition in the loop and it moves the baseline.
- **Calm and frenzy**: a hashed per-actor morale offset read by the street-panic and
  brawl rules; the renderer shows nothing but the body's behaviour and a small effect
  mote.
- **Cost**: the existing wind pool plus the cooldown the spells already carry; no new
  magicka bar.
- **Visible effects**: particles and light (render-only, from the sim's events), plus
  the HUD countdown that held effects already have.

*Screen*: the existing grimoire page plus a quick-bar of six, with the cost and cooldown
line; the casting key stays `C`. Screenshots: each of the four spells on screen.

### 8.6 Morrowind-style dialogue and the journal (phases 12 and 13)

*Today*: 4a and 12. The topic system is built; what is missing is the **keyword layer**.

**Data model.** A *topic* is a canonical id with an authored title, an *answer table*,
and the facts it *teaches*. Replies are authored text with inline keyword markers, for
example `The harbourmaster keeps the {tide-book} locked; ask about the {outfall}.`
Rendering a reply highlights each marker whose topic exists; selecting one asks that
topic on the spot. **The player knows a topic** when it has appeared in a reply they
heard, in a letter they read, or on their notes; a per-player hashed set (`LearnedTopics`,
integers only, byte-codec) records it with the turn it was learned and who said it. **A
learned topic can be asked of anyone**, and each person answers from their own table, or
deflects in their own voice:

```
answer(person, topic) = first match of
   1. a row keyed (topic, person)                     -- their own business
   2. a row keyed (topic, job)                         -- a docker knows the tides
   3. a row keyed (topic, faction) at their disposition band
   4. the topic's general row, if their rumour licence covers it
   5. a deflection row for their disposition and tone
```

This reuses what exists: `TopicKind`, the rumour licences in
`content/raws/rumors/rumors.json` (the whole gate today), faction and disposition from
`SocialLedger`, the tone dial. **Nothing is a persuasion check**; the repo's standing
rule, "if you cannot get an answer you are standing in front of the wrong person", is
kept.

**Text.** New dialogue goes in new files under `content/raws/dialogue/` (for example
`topics.json`, `answers_job.json`, `answers_faction.json`), each row carrying a
`canon` anchor into the novel and a `provenance` note, matching `spells.json`'s
convention. The novel text is in the repo (`Lore/`), so canon is checkable without the
sibling repository; the novel always wins. Scale: about 60 topics, each with a general
row, 8 job rows and 5 faction rows is a few thousand lines of prose, so the phase ships a
**vertical slice** (about 25 topics around the bloodletter case, the Gull and the
harbour) and the loader and a linter that fails the build on a marker with no topic, a
topic with no answer, or a row without provenance. The rest is a content pass the owner
can steer.

**Layout.** The conversation page keeps the strip-card shape: the speaker and
disposition above, the reply in a wide text pane with keywords in the accent colour and
the cursor on one, the topic list below. Keyword navigation: `Tab` and arrows move
between keywords, `Enter` asks; the existing `1`-`9` and paged list stays. Polish items
the phase owns: word wrap at the pane width with no widows, a typographic line height,
paragraph spacing, a visible "NEW" mark on a topic the first time it appears, and the
reply history scrolling in the pane. Screenshots of a conversation with several
keywords highlighted, the same topic asked of two different people, and a deflection.

**Journal and topic index (phase 13).** The J ring's Journal tile becomes an index over
the same data: every learned topic, person, place and lead, as a cross-linked entry
(a topic's entry lists who told you, when, what they said, and the leads and places it
mentions; a person's entry lists what you learned from them and the topics they
refused). Alphabetical index and a filter by kind (the Chart page's tab grammar), search
by typing, a "back to the conversation" link that re-opens the speaker's page where they
can still be found. The casebook stays what it is and is linked from the index, not
replaced. Sim state: the `LearnedTopics` set above plus an entry log; everything else is
derived. Screenshots of the index, an entry and a search.

### 8.7 Save and load (phase 14)

**The two roads** (`docs/SESSION-2026-10-09.md` item 5): encode all state, or record
inputs and replay. Recommendation: **encode.** Replay is cheap to write and fragile in
the one way that matters here: every gameplay change in any later patch breaks old saves,
and a long session costs the replay time: `native/README.md` measures about 31 ms per ward
tick at night in a Debug build, and a month of game time is about 2.6 million ticks. Encoding is more work once and
robust afterwards.

- **A versioned container** (a new `.granasav` file, or `.trojsav` sections if the owner
  prefers; the Java writer stays the only writer of *map* files). Magic, format version,
  the revision stamp, then length-prefixed, CRC-checked sections, one per subsystem,
  little-endian by hand like the existing codecs, so a save is byte-identical across
  toolchains.
- **Sections**: the clock and calendar; the player body and sheet; `SkillTrack`;
  `SocialLedger`, `FactionBook`, `NemesisBook`, `CrimeLedger`, `ContrabandSack`, `Kit` and
  ground items (the seven codecs that exist); the Gull (`Tavern`); the ward population
  (661 actors; the encoder mirrors `hash_into`, field for field); the compound roll, the
  casebook and questlines, radiant boards, `LearnedTopics`, shop stock, spells and
  held effects. Phases 8-13 each add theirs.
- **Where it is written**: next to the exe (the controls file's precedent), three manual
  slots plus an autosave on sleep, on entering a settlement and on quitting.
- **The proof**: *save, load, run N ticks* must equal *run N ticks straight* by world
  hash, on both toolchains, at several hours including 01:00 (the expensive night tick).
  The existing twin-run gate becomes a save-load twin run. Every section has a
  round-trip test and a version-bump test. A corrupt or newer file is refused with a
  message, never loaded.
- **The title screen** (1a) arrives here: NEW, CONTINUE, LOAD, SETTINGS, QUIT, with the
  save list; pause gains SAVE and LOAD rows.

### 8.8 Full loop and polish (phase 15)

An **ending** (10a): closing the case triggers an epilogue page (what the ward does
next, by the legend tracks and the nemesis state, authored text, no new mechanic), then
"continue in the district" or a new game; the credits are a plain page. The **undercellar**
(10c) is either a short authored dungeon behind one of the three sealed seams or an
honest end card, the owner's call (section 11, decision 7). Also the audit's loose
ends: the two band-18 leads proven reachable by a walking test; rentable beds beyond the
Gull; door open and close with the existing `DoorOpen/Close` sounds; settings for
volume, music and difficulty; a day-length setting (the 24-hour real-time day, 2d);
player hunger as a decision, not a default (section 11, decision 8); the stale
`README`/`PLAY` key tables and screenshots refreshed; and a scripted full playthrough
`--playthrough` that plays creation to ending to save and load and photographs each
beat.

---

## 9. Phases

Order follows issues #21-#35. Each phase is one issue and, unless it says "two PRs",
one reviewable PR that builds, passes the existing tests and the determinism gate
(`scripts/gate.ps1` / `gate.sh`, then `verify-windows.ps1` on Windows), and carries
before and after screenshots of the same scenario, viewport, clock and state, with the
base and branch commits named. A phase starts only on `@liefwarrior-bot go`.

**Standard captures** (existing flags; the "before" set is taken on the base commit with
the same command line): street at morning `--smoke=0 --hold --time=8`, dusk `--time=19`,
night `--time=22`; the taproom `--threshold=gull --time=20`; the piers `--threshold=piers
--time=9`; a look-up and a look-down with `--pitch=45` and `--pitch=-45`. Phase-specific
extras are named below. The `docs/frames/3d-slice-one` and `3d-world-synty` sets (kept in
phase 1) are the historical baseline.

**Legend for the right-hand column:** SIM = touches hashed simulation state (declares
whether a baseline moves); WIN = needs the owner on Windows (section 10).

| # | Issue | Phase | One-sentence scope | Proof beyond the standard | Flags |
|---|---|---|---|---|---|
| 1 | #21 | Cleanup | The SAFE rows of section 4.5 only. | Tests and twin run unchanged; a script lists every removed path with its grep evidence in the PR. | none |
| 2 | #22 | Split `session.cpp` | Section 5: the member files, `render/scripted/`, the shared-types extraction. | Byte-identical captures across every scripted line; the suite with no test edits. | none |
| 3 | #23 | GPU renderer foundation | 3a headless GL on llvmpipe (6.2) and the gate changes; 3b the shader pipeline, normals in the description, `render3d/rl/` split, the DECISIONS amendment. | Golden-image tests with tolerance; the readback ban grep; same-scene before and after. | WIN |
| 4 | #24 | World geometry and materials | Section 6.5 and the material generator of section 7. | Mesh determinism tests; the scene hash moves on purpose; the standard set from the same viewpoints. | WIN |
| 5 | #25 | Lighting and atmosphere | Section 6.6-6.8: per-pixel lights, shadows, sky, fog, water, post. | The sim-versus-shader falloff agreement test; morning, dusk and night set. | WIN |
| 6 | #26 | Characters and first-person view | Section 6.9 and the procedural people of section 7. | Animation is a pure function of sim integers (a test); busy street and taproom set. | WIN |
| 7 | #27 | Retire the old renderer | Section 6.11. | Every `--help` scripted line run before and after; doc updates. | none |
| 8 | #28 | Inventory | Section 8.2. | `test_kit*` extended; save codec round trip. | SIM |
| 9 | #29 | Weapons and equipment | Section 8.3. | The 24/25-row character pins re-blessed in the PR; appearance hash test. | SIM |
| 10 | #30 | Shops and trading | Section 8.4. | Stock and price determinism over a simulated week; full buy and sell captures. | SIM |
| 11 | #31 | Simple spells | Section 8.5. | A declared baseline move for the projectile and morale state; each spell captured. | SIM |
| 12 | #32 | Morrowind-style dialogue | Section 8.6, two PRs: 12a sim, data and linter; 12b page and layout. | The content linter in the gate; a conversation capture per person type. | SIM |
| 13 | #33 | Journal and topic index | Section 8.6. | Index built from a scripted conversation; link-back test. | SIM |
| 14 | #34 | Save and load | Section 8.7, the container, the title screen. | Save-load twin run at four hours on both toolchains. | SIM, WIN |
| 15 | #35 | Full loop and polish | Section 8.8, and a scripted playthrough. | `--playthrough` end to end with captures and a reload mid-run. | SIM, WIN |

### Per-phase detail

**1. Cleanup.** Files: the 18 folders and 63 PNGs under `docs/frames/`, `scripts/capture-p80.ps1`,
`scripts/capture-parallel2.ps1`, nine functions in `render/` and `workbench/`, `PLAY.md`'s
seven links, README and native/README text fixes that do not depend on later phases.
*Not* in it: the Java tree, the `.claude` skills and the doc archive moves, which are
separate small PRs under the same issue once the owner rules (section 11, decision 1);
nothing under `content/`. Risk: a function the grep missed; mitigation: the build and the
full suite, and a re-grep inside the PR.

**2. Split `session.cpp`.** As section 5. Risk: a merge conflict with anything else
touching `session.cpp` while the move is open; mitigation: the move is done in one sitting
against a freshly merged phase 1, and nothing else opens until it merges.

**3. GPU renderer foundation** (two PRs under one issue). *3a*: the headless spike
(6.2 routes A, B, C), the docker changes (Mesa runtime, `xvfb` if needed, the raylib
configure and the compile-line assertions), the readback-ban grep, golden-image
infrastructure and the 11 `test_render3d` cases re-pointed at llvmpipe; no visible change.
*3b*: the shader pipeline (`LoadShaderFromMemory`, uniforms, render texture, the offscreen
and overlay path), vertex data with normals, `rl_backend.cpp` split into `render3d/rl/`,
`render3d` already default so the old renderer stays behind `--2d` as the issue says, and
the **DECISIONS.md amendment** of section 2 plus the README and ARCHITECTURE corrections.
The 3b frame is intentionally near-identical to today's (the same vertex-colour look, now
drawn by a shader), so the diff is reviewable as a refactor; effects start in phase 5.
Windows: the exe must still start and draw on the owner's GPU. The spike's result decides
the Dockerfile wording; if route A fails the phase continues on B without a design change.

**4. World geometry and materials.** Roofs, wedge stairs and ramps, trim, interior floors
and beams, quay detail, the LOD ring and skyline, the procedural material set. Candidate
split into two PRs if it grows past review size: 4a geometry, 4b materials and LOD.
Risk: wedge ramps change what the classifier reports, which the raycaster shares; the
change is flagged until phase 7. The simulation is untouched.

**5. Lighting and atmosphere** (candidate two PRs: 5a lights, sky, fog; 5b shadows, water,
post). The agreement test of 6.3. Risk: llvmpipe speed with a shadow pass and a bloom at
the capture resolution; mitigation: the capture stays at the internal resolution (640x360
by default) and the budget is measured and written down in the PR.

**6. Characters and first-person view** (candidate two PRs: 6a people, 6b hands and
viewmodel). The `talk` clip for the dialogue phase is added here so phase 12 only wires
it. Risk: a crowd of 200-300 bodies at 500 triangles each is 100-150k triangles of
animated meshes; mitigation: CPU animation only for near bodies, rest pose beyond, as now.

**7. Retire the old renderer.** Section 6.11. A pure deletion plus the doc rewrites.

**8-13.** Section 8; each ships its codec and its baseline statement. Phase 11's projectile
is the largest sim addition and is called out in its PR as a declared baseline move.

**14. Save and load.** Section 8.7. The save-load twin run is added to the gate, so it
stays proven on every later build.

**15. Full loop and polish.** Section 8.8. This is where the audit's remaining holes close
and the whole loop is played by a script on both toolchains.

### Why this order, and where the code argues for changes

The order in the issues is sound: cleanup and the split make every later diff
reviewable, the renderer lands before the screens that depend on it, the old renderer
leaves only after render3d covers it, and save/load lands after the systems it saves.
Four adjustments, proposed in section 11: (1) move the shared-type extraction out of
phase 7 into phase 2; (2) make each of phases 8-13 ship its own save codec so phase 14 is
integration, not retrofit; (3) allow phase 3 to be two PRs (the headless spike has a real
chance of needing a second attempt and should not hold the shader work hostage), and
likewise phases 4, 5, 6 and 12 *if they grow*; (4) move the title screen's Continue row
and new-game plumbing explicitly into phase 14 (it cannot exist before saves do), and the
ending into phase 15.

---

## 10. Risks, and what needs Windows

### 10.1 What cannot be verified on Linux (needs the owner on Windows)

This environment is Linux. The shipped game is a mingw-w64 cross-built Windows `.exe`
on raylib's Desktop (GLFW) platform, on the owner's real GPU. None of the following can
be built, run or judged here, and each PR will say so:

1. **Every GPU look.** llvmpipe proves the shaders compile and render the same scene;
   whether a GL 3.3 driver on the owner's card does so, at a good frame rate, is a
   Windows check. Particular suspects: shadow-map bias and PCF on different vendors,
   `#version 330` strictness (AMD and Intel drivers differ on implicit conversions), and
   sRGB handling.
2. **Frame rate.** llvmpipe's speed says nothing about a GPU's; the plan budgets the
   internal resolution and light count and measures them on Windows.
3. **`scripts/*.ps1` and `verify-windows.ps1`**, which finish the cross-toolchain
   determinism gate (decoded-state fingerprint and world-hash report compared
   byte for byte between Linux/GCC and mingw/Windows) and are required after every phase
   that touches sim state (8-15).
4. **Input and window behaviour**: mouse-look feel at the real sensitivity, raw input,
   gamepad (SDL3), fullscreen and alt-tab, DPI.
5. **Audio on a real device** (the SDL3 backend; llvmpipe has no sound).
6. **Save files across toolchains**: a save written by the Windows build and loaded by
   the Linux test build (byte identity) is proved by the gate on both, and by one manual
   Windows save and load by the owner.
7. **The Windows standalone package** (`package-standalone.ps1`, `verify-standalone.ps1`),
   which will pick up new content folders (`content/art/oblivion/`, `content/audio/`,
   `content/raws/dialogue/`).

### 10.2 Risks

| Risk | Why it matters | Mitigation |
|---|---|---|
| **No headless GL route works with raylib** (6.2) | Blocks every shader | Three routes, a time-boxed first PR; B (Xvfb) is the near-certain fallback; C is a last resort. |
| **llvmpipe differs across Mesa/LLVM versions** | Golden images flake | Tolerance compare, not byte equality; golden images regenerated by a documented script; the docker image pins the Mesa packages the way it pins the base image by digest. |
| **llvmpipe is slow** with shadows and bloom | Slow tests and captures | Internal resolution stays low; shadow map small; frame tests render one frame, not sequences. |
| **raylib stays** | The first message could be read as banning raylib-class frameworks; the update says thin layers such as raylib are fine | Recorded in section 2; if the owner reverses it, the adapter is already confined to one folder. |
| **`session.cpp` move conflicts** | Nothing else can edit it meanwhile | Sequence (phase 2 immediately after phase 1; other branches wait). |
| **Baseline moves in phases 9-15** | Each re-bless needs both toolchains | One declared move per phase, the repo's own procedure; phase 11's projectile is the biggest. |
| **Save format churn** | Phases 8-13 add sections | Per-phase codecs (8.1), a versioned container, a section-level version byte. |
| **Content volume**: dialogue is prose, authored in canon | The owner's canon voice cannot be faked; a thin slice reads thin | A vertical slice plus a linter; the owner steers the rest. All text carries provenance. |
| **Art without art**: procedural textures and people can look cheap | The 2006 look depends on texture and light | Look target is tolerant of low-poly; real assets slot in through the existing catalogue; the owner judges the first materials PR before more is built on it. |
| **Word-diet conflict** (UI-EA-SPEC) | Wiki-like text vs 50% fewer words | Section 8.1 item 5, owner decision 6. |
| **Shadows do not hide the player** | A player may assume they do | Cosmetic by ruling 2; stated in the HUD help and the PR; decision 4. |
| **Distance and size**: the district is 256x192 | Low; draw whole under fog | LOD ring in 6.5. |
| **Silent checkout**: audio files absent | The "complete loop" is mute | Section 7, item 5; decision 3. |
| **A clean build was not run for this plan** | The audit is by reading | Each phase proves itself with the gate; the plan's numbers (line counts, case counts) are re-read at phase start. |

---

## 11. Decisions I need, and adjustments to the issue list

None of these blocks approving the plan; each is needed by the phase named.

1. **May the Java parity reference go?** (phase 1b, after phase 1.) If yes, the unreachable
   sim-core, its tests, Gradle and the `.claude` skills are removed, keeping the 47 files
   the golden generator and `import-map` use. If no, they stay and only the SAFE rows go.
   The README's three claims about the Java tree hold today (section 4.1).
2. **Raylib as the adapter** (phase 3): the update permits it. I read that as final.
3. **Audio provenance** (phase 15 or earlier): may I add a small CC0 or self-generated set
   under `content/audio/`, since the audio files are gitignored and a clean checkout is silent?
4. **Shadows are cosmetic** (phase 5): the sim's integer light field, not the shadow map,
   decides stealth. Accept?
5. **A player-carried torch** (phase 5 renders it; its stealth effect is a sim change that
   moves baselines). Render-only first, then decide?
6. **Word budget** (UI-EA-SPEC's 50% cut): applies to chrome and not to authored prose?
7. **"The trail goes down"** (phase 15): build a short dungeon behind one of the three
   sealed seams, or end with an honest epilogue and no dungeon?
8. **Player hunger and thirst** (phase 15 or never): not in the issue list, the north star
   does not require it; it is in the audit as MISSING. Include, or leave out on purpose?
9. **Day length** (phase 15): the default clock is real-time, a 24-hour day. Keep, or ship a
   faster default?
10. **Order adjustments** (section 9): (a) shared-type extraction into phase 2; (b) each of
    phases 8-13 ships its own save codec; (c) phases 3, 4, 5, 6 and 12 may each be two PRs;
    (d) title screen with Continue in 14, ending in 15. Say "as filed" and I will keep the
    issues exactly as they are.

---

## 12. What was run to write this plan

Honest record, so nothing here is claimed beyond it.

- **Read, not built.** Nothing in the repo was compiled, tested or run: no cmake, Ninja or
  Docker build was attempted on the planning machine. All BUILT / PARTIAL / MISSING
  statuses, counts and line numbers come from reading `native/` and `content/` at
  `364006b3`, and from static scripts over `git ls-files`. Each phase re-reads them.
- **Executed, one thing:** a small C program (not committed) that opened a surfaceless
  EGL context on the planning machine and printed `4.5 (Core Profile) Mesa 26.0.8`,
  renderer `llvmpipe (LLVM 21.1.8, 256 bits)`. That is the only evidence that a GL core
  context with no display is available; whether raylib can use it is the open question of
  6.2. raylib 6.0's `src/platforms/` and `rlgl.h` were read (the SDL platform, the depth
  texture and framebuffer calls, `LoadShaderFromMemory`, `UpdateModelAnimation`).
- **Counts** (files, lines, MB) are from `git ls-files`, `wc` and the audit scripts, not
  from the docs; the docs' own counts (349, 442, 679, 1,367 test cases) disagree with each
  other and are treated as stale.
- **Screenshots** referenced as "before" baselines (`docs/frames/3d-world-synty`,
  `3d-slice-one`) were viewed, not regenerated.
