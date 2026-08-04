<!--
  HISTORY OF THIS FILE.

  2026-07-12 — written by the 15-agent design workflow (7 subsystem designs, 7 adversarial
  critiques, 1 reconciling synthesis) and marked AUTHORITATIVE. It was a forward-looking
  design: at the time of writing almost none of it existed yet.

  2026-07-31 — reconciled against the code by three parallel audits (package map §3-§6,
  light subsystem, formats/scenarios/milestones §8-§12) ahead of the native C++ rewrite.
  The reconciliation ADDED status markers and a rewrite-priority section. It DELETED
  nothing: every ruling, every design paragraph, every milestone is still here, because
  unbuilt design is still a plan worth keeping. It is now labelled as a plan.
-->
# Simulation Core Architecture v0 — Granadad: The Darkstreets *(codename: Flame of Mercolas)*

**Original status line, 2026-07-12, preserved:** *"authoritative. Merges the seven subsystem
designs (world-storage, tick-determinism, thermal-materials, fluids, light, bubble-economy,
observer-tiled) with all REQUIRED changes from their adversarial critiques applied. Where a
critique offered options or a design conflicted with another, the ruling is stated here and
wins."* — That is still an accurate description of **how this document was produced** and the
seven design threads it merges. It is **SUPERSEDED as a claim about the code**: the word
"authoritative" meant "authoritative over the other design documents", and it was read as
"authoritative about what exists". Six of the seven merged subsystem designs were never
implemented.

**Original precedence ruling, preserved:** *"Where `docs/PLAN-v0.md` and this document disagree
on technical detail, THIS document wins: it post-dates the plan and every conflict was
explicitly ruled (see the mismatch register in section 1)."* — Still the right call **for
design intent**. It says nothing about implementation status, and `docs/PLAN-v0.md` carries a
third, conflicting milestone ladder (see §12). For questions of what exists, **the code wins
over both.**

> # ⚠ READ THIS BANNER BEFORE USING THIS DOCUMENT AS A SPECIFICATION
>
> **This document was written on 2026-07-12 as a forward-looking design. The program that
> then got built is a different program.** The design describes a voxel materials-physics
> engine — thermal diffusion, fluid mass transport, reactions and charge, propagated light,
> a freeze/thaw chunk bubble, a macro economy. What was actually built is a **substrate plus
> an actor simulation**: named people with jobs, skills, quests, factions, spells, barks and
> an item economy, running on a real deterministic world/save/event/material substrate.
>
> **The substrate is real and matches the doc. Every headline physics subsystem above the
> substrate is unbuilt.** 160 of sim-core's 259 files (62%) live in packages this document
> does not mention at all. Five of the packages it specifies in detail (`thermal`,
> `reaction`, `light`, `macro`, `scenario`) do not exist on disk.
>
> **Reconciled against the code 2026-07-31.** Every section now carries an explicit status.
>
> **What to trust:**
>
> | | |
> |---|---|
> | **Trust as description** | §2 modules, §3 rows marked BUILT, §8 chunk/lane/storage layout, §9 TROJSAV container, §10 raws schema + loader validation, §11.1 twin-run gate, §11.2 trade vocabulary, §6 determinism rules |
> | **Read as plan, not description** | §1.1/§1.2 rulings on unbuilt systems, §3 rows marked NOT BUILT, §4 phases with no registered system, §5 events with no emitter, §7 in its entirety, §11 flagship traces, §12 milestones M2-M7, §13 risks for unbuilt systems |
> | **Do not trust at all** | present-tense narrative prose in §7 and §11. It reads as behaviour that exists. None of it runs. |
> | **Missing entirely** | the actor simulation, progression, jobs, quests, spells, factions, barks — i.e. the thing the game currently *is*. See `docs/design/ACTORS-SPEC.md` and `docs/design/PROGRESSION-SPEC.md`, which are where the real specification for the built program lives. |
>
> Nothing in this document is enforced by a test. `ArchitecturePurityTest` constrains the
> classes that exist; it structurally cannot detect a package that was specified and never
> written. §12's own M7 criterion — "ARCHITECTURE.md ↔ code enforced by ArchUnit package
> rules" — is unmet, which is why this drift went unnoticed for nineteen days of sprints.

## Status legend

Used throughout. Every subsystem and every major claim below carries one.

| Marker | Meaning |
|---|---|
| **BUILT** | Exists in `src/main`, has production call sites, does what the doc says. |
| **PARTIAL** | Some of it exists. The marker always says which part, and what is missing. |
| **NOT BUILT** | Does not exist. Design intent only. Zero lines of implementation. |
| **SUPERSEDED** | The code went a different way on purpose. The marker says which way and why. |
| **BUILT, DEAD** | The type exists and is wired, but nothing in production reads or writes it. |

---

## 0. What the C++ rewrite should actually build

**This is the most useful section in the document right now.** It is derived from what is
BUILT and load-bearing, versus what is aspirational. Ordering is by "what breaks if you skip
it".

### Tier 1 — port these. They are real, load-bearing, and the game does not run without them.

| Thing | Where the Java lives | Why |
|---|---|---|
| **Chunk/coordinate math** | `sim/world/Coords`, `PackedPos`, `WorldConfig` | 32×32×8, `localIdx=(z<<10)\|(y<<5)\|x`, 30-bit PackedPos, VOID border. Already ported. Pinned by tests on both sides. |
| **Lane storage + ChunkCodec** | `sim/world/Lanes`, `ChunkWriter`, `world/io/ChunkCodec` | 7-lane registry, lane-wise RLE, versioned. Already ported. Format-critical. |
| **TROJSAV container** | `sim/world/io/TrojSav`, `WorldSaver`, `WorldLoader`, `WorldHasher` | Header/TOC/CRC32C/atomic rename. Already ported (read side). Format-critical. |
| **Material registry + raws loader** | `sim/material/*` | The validation list in §10 is fully implemented and boot-fatal. It is the single best-specified, best-implemented thing in the project. Port the rules, not just the parser. |
| **Engine + phases + event bus** | `sim/engine/*`, `sim/event/*` | Real, deterministic, tested. Port the *mechanism*. Do not port the empty phases as if they need occupants (see Tier 3). |
| **Counter-based RNG** | `sim/random/CounterRandomSource` | Determinism rests on it. Trivial to port, catastrophic to get wrong. |
| **The actor simulation** | `sim/actor/**` — 129 files | **This is the game.** Actors, jobs, schedules, pathfinding, needs, items, barter, food economy, payroll, bank ledger, factions, quests, spells, culling. It is 50% of sim-core and this document does not mention it. Spec: `docs/design/ACTORS-SPEC.md`. |
| **Progression** | `sim/progression/*` — 18 files | Morrowind-style use-XP skills. Spec: `docs/design/PROGRESSION-SPEC.md`. Also undocumented here. |
| **Barks + JSON** | `sim/bark/*`, `sim/json/*` | Small, load-bearing, undocumented here. |
| **Determinism rules (§6)** | `ArchitecturePurityTest` | No float in sim state, no hash containers, events are primitive records, sorted iteration. These four rules are the reason the twin-run gate is green. Port the rules as C++ lint/review discipline; the ArchUnit enforcement does not survive the language change, so the risk goes *up*, not down. |
| **The twin-run gate (§11.1)** | `client-observer/build.gradle.kts` + `TwinRunGateMain` | Build a C++ equivalent early. It is the only thing that has ever caught nondeterminism here. |

### Tier 2 — port, but with a correction

| Thing | Correction |
|---|---|
| **Client lighting** | Port `LampMarkersLoader` + `LampGlowMap` + `AmbientLight` + `WorldRenderer.lit()` + `DepthVision` as a **renderer** feature. Roughly a day of work, not a milestone. **While porting, fix the real defect: bake the `light_source` markers into the save** so the client stops reading `content/maps/src/*.tmx` at runtime. See §0.1. |
| **LIGHT / OPACITY / TEMPERATURE lanes** | Keep them in the reader as **inert format compatibility**. They are part of the v1 TROJSAV layout and the lane-count check. They are provably all-zero in 100% of shipped content and RLE crushes them to ~7 bytes a chunk. Comment them as placeholders with no producer so nobody reads their presence as "lighting is partly done". |
| **FLUID lane** | Same shape but not identical: it **does** carry authored data (docks: 7,429 wet tiles). Read it, render it, do not simulate it — there is no `FluidSystem` to port. |
| **Tiled importer** | Real and byte-deterministic. Port it — but `SummaryBaker` and `ImportMain` in §3 do not exist; the entry point is `ToolsLauncher`. |
| **Observer** | §3's client row names 21 types; 8 exist. The module is 162 files with whole subsystems the doc never mentions: `inspect` (37), `scenario` (23), `render` (19), `fpv` (17), `input` (14), `face` (7). Survey `client-observer/src/main` directly; do not use §3 as its map. |

### Tier 3 — do NOT build. This is the phantom scope.

Building these from §3-§13 would be roughly six months of work on systems the Java original
never had, with zero current consumers, while the actual game went unported.

| Do not build | Because |
|---|---|
| `thermal` — ThermalSystem, PhaseTransition, Fire | Package absent. Zero references. TEMPERATURE lane is all-zero (absolute zero) in all three shipped worlds. |
| `reaction` — PhorysReaction, ChargeSystem, ShatterSystem | Package absent. CHARGE overlay has zero cells in all shipped worlds. |
| `light` — LightSystem, four-queue relight, opacity cache | Package absent. See §0.1 — the honest treatment. |
| `fluid` beyond the registry | `FluidSystem`, `FluidView`, `FluidEmitters`, `FluidLedger`, `FluidAccountant`, `FluidTotals` all absent. The FALL/PRESSURE/SPREAD/THERMAL/REAGENT/SETTLE pipeline does not exist. |
| `bubble` freeze/thaw — tickets, journals, summaries, BoundaryFlux | 2 of 12 types exist (a facade interface and an enum). `AllConcreteBubble` returns ACTIVE unconditionally. §7 entirely, and §8's memory budget built on `ChunkSummaryStore`. |
| `macro` — the whole economy module, 16 types | Package absent. **The economy that exists lives in `actor/`** — `BankLedger`, `FoodEconomy`, `FoodMarket`, `Payroll`, `Barter`, `TradeGoods`. Different design, different altitude, and it works. Port that one. |
| `scenario` + golden-master harness | Package absent, `content/scenarios/` absent, **there are no golden files in the repo**, `ScenarioMain` does not exist. |
| The four §11 flagship traces | None of them exist. They are the highest-risk prose in this document — present-tense narrative describing per-tick event orderings for nine systems that were never written. |

### The single biggest correction

**This document describes a materials/thermal/fluid physics engine. The program that got
built is an actor simulation.** That is not the code being wrong — it is the project having
found what the game actually is. Physics-first was the 2026-07-12 bet; the sprints that
followed found that named people with jobs, skills, reputations and visible growth were the
thing worth building, and they built that instead, well, with its own specs and its own
gates (§11.1, §11.2 are the two most accurate sections in this file precisely because they
were written *after* the code they describe).

A C++ rewrite specified from §3-§13 would faithfully rebuild the wrong program.

---

## 0.1 The light system — the honest treatment

**Status: NOT BUILT in the sim. BUILT in the renderer, deliberately, and correctly.**

This is the case that triggered the whole reconciliation, and it is worth stating in full
because every other phantom subsystem has the same signature.

### What the document claims

§1.1 rulings #7, #15, #19; the §1.2 option rulings on skylight descent, in-flight queue
serialization and frozen-chunk queue entries; §3's `com.trojia.sim.light` row; §4 phase 6;
§5's `FireLuminanceChanged` and `ChargeStopChanged` consumers; §6's `LIGHT + OPACITY lanes |
LightSystem` ownership row and the `Light ← MATERIAL, FORM, FLAGS, FLUID` change-log
subscription; §8's 11 B/tile budget including LIGHT 2 + OPACITY 1 and the "light 1.8 ms
(≈ 60k-visit cap)" line; §9's `LGHT` section; §11's per-tick light beats in three of four
flagships; and §12's whole **M4 Light** milestone with five DoD tests.

Read together, that is a fully-specified subsystem: four-queue relight order, a ~60k-visit
budget, an OPACITY extension lane rebuilt from change-log readers, queues serialized to an
`LGHT` section, thaw-verification on six face shells,
`effectiveBrightness = max(block, (sky*celestial)>>5)`.

### What exists

- **No `com.trojia.sim.light` package.** Confirmed: the full sim package list is `actor`
  (+`faction`,`job`,`quest`,`spell`,`type`), `bark`, `bubble`, `engine`, `event`, `fluid`,
  `json`, `material`, `progression`, `random`, `world` (+`change`,`io`,`site`). `LightSystem`,
  `LightQuery`, `OpacityView`, `Luminance`, `CelestialProvider`, `LightRenderView`: none have
  ever existed as types. `LightQuery` is cited as a real collaborator in
  `client/art/LightTintTable.java:83` and in `ACTORS-SPEC.md:291`.
- **`TickPhase.LIGHT` is declared** (`engine/TickPhase.java:37`) with a five-line javadoc
  describing the relight. **Nothing registers into it.** The entire repo contains exactly two
  production `SimulationSystem` implementations: `actor/ActorsSystem` (phase `ACTORS`) and
  `headless/HeartbeatSystem` (phase `TICK_BEGIN`).
- **The LIGHT and OPACITY lanes are registered and all-zero.** Both lanes exist in
  `WorldBuilder`, are pinned at indices 5 and 6 in `Lanes`, appear in every shipped save's
  META, and decode to a single RLE run of zero covering all 8,192 cells of every chunk of
  every world:

  | world | chunks | cells | LIGHT nonzero | OPACITY nonzero | TEMPERATURE nonzero |
  |---|---|---|---|---|---|
  | `compound_block.trojsav` | 108 | 884,736 | **0** | **0** | **0** |
  | `docks_surface.trojsav` | 192 | 1,572,864 | **0** | **0** | **0** |
  | `tavern_fixture.trojsav` | 36 | 294,912 | **0** | **0** | **0** |

  Three of seven lanes — 5 of 11 bytes per tile, ~45% of dense chunk memory — are allocated,
  saved, RLE-compressed and hashed, and have never held a nonzero value. (TEMPERATURE at 0
  deciK is absolute zero: unwritten, not ambient.) The C++ test suite already asserts this at
  `native/content/tests/test_world_reader.cpp:155-165`.
