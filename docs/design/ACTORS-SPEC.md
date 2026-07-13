# ACTORS-SPEC — Streets of Rogue-style emergent actors (DECISIONS.md "Actors" row)

**Status:** spec-first, pre-implementation. Actors are their own milestone right after F2
("F2.5"), superseding the v0 "no agents" fence per the locked decision. This document is the
complete contract for that milestone.

**Binding constraints inherited:**
- DECISIONS.md Actors ruling: `Actor` base class + **one THIN subclass per type**; subclasses
  declare composed behavior policies + raws-driven stats; **depth-2 hierarchy** (Actor → type,
  never deeper); all numbers in data files.
- DECISIONS.md Identity seam: the Actor base's identity field is **Persona-shaped from day one**
  (trueIdentity + presentedIdentity, presented defaulting to true). ALL social reads (law,
  deference, witness reports, logs) use the PRESENTED identity.
- ARCHITECTURE.md §4–§6: integer/fixed-point math only, named counter-based RNG draws, sorted
  canonical iteration, events are records of primitives, saves at TICK_END only. Actors are
  FEW (hundreds, not millions) — real Java objects are legal, per the MacroSite precedent.
- NORTH STAR: social power maxed from tick zero — per-group deference to the Wielder is a
  **design table (§4.9), never a stat**; physical power grows via systems. Actors are the chaos
  engine: their needs colliding with the sim pillars (fire, water, property, law) produce the
  wild scenarios.
- Canon: novel cites `(novel L<n>)` per MATERIALS-CANON.md convention; WorldBible cites
  `(WB §n)`. The novel wins. **"Priest of the Flame" and "Disciple of the Flame" are
  GAME-CANON-ADDITION titles** (Eli), slotted into the canon Divine Light order without
  contradicting it (§4.4–4.5). Every invented name/number is **(placeholder)** or
  **(invention)**.
- Dossier principles consumed (Streets of Rogue / RimWorld / DF research): one shared verb set
  implemented once; relationships as a tiny discrete FSM; property + witness rules as the drama
  engine; status effects as the universal actor↔sim coupling channel; job anchor first,
  schedules additive; legibility over fidelity (every decision emits a readable event);
  richness = system intersections (acceptance test: **every type's policies touch ≥ 2 sim
  pillars or the type is not done**); hurt-but-don't-kill pacing; actors in their own stories.

Cross-references: COMBAT-SCREEN-SPEC.md §1 (encounter trigger contract — reconciled §2.7),
FACES-SPEC.md §3.3 (archetype mapping — §4 table), PROGRESSION-SPEC.md (XP hooks — §9.1).

---

## 1. Class architecture

### 1.1 The abstract `Actor` base (owns ALL state and ALL verbs)

One shared rulebook: every field and every verb lives on the base or in the policy library.
Subclasses add **zero** fields and **zero** verbs (ArchUnit-enforced, test A24).

```java
public abstract class Actor {
    // ---- identity ----
    final ActorId id;                 // int; assigned once by ActorRegistry, never reused
    final ActorTypeId typeId;         // raws key ("militia_watch", "animal_dock_dog", …)
    final Persona identity;           // { trueId, presentedId } — DECISIONS seam; presented
                                      // defaults to true; setActAs() lands with Play mode

    // ---- position + facing ----
    int cell;                         // PackedPos (ARCHITECTURE §1.1 #15)
    byte facing;                      // Dir ordinal

    // ---- needs vector (§3) ----
    final short[] needs;              // Need.COUNT = 5; 0..10,000 reserve, high = satisfied
    final int[] needAccum;            // fractional decay accumulators (integer, §3.2)

    // ---- perception (raws-driven) ----
    // sightRadiusByLight[4]: sight radius per brightness bucket (§2.4); hearingRadius:
    // Chebyshev, ignores opacity. Both live in the raws stat block, cached on the type.

    // ---- inventory-lite ----
    final short[] inventory;          // item ids (ItemsLite registry, §2.6); raws-capped;
    byte inventoryCount;              // slot 0..cap-1 packed, canonical order = pickup order

    // ---- faction + deference ----
    final short factionId;            // raws
    // deference profile: resolved from the §4.9 design TABLE by typeId — not stored per actor

    // ---- health-lite hook ----
    short hp;                         // full body model arrives with combat (G-track);
    short statusBits;                 // ON_FIRE, WET, DOWNED, ORPHANED, PANICKED, ALERTED
    short downedTimer;                // hurt-not-kill recovery (§2.7)

    // ---- behavior state (all of it — policies are stateless singletons) ----
    byte policyOrdinal;               // index into the type's policy stack
    byte targetKind; int targetKey;   // NONE | CELL(PackedPos) | ACTOR(actorId) | ITEM(itemId)
    short policyTimer;                // generic countdown (chase timeout, loiter dwell, …)
    int anchorCell;                   // job anchor (dossier principle 6); leash center
    int ownerId;                      // Animals: owning Keeper; others: NONE (-1)

    // ---- tick entry point (FINAL — the template is engine-owned) ----
    final void tick(ActorContext ctx) {
        decayNeeds(ctx);              // §3.2
        senseStimuli(ctx);            // §2.4 — fills a transient StimulusSet (not saved)
        selectPolicy(ctx);            // §1.3 — argmax over the type's stack, tie by order
        policies().get(policyOrdinal).act(this, ctx);   // one tick of behavior
        auditStatus(ctx);             // ON_FIRE spread/damage, WET decay, DOWNED recovery
    }

    protected abstract PolicyStack policies();   // returns the type's static constant
}
```

Shared **verbs**, implemented once as base/protected helpers used by policies (dossier
principle 1): `stepToward(cell)` (greedy + deterministic sidestep, §2.5), `pickUp(itemId)`,
`drop(itemId)`, `give(itemId, actorId)`, `take(itemId)` (crime if owned by another — §2.6),
`strike(actorId)` (sim-side scuffle, §2.7), `emitNoise(loudness, cause)`, `requestIgnite(cell)`
/ `requestDouse(cell)` (§2.3 command intake), `bark(lineKey)` (presentation-only draw).

### 1.2 The `BehaviorPolicy` interface (sense → score → act; deterministic)

```java
public interface BehaviorPolicy {
    PolicyId id();                                  // enum, append-only
    int score(Actor self, ActorContext ctx);        // pure, integer; 0 = not applicable
    void act(Actor self, ActorContext ctx);         // one tick; may draw named RNG (§2.2)
}
```

- Policies are **stateless singletons** — one instance shared by every actor of every type.
  All per-actor policy state lives on the Actor (targetKind/targetKey/policyTimer).
- **Score bands** (convention, raws-tunable per type — the numbers below are the
  (placeholder) defaults): `EMERGENCY ≥ 900` (flee fire/water, deference override) ·
  `RESPONSE 500–899` (apprehend, defend stock, recapture animal, need-critical) ·
  `NEED 300–499` (eat, rest at low threshold) · `JOB 100–299` (patrol, vend, work, tend) ·
  `IDLE 1–99` (loiter, graze). Base score per (type, policy) comes from the raws
  `policies.<ID>.priority` field; stimuli and need urgency add raws-declared bonuses.
- **Selection rule:** evaluate every policy in the type's stack; pick the maximum score;
  **ties break by stack position** (earlier wins). Score 0 everywhere is impossible — every
  stack ends in an IDLE policy whose score is ≥ 1 always (Loiter/GrazeWander).
- On change, emit `ActorPolicyChanged(actorId, from, to, reasonCode)` — the legibility event
  (dossier principle 7): reasonCode is an enum (`NEED_HUNGER_LOW`, `STIM_CRIME_SEEN`,
  `STIM_FIRE_SEEN`, `STIM_ALARM_HEARD`, `TARGET_LOST`, `TIMER_EXPIRED`, `DEFERENCE`, …). If
  the observer can't reconstruct WHY, the emergence is wasted.

### 1.3 The shared policy library (v1 set)

One library, every type composes from it. Adding behavior = adding a policy file + raws
parameters, never a subclass method. (All ids append-only.)

| PolicyId | Band | What it does (one tick) | Draws used |
|---|---|---|---|
| `DEFER_WIELDER` | EMERGENCY | posture per the §4.9 table when the presented-Wielder is in sight: step aside / approach / kneel; suppresses APPREHEND vs the Wielder | `actor.bark` |
| `FLEE` | EMERGENCY | run from the danger cell (fire/water/violence), greedy away-step; drops carried item on a panic draw | `actor.fleeJitter`, `actor.panicDrop` |
| `APPREHEND` | RESPONSE | Watch only in v1 rosters: chase target actor; adjacent → scuffle → arrest (target DOWNED+ALERTED, escorted to post) | `actor.scuffle` |
| `INVESTIGATE` | RESPONSE | walk to a heard/reported stimulus cell, look around, dwell | `actor.loiter` |
| `REPORT` | RESPONSE | walk to nearest Watch (or post) and hand over a witnessed-crime memory → re-emits `CrimeWitnessed` to that Watch | — |
| `DEFEND_STOCK` | RESPONSE | Shopkeeper: chase the thief of own property; give-up timer; mishap draw when chasing through cluttered tiles | `actor.mishap`, `actor.scuffle` |
| `RECAPTURE` | RESPONSE | Keeper: chase own strayed Animal; adjacent → compliance draw → leash restored | `actor.comply` |
| `DOUSE_FIRE` | RESPONSE | fetch water from nearest water tile (bucket = 1 unit), throw on burning tile via `requestDouse` — **moves real FLUID-lane water** | — |
| `LOOT_RUSH` | RESPONSE | Wastrel: grab unattended/dropped items when no deterrent witness in sight (§2.6) | `actor.targetPick` |
| `SEEK_FOOD` | NEED | path to nearest known food (own stash → market → **steal** if bold enough); eat restores hunger | `actor.slip` (Animals: leash-break), `actor.steal` |
| `EAT` / `REST` | NEED | consume carried food / sleep at anchor; restores the need | — |
| `WORK` | JOB | type-parameterized job at anchor: gut fish, haul cart, restock stall, feed animals | — |
| `PATROL` | JOB | walk raws-declared waypoint loop; look for crimes/alarms while moving | — |
| `VEND` | JOB | stand at counter, serve customer actors in queue order (actorId asc), exchange item↔coin | — |
| `BEG` | JOB | approach richest visible target (Disciple > Shopkeeper > Serf, §5.3), plead; on gift, satiate coin/hunger | `actor.bark` |
| `PREACH` | JOB | Priest: sermon at alms station; nearby Wastrels' duty/safety needs tick up (crowd magnet) | `actor.bark` |
| `GIVE_ALMS` | JOB | Priest/Disciple: hand food items to begging Wastrels in queue order | — |
| `FOLLOW` | JOB | stay within N tiles of target actor (Disciple→Priest; Animal→Keeper) | — |
| `TEND_ANIMAL` | JOB | Keeper: feed/water owned Animals at pen; resets Animal hunger | — |
| `GRAZE_WANDER` | IDLE | Animal: random leashed wander | `actor.wander` |
| `LOITER` | IDLE | dwell at/near anchor; occasional shuffle | `actor.wander`, `actor.bark` |

