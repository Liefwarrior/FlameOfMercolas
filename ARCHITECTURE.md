<!--
  AUTHORITATIVE architecture for v0. Produced by the 15-agent design workflow
  (7 subsystem designs, 7 adversarial critiques, 1 reconciling synthesis) on 2026-07-12.
  Where docs/PLAN-v0.md and this document disagree on technical detail, THIS document
  wins: it post-dates the plan and every conflict was explicitly ruled (see the
  mismatch register in section 1).
-->
# Simulation Core Architecture v0 — Flame of Mercolas

**Status:** authoritative. Merges the seven subsystem designs (world-storage, tick-determinism, thermal-materials, fluids, light, bubble-economy, observer-tiled) with **all REQUIRED changes from their adversarial critiques applied**. Where a critique offered options or a design conflicted with another, the ruling is stated here and wins.

---

## 1. Contract reconciliation

### 1.1 Mismatch register (found → ruled)

| # | Mismatch | Ruling |
|---|---|---|
| 1 | Fluids wanted THERMAL before FLUIDS (same-tick temps); orchestrator wanted mass-first | **FLUIDS before THERMAL.** Fluids reads the persistent temperature field (previous tick's values) — evaporation/freeze thresholds don't need same-tick freshness; water arriving in fire is heated same tick, which the flagships do need. |
| 2 | thermal-materials put REACTIONS before THERMAL | **REACTIONS after THERMAL** — reactions then see `TemperatureThresholdEvent` and `ReagentContactEvent` same tick. |
| 3 | Fire/phase-transition/charge as separate phases vs. one REACTIONS phase | Fire and PhaseTransition are **ordered sub-systems inside THERMAL**; PhorysReaction, ChromatisCharge, LightstoneShatter are **ordered sub-systems inside REACTIONS**. Visibility granularity is `(phase, registrationIndex)` (orchestrator critique #2, option a), so chromatis discharge shatters lightstone same tick. |
| 4 | World's thaw-in-`beginTick` full-diff vs. bubble's budgeted queues; one BUBBLE phase vs. promote/demote split | **Split phases, budgeted queues win.** `BUBBLE_PROMOTE` right after TICK_BEGIN, `BUBBLE_DEMOTE` after ECONOMY. Bubble module drives world's freeze/thaw hooks; world's `ActiveBubbleController` is deleted in favor of `TicketedBubbleManager`. |
| 5 | Cadence-10 economy never sees one-lap events | **Cadence-1 `EconomyAccumulator`** in ECONOMY folds lap events into per-site deltas every tick; heavy macro work runs on the 600-tick bucket scheduler. All bus consumers are now cadence-1, so plain one-lap retirement is correct. |
| 6 | Fluids' private `FluidChunkLayer` byte array vs. world's 16-bit FLUID lane | **World FLUID lane wins** (bits: depth 0–2, fluidId 3–5, SETTLED 6, 7–15 reserved). Written via `ChunkWriter` → change logs/revisions fire; lane survives freeze verbatim (fixes fluids critique #5); FALLING bit deleted (critique #7). Fluids keeps private frontier bitsets per chunk. |
| 7 | Light's three private byte channels vs. world LIGHT lane | LIGHT lane packs sky(5b)+block(5b); **OPACITY is a registered 8-bit extension lane** owned by light, rebuilt from change-log readers at top of LIGHT phase, saved (no relight on load). |
| 8 | Three save formats (RegionFile, TROJSAV, observer `.world`) | **One format: TROJSAV** sectioned container. World lanes are the `WRLD` section via `SystemStateWriter`. The Tiled importer's output *is* a TROJSAV at tick 0. Region files / eviction deferred past v0. |
| 9 | Two `WorldHasher`s | One. Engine owns the Sink protocol + per-system sub-hashes; world contributes canonical logical content (decode compressed lanes, ascending chunkIndex, lane-registry order, overlays sorted by localIdx; chunk lifecycle state excluded from the hash). |
| 10 | `ChunkLifecycleListener` vs. `FreezeThawParticipant` | Merged into one SPI: `FreezeThawParticipant { serialize (pure, used by saver AND freeze — world critique #7), load, contributeSummary, rehydrate, transientVeto }`. |
| 11 | Boundary policy: world "assert on frozen write" vs. fluids' banking vs. bubble's flux ledger | One mechanism: `ChunkWriter` **rejects** non-concrete writes with a defined return code; systems route the rejected quantity to `BoundaryFlux.credit(face, kind, amount)`; `BOUNDARY_FLUX` phase applies credits to `ChunkSummaryStore`/incidents. Fluids' `FrozenFluidAccounts` is deleted — the summary's water field *is* the bank; thaw re-injects whole-chunk bottom-up scanline, residual stays banked (fluids critique #4). |
| 12 | `ChunkSummary` record vs. SoA store | `ChunkSummaryStore` (SoA, whole-world resident) is truth; `ChunkSummary` is a flyweight/ephemeral view. Incidents mutate summaries **in lockstep** with journal appends (bubble critique #3). |
| 13 | Temperature units: deciK vs. deciC vs. °C raws | **Deci-Kelvin unsigned 16-bit everywhere in state, events, summaries.** Raws author integer Kelvin; loaders convert. Fluids raws re-specified in K. |
| 14 | Chromatis charge 1,000,000 cu vs. 16-bit sparse overlay | **Overlays are 16-bit.** Raws rescaled: chromatis capacity 60,000 cu, maxSafeDischarge 600/tick; lightstone 5,000 / spike 2,000. Loader validates fit. |
| 15 | Cell keys: 63-bit longs vs. 30-bit `PackedPos` vs. light's region-local ints | **30-bit int `PackedPos`** `(z<<24|y<<12|x)` is the lingua franca of every hot queue and event payload; `int chunkIndex` ascending is canonical chunk order. Light queue entries are longs `(packedPos | level<<32)` — kills the region-origin rebase bug (light critique #7). Orchestrator's `LongFrontier` is deleted; world's `ActiveSet` is *the* frontier utility. |
| 16 | Three RNG schemes (tuple SplitMix64, named streams, `derive()`) | One counter-based `RandomSource`: `h = mix64(worldSeed + K1*tick); h = mix64(h ^ systemSalt); h = mix64(h + spatialKey); h = mix64(h + drawIndex)` (orchestrator critique #3). Allocation-free primitive path `long draw(long key, int idx)` (critique #9). Only state saved: worldSeed — resolves thermal's "stream positions in save" concern by construction. Fluids draws zero. Macro hazards use the same derive (bubble critique #8). |
| 17 | Importer's two-material tiles (floor+fill) vs. world's one-material+form | **One material + form.** Importer collapses Tiled floor/fill: fill present → (fillMat, form); else floor → (floorMat, FLOOR); else OPEN. Revisit post-v0 if floor-over-rock matters. |
| 18 | World's read-only FROZEN_RESIDENT rind vs. bubble's full-physics BORDER | **Both, layered:** ACTIVE and BORDER are concrete with identical physics; a 1-chunk FROZEN_RESIDENT rind around BORDER is readable-never-writable; writes at the hull route to BoundaryFlux. |
| 19 | Light 0–31 vs. observer contract 0–15 | 0–31; observer contract updated. `effectiveBrightness = max(block, (sky*celestial)>>5)`, celestial fixed-point 0..32 (light critique #10). |
| 20 | Water quench "via conduction" requires water on the material grid | Pooled liquids live only in the FLUID lane. Thermal kernel reads the FLUID lane and substitutes fluid heat-capacity/conductivity when depth ≥ 4 (thermal critique #8); fire extinguish queries `FluidView` directly in its own sub-phase. |
| 21 | Evaporation owned by reactions vs. fluids vs. thermal | **Fluids owns evaporation/boil/freeze of pooled fluid** (mutating only its own lane — kills orchestrator critique #6's ownership violation). Thermal's PhaseTransition owns grid-material melt/boil/freeze and emits `MaterialPhaseChangedEvent(yieldUnits)`; fluids spawns the liquid next tick. `meltYieldUnits` is a raws field. |
| 22 | Phorys: reactions "consume liquid" cross-mutation | **Fluids consumes** the units in its own phase (reagent flag check, per-chunk `containsReagents` gate) and emits `ReagentContactEvent`; reactions owns the pressure pulse and phorys wear counter (now with declared storage). |
| 23 | Ash swap: orchestrator had REACTIONS consume `CombustionCompleted`; thermal had fire swap directly | **Fire swaps to ash itself** via `ChunkWriter` (it owns the burn lifecycle); `MaterialTransformedEvent(cause=BURNOUT)` is notification only. |
| 24 | Bubble's `WorldGenSource.generate` for pristine thaw — v0 has no worldgen | **Pristine = imported base world.** Snapshot-on-modify diffs against the imported TROJSAV; pristine thaw deserializes from the base file (kills bubble critique #5's unbounded generate cost). Importer also **bakes initial chunk summaries** (bubble critique #2). |
| 25 | SiteIndex: bubble's "≤1 site per column" vs. world's nested-site example | Per-column sorted `(zMin, zMax, siteId)` list; smallest-volume site wins, tie by SiteId (world critique #10). Macro attribution: innermost non-DISTRICT site, else district. |
| 26 | Two competing event vocabularies with duplicate names | Single taxonomy, §5. Change logs vs. events split ruled in §6. |

### 1.2 Option rulings (critiques that offered choices)

- Overlay change notification (world #2): **option b** — no overlay change logs; owning system self-tracks its tile set; overlay writes still mark changedBits + revision.
- World-edge packing (world #9): **option a** — permanent 1-chunk immutable VOID border, in-bounds, `ChunkWriter` rejects; no hot-path branch.
- Skylight zero-cost descent (light #1): **restricted to level-31 columns** (Minecraft rule); sub-31 skylight pays `max(1,opacity)` downward. Simpler than the conditional-removal fix.
- In-flight light queues at save (light #5): **serialized** (packed longs), not drained.
- Queue entries into frozen chunks (light #6, orchestrator #12): **lazily skip** cells outside the concrete set on dequeue/consume; freeze demote-pipeline folds pending mass-bearing carry-over (pressure) into the summary via participants; thaw verification pass reconciles light (light #2).
- Forge Economy observer surface (observer #8): **option a** — a `MacroPanel` over `MacroReadView`.
- Scenario ignition in observer (observer #9): scenarios ship a **replayable SimCommand script**; observer "run scenario" = load world + inject script. No heat-emitting light sources.
- `FocusPoint.priority` (bubble #12): **deleted**.
- Economy while observed (bubble #10): **ledger-always-canonical in v0**; stockpile tiles are a rendering refreshed at thaw/TICK_END. `StockpileDelta` cut from v0.

---

## 2. Gradle module layout

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

**com.trojia.sim.engine** — `SimulationEngine` (iface: `step(n)/tick()/save/submit/inspect`) · `Simulations` (static factories only; multi-engine per JVM) · `EngineConfig` (rec) · `SimulationSystem` (iface: id/phase/tick/serialize/load/hashInto) · `TickPhase` (enum, §4) · `SystemId` (rec; 64-bit salt, collision-checked at boot) · `TickContext` (iface: rng/events/emit/bubble) · `InputGate` (commands+script → paints via ChunkWriter + External* events + input log) · `TickClock` (100 ms/tick) · `TickProfile` (rec, diagnostics only) · `SimCommand` (sealed: PaintMaterial, ClearTile, Ignite, Extinguish, AddFluid, RemoveFluid, InjectCharge, PlaceLightSource, RemoveLightSource, SetFocus).

**com.trojia.sim.event** — `SimEvent` (sealed root) · `EventSink`/`EventReader` (pull-only; topic ids resolved at registration) · `PhasedEventBus` (internal; `(tick,phase,regIndex,seq)` stamps; 65,536/topic/tick hard-fail in ALL builds).

**com.trojia.sim.random** — `RandomSource` (counter-based; `long draw(spatialKey, drawIndex)` allocation-free hot path).

**com.trojia.sim.world** — `World` (iface, no tick methods) · `TickableWorld` (scheduler-only: beginTick/commitTick) · `WorldConfig` (rec) · `PackedPos`/`Coords` (all bit math, audited once) · `TilePos`/`ChunkPos` (recs, cold paths) · `Tile` (iface) · `TileCursor` (flyweight; debug tick-stamp) · `TileForm`/`Dir` (enums) · `LaneId`/`Lanes`/`LaneRegistry` (instance owned by WorldBuilder — no statics; aether = a later lane registration) · `ChunkView` (borrowed read arrays) · `ChunkWriter` (only write path; maintains BLOCKS_MOVE/BLOCKS_LIGHT on material/form writes; rejects non-concrete writes with return code) · `OverlayId` (CHARGE) · `FlagBits`.

**com.trojia.sim.world.change** — `ChangeLogs`/`ChangeLogReader` (per-lane packed-int logs; `hasReaders` skip; reader-lag cap asserted at commit) · `ActiveSet` (THE shared frontier: packed-int FIFO + per-chunk bitsets, deterministic insertion order) · `ChunkRevisions` (observer diff key; changedBits valid only for revision delta == 1).

**com.trojia.sim.world.io** — `TrojSav` (sectioned container, §9) · `WorldSaver`/`WorldLoader` · `ChunkCodec` (lane-wise RLE, versioned) · `WorldHasher` (+`Sink`).

**com.trojia.sim.world.site** — `SiteDef` (rec) · `SiteIndex` (per-column z-range list).

**com.trojia.sim.material** — `MaterialId` (rec, short) · `Material` (rec) · `MaterialRegistry` (immutable; deterministic id assignment; precomputed primitive tables incl. `invCapQ16`, pairwise conduction/cap-normalized weights) · `MaterialFeature` (sealed: `Chargeable`, `ShatterOnSpike`, `Emissive`, `ContactReactive`) · `Treatment` (rec; mints derived materials at load) · `MaterialRawsLoader` (validation list §10).

**com.trojia.sim.thermal** — `ThermalSystem` (diffusion + buoyancy + settle-to-ambient pass; energy-residual carry + per-chunk sink counter) · `ThermalQuery` · `HeatCommandBuffer` (sorted drain, same-pos injections summed) · `PhaseTransitionSystem` · `FireSystem` (ignite/burn/extinguish/ash; SMOLDER semantics specified) · `FireQuery` · `FireState` (enum).

**com.trojia.sim.reaction** — `PhorysReactionSystem` (wear counter stored in a per-chunk sparse map) · `ChargeSystem` (charge tick, stops, saturation heat via HeatCommandBuffer) · `ShatterSystem` (Chebyshev ≤ 2 incl. distance 0 — self-shatter) · `ReactionTable` · `ChargeCommandBuffer` (all charge mutation intake) · `MaterialInstances` (CHARGE overlay access).

**com.trojia.sim.fluid** — `FluidSystem` (FALL/PRESSURE/SPREAD/THERMAL/REAGENT/SETTLE; wake-on-decrease fan-out; per-chunk gates `thermallyInteresting`/`containsReagents`; per-chunk displacement cap 32; same-fluid guards in FALL/SPREAD; pending list for newly wetted chunks) · `FluidView`/`FluidCursor` · `FluidId`/`FluidDefinition`/`FluidRegistry` · `FluidEmitters` · `FluidLedger` (bank transfers are moves, excluded from create/destroy sums) · `FluidAccountant` (audit incl. summary banks) · `FluidTotals` (valid post-FLUSH, covers frozen via summaries).

**com.trojia.sim.light** — `LightSystem` (opacity rebuild from change logs w/ effective-opacity no-op; four-queue relight, budget ~60k visits checked at queue-phase boundaries; bulk-init pass at load, outside tick budget) · `LightQuery` (0–31; fixed-point effectiveBrightness) · `OpacityView` (FOV seam) · `Luminance` (rec) · `CelestialProvider`/`CelestialState` (int 0..32) · `LightRenderView` (reads legal only between ticks) · source registry keyed by handle slot, coord as value (stacking = max at seed).

**com.trojia.sim.bubble** — `ActiveBubble` (facade iface) · `TicketedBubbleManager` (composes `TicketRetargeter`, `ThawPipeline`, `FreezePipeline` — internal) · `TicketLevel` (ACTIVE/BORDER/FROZEN) · `BubbleTopology` (O(1) `levelOf`) · `FocusPoint` (rec, no priority) · `FreezeThawParticipant` (SPI, §1.1 #10) · `ChunkSummaryStore` (SoA, 64 B/chunk, whole world; `ChunkSummary` flyweight) · `DeltaJournal` (tombstone + compaction; set-deltas last-write-wins per chunk) · `AbstractDelta` (sealed: FireDamage, WaterLevel, StructureDamage, Temperature) · `BoundaryFlux` (iface; physics-facing credit API).

**com.trojia.sim.macro** — `MacroWorld` · `MacroSite` (real objects; thousands, not millions) · `SiteKind` (enum) · `Ledger` (long milli-units) · `CommodityRegistry` · `Recipe` (rec) · `Workshop` · `Market` · `PriceModel` (fixed-point; elasticity ∈ {½,1,2}) · `CaravanFlow` (tiebreak: commodityId asc) · `MacroScheduler` (600-bucket wheel + sorted far-heap for dueTick > 600) · `IncidentSystem` (sorted-key iteration; updates summaries in lockstep) · `MacroFlood` drains to an explicit per-site reservoir `long` with logged events · `MacroEventLog` · `MacroReadView` · `EconomyAccumulator` (cadence-1 event folder).

**com.trojia.sim.scenario** — `ScenarioDefinition` (rec) · `ScriptedAction` (wraps SimCommand at tick).

**headless** — `ScenarioMain` (`run <scenario> --ticks N --hash-every K [--bless]`; refuses journals whose world hash mismatches).

**client-observer** — `ObserverApp`/`ObserverLauncher` · `SimulationDriver` (fixed-timestep; clamp scales with speed; MAX = whole ticks in 12 ms) · `SpeedSetting` · `MapCamera` · `LayeredWorldRenderer` + `RenderPass` (sealed) · `ZPeekResolver` (GL-free) · `TileArtResolver`/`JsonTileArtResolver` (GL-free index math) + `AtlasRegionTable` (GL side) · `DebugHud`/`TimeControlBar`/`PalettePanel`/`InspectorPanel`/`StatsOverlay`/`MacroPanel` · `BrushController` + `Brush` (sealed) · `WorldFileWatcher` (AtomicBoolean polled in frame loop) · `CommandJournal` (per session, world-hash header; new file on F5).

**tools** — `TmxReader` (StAX; masks gid flip bits, warns) + `Tmx*` model records · `MaterialBinding` · `TiledValidator` (ordered `ValidationPass` list) · `TiledWorldImporter` (deterministic; annotations sorted (z, objectId); suggestion tiebreak (distance, lex)) · `PaletteGenerator` · `SummaryBaker` (bakes initial ChunkSummaryStore; validated against freeze-path output) · `ImportMain` (atomic tmp+rename output).

---

## 4. Tick pipeline (total order)

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

---

## 5. Event taxonomy

Events are records of primitives + ids only (ArchUnit-enforced); cell = `PackedPos` int. "Observer" rows are drained into polled logs, never bus subscriptions.

| Event (payload) | Emitter (phase) | Consumers (latency) |
|---|---|---|
| `ExternalIgnition(cell)` | InputGate (BEGIN) | Fire (same tick) |
| `ExternalFluidSpawned(cell, fluidId, units)` | InputGate | Fluids (same tick) |
| `ExternalChargeApplied(cell, deltaCu)` | InputGate | ChargeSystem (same tick) |
| `ChunkThawed(chunkIdx)` | ThawPipeline (PROMOTE) | all field systems: prime frontiers (same tick) |
| `ChunkFrozen(chunkIdx)` | FreezePipeline (DEMOTE) | field systems: drop entries (next tick); EconomyAccumulator |
| `ReagentContactEvent(cell, fluidId, units, solidId)` | Fluids | PhorysReaction (same tick) |
| `FluidVaporizedEvent(cell, fluidId, units, cause)` | Fluids | EconomyAccumulator (lap); steam/aether seam |
| `FluidFrozenEvent(cell, fluidId, units)` | Fluids | PhaseTransition: ice placement (same tick) |
| `TileIgnited / TileExtinguished(cell)` | Fire (THERMAL) | EconomyAccumulator (lap); Observer |
| `FireLuminanceChanged(cell, oldB, newB)` (4 buckets) | Fire | Light (same tick) |
| `TemperatureThresholdEvent(cell, thresholdId, dir)` | ThermalDiffusion | Fluids: wake settled water (next tick) |
| `MaterialPhaseChangedEvent(cell, from, to, yieldUnits)` | PhaseTransition | Fluids: spawn/remove liquid (next tick) |
| `MaterialTransformedEvent(cell, from, to, cause)` | Fire / PhaseTransition / Reactions | EconomyAccumulator (lap); Observer. Light wakes via change logs, not this. |
| `PressurePulseEvent(cell, gasId, magnitude)` | PhorysReaction | Fluids (next tick — documented canon latency) |
| `ChargeStopChangedEvent(cell, oldStop, newStop)` | ChargeSystem | Light (same tick); Observer |
| `ChargeSaturatedEvent(cell)` | ChargeSystem | EconomyAccumulator; Observer |
| `EnergyDischargedEvent(cell, releasedCu, rate)` | ChargeSystem | LightstoneShatter (same phase, later index) |
| `MacroIncidentStarted/Progressed/Ended` | IncidentSystem (ECONOMY) | Observer (MacroEventLog) |
| `PriceChanged / ProductionCompleted(siteId, …)` | Market / Workshop | Observer |

---

## 6. Shared plumbing & state ownership

**Two channels, one rule:** *field deltas travel on per-lane ChangeLogs (packed ints, allocation-free, duplicate-tolerant, consumer dedupe via ActiveSet); semantic facts travel as SimEvents (low-volume records).* Per-tile event emission is forbidden unless gated by a raws flag (e.g., `ContactReactive`).

Change-log subscriptions (sealed at registration; reader-less lanes skip appends): Thermal ← MATERIAL, FORM · Fluids ← MATERIAL, FORM · Light ← MATERIAL, FORM, FLAGS, FLUID. Observer uses `ChunkRevisions` only (full remesh when revision delta > 1).

**State ownership (sole writer / intake for others):**

| Field | Sole writer | Foreign intake |
|---|---|---|
| MATERIAL lane | Fire (burnout), PhaseTransition, Reactions (shatter/spent/ice), InputGate, Importer — all via ChunkWriter | events above |
| FORM lane | InputGate, Importer | — |
| FLAGS | ChunkWriter (derived bits, auto on material/form writes); Fire (ON_FIRE) | — |
| TEMPERATURE lane | ThermalDiffusion | `HeatCommandBuffer` |
| FLUID lane | FluidSystem | `FluidEmitters`, External/Pressure events |
| LIGHT + OPACITY lanes | LightSystem | — |
| CHARGE overlay | ChargeSystem | `ChargeCommandBuffer` |
| Summaries | FreezePipeline, BoundaryFlux, IncidentSystem | — |
| Ledgers/prices/journals | Macro only | EconomyAccumulator deltas |

**Determinism rules (binding):** no float/double in sim-core state or state-affecting math — integer/fixed-point only (Q8/Q16, milli-units, deciK); golden masters are therefore cross-platform. No `java.util.HashMap` in sim state; any side-effectful iteration is in sorted canonical key order. RNG per §1.1 #16. Saves at TICK_END only; `run K+N ≡ save@K, load, run N` including event carry-over, frontiers, quiet-tick counters, light queues, bubble queues + grace deadlines. Event cap hard-fails identically in all builds. Parallel seam (v1): `ChunkTickable` + `CommitBuffer` and per-chunk event/frontier staging buffers — **parallel primitive arrays, never object tuples** — merged at phase barriers in canonical chunk order; shipped single-threaded.

---

## 7. Bubble & freeze/thaw boundary

- **Tickets:** ACTIVE (7×7 focus columns × 4 z-chunks ≈ 196 chunks) · BORDER (1-ring shell, full physics, flux apron, ≈ 290) · FROZEN. A 1-chunk FROZEN_RESIDENT rind around BORDER stays readable. Hysteresis: promote ≤ R, demote ≥ R+2, 300-tick grace; compression deferred 64 ticks, capped/tick. Concrete-chunk hard cap 800 (force-freeze farthest first); lazy queue invalidation on retarget.
- **Freeze:** veto poll (600-tick force cap) → participants `contributeSummary` → in-flight handoff (burning → `MacroFire`; water volume; charge summed into `storedEnergyCu`, held constant unless the site declares an energy process) → snapshot **iff modified since import/last snapshot** → FROZEN. Sparse overlays (charge, wear, transition progress) and the FLUID/LIGHT lanes survive verbatim in the blob.
- **Thaw:** base-world or snapshot → journal replay `(tick, seq)` (FireDamage walks flammables in fixed scan order with derived RNG; WaterLevel settles bottom-up scanline; burned-out chunks convert N lowest-index flammables to `burnsTo`) → banked boundary inflow re-injected whole-chunk scanline (residual stays banked) → participants rebuild frontiers → light thaw-verification pass on the 6 face shells → enters as BORDER.
- **Hull physics:** `ChunkWriter` rejects non-concrete writes; the system credits `BoundaryFlux`; BOUNDARY_FLUX applies to summaries/incidents. Nothing silently dies or vanishes at the wall; conservation audits include summary banks.

---

## 8. Chunk & storage numbers

- **Chunk:** 32×32×8 = 8,192 tiles; `localIdx = (z<<10)|(y<<5)|x`; max world 4096×4096×64 + 1-chunk VOID border; flat `Chunk[]`, int chunkIndex.
- **Dense lanes (B/tile):** MATERIAL 2 · FORM 1 · FLAGS 1 · TEMPERATURE 2 (deciK) · FLUID 2 · LIGHT 2 · OPACITY 1 = **11 B/tile**; + changedBits (1 KB) + header → **≈ 91 KB per concrete chunk (~11.2 B/tile)**; system-private (thermal frontier 1 KB, fluid frontiers 2 KB wet-only) ≈ +3 KB. `laneDirtyMask` is an `int`.
- **Memory:** 256×256×32 region (256 chunks) ≈ **24 MB**. Full concrete set (486) ≈ 45 MB; rind (~480 resident) ≈ 44 MB; `ChunkSummaryStore` 196,608 × 64 B ≈ 12.6 MB (whole city, always); frozen-compressed city ≈ 50 MB (2–12 KB/chunk RLE). Total steady ≈ **150–170 MB**.
- **Budget (8 ms, single core):** engine/events 0.3 · promote 0.4 · fluids 2.0 · thermal+fire 2.0 · reactions 0.3 · light 1.8 (≈ 60k-visit cap) · flux+economy 0.3 · demote 0.4 · commit 0.3 ≈ 7.8 ms. Documented spike lines: boundary-crossing tick +0.5 ms; save ≤ 500 ms at TICK_END. Enforced by CI perf tests, not aspiration. Cross-system write ceiling ~200k/tick (world overhead ≤ 2 ms).

---

## 9. Save container (TROJSAV)

Little-endian; header (magic, formatVersion, worldSeed, tick, rawsFingerprint — mismatch = hard fail) + TOC + Deflate-1 sections with CRC32C: `META`, `INPT` (input log), `EVNT` (carry-over lap), `WRLD` (lanes/overlays via ChunkCodec, canonical order), one per system (`FLUD`,`THRM`,`REAC`,`LGHT`,`BUBL`,`ECON` — frontiers, quiet counters, queues, journals, summaries), `AETH` reserved-empty. Participants' `serialize` is pure and shared by saver and freezer. Missing section on load = system-default init only if the section is declared optional; otherwise hard fail. Atomic tmp+rename. The Tiled importer emits this format at tick 0; importer output is byte-deterministic.

---

## 10. Raws schema — canonical example (Chromatis)

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

**Tavern Fire.** Script tick 10 → `ExternalIgnition` (BEGIN) → Fire ignites (THERMAL), `FireLuminanceChanged` → Light same tick. Diffusion (invCap multiply-shift, energy-residual carry) heats oak past ignition on the frontier; jitter via counter RNG. Buoyancy ×4 up-z + 50% plume into z+1 carries heat across z-levels. Getilia-trudgeon (flammability 0, minted at load) never enters the burning map; stone conducts only. Tick 300 `ExternalFluidSpawned` water → FLUIDS places it → THERMAL same tick heats it; fire extinguishes by querying `FluidView`. Fuel-out: Fire swaps to ash via ChunkWriter (FLAGS caches auto-updated → light change log wakes; `MaterialTransformed(BURNOUT)` → economy damage). Golden asserts ash count, treated tiles unchanged, per-tick hash chain.

**Chromatis Experiment.** `ExternalChargeApplied` each tick → ChargeCommandBuffer → ChargeSystem accrues CHARGE overlay (survives freeze verbatim; summarized as `storedEnergyCu`). Stop crossings emit `ChargeStopChanged` → Light + observer bucket (silver/blue → orange → gold). ≥ 95%: `ChargeSaturated`; heat injected via HeatCommandBuffer warms neighbors next tick. Script drains 60,000 cu in one tick ≫ 600 → `EnergyDischarged(rate)` → ShatterSystem (same phase, later index) scans Chebyshev ≤ 2 incl. self → lightstone → shards (`MaterialTransformed(SHATTER)`); light emitter removed same tick.

**Sewer Flood.** Source → FLUIDS: 1 z/tick cascade; sealed chamber fills bottom-up via bounded BFS pressure (`z < headZ`, per-chunk displacement cap 32). Drain empties the pond because every depth *decrease* wakes 4 laterals + above. Near forge heat: `TemperatureThresholdEvent` wakes settled water next tick; hash-phase evaporation (pinned mix constants) removes units, ledgered. Water at the hull: write rejected → `BoundaryFlux` credit → frozen neighbor summary; audit exact to the unit including banks. Light no-ops on sub-threshold depth changes (effective-opacity rule).

**Forge Economy.** Fully abstract; sites + road edges + baked summaries from the importer. Workshop cycles on the timing wheel consume/produce ledger milli-units; fixed-point stock-ratio prices; caravans via far-heap scheduling. Mine shut off → `MacroShortage` incident + ingot price ≥ +20% within 5 abstract days. Identical results with the camera parked on the forge (ledger-always-canonical; stockpile tiles are a rendering). Observer sees it in `MacroPanel`; acceptance also verified headless.

---

## 12. Milestone plan

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

1. **Tick-budget aggregation optimism** — each subsystem quoted best case. *Mitigation:* per-phase budget lines (§8) enforced as CI regression gates from M2 on; degrade knobs already designed (light visit cap, displacement caps, compression cap).
2. **Freeze/thaw seam correctness** — the most fragile path in the system. *Mitigation:* single pure `serialize` shared by save and freeze; conservation audits every tick under `-ea`; re-freeze-mid-incident and mid-pan save/load tests are milestone gates.
3. **Golden-master brittleness** — one wrong bless poisons everything. *Mitigation:* per-system sub-hashes name the first divergent system + tick; integer-only math makes goldens cross-platform; bless only via `--bless` after review; RNG mixing fixed before first bless.
4. **Dual plumbing misuse** (change logs vs. events) — new code emitting per-tile events. *Mitigation:* the §6 rule + ArchUnit test banning unflagged per-tile emission; reader-lag caps fail loudly.
5. **Stuck-water class of bugs** — wake-rule regressions are silent. *Mitigation:* the regression trio + quiescence assertions in every fluid test; wake-on-decrease is a stated invariant, not a fix.
6. **Relight storms** in fire/flood scenarios. *Mitigation:* luminance buckets, effective-opacity no-op, budget slice with specified carry-over semantics; Sewer Flood relight-count assertion.
7. **Save-format churn** breaking goldens mid-project. *Mitigation:* sectioned container with per-section versions and in-system migrations; rawsFingerprint hard-fail; format frozen at M5.
8. **Macro scope creep / fixed-point fidelity** — the economy invites features and floats. *Mitigation:* v0 fence: elasticity ∈ {½,1,2}, no agents/labor market, ledger-always-canonical; Forge Economy acceptance test *is* the definition of done.
