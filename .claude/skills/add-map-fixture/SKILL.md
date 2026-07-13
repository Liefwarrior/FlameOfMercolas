---
name: add-map-fixture
description: Author, validate, bake, and wire a new Tiled (.tmx) world fixture into the observer — the full map/world CRUD pipeline (tavern_fixture, compound_block, etc.).
---

The full pipeline for a new authored world, following the exact convention `content/maps/src/tavern_fixture.tmx` and `compound_block.tmx` already established.

## Steps

1. **Read `content/maps/README.md`** first — it documents the authoring convention (one layer-group per z-level named `z:<sign><n>`, `terrain`/`floor`/`fluids`/`markers` sublayers, `material=`/`form=` tile properties) and lists every fixture that exists so far, with provenance notes.
2. **Author the raw Tiled XML** under `content/maps/src/<name>.tmx`. Reuse `content/maps/src/materials.tsx` as-is unless a genuinely new material/form combination is needed (check `content/raws/materials/` for what's already available before adding tiles — see the `add-material-raw` skill if a new material is actually needed). Add marker objects (object layer) at every meaningful spawn/anchor point, named clearly (e.g. `business_shipwright_anchor`) — downstream population code places actors from these names, not by re-deriving positions from raw tile scanning.
3. **Validate before baking**: `.\gradlew.bat :tools:build` then `.\gradlew.bat :tools:run --args="check-map content\maps\src\<name>.tmx"` — must report `OK`, not `FAIL`. Fix validator errors yourself; don't bake a map that fails validation.
4. **Bake it**: add a `<Name>FixtureBakeTest.java` (or `<Name>BakeTest.java`) under `client-observer/src/test/java/com/trojia/client/boot/`, mirroring `TavernFixtureBakeTest.java` EXACTLY — it re-bakes the committed `.trojsav` on every test run (byte-deterministic, so reruns are a no-op diff unless the map or raws actually changed — that's the intended freshness mechanism, not a bug). Output goes to `content/maps/baked/<name>.trojsav`.
5. **Wire the loader**: add a `FixtureWorldLoader.load<Name>()` (or generalize the existing loader if there are now 3+ fixtures) mirroring `loadTavern()`'s contract — raws-fingerprint check against the baked header, `IllegalStateException` with a clear rebake instruction if it's stale.
6. **Wire the observer**: `client-observer/src/main/java/com/trojia/client/ObserverApp.java` needs a way to select which fixture boots (a constructor flag or CLI arg — check how the most recent fixture wired this in before inventing a new mechanism).
7. **Smoke-test it**: `.\gradlew.bat :client-observer:run --args="--smoke=N"` over the new fixture — must render N frames and exit cleanly with no exceptions.
8. **Run `./gradlew.bat build --console=plain`** — full build, all modules.

## Gotcha already hit this session

If a concurrent task is mid-edit on `sim-core` (e.g. the actor system foundation), `:tools:build`/`:client-observer:build` can transiently fail for reasons that have nothing to do with your map — check `git status` / recent commits for concurrent in-flight work before assuming a build failure is your bug. A standalone Python (or similar) re-implementation of the validator's pass list is a legitimate fallback to self-verify a map's structural correctness when the real Java toolchain is transiently broken by unrelated work — but always re-run the REAL validator once the tree stabilizes to confirm the standalone check didn't miss anything.

## Canon note

If the fixture represents in-world housing, re-read `docs/design/DECISIONS.md`'s "Trojian housing: Compounds" ruling before laying out buildings — Trojian residential space is walled Compounds (courtyard/atrium farm center, condo/mansion ring, rooftop slum), not detached single-family houses.