**(invention)** The whole library; parameters all live in raws `policies` blocks (§6).

### 1.4 One thin subclass per type — and the "add a type" walkthrough

A subclass declares exactly three things: the composed policy stack (priority-ordered), the
raws stat-block reference (its `ActorTypeId`), and spawn constraints. Nothing else. Complete
real example — this is the WHOLE file:

```java
/** Type #9 walkthrough: the Ratcatcher (hypothetical). One file + one raws entry. */
public final class Ratcatcher extends Actor {
    public static final ActorTypeId TYPE = ActorTypeId.of("ratcatcher");
    private static final PolicyStack STACK = PolicyStack.of(
        Policies.DEFER_WIELDER,   // §4.9 row comes from raws
        Policies.FLEE,
        Policies.WORK,            // raws param: job = HUNT_VERMIN
        Policies.VEND,            // raws param: sells = rat_tail bounty
        Policies.EAT, Policies.REST,
        Policies.PATROL,          // raws param: waypoints = alley circuit
        Policies.LOITER);
    public static final SpawnConstraints SPAWN =
        SpawnConstraints.zones("sewer_grate", "warehouse_row");   // from raws spawn table

    Ratcatcher(ActorId id, ActorSeed seed) { super(id, TYPE, seed); }
    @Override protected PolicyStack policies() { return STACK; }
}
```

Plus one raws entry (`content/raws/actors/ratcatcher.json`, schema §6) declaring stats,
needs rates, policy parameters, deference row, spawn table, glyph. Register the class in
`ActorTypes` (one line, sorted by type id). **That is the entire cost of a new type** — no
engine change, no new phase logic, no new verbs. This is Eli's "we can just keep adding on
top", made checkable: test A23 adds a throwaway type this way in-test and runs the twin-run
determinism suite over it.

Depth-2 guard: ArchUnit rule — nothing extends any concrete Actor subclass; subclasses
declare no fields, no non-constructor methods beyond `policies()` (test A24).

---

## 2. Engine integration

### 2.1 New `ACTORS` tick phase — position and justification