- **`ChunkWriter.setLightBits` has zero production call sites** — one caller, in
  `ChunkWriterTest`. `setTemperatureDeciK` likewise: tests only. `Tile.light()` is never
  called anywhere in the repo, production or test.
- **`MaterialRegistry.opacity(int)` is called only from a test.** The authored per-material
  `light.opacity` (0..31) is loaded, range-validated, and folded into the raws fingerprint —
  real, hashed, and unconsumed. Meanwhile the `BLOCKS_LIGHT` flag bit *is* populated (1.45M
  cells in docks) and is derived from **form alone** (`WALL || VOID`), ignoring material
  opacity entirely; `FormOnlyClassifier`'s own javadoc says "used until the material registry
  provides opacity-aware classification".

### Where "lamp light sources loaded: 27" comes from

`client/ObserverApp.java:1288`, inside `loadLampGlow`. It is **entirely client-side, with no
sim involvement**:

1. `LampMarkersLoader` StAX-scans the **authored Tiled source map**
   `content/maps/src/<fixture>.tmx` — not the baked save — for `<object type="light_source">`,
   reads a `luminance` property (0..31), and classes warmth by marker name (brazier/cauldron/
   oven/torch/candle/hearth/fire → fire-warm, else lantern-warm).
2. `LampGlowMap` precomputes a packed-int glow plane per z-level once at load: radius
   `4 + (lum-8)/12` clamped 3.5..5.5 tiles, peak `0.55 + 0.45*lum/26`, falloff `P*(1-(d/R)²)²`,
   overlaps combined by saturating union with weighted-average colour.
3. `WorldRenderer.lit()` does one array lookup per drawn tile and lerps the frame's ambient
   toward the glow colour.
4. `AmbientLight` supplies the day/night curve as a pure function of `tick % DailyRhythm.DAY`.

The number is exactly the marker count: `docks_surface.tmx` contains **27** `light_source`
objects (compound_block 2, tavern_fixture 2, ubend_fixture 0). It reads a `.tmx` at runtime
because `TiledWorldImporter` parses the `markers` object layer and does not bake it
(`TiledWorldImporter.java:42-43`), so light sources **do not exist in the TROJSAV the client
loads**. `LampMarkersLoader`'s javadoc is candid about all of this and ends: "When marker
baking lands in the TROJSAV, delete this class and read them from the save."

So "27 lamps" is not sim state surviving a load. It is the observer re-reading authored
source art at boot, and it degrades to zero lamps — not a boot failure — in a checkout
without `content/maps/src`.

### Is client-side lighting a defect?

**No. It is correct as built, and the argument is worth keeping.**

Nothing in the sim depends on light. The plausible dependents were all checked:

- **Actor senses.** `ACTORS-SPEC.md:290-293` specifies `sightRadiusByLight[4]`. Not
  implemented, and both the loader and the record say so: the shipped `ActorTypeStats` has no
  light field of any kind.
- **Sleep, schedules, night behaviour.** Clock-driven, never light-driven —
  `nightWindowStart`/`nightWindowEnd` are tick-of-day bounds and every consumer goes through
  `params.inWindow(DailyRhythm.tickOfDay(tick))`.
- **Stealth.** No stealth system exists in sim-core.

The dependency graph is clean: **light → presentation only, one-way.** And §6's binding rule
makes client-side the right home for a concrete reason, not a taste one: no float/double in
sim-core state or state-affecting math. `LampGlowMap` accumulates in float and uses
`Math.sqrt`; `AmbientLight` runs float lerps. Moving that into sim-core would either break
the determinism contract or force a fixed-point rewrite of work that never changes a single
actor decision. `AmbientLight`, `LampGlowMap` and `DepthVision` each document "never read by
sim-core or the WorldHasher" in their own headers. This is a deliberate, documented boundary.

**The defect was never the placement. The defect was this document** describing a sim-side
`LightSystem` in enough operational detail that a reader had no way to tell it was unbuilt.

### Recommendation for the C++ rewrite

1. **Do not implement a sim-side light system.** Do not build M4.
2. **Keep the LIGHT/OPACITY lanes in the C++ world reader exactly as they are** — format
   compatibility, all-zero, no producer. Comment them so at `native/content/src/lanes.cpp`.
3. **Port the client lighting stack as-is** — `LampMarkersLoader` + `LampGlowMap` +
   `AmbientLight` + the `lit()` lerp + `DepthVision`. Small, self-contained, real test
   coverage (`LampGlowMapTest`, `AmbientLightTest`, `LampMarkersLoaderTest`,
   `DepthVisionTest`). Float is fine there; it is outside the determinism boundary by design.
   About a day of work.
4. **Fix the real defect while porting: bake the `light_source` markers into the save.** The
   importer already parses the markers layer and chooses not to emit it, which is why the
   client reads authored source maps at runtime — a contract violation that breaks any
   shipped build lacking `content/maps/src`. Baking them lets the C++ client delete the `.tmx`
   read entirely, which `LampMarkersLoader` already asks for by name.
5. **Defer `sightRadiusByLight` explicitly.** If actor perception should eventually care about
   darkness, *that* is the moment to build a real light system — and the cheapest honest
   version is not the four-queue flood-fill in §3. Lamps are static and authored, so the same
   `LampGlowMap` computation done in integer fixed-point at load would give a deterministic,
   hashable per-cell brightness the sim could read, with no per-tick relight budget at all.
   Only dynamic light (spreading fire, carried torches) forces incremental relight, and none
   of that exists either.

**Phantom scope avoided:** the M4 DoD alone — falloff, shadow-cone and shaft-seal tests,
lateral-window removal, flood relight-count cap, budget carry-over determinism — is a
multi-week milestone for a system with zero consumers.

**This was already leaking into the rewrite.** `native/content/src/lanes.cpp:40-41` registers
`light` (2 B) and `opacity` (1 B) and `world.hpp:87` exposes `light(tile)`. That is *correct*
for byte-compatible round-tripping — and wasted motion the moment anyone reads it as
"lighting is partly done".

---

## 0.2 Status at a glance

| Subsystem | Files | Status | One-line truth |
|---|---|---|---|
| `sim.engine` | 13 | **BUILT** (2 drifts) | 12 phases not 11; TickClock is 1000 ms/tick not 100 |
| `sim.event` | 24 | **PARTIAL** | Bus and codec real; 18 event types declared, 3 emitted, **0 consumed** |
| `sim.random` | 2 | **BUILT** | Counter-based, as specified |
| `sim.world` (+`change`,`io`,`site`) | 45 | **BUILT** | The best-matching section of the doc. `ActiveSet` and `ChangeLogs` are BUILT, DEAD |
| `sim.material` | 10 | **BUILT** | Registry + loader + full §10 validation list, all boot-fatal |
| `sim.fluid` | 3 | **PARTIAL — registry only** | `FluidId`/`FluidDefinition`/`FluidRegistry`. No `FluidSystem`. Lane baked, never simulated |
| `sim.bubble` | 2 | **PARTIAL — facade only** | `ActiveBubble` iface + `TicketLevel` enum. `AllConcreteBubble` says ACTIVE to everything |
| `sim.thermal` | 0 | **NOT BUILT** | Package absent. All 7 named types absent |
| `sim.reaction` | 0 | **NOT BUILT** | Package absent. All 6 named types absent |
| `sim.light` | 0 | **NOT BUILT** | Package absent. See §0.1 |
| `sim.macro` | 0 | **NOT BUILT** | Package absent. All 16 named types absent |
| `sim.scenario` | 0 | **NOT BUILT** | Package absent |
| **`sim.actor` (+5 subpkgs)** | **129** | **BUILT — undocumented here** | **The game.** Spec: `docs/design/ACTORS-SPEC.md` |
| **`sim.progression`** | **18** | **BUILT — undocumented here** | Spec: `docs/design/PROGRESSION-SPEC.md` |
| **`sim.json`** | **11** | **BUILT — undocumented here** | |
| **`sim.bark`** | **2** | **BUILT — undocumented here** | |
| `tools` | — | **PARTIAL** | Importer/validator/palette BUILT. `SummaryBaker`, `ImportMain` NOT BUILT |
| `headless` | 3 | **PARTIAL** | Heartbeat launcher only. `ScenarioMain`, golden harness NOT BUILT |
| `client-observer` | 162 | **PARTIAL + under-described** | 8 of 21 named types exist; six whole subsystems undocumented |

Arithmetic: 94 files documented-and-built, 5 documented-but-gutted, 0 in five phantom
packages, **160 undocumented (62%)**, total 259.

### 0.2.1 The C++ tree, and what of the above it has actually reached

The table above describes the **Java** tree, which is the behavioural reference and is not the
thing being built. `native/` is. Its status, kept honest as the rewrite goes:

