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
