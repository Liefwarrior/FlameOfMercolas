# Flame of Mercolas — MVP Plan (v0: Simulation Core)

## Context

Eli is building a game where you play **Gabri, Wielder of the Flame of Mercolas**, in a sandbox of **Granadad**, capital of the empire of **Trojia** (from his novel *Lord of Trojia*). Vision: Ptolus-style layered urban high fantasy (gated Inner City, lawless slum suburbs, sewers, crypts below) + Morrowind-style interlocking systems (start fragile, manipulate systems to become wildly powerful) + retro-2D presentation (Moonring / Skald / World of Horror aesthetic).

## ★ Design North Star (Eli: "VERY KEY — take to heart through the whole design process")

> **Social power is maxed from the start; physical power starts near zero and grows Morrowind-style.**
> Gabri is the Wielder of the Flame of Mercolas: lawful immunity, every door opens, everyone defers — a verb set no other RPG protagonist gets (walk into the palace, seize evidence, break any law, requisition anything). But the newly-chosen Wielder physically struggles to put down a single bloodletter. The whole game arc is closing that gap by manipulating the world's interlocking systems.
>
> Design consequences to honor everywhere: (1) authority/deference/law is a **first-class domain concept**, not a reputation stat bolted on later — even v0's abstract district/faction model should reserve the seam for it; (2) power growth comes from **systems, not levels** — the simulation must be honest enough to be exploited; (3) NPC fear/respect of the Wielder is social truth, never mechanical invincibility.

**Explicit mandate: simulation before gameplay.** v0 is a Dwarf Fortress-depth physical simulation of the world plus a god-view observer client. No player character, no combat, no quests yet.

The prior attempt (`C:\repositories\LordOfTrojia-MVP`, Unity) pivoted 2D → HD-2D iso → 3D and stalled each time on art-asset mismatch, engine/vendor coupling, and systems bolted on late. This project inverts that: **systems-first, code-first, in Eli's native language (Java)**.

## Decisions already made (with Eli, this session)

| Decision | Choice |
|---|---|
| Engine | **libGDX (Java)** — code-first, nothing fights the OOP mandate; proven on Steam (Slay the Spire, Mindustry); ships Windows + SteamOS via bundled JRE (jpackage). Unity 6 and Godot rejected. |
| Language/stack | Java 21 LTS (Temurin), Gradle multi-module, JUnit 5, package root `com.trojia` |
| OOP | Strict object-oriented design through the entire application, interface-first |
| Time model | Turn-based ticks (world advances on action; observer can run/pause/step/fast-forward) |
| World structure | **True z-levels, DF-style** (city above, sewers/undercity/caverns below) |
| Sim extent | **Active bubble** at full fidelity + abstract layer for the rest of the world |
| v0 physical pillars | **All four**: Materials & properties, Temperature & fire, Fluids & flow, Light & visibility |
| v0 living-world pillar | **Economy & production** (abstract layer). Bodies/wounds, NPC needs, weather = later milestones |
| World authoring | **Fully hand-authored** maps (Tiled editor; z-level convention) |
| v0 deliverable | **God-view observer client**: pan/zoom, z-scrub, run/pause/step, debug palette (paint fire/water/materials), tile inspector |
| Art | CC0/free retro packs behind a clean art-swap abstraction |
| VCS | Local git now (this repo is not yet a git repo — `git init` in Milestone 0) |