| Piece | Status | Where |
|---|---|---|
| TROJSAV / world-format reader | **BUILT** — 65 cases, 902,056 assertions over the real baked worlds | `native/content/` |
| Counter RNG, wrapping helpers, world hasher, phased tick loop | **BUILT** (M1) — bit-equivalent to the JVM by golden vectors | `native/src/sim/` |
| Twin-run determinism gate | **BUILT** (M1), extended S2 with a second entry that registers the tavern | `native/src/gate/` |
| Sub-tile Q8 player body, collision, the climb rule | **BUILT** (S1) | `native/src/sim/player.cpp` |
| Software first-person renderer, lamp bake, HUD | **BUILT** (S1) | `native/src/render/` |
| Content-directory resolution (env → the exe's own tree → configure-time) | **BUILT** (S2) — S1 shipped an .exe that could not find its own worlds | `native/content/src/content_dir.cpp` |
| **Actors** — identity, roles, daily schedules, tile-stepped movement with Q8 sub-tile position | **BUILT** (S2), for one building's worth | `native/src/sim/actor.cpp` |
| **Region pathing** — bounded breadth-first, deterministic, no corner-cutting | **BUILT** (S2) | `native/src/sim/region_path.cpp` |
| **The brawl / lethal rule** | **BUILT** (S2) — see `DECISIONS.md` | `native/src/sim/brawl.cpp` |
| **The Gilded Gull** — six staff, nine patrons, hours, trade, a door policy | **BUILT** (S2, one patron added S3) | `native/src/sim/tavern.cpp` |
| Spell raws reader (modular components, for the priest and for S4) | **BUILT** (S2) | `native/src/sim/spellbook.cpp` |
| **Bark tables** — the owner's 210 authored tables behind a family / attitude / hour fallback chain | **BUILT** (S3) | `native/src/sim/barks.cpp` |
| **The Forty Notables, their micro-histories and the rumor domains** — who may repeat which story | **BUILT** (S3) as a registry; only three of the 42 are bound to a spawned actor so far | `native/src/sim/notables.cpp` |
| **Relationships** — per-actor memory of what the player did, ward-wide reputation, hashed into world state | **BUILT** (S3) | `native/src/sim/social.cpp` |
| **Player skills** — use-XP over the 20-skill vocabulary in the raws | **BUILT** (S3), two skills consumed (`streetwise`, `cracksmanship`) | `native/src/sim/social.cpp` |
| **Barter** — haggling as an argument with rounds and patience, moved by standing and by skill | **BUILT** (S3) | `native/src/sim/barter.cpp` |
| **Conversation** — topics, gated socially and never by a roll; the surface that draws it | **BUILT** (S3) | `native/src/sim/dialogue.cpp`, `native/src/render/dialogue_view.cpp` |
| **Factions** — the owner's five, a ladder each, standing, rank and ward influence; hashed and byte-encodable | **BUILT** (S4) — four of the five are joinable in the Gilded Gull; the Watch has no recruiter in that room | `native/src/sim/faction.cpp` |
| **Questlines** — authored stages with conditions this build can actually resolve, and the journal that walks them | **BUILT** (S4) — one line ships, the Priest of the Flame's, six stages | `native/src/sim/questline.cpp` |
| **Spellcrafting** — canon's own cost model, the (axis × time-shape) pairing table, a composition bench and a grimoire | **BUILT** (S4) | `native/src/sim/spellforge.cpp` |
| Casting what you learned or composed | **NOT BUILT** (S4) — the grimoire records craftings and nothing resolves one. `SpellVerb`-equivalent, active effects and the warmth→REST coupling are all still Java-side only | — |
| Needs, wages, crime beyond one room, the macro economy | **NOT BUILT** | — |
| Save / load | **NOT BUILT** — S3 ships a versioned byte encoding for the relationship ledger, proven by round trip, with nothing writing it to disk | `SocialLedger::encode` |
| The dedicated first-person combat screen | **NOT BUILT** — S3 gave `escalated()` a consumer (the room remembers a drawn blade and the bar stops serving), but the screen itself does not exist | — |

Everything the Tier-3 "phantom scope" section below names — thermal, reactions, propagated
light, the fluid solver, the bubble, the macro economy — is still **NOT BUILT and must not be
built**. Nothing in S2, S3 or S4 changed anything about that.

**S4's binding design laws** (both recorded in full in `docs/design/DECISIONS.md`): every rung of
every faction ladder is earned, and is measured in standing *and* in that faction's own skill;
and the Flame of Mercolas ships nothing — `MAGIC-CANON.md` §5.5 — so the Priest of the Flame's
questline hands over the *Source* off the public-issue shelf, says so out loud in its own
authored line, and a test greps every authored row in `content/` for a seventh power.

**S3's one binding design law, taken from DOCKS-GAZETTEER section 5.3 and enforced in code:**
the investigation is never persuasion. No dialogue topic is gated by a dice roll. A topic is on
the list because the person in front of you is a party to that story, or because
`content/raws/rumors/rumors.json` licenses them to repeat it. Skill decides exactly one thing in
the conversation layer, and it is the price of a mug of ale.

---

## 1. Contract reconciliation

> **STATUS: MIXED.** The rulings themselves are sound and worth keeping — several were later
> vindicated. But most of them arbitrate between subsystems that were never written, so they
> are rulings about a hypothetical. The per-row status column is the 2026-07-31 addition.

### 1.1 Mismatch register (found → ruled)

| # | Mismatch | Ruling | Status 2026-07-31 |
|---|---|---|---|
| 1 | Fluids wanted THERMAL before FLUIDS (same-tick temps); orchestrator wanted mass-first | **FLUIDS before THERMAL.** Fluids reads the persistent temperature field (previous tick's values) — evaporation/freeze thresholds don't need same-tick freshness; water arriving in fire is heated same tick, which the flagships do need. | **NOT BUILT** — both phases exist and are empty |
| 2 | thermal-materials put REACTIONS before THERMAL | **REACTIONS after THERMAL** — reactions then see `TemperatureThresholdEvent` and `ReagentContactEvent` same tick. | **NOT BUILT** — neither system exists |
| 3 | Fire/phase-transition/charge as separate phases vs. one REACTIONS phase | Fire and PhaseTransition are **ordered sub-systems inside THERMAL**; PhorysReaction, ChromatisCharge, LightstoneShatter are **ordered sub-systems inside REACTIONS**. Visibility granularity is `(phase, registrationIndex)` (orchestrator critique #2, option a), so chromatis discharge shatters lightstone same tick. | **PARTIAL** — the `(phase, regIndex)` visibility mechanism is BUILT and real; all five named sub-systems are NOT BUILT |
| 4 | World's thaw-in-`beginTick` full-diff vs. bubble's budgeted queues; one BUBBLE phase vs. promote/demote split | **Split phases, budgeted queues win.** `BUBBLE_PROMOTE` right after TICK_BEGIN, `BUBBLE_DEMOTE` after ECONOMY. Bubble module drives world's freeze/thaw hooks; world's `ActiveBubbleController` is deleted in favor of `TicketedBubbleManager`. | **PARTIAL** — both phases declared and empty; `TicketedBubbleManager` NOT BUILT |
| 5 | Cadence-10 economy never sees one-lap events | **Cadence-1 `EconomyAccumulator`** in ECONOMY folds lap events into per-site deltas every tick; heavy macro work runs on the 600-tick bucket scheduler. All bus consumers are now cadence-1, so plain one-lap retirement is correct. | **NOT BUILT** — but one-lap retirement itself is BUILT and correct |
| 6 | Fluids' private `FluidChunkLayer` byte array vs. world's 16-bit FLUID lane | **World FLUID lane wins** (bits: depth 0–2, fluidId 3–5, SETTLED 6, 7–15 reserved). Written via `ChunkWriter` → change logs/revisions fire; lane survives freeze verbatim (fixes fluids critique #5); FALLING bit deleted (critique #7). Fluids keeps private frontier bitsets per chunk. | **PARTIAL** — the lane won and is BUILT and baked (docks: 7,429 wet tiles). Nothing writes it at runtime |
| 7 | Light's three private byte channels vs. world LIGHT lane | LIGHT lane packs sky(5b)+block(5b); **OPACITY is a registered 8-bit extension lane** owned by light, rebuilt from change-log readers at top of LIGHT phase, saved (no relight on load). | **NOT BUILT** — both lanes registered, both all-zero, no producer. See §0.1 |
| 8 | Three save formats (RegionFile, TROJSAV, observer `.world`) | **One format: TROJSAV** sectioned container. World lanes are the `WRLD` section via `SystemStateWriter`. The Tiled importer's output *is* a TROJSAV at tick 0. Region files / eviction deferred past v0. | **BUILT** — verified against the shipped bytes |
| 9 | Two `WorldHasher`s | One. Engine owns the Sink protocol + per-system sub-hashes; world contributes canonical logical content (decode compressed lanes, ascending chunkIndex, lane-registry order, overlays sorted by localIdx; chunk lifecycle state excluded from the hash). | **BUILT** |
| 10 | `ChunkLifecycleListener` vs. `FreezeThawParticipant` | Merged into one SPI: `FreezeThawParticipant { serialize (pure, used by saver AND freeze — world critique #7), load, contributeSummary, rehydrate, transientVeto }`. | **NOT BUILT** — the SPI does not exist; there is no freezer |
| 11 | Boundary policy: world "assert on frozen write" vs. fluids' banking vs. bubble's flux ledger | One mechanism: `ChunkWriter` **rejects** non-concrete writes with a defined return code; systems route the rejected quantity to `BoundaryFlux.credit(face, kind, amount)`; `BOUNDARY_FLUX` phase applies credits to `ChunkSummaryStore`/incidents. Fluids' `FrozenFluidAccounts` is deleted — the summary's water field *is* the bank; thaw re-injects whole-chunk bottom-up scanline, residual stays banked (fluids critique #4). | **PARTIAL** — the reject-with-return-code half is BUILT (`ChunkWriter.REJECTED_VOID`). `BoundaryFlux`, the credit API and the BOUNDARY_FLUX phase body are NOT BUILT |
| 12 | `ChunkSummary` record vs. SoA store | `ChunkSummaryStore` (SoA, whole-world resident) is truth; `ChunkSummary` is a flyweight/ephemeral view. Incidents mutate summaries **in lockstep** with journal appends (bubble critique #3). | **NOT BUILT** — no summaries exist anywhere |
| 13 | Temperature units: deciK vs. deciC vs. °C raws | **Deci-Kelvin unsigned 16-bit everywhere in state, events, summaries.** Raws author integer Kelvin; loaders convert. Fluids raws re-specified in K. | **PARTIAL** — the convention is BUILT into the lane, the writer signature and the raws loader. No producer ever writes a temperature |
| 14 | Chromatis charge 1,000,000 cu vs. 16-bit sparse overlay | **Overlays are 16-bit.** Raws rescaled: chromatis capacity 60,000 cu, maxSafeDischarge 600/tick; lightstone 5,000 / spike 2,000. Loader validates fit. | **PARTIAL** — overlay width, rescaled raws and the fit validation are all BUILT. Nothing ever creates charge |
| 15 | Cell keys: 63-bit longs vs. 30-bit `PackedPos` vs. light's region-local ints | **30-bit int `PackedPos`** `(z<<24\|y<<12\|x)` is the lingua franca of every hot queue and event payload; `int chunkIndex` ascending is canonical chunk order. Light queue entries are longs `(packedPos \| level<<32)` — kills the region-origin rebase bug (light critique #7). Orchestrator's `LongFrontier` is deleted; world's `ActiveSet` is *the* frontier utility. | **PARTIAL** — `PackedPos` and canonical chunk order are BUILT and load-bearing. `ActiveSet` is **BUILT, DEAD**: zero production references; the only mention outside its own file is a comment in `actor/PathFinder.java:257` citing its growth shape |
| 16 | Three RNG schemes (tuple SplitMix64, named streams, `derive()`) | One counter-based `RandomSource`: `h = mix64(worldSeed + K1*tick); h = mix64(h ^ systemSalt); h = mix64(h + spatialKey); h = mix64(h + drawIndex)` (orchestrator critique #3). Allocation-free primitive path `long draw(long key, int idx)` (critique #9). Only state saved: worldSeed — resolves thermal's "stream positions in save" concern by construction. Fluids draws zero. Macro hazards use the same derive (bubble critique #8). | **BUILT** — and it is the backbone of every green twin-run |
| 17 | Importer's two-material tiles (floor+fill) vs. world's one-material+form | **One material + form.** Importer collapses Tiled floor/fill: fill present → (fillMat, form); else floor → (floorMat, FLOOR); else OPEN. Revisit post-v0 if floor-over-rock matters. | **BUILT** |
| 18 | World's read-only FROZEN_RESIDENT rind vs. bubble's full-physics BORDER | **Both, layered:** ACTIVE and BORDER are concrete with identical physics; a 1-chunk FROZEN_RESIDENT rind around BORDER is readable-never-writable; writes at the hull route to BoundaryFlux. | **NOT BUILT** — every interior chunk is unconditionally concrete |
| 19 | Light 0–31 vs. observer contract 0–15 | 0–31; observer contract updated. `effectiveBrightness = max(block, (sky*celestial)>>5)`, celestial fixed-point 0..32 (light critique #10). | **PARTIAL** — the 0..31 range is BUILT into the raws schema (`light.opacity`, `lightLevel`) and validated. `effectiveBrightness` has no implementation |
| 20 | Water quench "via conduction" requires water on the material grid | Pooled liquids live only in the FLUID lane. Thermal kernel reads the FLUID lane and substitutes fluid heat-capacity/conductivity when depth ≥ 4 (thermal critique #8); fire extinguish queries `FluidView` directly in its own sub-phase. | **NOT BUILT** |
| 21 | Evaporation owned by reactions vs. fluids vs. thermal | **Fluids owns evaporation/boil/freeze of pooled fluid** (mutating only its own lane — kills orchestrator critique #6's ownership violation). Thermal's PhaseTransition owns grid-material melt/boil/freeze and emits `MaterialPhaseChangedEvent(yieldUnits)`; fluids spawns the liquid next tick. `meltYieldUnits` is a raws field. | **PARTIAL** — `meltYieldUnits` is BUILT as a validated raws field. The ownership split is NOT BUILT |
| 22 | Phorys: reactions "consume liquid" cross-mutation | **Fluids consumes** the units in its own phase (reagent flag check, per-chunk `containsReagents` gate) and emits `ReagentContactEvent`; reactions owns the pressure pulse and phorys wear counter (now with declared storage). | **NOT BUILT**, and partly **SUPERSEDED** — the NO-WEAR ruling (Eli, 2026-07-12) made phorys inexhaustible in v0; the wear counter was removed from the raw. §10 still prints "wear 5/unit"; that number is dead |
| 23 | Ash swap: orchestrator had REACTIONS consume `CombustionCompleted`; thermal had fire swap directly | **Fire swaps to ash itself** via `ChunkWriter` (it owns the burn lifecycle); `MaterialTransformedEvent(cause=BURNOUT)` is notification only. | **NOT BUILT** — no Fire |
| 24 | Bubble's `WorldGenSource.generate` for pristine thaw — v0 has no worldgen | **Pristine = imported base world.** Snapshot-on-modify diffs against the imported TROJSAV; pristine thaw deserializes from the base file (kills bubble critique #5's unbounded generate cost). Importer also **bakes initial chunk summaries** (bubble critique #2). | **NOT BUILT** — `SummaryBaker` does not exist; no summaries to bake |
| 25 | SiteIndex: bubble's "≤1 site per column" vs. world's nested-site example | Per-column sorted `(zMin, zMax, siteId)` list; smallest-volume site wins, tie by SiteId (world critique #10). Macro attribution: innermost non-DISTRICT site, else district. | **PARTIAL** — `SiteDef` and `SiteIndex` are BUILT. The macro-attribution half has no consumer |
| 26 | Two competing event vocabularies with duplicate names | Single taxonomy, §5. Change logs vs. events split ruled in §6. | **PARTIAL** — one taxonomy exists and is save-format-stable. 3 of 18 events are emitted; none is consumed |

### 1.2 Option rulings (critiques that offered choices)

> **STATUS: mostly NOT BUILT.** Per-item markers below.

- Overlay change notification (world #2): **option b** — no overlay change logs; owning system self-tracks its tile set; overlay writes still mark changedBits + revision. — **PARTIAL**: the mechanism is BUILT; there is no owning system, because no system writes an overlay.
- World-edge packing (world #9): **option a** — permanent 1-chunk immutable VOID border, in-bounds, `ChunkWriter` rejects; no hot-path branch. — **BUILT.**
- Skylight zero-cost descent (light #1): **restricted to level-31 columns** (Minecraft rule); sub-31 skylight pays `max(1,opacity)` downward. Simpler than the conditional-removal fix. — **NOT BUILT.**
- In-flight light queues at save (light #5): **serialized** (packed longs), not drained. — **NOT BUILT.**
- Queue entries into frozen chunks (light #6, orchestrator #12): **lazily skip** cells outside the concrete set on dequeue/consume; freeze demote-pipeline folds pending mass-bearing carry-over (pressure) into the summary via participants; thaw verification pass reconciles light (light #2). — **NOT BUILT.**
- Forge Economy observer surface (observer #8): **option a** — a `MacroPanel` over `MacroReadView`. — **NOT BUILT.**
- Scenario ignition in observer (observer #9): scenarios ship a **replayable SimCommand script**; observer "run scenario" = load world + inject script. No heat-emitting light sources. — **NOT BUILT** as specified. **SUPERSEDED**: the observer grew its own `client/scenario/` package (23 files) doing a different, working thing — `TwinRunGateMain`, `DocksActorsMain`, `GoodsCensus`.
- `FocusPoint.priority` (bubble #12): **deleted**. — moot; `FocusPoint` was never written.
- Economy while observed (bubble #10): **ledger-always-canonical in v0**; stockpile tiles are a rendering refreshed at thaw/TICK_END. `StockpileDelta` cut from v0. — **SUPERSEDED**: the built economy lives in `actor/` (`BankLedger`, `FoodMarket`, `Payroll`, `Barter`) and is ledger-canonical for its own reasons.

---

## 2. Gradle module layout

> **STATUS: BUILT**, with two annotations. The five modules exist with these dependency
> relationships and these purity constraints, and the ArchUnit purity suite is real
> (`ArchitecturePurityTest`: `SIM_CORE_STAYS_PURE`, `NO_HASH_CONTAINER_FIELDS`,
> `NO_FLOATING_POINT_FIELDS`, `EVENTS_ARE_RECORDS_OF_PRIMITIVES` — and the hash-container rule
> is *stronger* than the doc, banning bare `Map`/`Set` declared types so a HashMap cannot hide
> behind an interface).
>
> Two corrections: **headless has no golden-master harness and no scenario runner** — it is
> three classes; and **`content/scenarios/` and the `golden.v1.json` files do not exist.**
> `content/raws/` holds actors, barks, factions, fluids, jobs, materials, names, quests,
> reactions, rumors, skills, spells, treatments — note how many of those are actor content the
> rest of this document never mentions.

```
settings.gradle → sim-core, headless, tools, client-observer, content
sim-core        java-library. ZERO dependencies (JDK 21 only). Tests: JUnit 5 + ArchUnit
                (purity: no libGDX, no java.util.HashMap in sim state, events are primitives-only,
                 no float/double fields in @SimState types).
headless        application. depends: sim-core. CLI scenario runner + golden-master harness.
tools           application. depends: sim-core. Tiled importer (StAX), validators, palette gen,
                summary baker. No libGDX.
client-observer application. depends: sim-core, libGDX LWJGL3. gdx-tools TexturePacker as
                buildscript-only dep (:packArt task).
content         no code. raws/, maps/, scenarios/ (+ golden.v1.json files). Validated by tools.
```

---

## 3. Package map (key types, one line each)

> **STATUS: 40% fiction.** Five of the twelve `com.trojia.sim.*` packages below do not exist.
> Two more are a registry or a facade only. And the map is missing the four packages that are
> 62% of the code. Read the status line under each row; read §3.1 for what the map omits.

**com.trojia.sim.engine** — `SimulationEngine` (iface: `step(n)/tick()/save/submit/inspect`) · `Simulations` (static factories only; multi-engine per JVM) · `EngineConfig` (rec) · `SimulationSystem` (iface: id/phase/tick/serialize/load/hashInto) · `TickPhase` (enum, §4) · `SystemId` (rec; 64-bit salt, collision-checked at boot) · `TickContext` (iface: rng/events/emit/bubble) · `InputGate` (commands+script → paints via ChunkWriter + External* events + input log) · `TickClock` (100 ms/tick) · `TickProfile` (rec, diagnostics only) · `SimCommand` (sealed: PaintMaterial, ClearTile, Ignite, Extinguish, AddFluid, RemoveFluid, InjectCharge, PlaceLightSource, RemoveLightSource, SetFocus).

> **STATUS: BUILT (13 files), with three drifts.**
> - `TickPhase` is **12 constants, not 11** — `ACTORS` sits between `LIGHT` and `BOUNDARY_FLUX`
>   (`engine/TickPhase.java:51`), javadoc'd as "a deliberate, one-time, documented amendment".
>   §4 was never updated.
> - `TickClock` is **SUPERSEDED**: `MILLIS_PER_TICK = 1000`, "one simulated second per tick,
>   Dwarf-Fortress style". The doc's "100 ms/tick" is stale by a factor of 10, and every
>   millisecond figure in §8 that assumes a 100 ms tick inherits that error.
> - `InputGate` is **PARTIAL**: it drains all 10 commands into the input log, but only 5 do
>   anything (`PaintMaterial`/`ClearTile` → ChunkWriter; `Ignite`/`AddFluid`/`InjectCharge` →
>   events with no consumer). `Extinguish`, `RemoveFluid`, `PlaceLightSource`,
>   `RemoveLightSource`, `SetFocus` are explicit empty cases.
> - Undocumented but present: `PhasedSimulationEngine` (the only implementation) and
>   `AllConcreteBubble` (the bubble stand-in — see §7).

**com.trojia.sim.event** — `SimEvent` (sealed root) · `EventSink`/`EventReader` (pull-only; topic ids resolved at registration) · `PhasedEventBus` (internal; `(tick,phase,regIndex,seq)` stamps; 65,536/topic/tick hard-fail in ALL builds).

> **STATUS: BUILT (24 files) — plumbing only.** Stamps, pull-only reads and the unconditional
> 65,536/topic/tick cap are all real and enforced. Undocumented: `EventCodec`, `Events`.
> **No event is consumed by anything** — see §5.

**com.trojia.sim.random** — `RandomSource` (counter-based; `long draw(spatialKey, drawIndex)` allocation-free hot path).

> **STATUS: BUILT.** `RandomSource` + `CounterRandomSource`, exactly as specified.

**com.trojia.sim.world** — `World` (iface, no tick methods) · `TickableWorld` (scheduler-only: beginTick/commitTick) · `WorldConfig` (rec) · `PackedPos`/`Coords` (all bit math, audited once) · `Tile` (iface) · `TileCursor` (flyweight; debug tick-stamp) · `TileForm`/`Dir` (enums) · `LaneId`/`Lanes`/`LaneRegistry` (instance owned by WorldBuilder — no statics; aether = a later lane registration) · `ChunkView` (borrowed read arrays) · `ChunkWriter` (only write path; maintains BLOCKS_MOVE/BLOCKS_LIGHT on material/form writes; rejects non-concrete writes with return code) · `OverlayId` (CHARGE) · `FlagBits`. (`TilePos`/`ChunkPos` cold-path unpacked-coordinate records were removed 2026-07-14 as dead code — zero call sites anywhere in the repo; reintroduce only alongside an actual cold-path caller.)

> **STATUS: BUILT (31 files in `world`, 45 with subpackages).** Every named type is present and
> the `TilePos`/`ChunkPos` parenthetical is accurate. `LaneRegistry` really is instance-owned
> by `WorldBuilder` with no statics.
> One nuance worth carrying into C++: `BLOCKS_LIGHT` is maintained from **form alone**
> (`FormOnlyClassifier`: `WALL || VOID`), not from material opacity — its own javadoc flags
> this as provisional.
> Undocumented but present: `Walkability`, `TileClassifier`, `FormOnlyClassifier`, `Chunk`,
> `ChunkLifecycle`, `DenseWorld`, `DenseChunkWriter`, `SparseOverlay`, `OverlayView`,
> `EmptyOverlay`, `ChangeLogSink`/`ChangeLogsAdapter`, `RevisionSink`/`RevisionsAdapter`.

**com.trojia.sim.world.change** — `ChangeLogs`/`ChangeLogReader` (per-lane packed-int logs; `hasReaders` skip; reader-lag cap asserted at commit) · `ActiveSet` (THE shared frontier: packed-int FIFO + per-chunk bitsets, deterministic insertion order) · `ChunkRevisions` (observer diff key; changedBits valid only for revision delta == 1).

> **STATUS: BUILT, DEAD.** All three types exist and work.
> - `ChangeLogs` is instantiated per world and fed by the writer, but **zero readers are
>   registered in production**, so the `hasReaders` skip is permanently on and no change log is
>   ever read.
> - `ActiveSet` — "THE shared frontier" — has **zero production references**.
> - `ChunkRevisions` is wired into `DenseWorld`, but §6's "Observer uses `ChunkRevisions` only"
>   is **wrong**: the observer never calls `revisions()`.
>
> This is a coherent, tested, unused subsystem. For the C++ rewrite: port it only if a
> change-log consumer is actually being built, otherwise it is 3 more files of dead weight.

**com.trojia.sim.world.io** — `TrojSav` (sectioned container, §9) · `WorldSaver`/`WorldLoader` · `ChunkCodec` (lane-wise RLE, versioned) · `WorldHasher` (+`Sink`).

> **STATUS: BUILT (8 files).** Independently verified against the shipped bytes by two audits
> and by the C++ test fixtures. Port with confidence.

**com.trojia.sim.world.site** — `SiteDef` (rec) · `SiteIndex` (per-column z-range list).

> **STATUS: BUILT.**

**com.trojia.sim.material** — `MaterialId` (rec, short) · `Material` (rec) · `MaterialRegistry` (immutable; deterministic id assignment; precomputed primitive tables incl. `invCapQ16`, pairwise conduction/cap-normalized weights) · `MaterialFeature` (sealed: `Chargeable`, `ShatterOnSpike`, `Emissive`, `ContactReactive`) · `Treatment` (rec; mints derived materials at load) · `MaterialRawsLoader` (validation list §10).

> **STATUS: BUILT (10 files).** `invCapQ16` is real; `MaterialFeature` is sealed with exactly
> the four permitted subtypes; the §10 validation list is fully implemented and boot-fatal.
> Undocumented: `MaterialPhase`, `RawsBundle`, `ReactionDefinition`.
> **Caveat:** all four `MaterialFeature`s are parsed, cross-validated and fingerprinted, and
> **none is ever acted on** — there is no ChargeSystem, ShatterSystem, emissive renderer or
> contact-reaction path. `ReactionDefinition` raws load and nothing executes them.
> `MaterialRegistry.opacity(int)` is called only from a test.

**com.trojia.sim.thermal** — `ThermalSystem` (diffusion + buoyancy + settle-to-ambient pass; energy-residual carry + per-chunk sink counter) · `ThermalQuery` · `HeatCommandBuffer` (sorted drain, same-pos injections summed) · `PhaseTransitionSystem` · `FireSystem` (ignite/burn/extinguish/ash; SMOLDER semantics specified) · `FireQuery` · `FireState` (enum).

> ## STATUS: NOT BUILT
> Package absent from disk. All 7 named types absent. Zero references in any main source.
> The TEMPERATURE lane is all-zero (absolute zero) in all three shipped worlds and
> `ChunkWriter.setTemperatureDeciK` has zero production call sites. **Design intent only.**

**com.trojia.sim.reaction** — `PhorysReactionSystem` (wear counter stored in a per-chunk sparse map) · `ChargeSystem` (charge tick, stops, saturation heat via HeatCommandBuffer) · `ShatterSystem` (Chebyshev ≤ 2 incl. distance 0 — self-shatter) · `ReactionTable` · `ChargeCommandBuffer` (all charge mutation intake) · `MaterialInstances` (CHARGE overlay access).

> ## STATUS: NOT BUILT
> Package absent. All 6 named types absent. The CHARGE overlay exists as an `OverlayId` and
> has **zero cells** in all shipped worlds; its only writer is `WorldLoader` restoring what was
> saved. Note also that the phorys wear counter here is **SUPERSEDED** by the NO-WEAR ruling
> (Eli, 2026-07-12) recorded in `content/raws/reactions/phorys_hydration.json`.
> **Design intent only.**

**com.trojia.sim.fluid** — `FluidSystem` (FALL/PRESSURE/SPREAD/THERMAL/REAGENT/SETTLE; wake-on-decrease fan-out; per-chunk gates `thermallyInteresting`/`containsReagents`; per-chunk displacement cap 32; same-fluid guards in FALL/SPREAD; pending list for newly wetted chunks) · `FluidView`/`FluidCursor` · `FluidId`/`FluidDefinition`/`FluidRegistry` · `FluidEmitters` · `FluidLedger` (bank transfers are moves, excluded from create/destroy sums) · `FluidAccountant` (audit incl. summary banks) · `FluidTotals` (valid post-FLUSH, covers frozen via summaries).

> ## STATUS: PARTIAL — REGISTRY ONLY (3 of 11 types)
> **BUILT:** `FluidId`, `FluidDefinition`, `FluidRegistry` — the raws-registry triple.
> **NOT BUILT:** `FluidSystem`, `FluidView`, `FluidCursor`, `FluidEmitters`, `FluidLedger`,
> `FluidAccountant`, `FluidTotals`. The entire FALL/PRESSURE/SPREAD/THERMAL/REAGENT/SETTLE
> pipeline, the per-chunk gates, the displacement cap and the conservation ledger do not exist.
> Fluid *data* is baked into worlds by the importer (`TiledWorldImporter.java:215` — docks
> 7,429 wet tiles, tavern 10, compound 0) and rendered by the observer, but is **never
> simulated**. For C++: read and render the lane; do not build the solver.

**com.trojia.sim.light** — `LightSystem` (opacity rebuild from change logs w/ effective-opacity no-op; four-queue relight, budget ~60k visits checked at queue-phase boundaries; bulk-init pass at load, outside tick budget) · `LightQuery` (0–31; fixed-point effectiveBrightness) · `OpacityView` (FOV seam) · `Luminance` (rec) · `CelestialProvider`/`CelestialState` (int 0..32) · `LightRenderView` (reads legal only between ticks) · source registry keyed by handle slot, coord as value (stacking = max at seed).

> ## STATUS: NOT BUILT — see §0.1 for the full treatment
> Package absent. All named types absent. The only surviving occurrences of these names are two
> javadoc mentions in the client (`client/art/LightTintTable.java:83`,
> `client/render/LampGlowMap.java:55`) and one in `ACTORS-SPEC.md:291`.
> **Lighting ships client-side and that is correct, not a defect** — the sim has no light
> dependents, and putting float glow math in sim-core would break the no-float determinism
> rule for zero simulation benefit. **Do not build M4.** §0.1 has the evidence and the
> recommendation.

**com.trojia.sim.bubble** — `ActiveBubble` (facade iface) · `TicketedBubbleManager` (composes `TicketRetargeter`, `ThawPipeline`, `FreezePipeline` — internal) · `TicketLevel` (ACTIVE/BORDER/FROZEN) · `BubbleTopology` (O(1) `levelOf`) · `FocusPoint` (rec, no priority) · `FreezeThawParticipant` (SPI, §1.1 #10) · `ChunkSummaryStore` (SoA, 64 B/chunk, whole world; `ChunkSummary` flyweight) · `DeltaJournal` (tombstone + compaction; set-deltas last-write-wins per chunk) · `AbstractDelta` (sealed: FireDamage, WaterLevel, StructureDamage, Temperature) · `BoundaryFlux` (iface; physics-facing credit API).

> ## STATUS: PARTIAL — FACADE ONLY (2 of 12 types)
> **BUILT:** `ActiveBubble` (interface), `TicketLevel` (enum).
> **NOT BUILT:** `TicketedBubbleManager`, `TicketRetargeter`, `ThawPipeline`, `FreezePipeline`,
> `BubbleTopology`, `FocusPoint`, `FreezeThawParticipant`, `ChunkSummaryStore`, `ChunkSummary`,
> `DeltaJournal`, `AbstractDelta`, `BoundaryFlux`.
> What ships is `engine/AllConcreteBubble.java`: `levelOf()` returns `ACTIVE` unconditionally,
> `isConcrete()` returns `true` unconditionally, javadoc'd as "stands in until the bubble
> module's `TicketedBubbleManager` (F5) is wired in". **There is no freeze, no thaw, no
> summary, no journal, no hull.** §7 in its entirety and the §8 memory budget both rest on this.

**com.trojia.sim.macro** — `MacroWorld` · `MacroSite` (real objects; thousands, not millions) · `SiteKind` (enum) · `Ledger` (long milli-units) · `CommodityRegistry` · `Recipe` (rec) · `Workshop` · `Market` · `PriceModel` (fixed-point; elasticity ∈ {½,1,2}) · `CaravanFlow` (tiebreak: commodityId asc) · `MacroScheduler` (600-bucket wheel + sorted far-heap for dueTick > 600) · `IncidentSystem` (sorted-key iteration; updates summaries in lockstep) · `MacroFlood` drains to an explicit per-site reservoir `long` with logged events · `MacroEventLog` · `MacroReadView` · `EconomyAccumulator` (cadence-1 event folder).

> ## STATUS: NOT BUILT — and SUPERSEDED by something better
> Package absent. All 16 named types absent. `content/raws/macro/` does not exist.
> **The economy that exists lives in `actor/`**: `BankLedger`, `FoodEconomy`, `FoodMarket`,
> `Payroll`, `Barter`, `TradeGoods` — a different design at a different altitude, and the one
> that got playtested. It models people earning, spending, eating and trading rather than
> abstract sites producing commodities on a timing wheel. §11.2 documents its trade vocabulary
> and is accurate. **Port `actor/`'s economy, not this one.**

**com.trojia.sim.scenario** — `ScenarioDefinition` (rec) · `ScriptedAction` (wraps SimCommand at tick).

> ## STATUS: NOT BUILT
> Package absent; both types absent; `content/scenarios/` absent. An unrelated
> `client/scenario/` package of 23 files exists in the observer and does a different, working
> job (twin-run gate, docks actors soak, goods census).

**headless** — `ScenarioMain` (`run <scenario> --ticks N --hash-every K [--bless]`; refuses journals whose world hash mismatches).

> **STATUS: NOT BUILT as specified.** `ScenarioMain` does not exist. The module is 3 files:
> `HeadlessLauncher` (a fixed-tick heartbeat whose javadoc says "grows into the scenario runner
> (ScenarioMain, M2+)"), `HeartbeatSystem`, `ActorsDemoMain`. No `--ticks/--hash-every/--bless`
> CLI, no golden-master harness, no journal-hash refusal, **and no golden files in the repo**.

**client-observer** — `ObserverApp`/`ObserverLauncher` · `SimulationDriver` (fixed-timestep; clamp scales with speed; MAX = whole ticks in 12 ms) · `SpeedSetting` · `MapCamera` · `LayeredWorldRenderer` + `RenderPass` (sealed) · `ZPeekResolver` (GL-free) · `TileArtResolver`/`JsonTileArtResolver` (GL-free index math) + `AtlasRegionTable` (GL side) · `DebugHud`/`TimeControlBar`/`PalettePanel`/`InspectorPanel`/`StatsOverlay`/`MacroPanel` · `BrushController` + `Brush` (sealed) · `WorldFileWatcher` (AtomicBoolean polled in frame loop) · `CommandJournal` (per session, world-hash header; new file on F5).

> **STATUS: PARTIAL and badly under-described. 8 of 21 named types exist.**
> **BUILT:** `ObserverApp`, `ObserverLauncher`, `SimulationDriver`, `SpeedSetting`, `MapCamera`,
> `TileArtResolver`, `JsonTileArtResolver`, `AtlasRegionTable`.
> **NOT BUILT (13):** `LayeredWorldRenderer`, `RenderPass`, `ZPeekResolver`, `DebugHud`,
> `TimeControlBar`, `PalettePanel`, `InspectorPanel`, `StatsOverlay`, `MacroPanel`,
> `BrushController`, `Brush`, `WorldFileWatcher`, `CommandJournal`.
> **UNDOCUMENTED:** the module is 162 files, and whole subsystems appear nowhere in this
> document — `inspect` (37), `scenario` (23), `render` (19), `fpv` (17, first-person view),
> `input` (14), `atlas` (18), `hud` (7), `face` (7), `art` (6). Survey the module directly.

**tools** — `TmxReader` (StAX; masks gid flip bits, warns) + `Tmx*` model records · `MaterialBinding` · `TiledValidator` (ordered `ValidationPass` list) · `TiledWorldImporter` (deterministic; annotations sorted (z, objectId); suggestion tiebreak (distance, lex)) · `PaletteGenerator` · `SummaryBaker` (bakes initial ChunkSummaryStore; validated against freeze-path output) · `ImportMain` (atomic tmp+rename output).

> **STATUS: PARTIAL.**
> **BUILT:** `TmxReader` + `Tmx*` records (+ undocumented `TsxReader`), `MaterialBinding`,
> `TiledValidator` with an ordered `ValidationPass` list, `TiledWorldImporter`,
> `PaletteGenerator`.
> **NOT BUILT:** `SummaryBaker` (nothing to bake — no `ChunkSummaryStore`); `ImportMain` (the
> entry point is `ToolsLauncher`).

### 3.1 The packages this map omits — 62% of sim-core

> **STATUS: BUILT. Specified elsewhere. This is the program.**

| Package | Files | What it is | Real spec |
|---|---|---|---|
| `com.trojia.sim.actor` | 84 | Actors, needs, schedules, pathfinding, items, barter, `BankLedger`, `FoodEconomy`, `FoodMarket`, `Payroll`, `TradeGoods`, `CullVerb`, `ActorsSystem` (the only production `SimulationSystem`) | `docs/design/ACTORS-SPEC.md` |
| `com.trojia.sim.actor.spell` | 13 | The magic systems | `docs/design/ACTORS-SPEC.md` |
| `com.trojia.sim.actor.job` | 12 | Jobs, `JobParams`, `JobBehaviors`, work events, yields | `docs/design/ACTORS-SPEC.md` |
| `com.trojia.sim.actor.type` | 11 | Actor types + `ActorTypeStats` + `ActorRawsLoader` | `docs/design/ACTORS-SPEC.md` |
| `com.trojia.sim.actor.quest` | 6 | Quests — the "service" side of §11.2's trade categories | `docs/design/ACTORS-SPEC.md` |
| `com.trojia.sim.actor.faction` | 3 | Factions and standing | `docs/design/ACTORS-SPEC.md` |
| `com.trojia.sim.progression` | 18 | Morrowind-style use-XP skills. Emits `SkillLevelledEvent`, which is a real used event type **deliberately not a `SimEvent`** (its own javadoc says so) and therefore absent from §5 | `docs/design/PROGRESSION-SPEC.md` |
| `com.trojia.sim.json` | 11 | Dependency-free JSON for the raws loaders | — |
| `com.trojia.sim.bark` | 2 | `BarkSelector` and friends — authored dialogue selection | — |

**`ActorsSystem` is the only registered system in the shipped observer**
(`client/ObserverApp.java:425-426`: `List.<SimulationSystem>of(population.system())`), and it
writes its own `ACTR` save section. Everything the player sees happening in the world comes
from these 129 files. They are not in this document at all.

---

## 4. Tick pipeline (total order)

> ## STATUS: PARTIAL — the loop is real, 10 of 12 phases are permanently empty
>
> `PhasedSimulationEngine.tick()` genuinely iterates every phase in ordinal order, and
> `(phase, regIndex)` event visibility is real. The problem is registration: **the entire repo
> contains exactly two production `SimulationSystem` implementations** — `actor/ActorsSystem`
> (phase `ACTORS`) and `headless/HeartbeatSystem` (phase `TICK_BEGIN`).
>
> The listing below is also **numbered wrong**: there are 12 phases, not 11. `ACTORS` was
> inserted between `LIGHT` and `BOUNDARY_FLUX` on purpose and §4 was never updated. The
> corrected table:
>
> | # | Phase | Systems registered, ever |
> |---|---|---|
> | 0 | TICK_BEGIN | `InputGate` (engine-internal, hardcoded regIndex 0) + `HeartbeatSystem` in the headless demo only |
> | 1 | BUBBLE_PROMOTE | **none** — no thaw pipeline exists |
> | 2 | FLUIDS | **none** |
> | 3 | THERMAL | **none** — the `[ThermalDiffusion → PhaseTransition → Fire]` triple is fictional |
> | 4 | REACTIONS | **none** — `[PhorysReaction → ChromatisCharge → LightstoneShatter]` fictional |
> | 5 | FIELDS_RESERVED | none — **correct**, reserved by design |
> | 6 | LIGHT | **none** |
> | 7 | **ACTORS** *(undocumented in §4)* | **`ActorsSystem` — the only real registration** |
> | 8 | BOUNDARY_FLUX | **none** |
> | 9 | ECONOMY | **none** |
> | 10 | BUBBLE_DEMOTE | **none** |
> | 11 | TICK_END | engine-internal: `events.retireLap` + `world.commitTick` — **BUILT** |
>
> For C++: port the phase mechanism and the ordering discipline. Do not port the phase *list*
> as a work list. `FIELDS_RESERVED` is the model here — an honestly empty slot.

```
 0 TICK_BEGIN       tick++; InputGate: drain SimCommands (arrival order) + ScriptedActions →
                    input log; material/form paints applied directly via ChunkWriter;
                    quantity inputs emitted as External* events; event epoch advance.
 1 BUBBLE_PROMOTE   retarget diff (lazy queue invalidation); budgeted thaw ≤ 2 chunks & ≤ 2 ms
                    (base-world or snapshot + journal replay + participant rehydrate); ChunkThawed.
 2 FLUIDS           mass moves: fall, pressure BFS, spread, evap/boil/freeze, reagent consume.
 3 THERMAL          [ThermalDiffusion → PhaseTransition → Fire] (registration order).
 4 REACTIONS        [PhorysReaction → ChromatisCharge → LightstoneShatter].
 5 FIELDS_RESERVED  empty slot: aether/uether lands here without renumbering goldens.
 6 LIGHT            opacity cache update (change-log readers) → budgeted relight
                    (block-remove → block-add → sky-remove → sky-add; budget at phase boundaries).
 7 BOUNDARY_FLUX    apply hull credits to summaries/incidents; macro inflows into BORDER
                    (via FluidEmitters, dirty next tick).
 8 ECONOMY          [EconomyAccumulator (folds lap events → site deltas) → MacroScheduler.runDue
                    (workshops, prices, caravans, incidents; incidents update summaries in lockstep)].
 9 BUBBLE_DEMOTE    budgeted freeze ≤ 2/tick: transientVeto → grace → summarize → snapshot iff
                    modified → ChunkFrozen; RLE compression deferred 64 ticks, capped K/tick.
10 TICK_END         retire lap events; compact change logs; world.commitTick (revisions bumped,
                    dirty chunks sorted by chunkIndex); TickProfile; saves legal ONLY here.
```

**Rationale:** inputs → materialize → **mass → energy → state → (fields) → derived light** → boundary accounting → abstract → dematerialize → commit. Promote-before-physics makes the concrete set stable through phase 8 (parallel-seam precondition); demote-after-economy freezes against current summaries. Explicit enum, never dependency-inferred.

**Event visibility:** an event emitted at `(phase P, regIndex I)` of tick T is visible to consumers at positions after `(P,I)` in T and at-or-before `(P,I)` in T+1, then retired. All consumers are cadence-1 (EconomyAccumulator absorbs the macro cadence), so one-lap retirement is sound. Consumers must skip cells outside the concrete set; the demote pipeline folds pending mass-bearing carry-over into the summary at freeze.

> **On the rationale paragraph: BUILT in spirit.** The ordering discipline — explicit enum,
> never dependency-inferred — is real and worth keeping. The visibility rule is real and
> enforced. Only the occupants are missing.

---

## 5. Event taxonomy

> ## STATUS: PARTIAL — 18 types declared, 3 emitted, 0 consumed, 5 never written
>
> All 18 records below exist, all are registered in `EventCodec.topics()` in taxonomy order
> (save-format-stable), and the ArchUnit rule forcing them to be records of primitives is real
> and enforced. That much is genuinely BUILT and worth porting as a format.
>
> But **for 15 of the 18, the only construction site in the entire codebase is `EventCodec`'s
> decoder** (`event/EventCodec.java:145-163`). They are save-format placeholders, not traffic.
> And because `ActorsSystem` is the only registered system, **no event is read by anything at
> all** — the reader wiring is generic and correct, and there is nobody on the other end.
>
> | Event | Type exists | Real emitter | Consumer |
> |---|---|---|---|
> | `ExternalIgnition` | yes | `InputGate.java:153` | **none** (Fire absent) |
> | `ExternalFluidSpawned` | yes | `InputGate.java:155` | **none** (Fluids absent) |
> | `ExternalChargeApplied` | yes | `InputGate.java:157` | **none** (ChargeSystem absent) |
> | the other 12 rows below | yes | **none** | **none** |
> | `MacroIncidentStarted/Progressed/Ended`, `PriceChanged`, `ProductionCompleted` | **type never written** | — | — |
>
> The last five rows of the table below are not merely unemitted — those record types were
> never created.
>
> **Missing from §5:** `progression/SkillLevelledEvent` is a real, used event type,
> deliberately **not** a `SimEvent` (its own javadoc explains why). It is the only event in the
> project with actual traffic.

Events are records of primitives + ids only (ArchUnit-enforced); cell = `PackedPos` int. "Observer" rows are drained into polled logs, never bus subscriptions.

| Event (payload) | Emitter (phase) | Consumers (latency) | Status |
|---|---|---|---|
| `ExternalIgnition(cell)` | InputGate (BEGIN) | Fire (same tick) | emitted, no consumer |
| `ExternalFluidSpawned(cell, fluidId, units)` | InputGate | Fluids (same tick) | emitted, no consumer |
| `ExternalChargeApplied(cell, deltaCu)` | InputGate | ChargeSystem (same tick) | emitted, no consumer |
| `ChunkThawed(chunkIdx)` | ThawPipeline (PROMOTE) | all field systems: prime frontiers (same tick) | **NOT BUILT** |
| `ChunkFrozen(chunkIdx)` | FreezePipeline (DEMOTE) | field systems: drop entries (next tick); EconomyAccumulator | **NOT BUILT** |
| `ReagentContactEvent(cell, fluidId, units, solidId)` | Fluids | PhorysReaction (same tick) | **NOT BUILT** |
| `FluidVaporizedEvent(cell, fluidId, units, cause)` | Fluids | EconomyAccumulator (lap); steam/aether seam | **NOT BUILT** |
| `FluidFrozenEvent(cell, fluidId, units)` | Fluids | PhaseTransition: ice placement (same tick) | **NOT BUILT** |
| `TileIgnited / TileExtinguished(cell)` | Fire (THERMAL) | EconomyAccumulator (lap); Observer | **NOT BUILT** |
| `FireLuminanceChanged(cell, oldB, newB)` (4 buckets) | Fire | Light (same tick) | **NOT BUILT** |
| `TemperatureThresholdEvent(cell, thresholdId, dir)` | ThermalDiffusion | Fluids: wake settled water (next tick) | **NOT BUILT** |
| `MaterialPhaseChangedEvent(cell, from, to, yieldUnits)` | PhaseTransition | Fluids: spawn/remove liquid (next tick) | **NOT BUILT** |
| `MaterialTransformedEvent(cell, from, to, cause)` | Fire / PhaseTransition / Reactions | EconomyAccumulator (lap); Observer. Light wakes via change logs, not this. | **NOT BUILT** |
| `PressurePulseEvent(cell, gasId, magnitude)` | PhorysReaction | Fluids (next tick — documented canon latency) | **NOT BUILT** |
| `ChargeStopChangedEvent(cell, oldStop, newStop)` | ChargeSystem | Light (same tick); Observer | **NOT BUILT** |
| `ChargeSaturatedEvent(cell)` | ChargeSystem | EconomyAccumulator; Observer | **NOT BUILT** |
| `EnergyDischargedEvent(cell, releasedCu, rate)` | ChargeSystem | LightstoneShatter (same phase, later index) | **NOT BUILT** |
| `MacroIncidentStarted/Progressed/Ended` | IncidentSystem (ECONOMY) | Observer (MacroEventLog) | **record type never written** |
| `PriceChanged / ProductionCompleted(siteId, …)` | Market / Workshop | Observer | **record type never written** |

---

## 6. Shared plumbing & state ownership

> ## STATUS: MIXED. The determinism rules are BUILT and excellent. The ownership table assigns 5 of 9 fields to systems that were never written.

**Two channels, one rule:** *field deltas travel on per-lane ChangeLogs (packed ints, allocation-free, duplicate-tolerant, consumer dedupe via ActiveSet); semantic facts travel as SimEvents (low-volume records).* Per-tile event emission is forbidden unless gated by a raws flag (e.g., `ContactReactive`).

> **STATUS: SUPERSEDED in practice.** Neither channel carries traffic: no change-log reader is
> registered, and no event is consumed. `ActorsSystem` mutates its own registries directly and
> never writes world lanes. The rule remains a good rule for whoever builds the first system
> that needs it — but nothing in the shipped program exercises it, so it is untested design.

Change-log subscriptions (sealed at registration; reader-less lanes skip appends): Thermal ← MATERIAL, FORM · Fluids ← MATERIAL, FORM · Light ← MATERIAL, FORM, FLAGS, FLUID. Observer uses `ChunkRevisions` only (full remesh when revision delta > 1).

> **STATUS: NOT BUILT.** No subscriber exists, because none of the three subscribing systems
> exists. And "Observer uses `ChunkRevisions` only" is **wrong** — the observer uses neither
> channel; `revisions()` has no reference in `client-observer/src/main`.

**State ownership (sole writer / intake for others):**

| Field | Sole writer | Foreign intake | Status 2026-07-31 |
|---|---|---|---|
| MATERIAL lane | Fire (burnout), PhaseTransition, Reactions (shatter/spent/ice), InputGate, Importer — all via ChunkWriter | events above | **PARTIAL** — only `InputGate` and `TiledWorldImporter` write it. The three named sim systems do not exist |
| FORM lane | InputGate, Importer | — | **BUILT** — accurate as written |
| FLAGS | ChunkWriter (derived bits, auto on material/form writes); Fire (ON_FIRE) | — | **PARTIAL** — derived bits BUILT via `TileClassifier`/`FormOnlyClassifier`; no Fire, so ON_FIRE is never set |
| TEMPERATURE lane | ThermalDiffusion | `HeatCommandBuffer` | **NOT BUILT** — `setTemperatureDeciK` has zero production call sites; `HeatCommandBuffer` absent; lane all-zero in every shipped world |
| FLUID lane | FluidSystem | `FluidEmitters`, External/Pressure events | **PARTIAL** — written only by the importer at bake; never mutated at runtime |
| LIGHT + OPACITY lanes | LightSystem | — | **NOT BUILT** — `setLightBits` has zero production call sites; `ChunkWriter` has **no opacity setter at all** (only the generic `setLane`, itself with zero production call sites); both lanes all-zero everywhere. See §0.1 |
| CHARGE overlay | ChargeSystem | `ChargeCommandBuffer` | **NOT BUILT** — `OverlayId.CHARGE` exists; the only writer is `WorldLoader.java:157` restoring what was saved. Nothing ever creates charge |
| Summaries | FreezePipeline, BoundaryFlux, IncidentSystem | — | **NOT BUILT** — no summaries exist |
| Ledgers/prices/journals | Macro only | EconomyAccumulator deltas | **SUPERSEDED** — the built ledgers are `actor/BankLedger`, `actor/FoodMarket`, `actor/Payroll` |

**Determinism rules (binding):** no float/double in sim-core state or state-affecting math — integer/fixed-point only (Q8/Q16, milli-units, deciK); golden masters are therefore cross-platform. No `java.util.HashMap` in sim state; any side-effectful iteration is in sorted canonical key order. RNG per §1.1 #16. Saves at TICK_END only; `run K+N ≡ save@K, load, run N` including event carry-over, frontiers, quiet-tick counters, light queues, bubble queues + grace deadlines. Event cap hard-fails identically in all builds. Parallel seam (v1): `ChunkTickable` + `CommitBuffer` and per-chunk event/frontier staging buffers — **parallel primitive arrays, never object tuples** — merged at phase barriers in canonical chunk order; shipped single-threaded.

> ## STATUS: BUILT — this is the one part of the document that holds up completely
>
> | Rule | Status |
> |---|---|
> | No float/double in sim-core state | **BUILT + enforced** — `NO_FLOATING_POINT_FIELDS` |
> | No `java.util.HashMap` in sim state | **BUILT + enforced, stronger than the doc** — `NO_HASH_CONTAINER_FIELDS` also bans bare `Map`/`Set` declared types, so a HashMap cannot hide behind an interface |
> | Events are primitives + ids | **BUILT + enforced** — `EVENTS_ARE_RECORDS_OF_PRIMITIVES` |
> | sim-core purity (no libGDX/client/tools deps) | **BUILT + enforced** — `SIM_CORE_STAYS_PURE` |
> | Event cap hard-fails in all builds | **BUILT** — `PhasedEventBus.java:152`, unconditional |
> | Saves at TICK_END only | **BUILT** |
> | `run K+N ≡ save@K, load, run N` | **PARTIAL** — `TwinRunDeterminismTest` exists; the clause "including … frontiers, quiet-tick counters, light queues, bubble queues + grace deadlines" refers to state that does not exist |
> | Parallel seam (`ChunkTickable`, `CommitBuffer`) | **NOT BUILT** — no such types. The doc does say "shipped single-threaded", so this is honest |
>
> **For C++ this is the highest-value paragraph in the document, and the riskiest.** The four
> ArchUnit rules are what keep the twin-run gate green, and **none of them survives the move to
> C++** — there is no ArchUnit. Whatever replaces them (a clang-tidy check, a lint pass over
> struct members, a code-review checklist) needs to exist before the first system is written,
> not after the first nondeterminism bug.

---

## 7. Bubble & freeze/thaw boundary

> ## STATUS: NOT BUILT — ENTIRELY. Plan only.
>
> No part of this section runs. `engine/AllConcreteBubble` allocates every interior chunk as
> concrete, unconditionally and forever: `levelOf() → ACTIVE`, `isConcrete() → true`. There is
> no ticket, no hysteresis, no grace, no veto, no summary, no snapshot, no journal, no
> boundary flux, no rind, no hull. The 800-chunk cap does not exist; nothing has ever been
> frozen.
>
> The design below is good and detailed and should be kept for whoever needs a bubble later.
> **It should not be read as behaviour, and it must not be implemented in C++ during the
> rewrite** — nothing in the game currently needs it, and everything in §8's memory budget that
> depends on it is therefore also unbuilt. Read the whole section in the future conditional.

- **Tickets:** ACTIVE (7×7 focus columns × 4 z-chunks ≈ 196 chunks) · BORDER (1-ring shell, full physics, flux apron, ≈ 290) · FROZEN. A 1-chunk FROZEN_RESIDENT rind around BORDER stays readable. Hysteresis: promote ≤ R, demote ≥ R+2, 300-tick grace; compression deferred 64 ticks, capped/tick. Concrete-chunk hard cap 800 (force-freeze farthest first); lazy queue invalidation on retarget.
- **Freeze:** veto poll (600-tick force cap) → participants `contributeSummary` → in-flight handoff (burning → `MacroFire`; water volume; charge summed into `storedEnergyCu`, held constant unless the site declares an energy process) → snapshot **iff modified since import/last snapshot** → FROZEN. Sparse overlays (charge, wear, transition progress) and the FLUID/LIGHT lanes survive verbatim in the blob.
- **Thaw:** base-world or snapshot → journal replay `(tick, seq)` (FireDamage walks flammables in fixed scan order with derived RNG; WaterLevel settles bottom-up scanline; burned-out chunks convert N lowest-index flammables to `burnsTo`) → banked boundary inflow re-injected whole-chunk scanline (residual stays banked) → participants rebuild frontiers → light thaw-verification pass on the 6 face shells → enters as BORDER.
- **Hull physics:** `ChunkWriter` rejects non-concrete writes; the system credits `BoundaryFlux`; BOUNDARY_FLUX applies to summaries/incidents. Nothing silently dies or vanishes at the wall; conservation audits include summary banks.

> **One piece of §7 IS built:** `ChunkWriter` really does reject non-concrete writes with a
> defined return code (`REJECTED_VOID`, exercised by `ChunkWriterTest`). The rejection half
> shipped; the crediting half never did.

---

## 8. Chunk & storage numbers

> ## STATUS: SPLIT CLEANLY. Bullets 1 and 2 (layout) are BUILT and verified against shipped bytes. Bullets 3 and 4 (memory, budget) are NOT BUILT.

- **Chunk:** 32×32×8 = 8,192 tiles; `localIdx = (z<<10)|(y<<5)|x`; max world 4096×4096×64 + 1-chunk VOID border; flat `Chunk[]`, int chunkIndex.

> **BUILT** — `world/Coords.java`, `world/WorldConfig.java:22-30`, `world/PackedPos.java:36`.
> Already ported to C++ and pinned by tests on both sides.

- **Dense lanes (B/tile):** MATERIAL 2 · FORM 1 · FLAGS 1 · TEMPERATURE 2 (deciK) · FLUID 2 · LIGHT 2 · OPACITY 1 = **11 B/tile**; + changedBits (1 KB) + header → **≈ 91 KB per concrete chunk (~11.2 B/tile)**; system-private (thermal frontier 1 KB, fluid frontiers 2 KB wet-only) ≈ +3 KB. `laneDirtyMask` is an `int`.

> **PARTIAL.** The seven-lane registration at those widths is **BUILT** and confirmed in every
> shipped save's META (`material 2, form 1, flags 1, temperature 2, fluid 2, light 2,
> opacity 1`). But:
> - **Three of the seven lanes have never held a nonzero value in any shipped world** —
>   TEMPERATURE, LIGHT, OPACITY. That is 5 of 11 bytes per tile, ~45% of dense chunk memory,
>   allocated, saved, RLE-compressed and hashed every tick, permanently zero. Keep them for
>   format compatibility; RLE crushes them to ~7 bytes a chunk. Do not read their presence as
>   partial implementation.
> - `changedBits` is **BUILT but not where this says** — it lives in
>   `world/change/ChunkRevisions.java:30-40`, not in `Chunk`. `Chunk` is lanes + overlays only.
> - **`laneDirtyMask` does not exist** anywhere in the repo.
> - The system-private frontier figures describe systems that do not exist.

- **Memory:** 256×256×32 region (256 chunks) ≈ **24 MB**. Full concrete set (486) ≈ 45 MB; rind (~480 resident) ≈ 44 MB; `ChunkSummaryStore` 196,608 × 64 B ≈ 12.6 MB (whole city, always); frozen-compressed city ≈ 50 MB (2–12 KB/chunk RLE). Total steady ≈ **150–170 MB**.

> **NOT BUILT.** `ChunkSummaryStore` does not exist, and with no freeze path the
> "frozen-compressed city" figure is not measurable. The whole steady-state budget rests on a
> bubble that allocates everything concrete. **Do not size the C++ rewrite from these numbers.**

- **Budget (8 ms, single core):** engine/events 0.3 · promote 0.4 · fluids 2.0 · thermal+fire 2.0 · reactions 0.3 · light 1.8 (≈ 60k-visit cap) · flux+economy 0.3 · demote 0.4 · commit 0.3 ≈ 7.8 ms. Documented spike lines: boundary-crossing tick +0.5 ms; save ≤ 500 ms at TICK_END. Enforced by CI perf tests, not aspiration. Cross-system write ceiling ~200k/tick (world overhead ≤ 2 ms).

> **NOT BUILT, and the "enforced by CI perf tests, not aspiration" claim is false as written.**
> Six of the nine budgeted phases have no system at all. Only two timing gates exist in the
> whole repo: `WritePathBenchTest` (`@Tag("benchmark")`, **excluded from `test`**, ceiling
> relaxed from the M0 criterion of 15 ns to 150 ns) and the observer's opt-in `--perf` flag,
> which is not wired into `check`. `TickProfile` is recorded and never asserted on.
> Note also that the budget assumes the doc's 100 ms tick; the shipped tick is 1000 ms
> (`TickClock.MILLIS_PER_TICK = 1000`), so even the framing is stale.

---

## 9. Save container (TROJSAV)

> ## STATUS: BUILT — with four precise corrections. This is the most trustworthy section in the document after §11.1/§11.2.
>
> The container, TOC, CRC32C-over-uncompressed, lazy verify, atomic tmp+rename, importer-emits-
> at-tick-0 and byte-determinism claims are all real and were independently verified by
> decoding the shipped `.trojsav` files: every constant pinned in
> `native/content/tests/fixtures.hpp` (file sizes 845/2051/17695, chunk counts 36/108/192, META
> length 77, WRLD lengths 5668/22483/86436) matched an independent decode exactly.
>
> **Corrections:**
> 1. **Sections: PARTIAL.** All the listed section ids are declared constants, but **shipped
>    files contain only `META` + `WRLD`** (TOC size 2). At runtime `PhasedSimulationEngine.save`
>    writes INPT, EVNT, AETH, META, WRLD, CHNG + one per system; the only registered production
>    system is `ActorsSystem` → **`ACTR`**. **`FLUD`, `THRM`, `REAC`, `LGHT`, `BUBL`, `ECON`
>    have no owner and are never written** — `TrojSav.java:65-89` declares them with javadoc
>    telling a future owner how to pin them.
> 2. **`CHNG` is BUILT and missing from this section's list** (`TrojSav.java:59`). So is `ACTR`.
> 3. **"Deflate-1" is imprecise:** blobs are **zlib-wrapped** deflate (every blob starts
>    `78 01`), not raw deflate. `native/content/include/granadad/content/trojsav.hpp:22-25`
>    calls this out where the doc does not — and also flags that a C++ writer cannot reproduce
>    Java `Deflater`-level-1 bytes, so **byte-determinism of the save is a Java-side property
>    only**. That matters for the rewrite: the C++ writer will need its own baseline.
> 4. **"rawsFingerprint — mismatch = hard fail" is PARTIAL.** It is enforced in
>    `client/boot/FixtureWorldLoader.java:127-130`, **not** in sim-core's `TrojSav`/
>    `WorldLoader`; `PhasedSimulationEngine` writes `RAWS_FINGERPRINT_NONE = 0L` into every
>    runtime save. Baked files carry a real fingerprint (`0x6101F30069B57FF1`, identical across
>    all three).
> 5. **"Participants' `serialize` is pure and shared by saver and freezer" is NOT BUILT** —
>    `FreezeThawParticipant`, `contributeSummary` and `transientVeto` do not exist, and there is
>    no freezer to share with.

Little-endian; header (magic, formatVersion, worldSeed, tick, rawsFingerprint — mismatch = hard fail) + TOC + Deflate-1 sections with CRC32C: `META`, `INPT` (input log), `EVNT` (carry-over lap), `WRLD` (lanes/overlays via ChunkCodec, canonical order), one per system (`FLUD`,`THRM`,`REAC`,`LGHT`,`BUBL`,`ECON` — frontiers, quiet counters, queues, journals, summaries), `AETH` reserved-empty. Participants' `serialize` is pure and shared by saver and freezer. Missing section on load = system-default init only if the section is declared optional; otherwise hard fail. Atomic tmp+rename. The Tiled importer emits this format at tick 0; importer output is byte-deterministic.

---

## 10. Raws schema — canonical example (Chromatis)

> ## STATUS: BUILT — the schema, the loader and the full validation list are real and boot-fatal. Three drifts, all noted below.
>
> This is the best-implemented section of the document. `MaterialRawsLoader.java:54-95` carries
> the validation list as a doc block and every listed rule is implemented and fails boot.
> **Port the rules, not just the parser.**
>
> **Drifts:**
> 1. The middle color stop's tint is `#E3CE7A` in `content/raws/materials/chromatis.json`, not
>    `#E8842A` as printed below. Deliberate — the file's `notes` records the HYBRID ruling that
>    moved the orange to a renderer heat-glow overlay. **The file wins.**
> 2. "reactions (phorys: any `liquid` tag, expansion 240, **wear 5/unit**)" — the liquid trigger
>    and `expansion: 240` are correct; **wear is SUPERSEDED**. The NO-WEAR ruling (Eli,
>    2026-07-12) made phorys inexhaustible in v0 and the wear fields were removed from the raw.
>    "wear 5/unit" is a dead number still being printed.
> 3. **"macro (commodities/recipes/site-kinds/incidents)" is NOT BUILT** — `content/raws/macro/`
>    does not exist. What `content/raws/` actually holds: actors, barks, factions, fluids, jobs,
>    materials, names, quests, reactions, rumors, skills, spells, treatments.
>
> **Features are parsed but never acted on.** All four `MaterialFeature`s — `chargeable`,
> `shatterOnSpike`, `emissive`, `contactReactive` — are parsed, cross-validated (e.g.
> `shattersTo` must resolve; `contactReactive` must match the reaction raw) and folded into the
> raws fingerprint. **None has a consumer.** Glowstone emits nothing; nothing ever charges,
> shatters or contact-reacts.
>
> **"Appearance bucket = color-stop ordinal (0..3), served by `AppearanceQuery`" is NOT BUILT.**
> `AppearanceQuery` does not exist; `client/render/TilePlan.java:35` hardcodes
> `APPEARANCE_BUCKET = 0` for every tile, commented "F5 will read real charge state".
>
> **Treatments are BUILT** — getilia really does mint `trudgeon_wood@getilia_soak` with
> flammability 0 at load (`content/raws/treatments/getilia_soak.json`,
> `MaterialRawsLoader.java:50-53`).

`content/raws/materials/chromatis.json` (temps in integer Kelvin; Q8 fixed-point; charge in 16-bit-safe cu):

```json
{
  "id": "chromatis",
  "displayName": "Chromatis",
  "phase": "SOLID",
  "density": 6800,
  "hardness": 8,
  "flammability": 0,
  "ignitionK": null,
  "meltK": 2600, "meltsTo": "chromatis_melt", "meltYieldUnits": 7,
  "boilK": null,
  "conductivityQ8": 200,
  "heatCapacityQ8": 96,
  "fuelTicks": 0, "burnsTo": null,
  "valueCp": 40000,
  "tags": ["metal", "alloy"],
  "light": { "opacity": 31 },
  "features": {
    "chargeable": {
      "capacityCu": 60000,
      "maxSafeDischargePerTick": 600,
      "saturationPct": 95,
      "saturationHeatDeciKPerTick": 20,
      "equilibriumDeciK": 6000,
      "colorStops": [
        { "uptoPct": 60,  "tint": "#9FB8D8", "lightLevel": 0 },
        { "uptoPct": 95,  "tint": "#E8842A", "lightLevel": 4 },
        { "uptoPct": 100, "tint": "#F5C542", "lightLevel": 8 }
      ]
    }
  }
}
```

Appearance bucket = color-stop ordinal (0..3), served by `AppearanceQuery`; art mapping keys `byAppearance` on it. Sibling files: treatments (getilia mints `trudgeon_wood@getilia_soak`, flammability 0, at load), reactions (phorys: any `liquid` tag, expansion 240, wear 5/unit), fluids (Kelvin thresholds), macro (commodities/recipes/site-kinds/incidents — all numbers fixed-point at load; unknown precision = boot error). **Loader validation (boot fails):** conductivity ≤ 256; per-material `Σ(w/cap) ≤ ½` stability invariant (min heatCapacity enforced); FLAMMABLE ⇒ ignition+fuelTicks(≤4095)+burnsTo; melt ⇒ meltsTo+yield; liquid tag ⇒ boilsTo; chargeable/spike values fit 16 bits; treatment targets exist; derived-id collisions; reaction refs resolve; color stops monotone.

---

## 11. Flagship scenario traces

> ## ⚠ STATUS: NOT BUILT — ALL FOUR. THIS IS THE HIGHEST-RISK SECTION IN THE DOCUMENT.
>
> Four scenarios written in **present-tense narrative prose**, with per-tick event orderings
> and golden assertions, none of which exist. A reader taking §11 as specification would infer
> at minimum: a thermal system, a fire system, a fluid system with pressure BFS, a light
> system, a charge system, a shatter system, a boundary-flux ledger, a macro scheduler, and a
> golden-master harness. **That is the six months of phantom scope this reconciliation exists
> to prevent.**
>
> Concretely: **there are no golden files in the repo.** No `golden.v1.json`, no golden
> directory, no `content/scenarios/`. `headless/` is three classes. Every "Golden asserts…"
> and "acceptance also verified headless" sentence below describes a harness that was never
> written.
>
> The only surviving traces of Tavern Fire are two validators that keep the *map* honest for a
> scenario that does not exist: `tools/.../MarkerContractPass.java:136` checks that an ignition
> anchor sits on a flammable tile, and `TavernFixtureIntegrationTest.java:154` asserts the
> target is the oak table. The map is authored for a scenario that was never built.
>
> Keep this section — it is the clearest statement of what the physics engine was *for*, and if
> that engine is ever built these are its acceptance tests. Read every verb below as "would".

**Tavern Fire.** Script tick 10 → `ExternalIgnition` (BEGIN) → Fire ignites (THERMAL), `FireLuminanceChanged` → Light same tick. Diffusion (invCap multiply-shift, energy-residual carry) heats oak past ignition on the frontier; jitter via counter RNG. Buoyancy ×4 up-z + 50% plume into z+1 carries heat across z-levels. Getilia-trudgeon (flammability 0, minted at load) never enters the burning map; stone conducts only. Tick 300 `ExternalFluidSpawned` water → FLUIDS places it → THERMAL same tick heats it; fire extinguishes by querying `FluidView`. Fuel-out: Fire swaps to ash via ChunkWriter (FLAGS caches auto-updated → light change log wakes; `MaterialTransformed(BURNOUT)` → economy damage). Golden asserts ash count, treated tiles unchanged, per-tick hash chain.

**Chromatis Experiment.** `ExternalChargeApplied` each tick → ChargeCommandBuffer → ChargeSystem accrues CHARGE overlay (survives freeze verbatim; summarized as `storedEnergyCu`). Stop crossings emit `ChargeStopChanged` → Light + observer bucket (silver/blue → orange → gold). ≥ 95%: `ChargeSaturated`; heat injected via HeatCommandBuffer warms neighbors next tick. Script drains 60,000 cu in one tick ≫ 600 → `EnergyDischarged(rate)` → ShatterSystem (same phase, later index) scans Chebyshev ≤ 2 incl. self → lightstone → shards (`MaterialTransformed(SHATTER)`); light emitter removed same tick.

**Sewer Flood.** Source → FLUIDS: 1 z/tick cascade; sealed chamber fills bottom-up via bounded BFS pressure (`z < headZ`, per-chunk displacement cap 32). Drain empties the pond because every depth *decrease* wakes 4 laterals + above. Near forge heat: `TemperatureThresholdEvent` wakes settled water next tick; hash-phase evaporation (pinned mix constants) removes units, ledgered. Water at the hull: write rejected → `BoundaryFlux` credit → frozen neighbor summary; audit exact to the unit including banks. Light no-ops on sub-threshold depth changes (effective-opacity rule).

**Forge Economy.** Fully abstract; sites + road edges + baked summaries from the importer. Workshop cycles on the timing wheel consume/produce ledger milli-units; fixed-point stock-ratio prices; caravans via far-heap scheduling. Mine shut off → `MacroShortage` incident + ingot price ≥ +20% within 5 abstract days. Identical results with the camera parked on the forge (ledger-always-canonical; stockpile tiles are a rendering). Observer sees it in `MacroPanel`; acceptance also verified headless.

---

## 11.1 The twin-run gate (determinism, enforced)

> ## STATUS: BUILT — accurate in every detail, verified line by line.
> The task, the `dependsOn(twinRunGate)` on `:client-observer:check`, the
> `outputs.upToDateWhen { false }`, the `-PtwinTicks` override, `TwinRunGateMain`,
> `docs/BASELINE-WORLD-HASH.md` — all real. **This section and §11.2 are the two most accurate
> in the document, because both were written *after* the code they describe.** That is the
> lesson: write architecture docs behind the code, not ahead of it, or mark the ones written
> ahead. **Build a C++ equivalent of this gate early.**

"Two identical runs must produce byte-identical reports" was a house rule with no enforcement
behind it for five sprints — every twin-run claim in the log was a hand-run diff. It is now a
task:

```
./gradlew.bat :client-observer:twinRunGate            # 15,000-tick Docks soak, twice, one seed
./gradlew.bat :client-observer:twinRunGate -PtwinTicks=2000   # while iterating
```

It runs the soak twice and fails the build on ANY divergence, on two comparators that do not
subsume one another:

1. **the combined `WorldHasher` hash** — `WRLD` folded with the `ACTORS` section sub-hash, i.e.
   the whole persisted triad. Catches state divergence that never reaches a printed line.
2. **the full report text, byte-for-byte** — catches divergence that lives only in the reporting
   path (unordered iteration, an identity hash leaking into an ordering, a locale-formatted
   number), which the state hash cannot see.

The gate is wired into `:client-observer:check`, so `gradlew build` cannot go green on a
nondeterministic ward. The two runs deliberately share **one JVM**: static leakage and
identity-hash ordering diverge between two runs in the same process, and two pristine forked
JVMs would leak identically and pass. The trade-off is stated rather than hidden — this gate
does not catch divergence that is stable within a JVM but varies across machines; sim-core's
no-float rule covers that side.

`DocksActorsMain` ends its report with the `WORLD HASH` section the gate greps. A report with
no hash line is a hard failure in the gate, never a silently skipped comparator. The pre-arc
baseline lives in `docs/BASELINE-WORLD-HASH.md`; the gate reports MATCH/DRIFT against it and
never fails on drift.

## 11.2 The trade vocabulary (S8, "The Ward Prices Itself")

> ## STATUS: BUILT — accurate in every detail, verified line by line.
> `actor/TradeGoods.java` (4-value `Category` enum with symbol/weight/category/base-price rows),
> `actor/ActorRawsLoader.java:256-279` (symbol → kind, MATERIALS-only enforcement, both-or-
> neither pair), `content/raws/actors/{cat,feral,mouse}.json`,
> `actor/job/JobBehaviors.java:78-100`, `actor/Actor.java:234,1180-1186`, `actor/CullVerb.java`,
> `client/scenario/GoodsCensus.java:28-41`.
>
> **Note what this section is about.** Trade categories, item kinds, yields, scalps, culling,
> per-kind conservation — this is the *actor* economy, and it is the only economy the project
> has. §3's `com.trojia.sim.macro` and §11's Forge Economy are the unbuilt alternative. When
> the C++ rewrite needs an economy, this is the one.

Four fixed trade categories, ruled by Eli and binding across the S8-S12 arc: **materials, food,
commodities, services.** No fifth, no renames. `SERVICES` deliberately holds no item kind and
never will — a service IS a quest (S10), a contract to perform work, not a stack you can carry.

`ItemKinds` stays the append-only hardcoded vocabulary (ids ride the ItemsLite save format).
Beside it, `TradeGoods` carries one static row per kind: raws **symbol**, per-unit **weight**,
**category**, and a fixed **base price** (S9's daily tick moves off that base rather than
replacing it). The table is plain Java, not a raws loader, because a loader would reintroduce
the id-allocation problem the hardcoded vocabulary exists to avoid.

The **symbol column is the one place a content string becomes a kind id.** Actor raws name a
scalp with it (`scalpItem`), quest raws name their items with it, and both go through
`TradeGoods.kindForSymbol`. The observer's old hardcoded 5-case `itemKind` switch is gone.

**Yields.** `JobParams` carries a `yieldKind`/`yieldPerUnit` pair applied at
`JobBehaviors.awardWorkEvent` — the same seam the S5 training pair and the S6 DUTY pair ride
(unit completion / waypoint arrival / dwell completion), never per tick. A rope is finished,
not accrued by the second.

**Scalps are an item problem, not a counter.** A scalpable type declares `scalpItem` +
`scalpResist` in its raws and drops that named item when culled, so kill-tracking inherits
everything items already have: persistence, movement through counters, a conservation proof.
Scalps are MATERIALS — a raw harvested by-product like a hide. Vermin only through S12: combat
is out of the arc, so human scalps defer.

**The cull verb rides the work-event seam, not a policy.** A CULL policy would send people
walking across the ward to stand over carcasses, which moves the population, which is how you
quietly starve the predators whose food supply those carcasses are. Riding the seam means the
ward's routes do not change: the hands that cull are the hands already working there. The verb
never writes to the body's revive timer, and a per-culler `culledUntilTick` latch stops one
carcass being farmed. Both are covered by named tests, because both are the kind of thing that
fails silently.

**Conservation is per kind, never lumped.** `ActorsSystem` keeps a mint counter and a
genuinely-sunk counter for each kind; `GoodsCensus` checks each against an independent physical
scan of ItemsLite. One lumped goods total is satisfied by one busy yard while three others mint
nothing. Every reported line prints DISTINCT holders and the fattest single holding beside the
unit count, for the same reason: 200 units across the ward and 200 units in one hoarder's sack
are not the same ward.

## 12. Milestone plan

> ## STATUS: SUPERSEDED — abandoned after M1. Keep for the acceptance criteria, not for the route.
>
> | M | Status |
> |---|---|
> | **M0 Bootstrap** | **BUILT** — every named deliverable exists. One caveat: the write-path acceptance number is a `@Tag("benchmark")` test **excluded from `test`** with a 150 ns ceiling, not the 15 ns criterion |
> | **M1 Content spine** | **PARTIAL** — raws loaders, registries, treatments, Tiled importer with a byte-identical test, genPalette, 14 validator passes, observer rendering imported maps: all ✔. **`SummaryBaker` does not exist**, so "baked summaries match freeze-path output" is unmet on both sides |
> | **M2 Thermal/fire/reactions** | **NOT BUILT** |
> | **M3 Fluids** | **NOT BUILT** |
> | **M4 Light** | **NOT BUILT** — and should not be built. See §0.1. What ships instead is the client-side `LampGlowMap`/`LampMarkersLoader`/`AmbientLight` stack, which is the right answer |
> | **M5 Bubble & persistence** | **NOT BUILT** — `sim/bubble/` is two files |
> | **M6 Macro economy** | **NOT BUILT** — superseded by the `actor/` economy |
> | **M7 v0 complete** | **NOT BUILT**. Note its own criterion "ARCHITECTURE.md ↔ code enforced by ArchUnit package rules" is unmet — `ArchitecturePurityTest` has four rules and enforces **no package map**. §13 risk-4's mitigation (an ArchUnit test banning unflagged per-tile emission) also does not exist. **This is precisely why the drift this document is being reconciled for went unnoticed.** |
>
> **The ladder the project actually walked** is visible in git and §12 names none of it:
> `M0` → `F1` → `F2` → `M1` → `F2.5` → `M2 (art)` → Sprints 1-8 → quality pass → the native C++
> rewrite. The letters even collide: git's `M1` is the importer/observer and git's `M2` is
> sprites and art, neither of which is what §12 calls M1/M2. And `docs/PLAN-v0.md:82-89`
> carries a **third, different** M0-M7 ladder (its M2 = determinism spine, M3 = thermal,
> M5 = light) that conflicts with both. **Three incompatible milestone ladders exist in this
> repo. Do not navigate by any of them.**

| M | Deliverable | Acceptance criteria |
|---|---|---|
| **M0 Bootstrap** | Modules compile; world lanes + ChunkWriter + ChangeLogs + ActiveSet; engine phases + event bus + RNG + InputGate; TROJSAV skeleton; WorldHasher | Twin-run 1k scripted-write ticks: identical hash chains, two engines in one JVM; write path < 15 ns; ArchUnit purity suite green |
| **M1 Content spine** | Raws loaders + registries (incl. treatments); Tiled importer + genPalette + SummaryBaker; observer renders imported map; headless runs scripts | Tavern fixture imports byte-identical twice; observer 60 fps on imported map; validator error UX per spec; baked summaries match freeze-path output |
| **M2 Thermal/fire/reactions** | Whole-map-active physics on small maps | Conservation exact (`Σcap·T + sink`); Tavern Fire golden (minus water beat); chromatis charge/color/heat/shatter test; phorys chain vs. fluid stub; tick ≤ 4 ms |
| **M3 Fluids** | FluidSystem on the FLUID lane | Sewer Flood golden incl. drain-down; U-bend; per-tick audit exact; regression trio (drain-at-bottom, phorys pond, floating column); Tavern Fire water beat lands; combined ≤ 6 ms |
| **M4 Light** | Relight + opacity lane + buckets + observer tints | Falloff/shadow-cone/shaft-seal tests; lateral-window removal test; flood relight-count cap; budget carry-over determinism; full pipeline ≤ 8 ms |
| **M5 Bubble & persistence** | Tickets, participants, BoundaryFlux, journals, full save/load | Freeze/thaw bit-identical; save mid-lap resumes identical; hull conservation to the unit; 20-column pan soak under fast-forward holds memory cap & budget; re-freeze-mid-incident test |
| **M6 Macro economy** | Scheduler, workshops, prices, incidents, MacroPanel | Forge Economy golden; abstract tavern burnout ∈ [7200, 21600] ticks with getilia excluded; ledger-canonical with camera parked on site |
| **M7 v0 complete** | All flagships blessed; polish | Four goldens pass twin-run + save/load + (simulated) shuffled-merge; worst flagship tick ≤ 8 ms on reference core; observer journal replays in headless to identical hash; ARCHITECTURE.md ↔ code enforced by ArchUnit package rules |

---

## 13. Top 8 risks & mitigations

> ## STATUS: MIXED — and risk #9 is the one that actually landed.
> Risks 1-8 below are risks to a program that was never built, so their mitigations are
> unbuilt too. Two are worth calling out specifically:
> - **#1's mitigation is NOT BUILT** — there are no CI per-phase perf gates. See §8.
> - **#4's mitigation is NOT BUILT** — there is no ArchUnit test banning unflagged per-tile
>   emission.
>
> **The risk that actually materialised is not on this list:**
>
> **9. Documentation drift into phantom scope.** An architecture document written ahead of the
> code, marked AUTHORITATIVE, and never reconciled, describes unbuilt subsystems in
> present-tense operational detail — and the next team (or the next rewrite) builds them.
> *What it cost here:* nineteen days of sprints during which this file described a
> materials-physics engine while the project built an actor simulation, and a C++ rewrite that
> was about to be specified from it.
> *Mitigation, binding from now on:* (a) every subsystem section carries an explicit status
> marker, checked whenever the section is touched; (b) sections written ahead of the code are
> marked as plan at the moment they are written, not retroactively; (c) prefer §11.1/§11.2's
> pattern — document behind the code; (d) M7's "ARCHITECTURE.md ↔ code enforced by ArchUnit
> package rules" was the right idea and was never built, so it stayed a document problem
> instead of a build failure. A trivial version — assert the set of packages under
> `com.trojia.sim` matches a list in this file — would have caught the light package on day
> one and costs an afternoon.

1. **Tick-budget aggregation optimism** — each subsystem quoted best case. *Mitigation:* per-phase budget lines (§8) enforced as CI regression gates from M2 on; degrade knobs already designed (light visit cap, displacement caps, compression cap).
2. **Freeze/thaw seam correctness** — the most fragile path in the system. *Mitigation:* single pure `serialize` shared by save and freeze; conservation audits every tick under `-ea`; re-freeze-mid-incident and mid-pan save/load tests are milestone gates.
3. **Golden-master brittleness** — one wrong bless poisons everything. *Mitigation:* per-system sub-hashes name the first divergent system + tick; integer-only math makes goldens cross-platform; bless only via `--bless` after review; RNG mixing fixed before first bless.
4. **Dual plumbing misuse** (change logs vs. events) — new code emitting per-tile events. *Mitigation:* the §6 rule + ArchUnit test banning unflagged per-tile emission; reader-lag caps fail loudly.
5. **Stuck-water class of bugs** — wake-rule regressions are silent. *Mitigation:* the regression trio + quiescence assertions in every fluid test; wake-on-decrease is a stated invariant, not a fix.
6. **Relight storms** in fire/flood scenarios. *Mitigation:* luminance buckets, effective-opacity no-op, budget slice with specified carry-over semantics; Sewer Flood relight-count assertion.
7. **Save-format churn** breaking goldens mid-project. *Mitigation:* sectioned container with per-section versions and in-system migrations; rawsFingerprint hard-fail; format frozen at M5.
8. **Macro scope creep / fixed-point fidelity** — the economy invites features and floats. *Mitigation:* v0 fence: elasticity ∈ {½,1,2}, no agents/labor market, ledger-always-canonical; Forge Economy acceptance test *is* the definition of done.

---

## 14. Reconciliation provenance (2026-07-31)

Added by the reconciliation pass so the next reader knows exactly how much to trust the status
markers above.

### How the statuses were established

Three parallel audits against the **main working tree only** (`.claude/worktrees/*` copies
deliberately excluded), plus a verification pass:

1. **§3-§6 package audit** — file-by-file package census, grep for every named type, grep for
   `implements SimulationSystem`, plus an empirical lane scan loading all three shipped baked
   worlds through the real `WorldLoader`.
2. **Light audit** — package listing, `TickPhase` reference grep, an independent PowerShell
   decode of all three `.trojsav` files (TrojSav container → inflate → META lane table → WRLD →
   per-chunk `ChunkCodec` RLE/RAW), and a marker count in the authored `.tmx` files.
3. **§8-§12 format/milestone audit** — an independent Python decoder for the container and
   chunk codec, cross-checked against every constant pinned in `native/content/tests/
   fixtures.hpp`.
4. **Verification pass before this rewrite** — re-confirmed the load-bearing claims
   independently: the five absent packages, the 12 `TickPhase` constants with `ACTORS` at
   ordinal 7, `TickClock.MILLIS_PER_TICK = 1000`, the two `SimulationSystem` implementations,
   zero production call sites for `setLightBits`/`setTemperatureDeciK` and zero calls to
   `Tile.light()`, the 27 `light_source` markers in `docks_surface.tmx`, the four ArchUnit
   rules, the `twinRunGate` task and its `check` wiring, the chromatis `#E3CE7A` tint drift,
   the phorys NO-WEAR ruling, the absence of `content/scenarios/` and any golden file, and the
   package file counts (259 sim-core, 129 actor, 18 progression, 162 observer).

The all-zero TEMPERATURE/LIGHT/OPACITY finding has **three independent confirmations**: two
separate byte decoders written in different languages, and the C++ test suite's own assertion
at `native/content/tests/test_world_reader.cpp:155-165`.

### What was NOT verified — read this before relying on a status marker

- **Nothing was built or run.** No `gradlew test`, no `twinRunGate` execution, no observer
  boot, no screenshot. Every **BUILT** verdict rests on reading source plus decoding shipped
  bytes, **not on a green build**. In particular: `TwinRunDeterminismTest`,
  `ArchitecturePurityTest`, `TiledImporterDeterminismTest` and the twin-run gate are confirmed
  to be *declared*, not confirmed to *pass*.
- **The native C++ tests were not run** (no cmake/ninja/compiler on PATH; the documented path
  is `docker compose run --rm --build build`). §9's C++-side claims were corroborated by
  independent decode matching the pinned fixtures, which validates the *pinned facts*, not the
  *code*.
- **"lamp light sources loaded: 27" was never observed on screen.** It was traced statically
  (`ObserverApp.java:1288` → `loadLampGlow` → `LampMarkersLoader`) and the number was confirmed
  by counting 27 `light_source` markers in `docks_surface.tmx`. The print itself was not seen.
  Lamps were also not visually confirmed to render.
- **Git history was not checked** for whether any of the phantom subsystems were built and
  later removed, versus never built at all. The statuses say "does not exist now", not "never
  existed".
- **The 14 `.claude/worktrees/*` checkouts were not read.** They contain near-identical copies;
  it was not checked whether any holds a light/thermal implementation that never merged.
- **"Zero production call sites" was established by grepping `src/main`.** Reflection or
  string-based dispatch would evade it. No evidence of either was seen; its absence was not
  proven.
- **The baked worlds were not re-fingerprinted.** All three carry `0x6101F30069B57FF1` and
  agree with each other; it was not recomputed from `content/raws/`.
- **`content/` was opened read-only and nothing in it was modified.** It is authored canon.
