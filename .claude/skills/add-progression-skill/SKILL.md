---
name: add-progression-skill
description: Add a new Morrowind-style use-XP skill to the progression engine (content/raws/skills/skills.json + PROGRESSION-SPEC.md), and update every place its count is hardcoded.
---

Adds one skill to `com.trojia.sim.progression` (sim-core, no `Actor` coupling by design — see `package-info.java`).

## Steps

1. **Read `docs/design/PROGRESSION-SPEC.md` §2** (the skill table) and `docs/design/SPEC-INDEX.md` first — every skill needs an Eli ruling or an explicit `(placeholder)` marker plus a canon citation if one exists. Do not invent a skill without checking whether it duplicates or should merge with an existing one (this project has already merged several: Sidearms, Dire Bows, Deftness→Skyrunning, Streetwise).
2. **Add the entry to `content/raws/skills/skills.json`** — this is a single **container file**, not one-file-per-skill (deliberate design choice, same pattern as `content/raws/jobs/jobs.json`). Read `sim-core/src/main/java/com/trojia/sim/progression/SkillRawsLoader.java` for the exact field contract before writing the entry (governing attribute, aptitude tier, use-XP weighting) — don't guess the schema from the spec prose alone.
3. **Update `docs/design/PROGRESSION-SPEC.md` §2's table** — add the row, keep the existing columns (name, description, governing attribute, aptitude, canon citation).
4. **Bump every hardcoded skill count** (this WILL break otherwise — verified by direct experience, not speculation):
   - `sim-core/src/test/java/com/trojia/sim/progression/SkillRawsLoaderCommittedTest.java` — `assertEquals(16, registry.size())` (or whatever the current count is; grep for it, don't trust this number staying current).
   - Check `AttributeWeights.java`'s per-attribute weight sums (§5) still total 128 per attribute if the new skill's weight touches an existing attribute's allocation — `AttributeCalculatorTest` asserts this.
5. **Run the full build, not just sim-core** — `./gradlew.bat build --console=plain`. This project has independent test suites in `sim-core` AND `tools` that both assert raws counts/consistency; a change that only looks green in one module has broken the other more than once this session.

## Gotcha already hit this session

`content/raws/skills/skills.json` and `content/raws/jobs/jobs.json` are intentionally single container files with no top-level `"id"` field (each *entry inside* has its own id). The generic `tools/src/main/java/com/trojia/tools/validate/RawsLoader.java` validator and `sim-core/src/test/java/com/trojia/sim/json/RawsRoundTripTest.java` both had to be taught to treat container/config raws files as a distinct case from the one-raw-per-file convention used by materials/fluids/actors. If you add a THIRD kind of container file, check both of those before assuming it'll just work.