### Defaults adopted (override any of these at review)
- **Aether/uether polarity field**: design the seam (an energy-field layer using the same machinery as temperature) but don't implement in v0.
- **Canon** (per Eli's follow-up): lore stays in `C:\repositories\LordOfTrojia-MVP` and is **referenced in place — never modified; that repo is off-limits for work**. Derived game data (material raws, district specs) lives in this repo with provenance notes pointing at the old repo's `Documentation/WorldBible.md` + novel. Canon hierarchy honored: **novel wins; the Lore HTMLs are non-canon** (brother's AI fan summaries — ability-name flavor only).
- **Flagship demo scenarios / regression tests**: (1) **The Tavern Fire** (flammability spread, stone survives, getilia-treated wood resists, heat rises through z-levels, ash remains) and (2) **The Chromatis Experiment** (ingot charges: silver→orange→gold, radiates heat when saturated, sudden discharge shatters an adjacent lightstone). Sewer flood + forge economy demos also land with their pillars.

## Lore → simulation seeds (from the novel + WorldBible §9)

- **Chromatis**: energy-storing alloy; charge state couples color (silver/blue→orange→gold), temperature, and behavior. The flagship "charge state" material feature.
- **Phorys**: mineral; contact with liquid converts it to pressurized gas (reagent-reaction feature; powers Devastators/flame bow).
- **Lightstone**: chargeable light source; shatters on sudden discharge. **Glowstone**: passive red light.
- **Getilia-treated trudgeon wood**: treatment-derived material (density up, flammability ~zero) — the "material + treatment = derived material" feature.
- **Crafting/energy magic** is rule-based (energy seeks easiest route, dissipates with distance/size, needs links) — future pillar, not v0.
- **Granadad districts** for hand-authored maps: Inner City (palace/cathedral/senate), slum suburbs, sewers, royal crypts.
- **Gameplay vision (Eli's follow-up, resolved)**: you ARE the Flame of Mercolas hunting evil in the capital — everyone respects you and you may break any law (canon lawful immunity of the Wielder). Resolution of the power curve: **social power maxed from day one** (immunity, access, deference — a unique verb set), **physical/Flame power starts weak** (newly chosen Wielder, struggles with a single mob) and grows via Morrowind-style system manipulation. Companion archetypes: Devin (apprentice), Vallech (freed Y'marr ally). Gameplay design itself is post-v0.

## Architecture (summary — full detail lands in ARCHITECTURE.md at M0)

### Toolchain
Java 21 LTS (Temurin, via winget + Gradle toolchain API auto-provisioning), Gradle 8.10+ **Kotlin DSL** + version catalog, libGDX 1.13.x (client only), Jackson (raws/Tiled JSON), fastutil (primitive collections), JUnit 5, **ArchUnit** (automated architecture guardrails), `StrictMath` only in sim-core (cross-OS bit determinism).

### Modules
```
sim-core/         com.trojia.sim.*      PURE Java — zero libGDX/AWT (ArchUnit-enforced). + test fixtures
content/          resources only        raws/*.json, maps/src/*.tmj, maps/baked/*.tregion, placeholder art
tools/            com.trojia.tools.*    TiledImporter (.tmj → .tregion), RawsValidator (fail-fast CLIs)
headless/         com.trojia.headless.* ScenarioRunner + BenchmarkHarness (depends on sim-core only)
client-observer/  com.trojia.client.*   libGDX god-view observer (LWJGL3)
```

### The key trick: strict OOP API over SoA storage
Per-tile heap objects are forbidden in hot storage. `ChunkSlab` (32×32×1 z) holds structure-of-arrays primitives (`short[] terrainMaterial`, `float[] temperature`, `byte[] fluidLevel 0-7`, `byte[] lightSky/lightBlock`, sparse fastutil maps for charge/fire). The OOP surface: `TileView` interface + reusable `TileCursor` flyweight (never stored, never escapes a tick) + all writes funneled through `TileWriter` (one funnel = determinism + dirty-tracking + events). Typed ids (`MaterialId` record) at boundaries, raw shorts inside. Interface-first at every seam (registries, systems, client, tools); primitives inside a system's own private sweep.

### Tick pipeline (single-threaded, order fixed forever; parallelism seams built in)
1 Commands → 2 Bubble freeze/thaw → 3 Scheduler → 4 Fluids (+gas) → 5 Thermal → 6 Fire → 7 Reactions (phase changes, phorys→gas, Chromatis charge coupling) → 8 Light (incremental BFS relight) → 9 Economy (every 10th tick) → 10 EventFlush (queued, never reentrant) → 11 Maintenance (publish immutable RenderSnapshot to GL thread).
Rationale: mass moves before heat conducts; heat settles before ignition; matter settles before light (pure derived state); bubble reshapes active sets before any physics runs.

### Determinism & sim/render split
Seeded `RngStreams` per (system, chunk); fixed iteration order (chunk key asc, cell asc; no HashMap iteration in tick paths); `WorldHasher` golden-master hashes; dedicated sim thread publishing copy-on-write snapshots of dirty slabs to the render thread. Save format: custom sectioned binary (per-section versions, registry name→id manifest for raw-file reordering safety, reserved FIELD section for aether/uether).

### Key domain packages (~50 types inventoried by the design agents)
`com.trojia.sim.core` (Simulation, SimulationPipeline, SimulationSystem, SimCommand sealed hierarchy, CommandQueue) · `.world` (WorldGrid, ChunkColumn/ChunkSlab, TileView/TileCursor/TileWriter, DirtyTracker frontiers) · `.world.bubble` (BubbleManager, ChunkTicket, FULL/BORDER/ABSTRACT fidelity, FreezeCodec — lossless thaw, BoundaryPolicy) · `.material` (MaterialRegistry, Material, sealed MaterialBehavior: ChargeBehavior/ReagentBehavior/TreatmentDefinition, DerivedMaterialFactory, RawsLoader) · `.thermal`/`.fluid`/`.reaction`/`.light` (frontier-based systems) · `.field` (EnergyFieldLayer seam, NullFieldLayer in v0) · `.economy` (Workshop/Recipe/Stockpile/PriceBoard, ledger-only in v0) · `.persist`/`.snapshot`/`.event`/`.rng`/`.time` · `com.trojia.client` (ObserverApp, SimulationRunner, WorldRenderer with z-ghost layers, TileAppearanceResolver = the art-swap seam, BrushPalette → SimCommands only, TileInspectorPanel, StatsOverlay).

### Tiled multi-z convention
One .tmj per district; one layer-group per z-level named `z:+0`, `z:-1`…; fixed sublayers (terrain/floor/fluids/markers); materials via tileset custom properties resolved against the registry; importer fails the build on unknown names. Client never reads Tiled — only baked `.tregion`.

## Milestones (each ends with something watchable)

- **M0 Bootstrap**: JDK via winget, Gradle wrapper, git init + .gitignore/.gitattributes (covers .idea/), 5 modules stubbed, ArchUnit purity test green, version catalog pinned. Eli's IDE: IntelliJ IDEA Community (+ Claude Code JetBrains extension, + Tiled from M1). Accept: blank LWJGL3 window at 60fps; headless prints 100 hello-ticks; clean-machine build green.
- **M1 World + materials + Tiled + first render**: chunked z-level world, raws-driven MaterialRegistry (incl. one treatment-derived material), importer bakes a 3-z test map (street/sewer/cavern), observer renders with pan/zoom/z-scrub/inspector. Accept: map correct on all 3 z; broken map rejected with useful error.
- **M2 Determinism spine**: pipeline stages 1-3+10-11, clock UI, RngStreams, EventLog, WorldHasher, save/load skeleton, paint brush end-to-end. Accept: 1000 ticks hash-identical across 2 OSes; save→load hash-identical.
- **M3 Thermal & fire**: conduction, ignition, spread, fuel, burnout→ash, melt/freeze. Accept: wooden room burns to ash + self-extinguishes; stone room never ignites (golden tests); temperature overlay demo.
- **M4 Fluids & gas**: 0-7 water, z-falls, U-bend pressure (bounded BFS), evaporation×thermal, GasSystem + phorys reagent reaction. Accept: sealed basin conserves exactly 1000 units / 500 ticks; sewer-flood demo.
- **M5 Light & the Flame**: sky/block channels, incremental relight, baked + dynamic (fire) lights, **Chromatis charge coupling** (charge → color ramp silver→orange→gold + heat + light; discharge shatters lightstone). Accept: dark undercity, torch pools, Chromatis Experiment golden test.
- **M6 Bubble + economy + full persistence**: freeze/thaw hash-verified round trip (frozen fire resumes identically), ledger economy with price response, complete save incl. RNG state. Accept: quit→reload mid-fire mid-flood hash-identical; bakery supply-shock demo.
- **M7 Hardening**: benchmark budgets (≈10k active fluid + 5k thermal + relight ≤ 5ms/tick; ~0 alloc/tick steady state), cross-OS CI hash matrix, art-swap proof (new TileAppearanceResolver impl only), ARCHITECTURE.md + "no gameplay in v0" scope contract.

## Verification

- **Per milestone**: the acceptance criteria above — each is either a headless golden-master test (committed expected world-hashes; failures print first divergent slab/property) or a demoable observer interaction.
- **Flagship regression scenarios** (permanent suite): Tavern Fire, Chromatis Experiment, Sewer Flood, Forge Economy — plus conservation, U-bend, freeze/thaw round-trip, save-migration tests.
- **Continuous**: `gradlew build` runs unit + scenario + ArchUnit tests; optional GitHub Actions windows+ubuntu matrix proves SteamOS-relevant cross-platform determinism.
- **Live verification**: `gradlew :client-observer:run` → paint a wooden room, ignite it, watch it burn out; pour water down a stairwell; charge a Chromatis tile and watch it turn gold and shed heat/light. The observer's stats overlay (tick ms, active cells, allocs/tick) makes performance regressions visible while playing with the sim.

## Top risks (full list with mitigations goes to ARCHITECTURE.md)
1. Fluid oscillation/instability → integer 0-7 rules, fixed scan order, move budgets, conservation goldens from day one.
2. Determinism erosion → TileWriter funnel, StrictMath, no-HashMap-iteration rule, cross-OS hash CI.
3. GC pressure from OOP habits → SoA + cursor contract, allocs/tick visible in the stats overlay.
4. Scope creep toward gameplay → "no agents, no item entities, no player in v0" written into the README contract.
5. Solo-dev over-engineering → interfaces only at seams with ≥2 plausible implementations; every milestone ends watchable.

## Post-approval first actions
1. Save project memory (north star, engine decision, canon rules, do-not-touch LordOfTrojia-MVP).
2. M0 bootstrap exactly as specified above.
3. Reconcile the subsystem workflow's synthesis into ARCHITECTURE.md alongside this plan's architecture (workflow may still be in flight at approval time; its refinements fold into M1+ implementation, not into re-planning).
