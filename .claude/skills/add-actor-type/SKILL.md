---
name: add-actor-type
description: Add a new Actor subclass (Streets-of-Rogue-style emergent agent type) to sim-core's actor package, wired into raws, jobs, and the observer's render table.
---

Adds one actor type to `com.trojia.sim.actor` per `docs/design/DECISIONS.md`'s Actors ruling: thin subclasses over a shared `Actor` base, depth-2 hierarchy, all numbers in data files — never hardcode a new type's stats in Java.

## Steps

1. **Read `docs/design/ACTORS-SPEC.md` end to end first** (it's the audited spec — §7.1 has the observer glyph/tint table, §10 covers Goals & Jobs, §11 covers Home/Inventory/Relationships). Confirm the new type doesn't already exist as a role of an existing type (e.g. don't add "Fisherman" if it should just be a `Job` under the existing `Serf` or `Shopkeeper` type — type vs. job is a real distinction in this codebase: **type = what you are, job = why you're here**).
2. **Add the thin subclass** under `sim-core/src/main/java/com/trojia/sim/actor/type/` — look at an existing one (e.g. `Wastrel.java` or `MilitiaWatch.java`) as the template. It should declare composed `BehaviorPolicy` objects and raws-driven stats, nothing else.
3. **Add its raw** under `content/raws/actors/<type>.json` — one file per type (NOT a container file — actors follow the materials/fluids convention, unlike jobs/skills). Check `ActorRawsLoader.java` for the exact field contract.
4. **Wire it into `ActorTypes.java` and `ActorTypeStatsTable.java`** — these are the registries that bind the Java subclass to its raw.
5. **Give it at least one default `Job`** — add a `defaultFor` entry in `content/raws/jobs/jobs.json` pointing at the new type's raw id (see `JobBinder.java` for the fail-fast 1:1 contract this must satisfy).
6. **Add its row to ACTORS-SPEC §7.1's glyph/tint table** — this is what the observer's actor renderer reads to draw it (luminous-on-black art register per `docs/design/DECISIONS.md`'s Art register ruling).
7. **Run `ActorPurityTest`** (`sim-core/src/test/java/com/trojia/sim/actor/ActorPurityTest.java`, ArchUnit) — it enforces the same determinism rules as the rest of sim-core (no float/double state, no HashMap/HashSet state fields) specifically against the actor package.
8. **Run `./gradlew.bat build --console=plain`** — full build, all modules.

## Note on identity/disguise

If this type can plausibly have a secret job (villain-adjacent), give it a `cover()` following the existing `Job.Villain.Robber`/`Job.Villain.Cutpurse`/`Job.Villain.Skyrunner` pattern — `CoverSpec` pairs a `presentedJob` + `actorType` so social systems read the disguise, not the truth (`Persona` trueIdentity/presentedIdentity seam, per `docs/design/DECISIONS.md`'s Identity ruling).