**Proposed pipeline (ARCHITECTURE §4 revised; ruling for Eli's bless):**

```
 0 TICK_BEGIN … 6 LIGHT   (unchanged)
 7 ACTORS          decay needs → sense → select policy → act, in ascending ActorId order;
                   actor effects route out via events/command buffers (§2.3).
 8 BOUNDARY_FLUX   (was 7)
 9 ECONOMY         (was 8) — EconomyAccumulator now also folds actor trade/theft lap events.
10 BUBBLE_DEMOTE   (was 9)
11 TICK_END        (was 10)
```

Justification for the slot (after LIGHT, before BOUNDARY_FLUX/ECONOMY):

1. **Perception wants fresh light.** Sight is gated by the LIGHT lane (§2.4). Placing ACTORS
   after phase 6 means an actor sees this tick's darkness/fire-light, not yesterday's — the
   dark-alley gameplay is the whole register.
2. **Actors react to this tick's physics.** `TileIgnited`, `FluidFrozenEvent`,
   `MaterialTransformed` from phases 2–4 are visible in-tick at phase 7 (event visibility
   rule, ARCHITECTURE §4): fire breaks out and the Serf flees the SAME tick, not one late.
3. **Economy sees actor facts same tick.** Theft/trade lap events fold into
   `EconomyAccumulator` at phase 9 with no added latency.
4. **Actors never write lanes** (§2.3), so no state-ownership row changes and no ordering
   hazard with FLUIDS/THERMAL/LIGHT: actor→world effects are next-tick by design, documented
   like the `PressurePulseEvent` canon latency.

**Why not the FIELDS_RESERVED slot (5):** it precedes LIGHT, breaking justification 1, and
aether still needs it.

**Save-version event (pre-freeze):** inserting phase 7 renumbers BOUNDARY_FLUX..TICK_END and
changes event-visibility stamps ⇒ one-time golden re-bless of all flagships + TROJSAV
`formatVersion` bump + new `ACTR` section (§2.8). Legal: the save format freezes at M5 and
the TickPhase enum's append-only rule is amended once, here, deliberately — recorded so it is
never done casually again.

**Budget:** ACTORS ≤ 0.4 ms for ≈ 300 actors (integer policy eval + capped sense scans;
≈ 1.3 µs/actor). ARCHITECTURE §8's budget table gains the line; the documented total moves
7.8 → 8.2 ms **(needs-blessing: either accept 8.2 as the new reference-core line or shave the
fluids reserve)**. CI perf gate from the first actors milestone.

### 2.2 Determinism: storage, iteration, RNG

- **`ActorRegistry`** (in `com.trojia.sim.actor`): real objects, hundreds; flat
  `Actor[]`-backed map keyed by int ActorId; **tick iteration is ascending ActorId, always**.
  ActorIds are assigned by a monotonic `nextActorId` counter (saved), never reused. Spawn
  order at import is deterministic: (zone id asc, spawn-table row order, index) — the
  importer/ActorSeeder bakes actors into the tick-0 TROJSAV exactly like SummaryBaker bakes
  summaries.
- **Spatial index:** per-chunk sorted actor-id lists, maintained incrementally on every move
  (an actor is in exactly one chunk bucket). `actorsInRadius(cell, r)` returns ids ascending.
  No HashMap; arrays + binary search.
- **RNG:** every random choice is a named draw through the ONE engine `RandomSource`
  (ARCHITECTURE §1.1 #16): `systemSalt` = hash of the stream name (`actor.wander`,
  `actor.slip`, `actor.steal`, `actor.scuffle`, `actor.comply`, `actor.mishap`,
  `actor.fleeJitter`, `actor.panicDrop`, `actor.targetPick`, `actor.loiter`, `actor.bark` —
  registry append-only), **spatialKey = actorId**, **drawIndex = per-actor per-tick draw
  sequence number, reset to 0 each tick** (sound because tick is already mixed into the
  counter hash — zero saved RNG state, same argument as ARCHITECTURE #16). `actor.bark` is
  presentation-only: no game state ever reads its result (COMBAT-SCREEN `combat.logline`
  precedent, test A21).
- Policy SELECTION is draw-free (deterministic argmax); draws happen only inside `act()` and
  only where the table in §1.3 declares them.

### 2.3 Writes: actors never touch lanes (state-ownership preserved)

Actors own actor state (registry + ItemsLite) and nothing else. World effects route through
the existing intake mechanisms, consumed next tick by the owning system:

| Actor effect | Channel | Owner that applies it |
|---|---|---|
| start a fire (knocked lantern, ON_FIRE actor crossing flammables) | `ActorIgnition(cell)` event | FireSystem (THERMAL, next tick — documented 1-tick latency) |
| douse (bucket line) | `ActorDouse(cell, units)` + paired `FluidEmitters` withdrawal at the source tile | FireSystem / FluidSystem, next tick; **water is conserved and ledgered** — the bucket is real |
| heat (future: torches) | `HeatCommandBuffer` | ThermalDiffusion |
| trade/theft economic deltas | lap events → `EconomyAccumulator` | Macro |

`ChunkWriter` is never handed to a policy (ArchUnit, test A24). Frozen chunks are impassable
to actors (§2.9), so actor writes can never even reach the BoundaryFlux path.

### 2.4 Perception

- **Sight:** `sightRadiusByLight[4]` in raws — a radius per brightness bucket of
  `LightQuery.effectiveBrightness` (0–31) at the TARGET tile: buckets DARK 0–7, GLOOM 8–15,
  LIT 16–23, BRIGHT 24–31 (aligned with COMBAT-SCREEN §2.3's tiers; (placeholder) bucket
  edges). Target visible ⟺ Chebyshev dist ≤ radius[bucket(targetLight)] AND LOS clear via
  the OPACITY lane (`OpacityView` — the FOV seam ARCHITECTURE reserved). A lantern-lit thief
  is seen from 8 tiles; the same thief in a black alley from 2. Light is a stealth system for
  free — actor × LIGHT pillar coupling.
- **Hearing:** `NoiseEmitted(cell, loudness, cause)` and `AlarmRaised` are heard by every
  actor with Chebyshev dist ≤ min(loudness, hearingRadius) — no LOS, walls don't block v1
  (flagged refinement: opacity-damped hearing).
- Sensing is event- and stimulus-driven, not exhaustive: per tick an actor scans (a) events
  it can hear, (b) actors/items within sight radius **only when a policy asks** (Patrol,
  DEFEND_STOCK, witness scan §2.6), capped at the 4-bucket max radius. Sense results are
  transient (`StimulusSet`, never saved — rebuilt every tick, so save/load can't fork).

### 2.5 Movement

1 step per `speedTicksPerStep` ticks (raws; accumulator, integer). `stepToward`: greedy
Chebyshev-reducing step; blocked → deterministic sidestep scan in fixed Dir enum order;
blocked N ticks → `TARGET_LOST`. Actors treat BLOCKS_MOVE flags, VOID, and **non-concrete
chunks** as walls. Leash: no step may exceed `leashRadius` from `anchorCell` except under
FLEE/APPREHEND/RECAPTURE (raws flag `ignoresLeash` per policy). **(invention; pathfinding
refinement — flow fields or A* — flagged for later, greedy is v1.)**

### 2.6 Property, items-lite, crime, witnesses (the drama engine)

- **ItemsLite** (actor-layer registry, consistent with COMBAT-SCREEN §5.1's "no sim item
  entities"): `itemId → (kindId, ownerActorId|NONE, location: carriedBy|cell)`. Minted at
  import from map annotations (stall stock, pen feed) and by VEND/GIVE_ALMS.
- **PropertyIndex:** cell → owning actorId for annotated tiles (stall, pen, shopfront,
  sleeping spot), baked by the importer from TMX object properties. A stall is **ATTENDED**
  iff its owner is within 2 tiles (placeholder).
- **Crime =** `take`/`strike`/enter vs an owned thing by a non-owner: emits
  `CrimeCommitted(culpritId, kind, cell, victimId, itemId)`. Same tick, the witness scan
  runs: every actor within sight of `cell` (LOS + light rule, evaluated in ActorId order)
  emits `CrimeWitnessed(witnessId, culpritPresentedId, kind, cell)` — **presented** identity,
  per the Persona seam. Watch actors consume CrimeWitnessed (heard directly or via REPORT)
  → INVESTIGATE/APPREHEND. **Deterrence:** crime-capable policies (LOOT_RUSH, SEEK_FOOD
  steal branch) score 0 while a Watch actor or the victim has LOS to the target — checked
  deterministically in the score pass. Wastrels steal when the law can't see; that single
  rule generates theft→chase→brawl chains (dossier principle 3).
- Animals commit crimes too (dog steals fish) but are not APPREHEND targets — the Watch
  ladder routes to the OWNER (Keeper liability, §5.2) **(invention)**.

### 2.7 Violence, hurt-not-kill, and the combat-screen contract

- **NPC↔NPC violence is sim-side**: the `strike` verb = opposed `actor.scuffle` draws +
  raws `scuffle.strike/grit` ints; loser loses hp; hp ≤ 0 ⇒ **DOWNED** (statusBit, timer,
  recovers) — never sim-side death from scuffles (dossier principle 9). Death exists only
  via: fire (ON_FIRE hp drain), drowning (FLUID depth ≥ 6 over head, v1 flee threshold makes
  it rare), and the player combat screen. `ActorDied` → corpse item + ownership cascade
  (§4.8).
- **Player combat**: per COMBAT-SCREEN §1.1, encounter contact with a HOSTILE group opens
  the combat screen. The ACTORS layer supplies the inputs: disposition FSM state (§2.10),
  engagement zone from raws. **Reconciliation of the standing conflict:** COMBAT-SCREEN L46
  "Guards are HOSTILE-capable toward everyone" is now gated: Watch can never enter HOSTILE
  toward the **presented Wielder** (§4.9 clamp; L2967 immunity); a disguised Gabri
  (presented ≠ Wielder) is arrest-able like anyone — disguise sheds immunity by design
  (DECISIONS Identity row).
- While the combat screen is open the sim is paused (COMBAT-SCREEN §1.2); actors freeze
  mid-policy and resume exactly (their state is plain data; test A15 covers save/load
  mid-goal, the same property).

### 2.8 Events + TROJSAV `ACTR` section

**Emitted** (all records of primitives; hundreds of actors ⇒ far under the 65,536/topic cap,
test A22): `ActorSpawned(actorId, typeId, cell)` · `ActorDied(actorId, cause, cell)` ·
`ActorPolicyChanged(actorId, from, to, reason)` (observer-drained) ·
`ActorMoved(actorId, fromCell, toCell)` (observer-drained; systems use the spatial index,
not this) · `CrimeCommitted(...)` · `CrimeWitnessed(...)` · `AlarmRaised(actorId, kind,
cell, loudness)` · `NoiseEmitted(cell, loudness, cause)` · `ItemTransferred(fromId, toId,
itemId, cause: STEAL|GIVE|LOOT|VEND|DROP)` · `OwnershipTransferred(animalId, fromKeeper,
toKeeper)` · `AnimalOrphaned(animalId, deadKeeperId)` · `ActorIgnition(cell)` ·
`ActorDouse(cell, units)` · `ActorStatusChanged(actorId, statusBit, on)`.

**Consumed:** `TileIgnited`/`TileExtinguished` (fear/respond stimuli), `ChunkThawed`/
`ChunkFrozen` (registry bookkeeping §2.9), `NoiseEmitted`, `AlarmRaised`, `CrimeWitnessed`
(Watch), `MaterialPhaseChangedEvent` (ice underfoot — flagged, v1 ignores). Water level is
POLLED from the FLUID lane at own+adjacent tiles (cheap, no event needed).

**TROJSAV `ACTR` section** (new; formatVersion bump): header `{version, nextActorId,
nextItemId, count}` then per-actor records **sorted by actorId**: actorId i32 · typeId i16 ·
personaTrue i32 · personaPresented i32 · cell i32 · facing i8 · needs 5×i16 · needAccum
5×i32 · hp i16 · statusBits i16 · downedTimer i16 · policyOrdinal i8 · targetKind i8 ·
targetKey i32 · policyTimer i16 · anchorCell i32 · ownerId i32 · inventoryCount i8 +
itemIds i16[] · dispositionOverrides count i16 + (otherActorId i32, state i8)[]. Then the
ItemsLite table sorted by itemId. `WorldHasher` gains an `ACTR` sub-hash (canonical order =
the same sort). Round-trip is test A14; `run K+N ≡ save@K,load,run N` holds because
StimulusSet is transient and RNG needs no state.

### 2.9 Bubble interaction — v1 ruling

**Actors exist only in concrete chunks (ACTIVE/BORDER).** Rules:

1. **Impassable dark:** non-concrete chunks are walls to `stepToward` (§2.5). With the
   MVP being one district (the Docks) inside a ~486-concrete-chunk bubble, the whole
   playable stage is normally concrete; the rule is the safety net at the rim.
2. **Freeze:** `ActorSystem` is a `FreezeThawParticipant`. `serialize` (pure, shared with
   the saver) writes every actor whose `cell` is in the freezing chunk **verbatim** into the
   chunk's freeze blob (full §2.8 record — no lossy summarizing of actor state);
   `contributeSummary` adds per-type population counts + an `arrestedCount`/`downedCount`
   byte to the ChunkSummary so macro/observer still "see" the frozen populace. The actor
   object leaves the registry (id stays reserved).
3. **Thaw = re-materialize:** `load`/`rehydrate` rebuilds the objects; then one **"while
   you were away" repair**: needs decayed by `frozenTicks × rate` (clamped ≥ 0, saturating
   int math), policy reset to the stack's JOB/IDLE default at `anchorCell` if the saved
   target is no longer valid (anchored reactive actors thaw cleanly — dossier principle 6).
   No offscreen simulation in v1.
4. **Pair split at the boundary:** if a Keeper freezes while its Animal is concrete (or
   vice versa), the registry marks the live one's partner ABSENT: the Animal runs a leashed
   stray-lite stack at the pen (not full ORPHANED); the Keeper's RECAPTURE won't fire on a
   frozen Animal. Reunion is automatic at thaw (ownerId link is by id, ids never reused).
5. **Flagged refinements (post-v1, explicitly out):** cross-chunk travel goals, co-freeze
   dragging of Keeper↔Animal pairs, frozen-actor schedule advancement ("the shopkeeper
   restocked while you were away"), migration between districts.

### 2.10 Disposition FSM (dossier principle 2)

Per-pair relationship is a 4-state integer enum **(invention, trimmed from SoR's 6):**
`HOSTILE < WARY < NEUTRAL < FRIENDLY`. Default = faction table (raws); only non-default
pairs are stored (sparse list on the actor, sorted by other actorId). Transitions ONLY via
named events: CrimeWitnessed vs me → HOSTILE; gift → +1 step; ALARM about you → WARY;
decay to default after 6,000 ticks (placeholder). **Toward the presented Wielder the §4.9
table sets the STARTING state and a clamp floor — no event may lower it** (north star;
test A18). Disposition feeds the combat-screen trigger (HOSTILE + engagement zone) and
VEND prices never (social power is static; PROGRESSION §5 fence).

---

## 3. Needs model

### 3.1 The vector — five small integers, tuned for legibility

`Need` enum (append-only): **HUNGER, REST, COIN, SAFETY, DUTY** (relabeled FAITH for the
two clergy types in UI only). Stored as **reserve 0..10,000** (high = satisfied); the
inspector renders `value/100` as a 0–100 bar. Thresholds (global, (placeholder)):
**LOW = 3,000** (need-band policies activate) · **CRITICAL = 1,000** (need scores jump a
band; barks turn desperate). All comparisons are `≥/<` on integers; boundary semantics
pinned by test A11.

- HUNGER: eating restores to 10,000. Decay makes an actor eat ~2×/day.
- REST: sleep at anchor restores; decays faster at night activity for day types.
- COIN: not literal purse contents — the *felt* need to earn; satiated by VEND/WORK/BEG
  income events, decays daily. (Purse = ItemsLite coin item count.)
- SAFETY: NOT decayed — it is **event-driven**: sighted fire/violence/rising water/alarms
  subtract raws-declared chunks; recovers +N per quiet tick. Low SAFETY powers FLEE and
  panic barks.
- DUTY/FAITH: the job drive; decays when idle, restored by doing the type's JOB policies.
  Low DUTY is why the Watch returns to patrol and the Priest to preaching.

### 3.2 Decay math (integer, no division on the hot path)

Raws give `decayPerKilotick` (integer units per 1,000 ticks). Per tick:
`needAccum[i] += decayPerKilotick; while (needAccum[i] ≥ 1000) { needAccum[i] -= 1000;
needs[i] = max(0, needs[i] - 1); }` — exact, drift-free, save-stable (accumulators are in
`ACTR`). Event-driven deltas apply directly with saturating clamp [0, 10,000].

### 3.3 Threshold → policy coupling

Need urgency feeds scores, not special cases: a NEED-band policy's score =
`priority + urgencyBonus` where `urgencyBonus = raws.lowBonus` if reserve < LOW else
`raws.critBonus` if < CRITICAL else 0 (critBonus > lowBonus stacks the band jump). One
mechanism, all types, all raws-tuned. The observer inspector (§7.2) shows the five bars,
the current policy, its reason code, and the target — an observer must be able to read
"HUNGER 8/100 → SEEK_FOOD → fish stall" at a glance. Legibility is the acceptance bar.

### 3.4 Daily rhythm

`DAY = 24,000 ticks` (placeholder; 40 real minutes at 100 ms/tick), `tickOfDay = tick mod
24000`. Dawn 0 · noon 6,000 · dusk 12,000 · midnight 18,000. Raws declare per-type
**rhythm windows** that add a JOB-score bias (`rhythmBonus` inside window, 0 outside) —
schedules are additive scoring, not a scripting layer (dossier principle 6: anchors first,
schedules later; this is the minimal schedule). Windows per type in §4.

---

## 4. The eight types

Format per type: role · needs weighting (decay tuning) · policy stack (priority order) ·
daily rhythm · deference row (summary; full table §4.9) · world-event reactions · pillars
touched (acceptance: ≥ 2).

### 4.1 Militia Watch

- **Role:** the law pillar's face. Gate-and-post culture: heavy at the Inner-Wall gate
  line and watch posts, thin in the suburbs (canon texture: watch at gates/palace, suburbs
  lawless — novel L2410, L2968, L2328); pairs, bribable-lazy flavor ("Guards sleep on
  their posts", novel L439). Gear register per WB §8 (leather/chain, spear + saxe).
- **Needs:** DUTY dominant (fast decay → returns to patrol); HUNGER/REST standard; COIN
  slow; SAFETY recovers fast (they stand ground longer — FLEE fire threshold higher).
- **Stack:** `DEFER_WIELDER · FLEE · APPREHEND · INVESTIGATE · DOUSE_FIRE · PATROL · EAT ·
  REST · LOITER(post)`.
- **Rhythm:** two shifts (placeholder): day squad 0–12,000, night squad 12,000–24,000;
  off-shift Watch sleep at the post.
- **Deference:** UNCONDITIONAL COMPLIANCE (novel L2967, L2968, L2414). Will do: step aside,
  open doors, answer any question. Will tell: patrol routes, crime reports, rumors
  (Gutter-Ken quality-gated). Will give: escort on request. Never: arrest/obstruct the
  presented Wielder.
- **Reactions:** fire → AlarmRaised(FIRE) + INVESTIGATE, then DOUSE_FIRE if burning
  structure is attended-property or post; water rising → herd civilians (INVESTIGATE +
  barks), retreat at depth ≥ 4; witnessed theft/violence → APPREHEND (culprit) with
  hurt-not-kill scuffle → arrest; bloodletter sign sighted → AlarmRaised(UNCANNY) + REPORT
  up-chain + WARY (they don't touch it — plot hook space).
- **Pillars:** law (owner), fire (douse/alarm), property (enforcement) — 3. ✓

### 4.2 Serf

- **Role:** the Docks laboring commoner — porters, fish-gutters, cart-haulers, tenement
  dwellers (canon stratum: suburb destitute, novel L2328; "Serf" as label is
  **GAME-CANON-ADDITION** covering the novel's servant/laborer/peasant stratum, explicitly
  NOT the army's chattel slaves — that is a separate war institution, novel L238-240).
- **Needs:** HUNGER fast (food insecurity is the loop); COIN fast (day wages); REST
  standard; DUTY moderate (work or starve).
- **Stack:** `DEFER_WIELDER · FLEE · DOUSE_FIRE(workplace) · REPORT · WORK · EAT · REST ·
  LOITER`.
- **Rhythm:** work 1,000–11,000; evening loiter; sleep 14,000–23,000.
- **Deference:** COMPLY + AVOID — steps aside, eyes down; white robes unwelcome in these
  streets (novel L2410). Will tell: what they saw, if asked directly (fear compels). Will
  give: nothing they can't spare.
- **Reactions:** fire → flee, then bucket line if it's the workplace/tenement (livelihood);
  water rising → flee upslope, salvage one carried item (`actor.panicDrop` inverse); theft
  witnessed → REPORT to Watch if a Watch is near, else keep head down (score gated on
  Watch-within-hearing — legible cowardice); violence → flee + alarm bark; bloodletter
  sign → SAFETY hit, avoid the cell permanently (memory-lite: cell added to avoid list,
  cap 4, FIFO) **(invention)**.
- **Pillars:** water (flood response, bucket), fire (bucket line), property (work +
  reporting) — 3. ✓

### 4.3 Wastrel

- **Role:** beggars, urchins, drunkards, petty thieves — the parasite pressure that makes
  property interesting (canon: beggars fixture novel L2334, L439; coin-mobbing crowd
  L385-387; sleeps in sewer grates like Zradist, L2345).
- **Needs:** HUNGER fast, COIN fast, SAFETY twitchy (big event deltas), REST cheap (sleeps
  anywhere), DUTY near-nil.
- **Stack:** `DEFER_WIELDER(APPROACH variant) · FLEE · LOOT_RUSH · BEG · SEEK_FOOD(steal
  branch) · EAT · REST(grate) · LOITER`.
- **Rhythm:** crepuscular: beg at market hours 2,000–10,000, scavenge at dusk, sleep
  irregular.
- **Deference:** APPROACH — the one suburb group drawn TO the white robes: the order feeds
  them (alms canon, novel L2420). Begs, follows, complies instantly. Will tell: everything
  (gutter rumors — the Gutter-Ken informant pool). Will give: nothing; will take: anything.
- **Reactions:** fire → watch from safe distance, then LOOT_RUSH the abandoned property
  (the signature chaos move); water → knows the grates, flees early; dropped
  coins/goods → converging frenzy, all wastrels in sight (the L386 juggler scene as a
  mechanic); theft witnessed → nothing (not their problem); Watch approaching → scatter
  (WARY default toward Watch faction); bloodletter sign → flee, won't speak of it above a
  whisper (rumor available only at BEG/gift — Gutter-Ken hook).
- **Pillars:** property (theft/looting — owner-pressure), law (the Watch's main customer),
  fire (loot-after-fire) — 3. ✓

### 4.4 Priest of the Flame — GAME-CANON-ADDITION (rank), canon-anchored slot

- **Role & canon fit:** an ordained monk seconded by Minister John to permanent out-wall
  pastoral duty — runs the alms station/abbey circuit in the Docks (alms outside the wall
  are canon: the old abbey food runs, novel L2420). Wears white; tolerated-but-unwelcome
  (novel L2410). Sits BELOW Minister/monks, far below the one Wielder; never touches the
  Flame (one-Wielder rule intact, novel L2413). The novel's only "priests" are U'mar's
  blood-cult (novel L919-924) — this title is Eli's addition and the spec keeps it
  visually/structurally Divine Light (white robes, monastery chain of report).
- **Needs:** FAITH dominant (preach/alms restore); HUNGER/REST modest (monastic); COIN
  nil; SAFETY steady (faith steels him).
- **Stack:** `REVERE_WIELDER(DEFER variant) · FLEE · GIVE_ALMS · PREACH · REPORT ·
  INVESTIGATE(pastoral: the injured, the uncanny) · EAT · REST · LOITER(station)`.
- **Rhythm:** dawn office 0–1,000; alms 2,000–6,000; circuit walk 7,000–11,000; dusk
  sermon 12,000–13,000; retire.
- **Deference:** REVERE + PETITION — instant deference, but he is the ONE type that will
  approach the Wielder with requests (order politics beneath reverence, novel L2413-2414).
  Will tell: everything the order knows locally. Will give: alms stock, shelter, healing-lite.
- **Reactions:** fire → organize (AlarmRaised + DOUSE_FIRE — shepherd under pressure);
  water → move the alms line uphill, carry the lame (escort-lite); witnessed crime →
  REPORT, then PLEAD variant at the arrest (petitions the Watch for mercy — §5.4 ladder;
  hurt-not-kill made diegetic); violence → interpose bark, never strikes; **bloodletter
  sign → the strongest reaction in the roster:** INVESTIGATE + REPORT (to the monastery,
  off-map) + FAITH surge — this is Gabri's canon breach-investigation trail
  (DECISIONS.md opening), and the Priest is its street-level sensor.
- **Pillars:** law (report/plead), fire (douse-organizing), property (alms transfers) — 3. ✓

### 4.5 Disciple of the Flame — GAME-CANON-ADDITION (rank), canon-anchored slot

- **Role & canon fit:** gray-robed apprentice/seminary rank (Devin's tier: gray apprentice
  robes, novel L2969; seminary initiates, L2413) assisting a Priest on the out-wall
  circuit — carries the food, learns, may hold a mace (Devin's kit, WB §5).
- **Needs:** FAITH high; HUNGER standard (growing kids); REST standard; SAFETY jumpy
  (young); COIN nil.
- **Stack:** `REVERE_WIELDER · FLEE · FOLLOW(assigned Priest) · GIVE_ALMS · WORK(carry
  stock) · INVESTIGATE(curiosity — scores on ANY unusual stimulus; the young one wanders
  toward trouble) · EAT · REST · LOITER`.
- **Rhythm:** mirrors the assigned Priest, offset by fetch-and-carry runs.
- **Deference:** REVERE (absolute) — starstruck apprentice awe (Devin's tier; even a
  Y'marr shows true fear before the Wielder, novel L2750). Will do anything asked
  instantly. Will tell: everything, breathlessly, plus what the Priest wouldn't.
- **Reactions:** fire/water → follows the Priest's response one tick late (FOLLOW makes
  this emergent, not scripted); crime → gasps (bark) + REPORT to own Priest, not the
  Watch; bloodletter sign → curiosity vs terror — INVESTIGATE scores high but FLEE
  triggers on approach (an oscillation the observer can watch) **(invention, kept: it's
  legible and story-generative)**.
- **Assignment invariant:** every Disciple spawns bound to a Priest (FOLLOW target,
  spawn-table pairing like Keeper↔Animal but soft — a Priest's death ORPHANS the Disciple
  to the alms-station anchor, no registry hard-invariant) **(invention)**.
- **Pillars:** property (alms), law (reporting), fire (assist douse) — 3. ✓

### 4.6 Shopkeeper

- **Role:** stall/shop keepers of the Docks set — fishmonger (salmon chain canon, novel
  L2351), chandlery/skins (L380), grungy inn (L355), herbs shop (Gretta template, L1336).
  Vendors threading morning streets are canon (L426); haggling culture (L1089).
- **Needs:** COIN dominant (the till is the point); DUTY = shop-tending; HUNGER/REST
  standard; SAFETY tied to stock (stock crime = SAFETY hit AND response).
- **Stack:** `DEFER_WIELDER(APPEASE variant) · FLEE · DEFEND_STOCK · DOUSE_FIRE(own
  stall) · VEND · WORK(restock) · EAT · REST · LOITER(counter)`.
- **Rhythm:** open 1,000; peak VEND 2,000–10,000; restock dusk; shutter and sleep at the
  shop 14,000+ (they sleep where the stock is — burglary hook).
- **Deference:** COMPLY + APPEASE — obsequious service, refuses payment nervously, gossips
  after he leaves (INFERRED; they fear the immunity more than they love the faith, novel
  L2967). Will give: goods free to the Wielder (a legal shoplifting seam — the north
  star's social power as gameplay); will tell: customer gossip, prices, who owes whom.
- **Reactions:** theft of OWN stock → DEFEND_STOCK chase + AlarmRaised(THIEF); theft of
  another's → alarm bark only; fire → DOUSE own stall at any risk, flee otherwise; water →
  stack stock high (WORK variant), close early; bloodletter sign near shop → pays a Wastrel
  to scrub it (item transfer + rumor suppression — flagged nicety, v1 just WARY).
- **Pillars:** property (owner), law (alarm customer), fire (dousing), economy (VEND
  ledger events) — 4. ✓

### 4.7 Animal Keeper

- **Role:** owns and works animals: cart mule/dray, ratter-dog pack, pig sty (pigpens +
  "Pig-snatcher!!" canon, novel L238, L1044), goats (goat-milk canon, L789). **Always owns
  ≥ 1 Animal** — the ruled invariant, enforced by the registry (§4.8.3).
- **Needs:** DUTY = the animals (TEND restores); COIN standard (sells work/milk); others
  standard.
- **Stack:** `DEFER_WIELDER · FLEE · RECAPTURE · TEND_ANIMAL · WORK(feed/water/drive) ·
  EAT · REST · LOITER(pen)`.
- **Rhythm:** feed at dawn 500–1,500 and dusk 12,500–13,500; work the animal between;
  sleeps by the pen.
- **Deference:** COMPLY + AVOID (Serf tier, INFERRED). Will tell: animal gossip, who
  bought what beast. Will give: a day's cartage on request.
- **Reactions:** own Animal strays/panics → RECAPTURE overrides nearly everything (his
  livelihood is running away on legs); fire near pen → open the pen (release beats roast —
  emits the strays that make fire scenes chaotic) **(invention)**; water → drive animals
  upslope; his Animal's crime → liability: pays the victim (ItemTransferred coin, ledger
  event) or faces the Watch ladder (§5.2).
- **Pillars:** property (owns animals + liability), water/fire (herd responses), law
  (liability) — 3+. ✓

### 4.8 Animal — and the Keeper↔Animal invariant (registry-enforced)

- **Role:** the innocent chaos vector. v1 species set **(GAME-CANON-ADDITION per dossier):**
  dock dog (ratter), pig (canon L238/L1044), goat (canon L789), dray mule. Rats stay
  ambient vermin texture, not Actors; **no wyverns/pegii as civilian animals ever** (canon:
  military/sacred — novel L316, WB §8, WB §1); gulls skipped in v1.
- **Needs:** HUNGER, REST, SAFETY only (COIN/DUTY frozen at 10,000, never decay — the
  vector stays uniform, the raws zero the rates). No deference row: INDIFFERENT (no canon
  of animals reacting to the Flame; §4.9).
- **Stack:** `FLEE(panic — hair-trigger vs fire/loud noise) · SEEK_FOOD(steal branch,
  boldness raws per species) · FOLLOW(owner, leash) · GRAZE_WANDER · REST`.
- **ORPHANED override stack** (statusBit swaps the stack — the ONE sanctioned stack swap,
  base-class mechanism): `FLEE · SEEK_FOOD(bolder) · GRAZE_WANDER(wide leash from last
  pen)` — a stray dog is bolder, shyer of people, and hungrier: a standing street story.
- **The invariant (ruled: enforced by the registry, not convention):**
  1. **Spawn-time pairing is atomic.** `ActorRegistry.spawnKeeper(...)` spawns the Keeper
     AND its raws-declared animals in one operation, wiring ownerId both ways. Spawning an
     Animal with no live Keeper ownerId **hard-fails** (boot/scenario error, not a warning)
     — test A5.
  2. **Ownership transfer on Keeper death:** same phase, deterministically: candidate =
     nearest live Keeper of a compatible species-handling type by (Chebyshev distance,
     then actorId asc); found → `OwnershipTransferred`; none → `AnimalOrphaned` +
     ORPHANED statusBit + stack swap. Tests A6–A8.
  3. **Keeper always owns ≥ 1 Animal:** if a Keeper's last Animal dies, the registry runs
     the adoption scan (any ORPHANED animal of compatible species → adopt, same
     deterministic order); if none, the Keeper enters a SEEKING grace (raws
     `reacquireTicks`, placeholder 12,000) after which the registry spawns a juvenile at
     the pen from the spawn table ("the sow farrows" **(invention — the invariant must be
     restorable without a market system in v1)**). The `-ea` audit asserts, every tick:
     every Animal has a live owner XOR ORPHANED; no Keeper is animal-less beyond grace —
     test A9.
  4. Orphan adoption runs each tick before policy selection (registry pass, ActorId
     order), so ORPHANED is always a transient-or-stable truth, never a race.
- **Pillars:** property (theft, being property), fire (panic vector + ON_FIRE spread —
  a burning animal fleeing across dry tiles IS fire spread, dossier principle 4) — 2+. ✓

### 4.9 Deference design table (per-group, a TABLE not a stat — north star)

Read by `DEFER_WIELDER`/`REVERE_WIELDER` variants against the **presented** identity.
Starting disposition + clamp floor per §2.10. No event, skill, or system may modify this
table at runtime; raws-load only (test A18).

| Group | Posture | Will do | Will tell | Will give | Clamp floor | Canon |
|---|---|---|---|---|---|---|
| Militia Watch | UNCONDITIONAL COMPLIANCE — never obstructs/arrests | step aside, open doors, escort | patrols, crime reports | escort, access | FRIENDLY | novel L2967, L2968, L2414 |
| Serf | COMPLY + AVOID — steps aside, wary | answer direct questions | what they saw | little | NEUTRAL | novel L2410, L2328 |
| Wastrel | APPROACH — begs, follows | comply instantly | gutter rumors (best informants) | nothing | NEUTRAL | novel L2420, L2334 |
| Priest of the Flame | REVERE + PETITION | anything; also asks | order knowledge | alms stock, shelter | FRIENDLY | novel L2413-2414 (rank: game-canon) |
| Disciple of the Flame | REVERE (absolute) | anything, instantly | everything, breathlessly | anything carried | FRIENDLY | novel L2969, L2413 (rank: game-canon) |
| Shopkeeper | COMPLY + APPEASE | free goods, nervous service | customer gossip, prices | goods gratis | NEUTRAL | novel L2967 + INFERRED |
| Animal Keeper | COMPLY + AVOID (Serf tier) | as Serf | animal trade gossip | a day's cartage | NEUTRAL | INFERRED |
| Animal | INDIFFERENT (no deference policy in stack) | — | — | — | — | no canon of animal Flame-reaction |

Canon hostile exception (recorded for the dungeon/encounter roster, NOT these eight):
Beast-aligned creatures flee the Wielder on instinct and feel "true fear" (novel L2344,
L2750).

---

## 5. Interaction matrix and emergent-scenario traces

### 5.1 High-chemistry pairings

Each cell: **trigger → escalation ladder → possible outcomes.** All resolution is the
§1–§2 machinery — the ladders below are *predictions*, not scripts (the sim is co-author,
never author).

| Pairing | Trigger | Escalation ladder | Outcomes |
|---|---|---|---|
| **Watch × Wastrel** | Wastrel loiters on owned frontage / is seen stealing | move-along bark → Wastrel flees (usual) → refuses (SAFETY low + REST critical) → APPREHEND chase → scuffle | scattered · arrested (DOWNED, escorted to post) · escapes into alley dark (light-gated sight) → Watch INVESTIGATE dwell |
| **Animal × Shopkeeper stock** | hungry Animal's SEEK_FOOD targets displayed stock | slip-leash draw → steal → CrimeWitnessed → DEFEND_STOCK chase → mishap draw per cluttered tile → Keeper liability ladder | fed dog + paid-off victim · chase knocks lantern (fire!) · stall UNATTENDED → Wastrel loot window · Watch routes to the Keeper |
| **Disciple × Wastrel alms** | GIVE_ALMS at station | queue forms (actorId order) → stock runs out → jostling (shove scuffle-lite) → coin/bread dropped → converging frenzy (L386 as mechanic) → Watch move-along | fed wastrels · frenzy tramples a Serf (violence witness chain) · Disciple flees, Priest PLEADs |
| **Priest × crime** | Priest witnesses theft/violence | REPORT to Watch → arrest at scene → Priest PLEAD (mercy petition) → Watch disposition check | culprit released to the Priest's custody (fed, preached at) · arrest proceeds, Priest tends the DOWNED · Priest becomes rumor node about the culprit |
| **Serf × Watch** | Serf witnesses crime with Watch in hearing | REPORT (only if Watch near — legible cowardice) → Watch INVESTIGATE → culprit fled? dwell + alarm | crime solved · false-quiet (no Watch near ⇒ crime pays — Wastrel learns nothing, PLAYER learns the map's dark spots) |
| **Wielder × everyone** | presented-Wielder enters sight | every type's deference posture fires per §4.9 — the street reshapes itself: Watch parts, Serfs make way, Wastrels converge begging, clergy revere, Shopkeepers press goods | free goods, open doors, a begging tail — and total witness silence: no CrimeWitnessed against the presented Wielder is actionable (immunity); disguise flips ALL of it (Persona seam) |

### 5.2 Watch liability ladder for Animal crime **(invention)**

Animal steals → Watch APPREHEND retargets the OWNER: bark → Keeper pays victim
(coin transfer) → can't pay → Keeper WARY-flagged + the Animal is fair game for
DEFEND_STOCK scuffles (a shopkeeper may whack the dog: hurt-not-kill). No animal arrests.

### 5.3 BEG target ranking

Richest visible: Disciple/Priest (they give) > Shopkeeper > Serf > Watch (never gives,
moves you along). Ties by actorId. Presented-Wielder present → all Wastrel BEG targets
him (§4.9 APPROACH).

### 5.4 Trace 1 — "The dog, the fish, the fire" (~30 ticks)

Setup (tick 5,000, market hours): dog #41 (owner Keeper #40, pen 8 tiles from the fish
stall), Shopkeeper #12 at stall (stock: fish item ids 301–306; lantern prop tile adjacent,
`cluttered` tag), Serf #23 gutting fish 4 tiles away, Wastrel #77 loitering 6 tiles away,
Watch #5 on patrol 14 tiles away. Draw values are **illustrative** — golden pins real
values at implementation (FACES §6 precedent).

| Tick | Actor | Event / draw | Result |
|---|---|---|---|
| +0 | dog 41 | HUNGER hits 2,996 < LOW; SEEK_FOOD 520 > FOLLOW 300; leash-break: `actor.slip`(41, idx 0) → 11,402 < 16,384 threshold | PolicyChanged(41, FOLLOW→SEEK_FOOD, NEED_HUNGER_LOW) |
| +1..6 | dog 41 | stepToward stall, 1 tile/tick | closes 6 tiles |
| +7 | dog 41 | adjacent: `take(fish 303)` — owned by #12 | `ItemTransferred(WORLD→41, 303, STEAL)`; `CrimeCommitted(41, THEFT, cell, victim 12)` |
| +7 | scan | witness pass (ActorId order): #12 dist 1 ✓, #23 dist 4 ✓ (LIT bucket, radius 8), #77 dist 7 ✓ | 3× `CrimeWitnessed(…, culprit=41)` |
| +8 | shop 12 | DEFEND_STOCK 910 fires; `AlarmRaised(12, THIEF, loudness 10)` | chase begins; dog FLEE (SAFETY −2,000 on being chased) |
| +8 | keeper 40 | hears alarm (dist 9 ≤ 10) | RECAPTURE(target 41) |
| +10 | — | #12 is 3 tiles from stall → stall UNATTENDED | loot window opens |
| +12 | shop 12 | chase step through `cluttered` lantern tile: `actor.mishap`(12, idx 0) → 3,101 < 6,554 (10% Q16) | **lantern falls**: `ActorIgnition(awning cell)` |
| +13 | Fire | consumes ActorIgnition (THERMAL) | `TileIgnited`; light spike |
| +13 | serf 23 | sees fire (stimulus same tick at phase 7) | FLEE + `AlarmRaised(23, FIRE, loudness 14)` |
| +14 | wastrel 77 | LOOT_RUSH score: stall UNATTENDED, no Watch/victim LOS ✓ | PolicyChanged(77, LOITER→LOOT_RUSH, STIM_OPPORTUNITY) |
| +14 | watch 5 | hears FIRE alarm (dist 12 ≤ 14) | INVESTIGATE(fire cell) |
| +15..17 | wastrel 77 | grabs fish 304, 305 | 2× ItemTransferred(LOOT) + CrimeCommitted ×2 (unwitnessed — Serf fled, facing away) |
| +18 | shop 12 | chaseTimer expires (dog faster) → TARGET_LOST; sees OWN STALL BURNING | DOUSE_FIRE: bucket route to harbor edge (3 tiles) |
| +19..22 | shop 12 | two bucket runs: `ActorDouse(cell, 1)` ×2 + FluidEmitters withdrawal at harbor | fire out at +23; 3 awning tiles → ash (`MaterialTransformed(BURNOUT)` ×3) |
| +20 | watch 5 | arrives; sees #77 fleeing with items flagged stolen (LIT, dist 5) | APPREHEND(77) |
| +24 | watch 5 | corners #77 (alley dead-end); `actor.scuffle`(5, idx 0)=44,102 vs (77, idx 0)=12,997 + stat gap | #77 DOWNED, arrested; fish recovered → returned to stall inventory |
| +26 | dog 41 | eats fish (HUNGER → 10,000); keeper adjacent: `actor.comply`(41, idx 0) → pass | RECAPTURE succeeds; FOLLOW restored |
| +30 | — | quiet. Ledger: stock −1 fish (eaten), 3 ash tiles, 2 water units moved, 1 arrest | six actors, four pillars (fire, water, property, law), zero scripts |

### 5.5 Trace 2 — "Bread in the gutter" (~20 ticks)

Setup (tick 3,000, alms hour): Priest #60 + Disciple #61 at alms station (6 bread items),
Wastrels #70, #71, #72 in radius, Serf #24 passing with a coin purse, Watch #6 at post
10 tiles off. Shallow gutter water (FLUID depth 2) along the lane.

| Tick | Actor | Event / draw | Result |
|---|---|---|---|
| +0 | priest 60 | PREACH; wastrels' BEG scores rise (crowd magnet) | #70–72 converge, queue by actorId |
| +2..5 | disciple 61 | GIVE_ALMS in queue order: bread → 70, 71, 72 | 3× ItemTransferred(GIVE); HUNGER satiated |
| +6 | wastrel 72 | still CRITICAL hunger (one bread short of LOW); `actor.targetPick`(72, idx 0) selects Serf 24's visible purse; deterrence check: Watch #6 not in LOS ✓, victim IS (pickpocket = contested, allowed vs unaware) | SEEK_FOOD steal branch: `actor.steal`(72, idx 1) → 51,003 ≥ 45,875 threshold — **fumbled** |
| +6 | — | fumble = shove: `CrimeCommitted(72, ASSAULT-lite, cell, victim 24)`; purse drops to gutter cell | coins + Serf's bread hit water (depth 2) |
| +7 | scan | witnesses: Serf 24, Priest 60, Disciple 61, Wastrels 70–71 | CrimeWitnessed ×5 |
| +7 | wastrels 70,71 | dropped coins in sight → LOOT_RUSH (frenzy — L386 as mechanic) | converging scramble |
| +8 | serf 24 | Watch in hearing (10 ≤ 12): REPORT + `AlarmRaised(24, THIEF, 10)` | Watch #6 APPREHEND(72) |
| +9..12 | — | scramble: #70 grabs 2 coins, #71 grabs 1; bread item in water gains RUINED kind-flag (ItemsLite rule: food + FLUID depth ≥ 1) **(invention)** | property scattered; bread destroyed by the water pillar |
| +13 | watch 6 | arrives; #70/#71 scatter (WARY); corners #72 | scuffle → #72 DOWNED |
| +14 | priest 60 | PLEAD at arrest: disposition check vs Watch (FRIENDLY, faction table) | Watch releases #72 to Priest custody (raws `pleadThresholdQ16`, draw-free — disposition lookup) |
| +16..20 | — | Priest feeds #72 the last bread; #72 disposition → FRIENDLY toward clergy; follows the alms circuit next day | the order just recruited a tail; Serf is out 3 coins and lunch; two coins entered wastrel circulation (economy lap events) |

### 5.6 Trace 3 — "Night water" (~25 ticks)

Setup (tick 19,000 — night, DARK buckets everywhere without lanterns): sewer backflow
raises FLUID depth in Grate Alley (the Sewer Flood flagship as ambient cause). Wastrel #75
asleep on grate; Shopkeeper #13 asleep in shuttered chandlery fronting the alley; pig #82
(Keeper #80) in sty at alley mouth; Watch #7 night patrol, lantern (LIT radius bubble).

| Tick | Actor | Event / draw | Result |
|---|---|---|---|
| +0..3 | FLUIDS | depth at grate cell reaches 4 | — |
| +4 | wastrel 75 | polls FLUID at own tile ≥ 4 → SAFETY −4,000 → FLEE | wakes, flees the water uphill |
| +6 | wastrel 75 | flee path ends at chandlery porch (PropertyIndex: owner #13); DARK → nobody sees | REST re-scores: sleeps on owned frontage — **trespass** (CrimeCommitted, TRESPASS, unwitnessed) |
| +8 | pig 82 | water enters sty (depth 3): panic FLEE; pen gate draw `actor.slip`(82, idx 0) → pass (panicked beasts break fences, boldness ×2 while PANICKED) | pig loose in the dark |
| +9 | keeper 80 | hears sty clatter (`NoiseEmitted` loudness 8, dist 5) | wakes → RECAPTURE(82) |
| +11 | shop 13 | hears porch shuffle: `NoiseEmitted` loudness 4, dist 2 | wakes → INVESTIGATE own porch |
| +12 | shop 13 | DARK bucket: sight radius 2 — sees #75 at 1 tile | `AlarmRaised(13, PROWLER, 9)` |
| +13 | wastrel 75 | FLEE again (alarm directed at him) — but floodwater blocks downhill, leash ignored under FLEE | doubles into Cooper's Lane |
| +14 | watch 7 | hears alarm (dist 8 ≤ 9) → INVESTIGATE; lantern raises local bucket to LIT | moving light: the observer literally watches the law's light sweep the lane |
| +16 | pig 82 | SEEK_FOOD: smells Serf grain store (open sacks, PropertyIndex owner Serf #24): eats | CrimeCommitted(82, THEFT, …) — unwitnessed in the dark |
| +18 | watch 7 | lantern LOS catches #75 in Cooper's Lane (LIT radius 8) | move-along ladder: #75 complies (flees on), no arrest — nothing stolen, TRESPASS unwitnessed: **the crime the law never saw stays unpunished** — legible from the event log |
| +20..23 | keeper 80 | tracks pig by noise events; `actor.comply`(82, idx 1) → fail (still eating); second attempt +23 → pass | pig penned; grain sacks −N (Serf #24 discovers at dawn — WORK restock finds shortfall, alarm bark: tomorrow's rumor) |
| +25 | — | water crests and drains (fluid sim); wastrel sleeps in a doorway; ledger: grain −N, one pen rail broken (flagged nicety: v1 no fence damage — pen breach is the slip draw only) | pillars: water (cause), property (trespass ×1, theft ×2), law (patrol response), light (every sight check) |

---

## 6. actors.json raws schema

**Path:** `content/raws/actors/<type_id>.json` (relocates with the raws move like
materials). Loader validation (boot fails): every policy id exists; policy params complete
for the stack; needs rows complete (5); deference posture ∈ enum; spawn zones exist in the
map's zone annotations; Animal types declare `species`; Keeper types declare `owns` with
min ≥ 1; glyph is one printable ASCII char; all numbers integer; sightRadiusByLight has
exactly 4 entries; scuffle stats present for any type whose stack includes a scuffle-capable
policy.

```json
{
  "id": "militia_watch",
  "displayName": "Militia Watch",
  "actorClass": "MilitiaWatch",
  "faceArchetype": "guard",
  "glyph": "W", "tint": "#6FA0D0",
  "factionId": "watch",
  "hp": 30,
  "speedTicksPerStep": 2,
  "sightRadiusByLight": [2, 5, 8, 10],
  "hearingRadius": 12,
  "inventoryCap": 4,
  "leashRadius": 24,
  "scuffle": { "strike": 5, "grit": 6 },
  "needs": {
    "hunger": { "start": 9000, "decayPerKilotick": 700, "lowBonus": 250, "critBonus": 500 },
    "rest":   { "start": 9000, "decayPerKilotick": 550, "lowBonus": 250, "critBonus": 500 },
    "coin":   { "start": 8000, "decayPerKilotick": 200, "lowBonus": 100, "critBonus": 200 },
    "safety": { "start": 10000, "decayPerKilotick": 0, "recoverPerTick": 2,
                "fireSeenDelta": -1500, "violenceSeenDelta": -1200, "waterDelta": -1000,
                "lowBonus": 400, "critBonus": 800 },
    "duty":   { "start": 9000, "decayPerKilotick": 900, "lowBonus": 300, "critBonus": 400 }
  },
  "policies": {
    "DEFER_WIELDER": { "priority": 980, "posture": "UNCONDITIONAL_COMPLIANCE" },
    "FLEE":          { "priority": 940, "fleeFireDepth": 1, "fleeWaterDepth": 5,
                       "ignoresLeash": true },
    "APPREHEND":     { "priority": 800, "chaseTimeoutTicks": 120, "ignoresLeash": true,
                       "escortToAnchor": true },
    "INVESTIGATE":   { "priority": 620, "dwellTicks": 40 },
    "DOUSE_FIRE":    { "priority": 560, "onlyAttendedProperty": true, "bucketUnits": 1 },
    "PATROL":        { "priority": 220, "waypointSet": "dock_patrol_A",
                       "rhythmWindow": [0, 12000], "rhythmBonus": 60 },
    "EAT":           { "priority": 320 }, "REST": { "priority": 300,
                       "rhythmWindow": [12000, 24000], "rhythmBonus": 80 },
    "LOITER":        { "priority": 10, "shuffleChanceQ16": 6554 }
  },
  "deference": {
    "posture": "UNCONDITIONAL_COMPLIANCE", "clampFloor": "FRIENDLY",
    "willTell": ["patrol_routes", "crime_reports", "rumors"],
    "willGive": ["escort"],
    "canon": "novel L2967, L2968, L2414"
  },
  "spawn": {
    "zones": { "watch_post_gate": 2, "watch_post_market": 2 },
    "shift": "WATCH_ROTATION"
  }
}
```

```json
{
  "id": "animal_dock_dog",
  "displayName": "Dock Dog",
  "actorClass": "AnimalActor",
  "species": "dog",
  "faceArchetype": null,
  "glyph": "d", "tint": "#C8A050",
  "factionId": "animals",
  "hp": 10,
  "speedTicksPerStep": 1,
  "sightRadiusByLight": [4, 6, 8, 8],
  "hearingRadius": 16,
  "inventoryCap": 1,
  "leashRadius": 6,
  "scuffle": { "strike": 2, "grit": 3 },
  "needs": {
    "hunger": { "start": 8000, "decayPerKilotick": 1600, "lowBonus": 300, "critBonus": 600 },
    "rest":   { "start": 9000, "decayPerKilotick": 800,  "lowBonus": 200, "critBonus": 400 },
    "coin":   { "start": 10000, "decayPerKilotick": 0 },
    "safety": { "start": 10000, "decayPerKilotick": 0, "recoverPerTick": 4,
                "fireSeenDelta": -4000, "violenceSeenDelta": -2000, "waterDelta": -2500,
                "loudNoiseDelta": -1500, "lowBonus": 500, "critBonus": 900 },
    "duty":   { "start": 10000, "decayPerKilotick": 0 }
  },
  "policies": {
    "FLEE":         { "priority": 950, "ignoresLeash": true },
    "SEEK_FOOD":    { "priority": 480, "stealBoldnessQ16": 16384,
                      "panicBoldnessMulQ16": 131072, "foodKinds": ["fish", "bread", "grain"] },
    "FOLLOW":       { "priority": 300, "followRadius": 3 },
    "GRAZE_WANDER": { "priority": 20 },
    "REST":         { "priority": 260 }
  },
  "orphanOverrides": {
    "SEEK_FOOD": { "stealBoldnessQ16": 29491 },
    "GRAZE_WANDER": { "leashRadius": 20 }
  },
  "comply": { "recaptureQ16": 45875, "retryCooldownTicks": 10 },
  "spawn": { "ownedBy": "animal_keeper_dog", "countRange": [1, 3], "zones": {} }
}
```

Keeper side (excerpt): `"owns": [{ "type": "animal_dock_dog", "count": [1, 3] }],
"reacquireTicks": 12000` — the registry reads this for the atomic pairing (§4.8).

All numbers above **(placeholder / needs-blessing)**.

---

## 7. Observer

### 7.1 Per-type glyph/color (luminous-on-black; final art via the art-swap seam)

| Type | Glyph | Tint (placeholder) | Rationale |
|---|---|---|---|
| Militia Watch | `W` | steel-blue `#6FA0D0` | cold law-light |
| Serf | `s` | dun `#A08860` | dull, numerous |
| Wastrel | `w` | ash-grey `#787068` | barely-there against the black |
| Priest of the Flame | `P` | white `#F0EEE6` | the unwelcome white robe, glowing |
| Disciple of the Flame | `p` | grey `#B8B4AC` | apprentice grey (novel L2969) |
| Shopkeeper | `$` | amber `#E0A030` | lamplit commerce |
| Animal Keeper | `K` | tan `#B09048` | |
| Animal | per-species: dog `d`, pig `g`, goat `t`, mule `m` | species tint from raws | |

Status ring accents (glyph background/overlay): ON_FIRE flicker orange · DOWNED dark red ·
ORPHANED dim pulse · ALERTED white tick. Actors render above tiles, below UI; the LIGHT
lane tints them like everything else — an actor in the dark is genuinely hard to see
(that's the game).

### 7.2 Selection inspector (`InspectorPanel` extension)

Selected actor shows: name/typeId + glyph · FACES-SPEC portrait (archetype per §6 raws;
`(worldSeed, actorId)` — actors ARE the npcIds) · the five need bars (0–100 with LOW/CRIT
notches) · **current policy + reason code + target** ("SEEK_FOOD · NEED_HUNGER_LOW · fish
stall (312,88)") · status bits · inventory list · owner/owned links (click-through) ·
deference row (posture + canon cite) · disposition overrides list · last 10
`ActorPolicyChanged`/crime events for this actor (drained log, scrollback). The inspector
is the legibility acceptance surface: a playtester must reconstruct any trace in §5 from
it alone.

### 7.3 Population overlay

Toggle: per-chunk actor counts as luminous digits/heat tint (concrete chunks from the
registry's chunk buckets; frozen chunks from ChunkSummary population bytes — §2.9.2), plus
a roster sidebar: count by type, arrests today, crimes today (unwitnessed count shown in
grey — the observer can see what the law can't). `MacroPanel` precedent; read-only views,
GL-free core per `LightRenderView` rules.

---

## 8. Unit-test list

All integer-deterministic, cross-platform; goldens via `--bless` only. New ArchUnit rules
in the purity suite. (Names unique across all specs; SPEC-INDEX totals to be updated.)

1. `A1_twinRun_withActors_identicalHashChains` — 2,000 ticks, full Docks actor roster,
   two engines in one JVM: identical per-tick hash chains including the new `ACTR`
   sub-hash (M0 pattern extended).
2. `A2_registry_iterationIsActorIdOrder` — spawn actors in shuffled order; tick iteration
   and witness scans visit ascending ActorId; hash identical vs sorted spawn.
3. `A3_phaseOrder_actorsAfterLightBeforeFlux` — TickPhase enum ordinal assertions +
   an actor's sight check reads THIS tick's light (ignite at phase 3 → actor flees at
   phase 7 same tick).
4. `A4_actorNeverWritesLanes` — ArchUnit: `com.trojia.sim.actor` has no ChunkWriter
   dependency; instrumented run: zero lane writes originate in phase 7.
5. `A5_keeperSpawn_atomicPairing` — spawnKeeper creates keeper + raws-count animals with
   both-way links; spawning an Animal with dead/absent owner hard-fails.
6. `A6_keeperDeath_transferNearest_tieByActorId` — two candidate keepers equidistant →
   lower actorId adopts; `OwnershipTransferred` emitted exactly once.
7. `A7_keeperDeath_noCandidate_orphanStackSwap` — Animal gains ORPHANED bit; policy stack
   evaluates the orphanOverrides params; `AnimalOrphaned` emitted.
8. `A8_orphanAdoption_scanDeterministic` — new keeper spawns near two orphans → adopts
   in actorId order up to raws count.
9. `A9_registryAudit_invariantHolds10kTicks` — `-ea` soak: every Animal owner-XOR-ORPHANED
   every tick; keeper animal-less ≤ reacquireTicks; "sow farrows" respawn fires at grace
   expiry, deterministically.
10. `A10_policySelection_maxScore_tieByStackOrder` — crafted scores equal → earlier stack
    entry wins; `ActorPolicyChanged` carries the right reasonCode.
11. `A11_needDecay_exactBoundary` — decayPerKilotick accumulator: reserve crosses LOW at
    the exact predicted tick; 2,999→no switch, 3,000-boundary semantics pinned (< LOW).
12. `A12_needClamp_saturating_noWrap` — massive event deltas clamp to [0, 10,000]; no
    negative/overflow ever.
13. `A13_perception_lightBucketsGateSight` — same thief at dist 6: BRIGHT seen, DARK
    unseen; carried-lantern light raises the bucket and flips the result; LOS blocked by
    OPACITY wall → unseen at any light.
14. `A14_actrSection_roundTrip_allFields` — save/load: every §2.8 field byte-identical,
    ItemsLite included; missing ACTR section on a declared-required load = hard fail.
15. `A15_saveLoad_midGoal_equivalence` — save at TICK_END mid-APPREHEND-chase; load; run
    N ⇒ identical to uninterrupted run K+N (targets, timers, needAccum all carried).
16. `A16_freezeThaw_actorVerbatim_plusRepair` — actor frozen with chunk, thawed after T
    ticks: identity/inventory verbatim; needs decayed exactly T×rate; invalid target →
    anchor reset with TARGET_LOST reason; ChunkSummary population counts correct while
    frozen.
17. `A17_frozenChunks_impassable_leashHolds` — actor at bubble rim never steps into
    non-concrete chunk; no BoundaryFlux credits ever originate from phase 7.
18. `A18_deferenceTable_immutable_presentedIdentity` — no runtime write path to the §4.9
    table; Watch never enters HOSTILE toward presented-Wielder under any event storm;
    presented ≠ Wielder (disguise seam) → APPREHEND eligible; clamp floors hold.
19. `A19_witnessScan_losOrderAndDeterrence` — crime with 3 witnesses emits CrimeWitnessed
    in ActorId order; LOOT_RUSH scores 0 while Watch/victim has LOS, > 0 one tick after
    LOS breaks.
20. `A20_crimeUnwitnessed_lawSilent` — DARK-alley theft with no witness: zero
    CrimeWitnessed, Watch never responds; observer overlay counts it grey.
21. `A21_barkDraw_presentationOnly` — strip all `actor.bark` draws: world hash chain
    unchanged (no state reads barks); with barks: draw keys match the §2.2 addressing.
22. `A22_eventVolume_underCap` — 300 actors × worst-case alarm storm: every topic
    < 65,536/tick; ActorMoved observer-drain only.
23. `A23_addAType_oneFileOneRawsEntry` — test registers the §1.4 Ratcatcher (subclass +
    raws string) at boot; full twin-run suite passes; removing it restores prior goldens
    (proves isolation).
24. `A24_archUnit_thinSubclasses_depth2_purity` — no class extends a concrete Actor
    subclass; subclasses declare no fields/methods beyond `policies()` + constructor;
    actor package: no float/double, no java.util.HashMap in state, events
    primitives-only.
25. `A25_scuffle_hurtNotKill_downedRecovers` — hp to 0 via scuffle ⇒ DOWNED not dead;
    recovers after downedTimer; death only via fire/drowning paths; `ActorDied` cascade
    runs ownership transfer same phase.
26. `A26_douse_conservesWater_ledgerExact` — bucket line: FluidLedger net zero
    (withdrawal at source == arrival at fire cell − vaporized), audit exact to the unit.
27. `A27_onFireActor_spreadsViaIgnitionRequests` — ON_FIRE animal fleeing across dry
    grass leaves ActorIgnition trail; FireSystem consumes next tick; documented 1-tick
    latency pinned.
28. `A28_golden_dogFishFire` — flagship: §5.4 setup scripted from fixed seed ⇒ blessed
    event-log + hash chain (the actor system's Tavern-Fire equivalent); twin-run +
    save/load-at-tick-+15 both identical.
29. `A29_spawnBake_deterministic` — importer/ActorSeeder twice from same TMX ⇒
    byte-identical tick-0 TROJSAV including ACTR; zone/table/index id assignment order
    pinned.
30. `A30_rhythmWindow_scoringOnly` — inside/outside window changes scores by exactly
    rhythmBonus; no policy is ever hard-scheduled (needs can override rhythm).

---

## 9. Cross-spec hooks and open blessing items

### 9.1 PROGRESSION-SPEC hooks (actors feed the XP economy)

Actors are the live entities PROGRESSION's gates require: Shadow-Wait's "live observer
within 10 tiles" = any Actor with sight of the tile (gate 3.2#3); pickpocket targets and
satiation contextKeys (observer/target entity id) are ActorIds; Gutter-Ken informants are
Wastrels/Shopkeepers with raws rumor tables (information only — the §2 #17 fence holds);
trainers (Monastery, fixers) will be placed as actors post-MVP. Seam E1's hireling thugs
are encounter-roster actors, not the eight civic types.

### 9.2 Blessing queue (Eli)

1. **ACTORS phase position (§2.1)** — after LIGHT, renumbering 8–11, golden re-bless +
   formatVersion bump; and the 7.8 → 8.2 ms budget line.
2. "Priest/Disciple of the Flame" slot-in framing (§4.4–4.5) — ordained-monk-seconded /
   gray-apprentice reading of the canon order.
3. "Serf" label scope (novel's servant/laborer/peasant stratum, never army slaves).
4. Animal roster (dog/pig/goat/mule; rats ambient; no gulls v1; wyverns/pegii excluded).
5. Hurt-not-kill rules: scuffle/DOWNED, no NPC-NPC sim deaths except fire/drowning; the
   Watch arrests, never executes (§2.7).
6. Keeper liability ladder (§5.2) and the "sow farrows" invariant-restore (§4.8.3).
7. Keeper opens the pen when fire nears (§4.7) — chaos-positive invention.
8. All (placeholder) numbers: need thresholds/decays, score bands, DAY=24,000, bucket
   edges, radii, Q16 chances in §5–§6.
9. Disposition FSM trimmed to 4 states (§2.10) vs SoR's 6.
10. Watch HOSTILE-capable wording in COMBAT-SCREEN §1.1 amended per §2.7 (presented-
    identity gate) — co-sign needed from that spec.

---

*Spec produced 2026-07-12 for the F2.5 actors milestone. Consumes the Streets of Rogue
emergence dossier and the canon-types dossier; every canon claim carries a novel/WB cite;
every invention is marked. SPEC-INDEX.md should gain this row (30 tests) at the next index
refresh.*
