# Pre-S8 baseline world hash

The state of the Docks the moment before the economy arc (S8–S12, "The Ward Prices Itself")
started touching it. Committed so any later arc slice can prove — with a number, not a claim —
whether it disturbed the ward or not.

## The number

```
run:       ./gradlew.bat :client-observer:runDocksActors --args="--ticks 15000"
souls:     692
ticks:     15,000  (the same soak length the twin-run gate uses)
at tick 15000;  WRLD=5f23f797e04de292  ACTORS=7dcb00a148a8b425
COMBINED WORLD HASH: 0x3685019bfa04c4c3
```

Recorded at commit `7d5b39e` (S8 slice 2), on branch `wip/s8-goods-and-scalps`, whose parent
is `9c366f7` — the last pre-S8 commit on `main`. Slices 1 and 2 changed only report text and
verification code, never sim state, which is why this baseline is also the state of `9c366f7`:

* the 400-tick combined hash is `0xf2d2f86753d8ee73` both before and after slice 2;
* the 15,000-tick combined hash is `0x3685019bfa04c4c3` both in slice 1's twin-run gate run
  and in the slice-2 verification run.

## What the three numbers are

| field | meaning |
| --- | --- |
| `WRLD` | `WorldHasher.hashWorld` — decoded lane values, chunks ascending, lanes in registry order, overlays by localIdx. The terrain and its contents. |
| `ACTORS` | the `ActorsSystem` section sub-hash: the whole persisted triad in canonical order — actors, homes, relationships, ItemsLite entries, ItemsLite free stack, bank, shoveLog, skillTracks, factionStandings, crimeLog, questLog, fishingSpots, deathLog. |
| `COMBINED` | both sub-hashes folded in ascending-salt order; invariant to the order the sinks were fed. This is the one the twin-run gate compares. |

A side note worth keeping: `WRLD` reads `5f23f797e04de292` at 400 ticks and at 15,000 ticks
alike. That is the "actors never write world lanes" house rule showing up as a measurement
rather than an assertion — 15,000 ticks of 692 souls living, working, fighting and dying moved
not one byte of terrain.

## How it is used

`:client-observer:twinRunGate` reads this file and prints `MATCH` or `DRIFT` against the
`COMBINED WORLD HASH` line above. **Drift is reported, never failed.** S8 ships scalps and S9
ships a price tick; those slices *should* drift, and a gate that blocked them would have turned
a determinism harness into an accidental freeze on the whole arc. The gate fails on one thing
only: the two twin runs disagreeing with each other.

So the discipline is: when a slice drifts, that is expected — say so in the commit, and say
what changed. When a slice that should have been inert drifts, that is the bug this file
exists to catch.

## Re-blessing

Do not edit the number to make something go green. Re-record it only when the arc deliberately
moves the world forward, and when you do, replace the whole block above — run, tick count,
commit, and all three hashes — so the next reader can reproduce it.

---

## The S8 drift, on the record

S8 ("goods and scalps") drifted the ward on purpose. This section says exactly where, so a
later reader can tell a deliberate step from a regression.

### What was proved INERT (the number held)

At commit `ec85f32`, with the whole YIELD pair wired — `JobParams.yieldKind`/`yieldPerUnit`,
the mint at `JobBehaviors.awardWorkEvent`, the per-kind conservation counters, the seven-line
trade-goods report — and **every yield set to 0**, the 15,000-tick soak returned
`COMBINED WORLD HASH: 0x3685019bfa04c4c3`: byte-identical to the baseline above. The only
change in the whole report was the new, all-zero goods block.

The cull verb got its own inertness measurement, holding everything else constant. With no
actor type declaring a `scalpItem`, `CullVerb.tryCullInReach` makes no draw and writes no
state, so a 15,000-tick run with the verb wired is **byte-identical** to the same run with the
call commented out — reports compared in full, not just the hash.

### What drifted, and why it had to

Two changes moved the number by construction, not by behavior:

1. **Four new job ids** (`serf.ropewalker`, `serf.tarhand`, `serf.cooper`, `serf.salter`).
   `Jobs.ALL` is sorted by id, so appending shifts every later job's ordinal, and
   `jobOrdinal` is persisted and hashed. A yield is `JobParams` data, so four different yields
   need four bound param sets; there is no version of this that leaves ordinals alone.
