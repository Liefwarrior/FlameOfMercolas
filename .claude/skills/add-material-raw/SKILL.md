---
name: add-material-raw
description: Add a new material/fluid/treatment/reaction raw (content/raws/{materials,fluids,treatments,reactions}) via MaterialRawsLoader, and update every hardcoded count across sim-core AND tools.
---

Adds one raw to `com.trojia.sim.material`'s registry family. This is the SAME loader/count-brittleness pattern for all four subdirectories (materials, fluids, treatments, reactions) — one file per raw, each with a top-level `"id"`.

## Steps

1. **Check `content/raws/materials/` (or the relevant subdir) first** — don't duplicate an existing material. Read an existing raw close to what you're adding (e.g. `steel.json` for a hard/dense metal, `leather.json`/`cloth.json` for soft flammables) as your field-contract template rather than re-deriving the schema from scratch.
2. **Read `sim-core/src/main/java/com/trojia/sim/material/MaterialRawsLoader.java`'s class javadoc** — it documents every validation rule (thermal stability invariant, FLAMMABLE field triple, melt/boil cross-reference rules, chargeable/emissive feature contracts). Violating one of these fails fast with a `RawsValidationException` naming the file+field — that's the loader working correctly, not a bug to work around.
3. **Bump every hardcoded count** (confirmed broken 3 times in one session by exactly this class of change):
   - `sim-core/src/test/java/com/trojia/sim/material/MaterialRawsLoaderCommittedTest.java` — `EXPECTED_IDS` (sorted-string-key order — insert alphabetically, don't just append) AND the `assertEquals(N, bundle.materials().size())` in the test whose name literally spells out the count (rename that test method too, e.g. `loadsNineteenMaterials...` — a stale name is a smell).
   - `sim-core/src/test/java/com/trojia/sim/material/MaterialRawsLoaderClasspathTest.java` — `assertEquals(N, fromJar.materials().size())`.
   - `sim-core/src/test/java/com/trojia/sim/json/RawsRoundTripTest.java` — `MIN_EXPECTED_RAWS` is a floor, not exact; only touch it if the total raws file count would drop below it.
4. **If this material needs to be paintable in a Tiled map fixture**, add tile(s) to `content/maps/src/materials.tsx` (`tilecount` attribute goes up by however many WALL/FLOOR forms you add) — then:
   - `tools/src/test/java/com/trojia/tools/tmx/TavernFixtureIntegrationTest.java` — `tilesetMatchesFixtureGidRange()` hardcodes `assertEquals(N, tileset.tileCount())`.
   - This does NOT require touching `MaterialRawsLoaderCommittedTest` again — the tileset count and the material count are two independent hardcoded numbers that happen to move together when a material is both registered AND made paintable.
5. **Run `./gradlew.bat build --console=plain`** (the whole multi-module build, not just `:sim-core:test`) — `tools:test` has its own independent `check-raws`/`check-map` validator (`tools/src/main/java/com/trojia/tools/validate/RawsLoader.java`) that walks `content/raws/**` generically; it has caught real regressions the sim-core suite alone missed.

## Gotcha already hit this session

The `tools` module's generic `RawsLoader` walks every subdirectory under `content/raws/`, not just materials/fluids/treatments/reactions — non-material raws categories (`actors/`, `jobs/`, `skills/`) are deliberately ignored with a warning, not an error. If you introduce a new raws category, it'll warn (harmless) unless it also happens to hit the "no id field" check before the category switch — read the current `RawsLoader.java` to confirm the category-check-before-id-check ordering is still in place (it was a real bug here once).