2. **One new persisted scalar per actor** (`Actor.culledUntilTick`, the cull latch). The house
   rule is that any new persisted state appears in `serialize()`, `load()` AND `hashInto()` in
   matching order, appended last. Growing the hash stream necessarily changes the hash.

Neither can be measured away, and neither is a behavior change hiding in a number. The honest
inert proofs are the two above, which bracket them.

### The post-S8 number (for S9's inert proofs)

```
at commit 8e19483, 15,000 ticks, 692 souls
COMBINED WORLD HASH: 0xf1d9bdb6b63e9d97
```

Recorded from the twin-run gate inside the green full build. The pre-arc number above is
deliberately NOT re-blessed to this — it is the pre-S8 reference and stays that. Use this one
when S9 wants to prove a slice of its own was inert.

---

## The Simple Magic drift, on the record

Simple Magic ("the public shelf") drifted the ward. This section says exactly where, in this
file's own discipline, so a later reader can tell a deliberate step from a regression. The gate
reports `DRIFT` against the pre-S8 number and that is the expected reading.

### What drifted, and why it had to

Three changes moved the number **by construction**, not by behaviour:

1. **`Actor.hp` is initialised for the first time.** `hp` had been declared, persisted and hashed
   since the ACTR chunk existed and was never once written — every actor carried 0 and the sheet
   printed `hp: 0`. The vitality axis is the first thing in the project that ever read it, so it
   now starts at the type's authored maximum (`Actor` ctor, `this.hp = stats.hp()`). That is one
   hashed per-actor scalar changing value for all 692 souls. Nothing behavioural moved with it —
   *nothing read hp before* — and there is no version of the fix that leaves the hash alone.
2. **Two new persisted frames, appended last.** `ActiveEffects` (96 rows × target/kind/mode/param/
   magnitude/startTick/endTick) rides the `ActorsSystem` chunk's `serialize`/`load`/`hashInto`,
   and `Actor.castUntilTick` is a new per-actor scalar in all three. The house rule is that new
   persisted state appears in the triad in matching order, appended last; growing the hash stream
   necessarily changes the hash.
3. **One new skill id (`linkcraft`).** `SkillRawsLoader` keys skills into a `TreeMap`, so the
   registry is alphabetical — inserting `linkcraft` shifts the raw index of every alphabetically
   later skill, and skill raw indices are the array layout `skillTracks` persists and hashes (the
   skill count is a framing guard on load). This is the `Jobs.ALL` ordinal case S8 recorded, one
   raws file over.

None of the three can be measured away, and none is a behaviour change hiding in a number.

### What was proved INERT

**The soak has no player in it, and no AI works a crafting.** The whole cast path is therefore
unreached across 15,000 ticks: `SpellVerb` is never called, `ActiveEffects.tick` sweeps 96 free
rows and writes nothing, and the spell registry is bake-immutable, never persisted and never
hashed. The Simple Magic report block in the soak prints `lingering rows live: 0/96` for exactly
that reason — that is the honest reading, not a hidden failure.

Which is precisely why `CraftingDeterminismTest` exists beside the gate rather than relying on
it: it drives a cast tape through two independent builds of one seed in lockstep and demands the
hardened ACTORS hash match at every 500-tick checkpoint, then runs a third build with the tape
removed and demands the hash **differ** — so a green soak cannot mean the magic never fired.

**Round 3 (the correctness round) is inert against round 2 by construction.** Every fix in it is
either unreachable without a caster (the shared verb's refusal ladder, the mend arithmetic) or
provably identity-preserving on the shipped content: the cost model widened to `long` and
saturates at a ceiling no shipped resist approaches (the whole shelf sits under 60), and the new
unbridged tax on the area axis is charged only when `areaRadius > 0`, which no shipped crafting
authors. The loader's new skill-key validation accepts every shipped row unchanged. This is a
construction argument, not a second measurement — stated as such.

### The post-Simple-Magic number

```
at commit 788c779 + round 3, 15,000 ticks, 692 souls
COMBINED WORLD HASH: 0x4483dbe6d7ebd9e1
```

Recorded from the twin-run gate inside the green full build (`run A` and `run B` identical, report
text byte-identical). The pre-S8 number at the top of this file is **not** re-blessed to it — that
one is the pre-arc reference and stays that. Use this one when a later slice wants to prove itself
inert against the ward as Simple Magic left it.
