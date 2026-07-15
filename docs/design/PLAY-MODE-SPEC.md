# PLAY-MODE-SPEC — Play mode: choose your real role, choose your presenting role

Status: MVP slice specced and ready for implementation. Eli's directive (2026-07-14,
verbatim): "Get started on a play mode where you can choose your real role and also your
presenting role." Explicitly a "get started" ask — this spec sizes a minimal, working slice,
not the full Play-mode/combat/Gabri experience.

Grounding read before writing this spec (cited by file/line where it matters below):
`docs/design/DECISIONS.md` (Identity row, Companion row), `docs/design/ACTORS-SPEC.md`
(§1.1 Persona field, §2.6/§4.10 presented-identity social reads, §10.4/§10.5 Job cover vs.
presented display), `docs/design/COMBAT-SPEC.md` (confirmed spec-only, no code — see §5),
`sim-core/.../actor/Persona.java`, `sim-core/.../actor/Actor.java`,
`sim-core/.../actor/ActorsSystem.java` (`wielderId()`), `sim-core/.../actor/PolicyStack.java`,
`HeldPolicy.java`/`ExecutedPolicy.java`/`PolicyId.java` (the sentinel-score override
precedent), every `sim-core/.../actor/type/*.java` thin subclass,
`client-observer/.../ObserverApp.java`, `input/{CameraInput,InspectorInput,TimeControlInput}.java`,
`inspect/{ActorPicker,InspectorState,InspectorText,JobDisplay,EventLogTracker}.java`,
`render/InspectorRenderer.java`, `face/{FaceGen,InspectorFaces}.java`.

---

## 1. What "choosing your real role" means

**At full maturity** (post-MVP, per DECISIONS.md's Companion ruling — "Solo Gabri in the
MVP"): the player's real role is **Gabri specifically**. A PC builder assigns Gabri's
`trueIdentity` once at campaign start; every other actor in the world is NPC-simulated. The
Wielder-immunity/deference systems (§4.10, already implemented — see `DeferWielderPolicy`,
`ActorsSystem.wielderId()`) key off whichever actor holds `Job.FlameOfMerc` and is presenting
as itself; that actor is canonically Gabri.

**Today**, no Gabri actor exists in any fixture (`DocksPopulation`/`CompoundBlockPopulation`
spawn only the NPC roster — Serf/Wastrel/Shopkeeper/Militia Watch/Priest/Disciple/Animal
Keeper/Animal; no `Job.FlameOfMerc` holder is ever bound — confirmed by grep: `FlameOfMerc` is
referenced only in `ActorContext`/`ActorsSystem`/`Job`/`Jobs`, never in a scenario spawner).
Building a dedicated Gabri actor, combat, and the full disguise-consequence loop is separate,
larger work gated on `COMBAT-SPEC.md` (confirmed still spec-only this session — no
`sim-core`/`client-observer` class implements hit resolution, armor, or the combat screen).

**This MVP slice generalizes "choose your real role" to: pick any already-spawned actor in the
currently loaded fixture and drive it directly.** No new actor is spawned, no PC builder, no
Gabri-specific plumbing. "Real role" = **which existing actor's body you are now steering**;
its `trueId` (an immutable fact set at spawn, `Persona.of(id)`) never changes — you are not
reassigning WHO an actor truly is, only taking the wheel of one that already exists. This is a
deliberate, named simplification, not an oversight: full "choose your real role" (build/name a
new Gabri instance) is future work (§4).

## 2. What "presenting role" means and how it interacts with the Persona seam

`Persona(int trueId, int presentedId)` (`Persona.java`) is, today, a genuine but **inert**
seam: `presentedId` defaults to `trueId` at spawn and nothing in `client-observer` reads it at
all (`grep` for `identity()`/`presentedId` under `client-observer/src/main/java` returns zero
matches). In `sim-core` it has exactly one live functional consumer:
`ActorsSystem.wielderId()` (line ~380) returns `Actor.NONE` — no Wielder — unless the
`Job.FlameOfMerc` holder's `presentedId == id()`. That single check is the entire currently-
wired payoff of the seam: **a disguised Wielder (presented != true) stops triggering
`DEFER_WIELDER` in every other actor's stack**, exactly as ACTORS-SPEC.md §4.10/DECISIONS'
Identity row require. Everything else Persona is meant to eventually gate (§2.6 witness
attribution, §4.10 deference beyond the Wielder case, faces, logs) is unwired: nothing else in
the shipped code resolves through `presentedId` yet.

**Both `trueId` and `presentedId` are `ActorId`-shaped ints** (Persona.java's own javadoc,
ACTORS-SPEC.md §2.8's `personaTrue i32 · personaPresented i32` TROJSAV layout) — i.e.
`presentedId` names *another actor* whose identity is being borrowed (the canon precedent
DECISIONS.md gives is exactly this shape: "Bledhreft impersonating Senator Harris" — a
specific individual, not a generic archetype). This is worth stating precisely because it is
easy to conflate with a *different*, already-implemented "presented" mechanism in this
codebase: `JobDisplay.presentedJobId` / `Job.Villain.cover().presentedJob()`, which lets a
`Job.Villain.Cutpurse` *actor-type* literally spawn as a `wastrel` (a **type-level** cover,
derived from the Job, never touching Persona). That mechanism is unrelated to Play mode and is
untouched by this spec. **Play mode's "presenting role" is specifically the Persona seam**:
picking another existing actor (by id) whose identity your played actor now presents as.

**Systems that already exist and would react to a real disguise, and what they'd need
(deferred, not implemented this pass):**

| System | Reads today | Would need to change to react to disguise |
|---|---|---|
| `DeferWielderPolicy` / `ActorsSystem.wielderId()` | `presentedId` (already wired!) | Nothing — already correct: a disguised Wielder already stops triggering deference. This is the one place the seam is "hot" today, just untriggered (no Wielder spawns yet). |
| Justice system / Watch arrest detection (`JobBehaviors.checkArrestExposure`, §2.6 witness scan, `CrimeWitnessed(witnessId, culpritPresentedId, ...)`) | Per ACTORS-SPEC.md §2.6/§5.1's *design*, `CrimeWitnessed` should carry the PRESENTED id — but the currently-shipped `checkArrestExposure`/witness code was not read in full for this pass; whatever it emits today should be audited against presentedId before any disguise-driven crime mechanic ships. **Out of scope here.** |
| Inspector panel (`InspectorText.describe`, `InspectorRenderer`) | `actor.typeId()`/`actor.id()` directly (true identity only) — zero Persona reads | This pass wires it (§3) to also show the presented actor's type, following the exact existing convention `InspectorText` already uses for Job (`"job: X presents: Y"`, always both, no god-view toggle exists in code despite ACTORS-SPEC §10.5 describing one — the shipped inspector is already "god view always"). |
| Face portrait (`InspectorFaces.draw`, `FaceGen.compose`) | `(worldSeed, actor.id(), archetypeForActorType(actor.typeId().key()))` — true id and true type, always | This pass wires it (§3) to resolve the SAME triple from the presented actor when disguised, so the portrait visibly becomes the impersonated actor's face. |
| Event log (`EventLogTracker.tag`) | `actor.id() + " " + actor.typeId().key()` — true id/type, always | **Left unchanged this pass.** This is the observer's own god-view debug feed (distinct from the future in-world `CrimeWitnessed` log ACTORS-SPEC §2.6 describes, which is not wired into this UI at all yet); it should keep showing ground truth. Noted here so a future pass doesn't conflate the two logs. |

## 3. MVP slice vs. future work

### Ships this pass

1. **Role selection** — reuses the existing click-to-select machinery (`InspectorInput`,
   `ActorPicker`, `InspectorState`) verbatim. No new picking UI. Pressing a new keybind
   (`P`) while an actor is selected toggles Play mode on/off *for the currently selected
   actor*. The "played actor" is not a separately-tracked id — it *is*
   `InspectorState.selectedActorId()`; if the user clicks a different actor while Play mode
   is active, the played actor follows the new selection (stated simplification, §4).
2. **Movement** — while Play mode is active, `W`/`A`/`S`/`D` (held, continuous — mirroring
   `CameraInput`'s existing pan-key polling style) drive the played actor's `stepToward`
   instead of panning the camera. Movement is walkability-checked via the same
   `ActorContext.isWalkable`/`Actor.stepToward` path every NPC uses, gated by the actor's own
   `speedTicksPerStep` (a played Serf walks at Serf speed, not player speed) and only actually
   steps on ticks the `SimulationDriver` executes (paused = no movement, matching every other
   actor). Player-driven steps ignore the leash (`ignoresLeash=true`, the same flag
   FLEE/APPREHEND-style overrides already use) — the leash is an AI-wandering-radius concept,
   not a player-cage.
3. **Camera follow** — entering Play mode force-enables the existing follow-camera path
   (`InspectorState`/`ObserverApp.applyFollowCamera`) for the played actor; exiting restores
   free camera. `[`/`]` zoom and Up/Down z-scrub keep working during Play mode (harmless,
   useful); `W`/`A`/`S`/`D` panning is suppressed while Play mode is active (movement owns
   those keys; follow keeps the camera centered anyway).
4. **Presenting role** — a new keybind (`I`) arms "impersonation-pick" mode while Play mode is
   active; the next left-click picks an actor (via the same `ActorPicker.pickAt`) and calls
   the new `Actor.setActAs(otherActorId)` on the played actor. Clicking the played actor's
   own tile reverts to presenting as itself (no extra keybind needed for "drop disguise").
   This is deliberately generalized to "present as any existing spawned actor" rather than "a
   short list of archetypes" (§4 explains why: it is what `presentedId`'s actual, already-
   authored `ActorId`-shaped contract supports with zero new data modeling, and it exactly
   matches DECISIONS.md's own worked example of impersonating a specific named individual).
5. **Visible proof the seam works end to end**: the inspector panel and face portrait for the
   played (or any) actor resolve through `presentedId` when disguised (§5 mechanism), so
   picking a Militia Watch to present as visibly changes the panel's type line and swaps the
   portrait to a Watch face — with zero new social/law reaction (see §4).

### Deliberately deferred (explicitly out of scope this pass)

- A PC builder / new Gabri actor / any spawning of a player-specific character.
- Any social/law system *reacting* to a disguise beyond the one already-wired
  `wielderId()`/`DeferWielderPolicy` check (no Wielder is spawned in any fixture today, so
  this pass cannot even exercise that check with a live demo — it is simply confirmed not to
  regress). No changes to `JobBehaviors.checkArrestExposure`, the witness scan, or
  `CrimeWitnessed` attribution.
- Combat of any kind (`COMBAT-SPEC.md` stays spec-only; nothing here adds a combat screen,
  hit resolution, or damage).
- Persisting Play-mode/disguise state meaningfully across save/load (see §6 — the
  `PLAYER_CONTROLLED` status bit is explicitly cleared on every load/thaw; a fresh app launch
  always starts in spectator mode and the disguise, riding the existing `statusBits`/Persona
  fields, round-trips for free but has no live human reattached until Play mode is
  re-entered).
- A god-view/default-view toggle for the inspector (none exists today for Job either — see
  §2's table; this pass follows that existing "always show both" precedent rather than
  inventing a toggle).
- Multi-actor party control, NPC-to-player handoff mid-goal preservation, or any UX polish
  beyond "it works and is legible."

## 4. Confirmed minimal-slice scope (final decision)

The orchestrator's recommended scope is **confirmed with one revision**: the presented-role
picker is **not** "a short list of existing ActorTypeIds or a face-archetype choice" — it is
"pick another existing spawned actor via the same `ActorPicker` click machinery already used
for role selection." Reasoning:

- `Persona.presentedId` is contractually `ActorId`-shaped, not `ActorTypeId`-shaped, in both
  the current code (`Persona.java`'s own javadoc) and the TROJSAV format ACTORS-SPEC.md §2.8
  already reserves (`personaPresented i32`). Building a type-list picker would mean inventing
  a *new* representation (e.g. "presented type" separate from Persona) with no consumer and
  no spec backing — pure scope creep for an MVP slice whose entire point is "prove the seam
  that already exists."
- Reusing `ActorPicker` a second time (for "who do I present as") costs zero new UI classes
  and mirrors DECISIONS.md's own canon example (impersonating a specific named individual,
  not "a generic guard").
- The only real cost is wiring the inspector/face lookups to resolve through `presentedId`
  (§5) instead of the actor's own id/type — a small, local change, not new machinery.

Everything else in the recommended scope stands as proposed: reuse `ActorPicker`/inspector
click-select for role selection; WASD drives the played actor via `stepToward`; a new
`setActAs()` on `Actor` performs the identity change. This is correctly sized for a "get
started" ask this late in the backlog — it touches `Actor`/`Persona`'s existing seam, adds one
new policy + one new keybind class, and edits ~9 existing files by one or two lines each
(the type stacks) — no new subsystem, no new content-raws file, no save-format version bump.

## 5. Exact mechanism design

### 5.1 Spectator-camera-pan vs. play-mode-controls-an-actor toggle

New keybind: **`P`** (unused today — full audit of `Input.Keys.*` across
`client-observer/src/main/java` shows only `A/D/W/S/Left/Right/Up/Down/[/]` (`CameraInput`),
`SPACE/F/PERIOD` (`TimeControlInput`), `C`/left-click (`InspectorInput`), `ESCAPE`
(`ObserverApp`) are bound — `P` and `I` are both free).

- `P`, `isKeyJustPressed`, only actionable while `InspectorState.hasSelection()` is true
  (no-op otherwise — nothing to play). Toggles a new `PlayModeState.active` flag.
- On activate: calls `actor.setStatus(StatusBit.PLAYER_CONTROLLED, true)` on
  `registry.get(inspector.selectedActorId())`, and forces `InspectorState` follow on (reusing
  `toggleFollow()` if not already following).
- On deactivate (`P` again, or selection cleared by a deselect-click, or a new actor picked
  while active per §3 item 1): clears `StatusBit.PLAYER_CONTROLLED` on whichever actor id was
  previously played, so its normal `BehaviorPolicy` stack resumes immediately (score reverts
  to 0, next tick's `selectIndex` picks the next-highest scorer with no special-casing).
- New class `client-observer/.../input/PlayModeInput.java` (mirrors `InspectorInput`'s
  shape): owns the `P`/`I` keybinds and, while active, the WASD-to-movement poll. `ObserverApp`
  gates `CameraInput`'s pan (not its zoom/z-scrub) off while `PlayModeState.active` is true —
  simplest correct implementation: change `CameraInput.poll`'s signature to accept a
  `boolean panEnabled` (the only production call site is `ObserverApp`; `CameraInputTest`
  exercises the pure `panDelta` helper directly and is unaffected).

### 5.2 WASD movement vs. the actor's own BehaviorPolicy stack — override design

Directly follows the `HeldPolicy`/`ExecutedPolicy` sentinel-score precedent
(`PolicyStack.selectIndex` — max score wins, ties broken by stack position):

- New `PolicyId.PLAYER_CONTROL` (appended to the enum, append-only per its own doc rule).
- New `PlayerControlPolicy implements BehaviorPolicy`:
  - `score(self, ctx)`: `self.hasStatus(StatusBit.PLAYER_CONTROLLED) ? PLAYER_CONTROL_SCORE : 0`.
  - `PLAYER_CONTROL_SCORE = 2000` — above every normal AI band (`RETURN_HOME`'s observed
    ~1305 ceiling, comfortably) so a played actor's own AI (SEEK_FOOD/RETURN_HOME/
    GOAL_PURSUE/DEFER_WIELDER/LOITER) never fights the player — but **below `HeldPolicy`'s
    5000 and `ExecutedPolicy`'s 6000**, deliberately: a played actor who gets arrested still
    gets held (you cannot just walk out of custody by holding a key), and a played Skyrunner
    hanged on a second offense stays permanently inert like anyone else. This is the intended
    interaction, not a bug: player input augments the actor's agency, it does not grant
    immunity from the justice system already built this session.
  - `act(self, ctx)`: reads a new plain-scalar field `Actor.playerMoveTargetCell` (the same
    "plain scalar, `goalProgress`/`heldUntilTick` precedent, not a side-table" pattern the
    arrest state already uses); if not `Actor.NONE`, calls
    `self.stepToward(target, true, ctx::isWalkable)` (leash-ignoring, walkability-checked —
    identical call shape to `HeldPolicy.act`'s escort step), then **consumes** the intent
    (`self.setPlayerMoveTarget(Actor.NONE)`) so a stale target never re-fires after the driver
    pauses; sets `lastReasonCode(ReasonCode.PLAYER_CONTROLLED)` (new, append-only enum entry)
    so the inspector's existing "reason:" line and the event log both surface Play-mode
    control legibly, for free, via machinery that already exists.
- `PlayModeInput.poll` (client-observer), once per frame while active: computes `(dx, dy)`
  from held `W/A/S/D` exactly like `CameraInput.panDelta` (reusing that same signed-delta
  shape, {-1,0,1} per axis, diagonals allowed — `stepToward`'s existing wall-slide logic
  already handles a blocked diagonal), and if `(dx,dy) != (0,0)`, computes
  `target = PackedPos.pack(x+dx, y+dy, z)` from the played actor's live cell and calls
  `actor.setPlayerMoveTarget(target)`. Recomputed every frame the key is held — harmless,
  since `stepToward` only actually commits a step on ticks the driver executes and only after
  its own speed-accumulator gate, so holding a key doesn't move faster than the actor's own
  `speedTicksPerStep` allows.
- **New type-stack wiring**: `Policies.PLAYER_CONTROL` is added to the front of every
  non-beast type's static `PolicyStack.of(...)` (`Serf`, `MilitiaWatch`, `Shopkeeper`,
  `PriestOfTheFlame`, `DiscipleOfTheFlame`, `AnimalKeeper` — before `DEFER_WIELDER`; `Wastrel`
  — after its existing `EXECUTED, HELD` prefix, before `DEFER_WIELDER`). **`AnimalActor` and
  `FeralActor` are excluded** — they have no `DEFER_WIELDER` row by design (no deference
  canon for beasts) and no face archetype (`InspectorFaces.hasFaceFor` already returns false
  for them); scoping Play mode to the human-ish roster only for this pass is a deliberate,
  stated cut (playing an Animal is a fun future idea, not needed to satisfy "get started").
  This is a one-or-two-line, purely-additive edit per file — the exact shape `HELD`/`EXECUTED`
  already established for `Wastrel` — and satisfies `ActorPurityTest`'s thin-subclass rule
  unchanged (a new element in an existing static `PolicyStack.of(...)` call is not a new
  field or method).

### 5.3 `setActAs()` — Persona mechanics and what re-reads `presentedId`

New method on `Actor` (the DECISIONS.md-mandated verb name, not a bare alias for the existing
generic `setIdentity(Persona)`, so the intent is named at the call site):

```java
/**
 * Play-mode disguise verb (DECISIONS.md Identity row): presents as {@code otherActorId} — an
 * existing ActorId in the registry — instead of this actor's true identity. Pass this
 * actor's own {@link #id()} to drop the disguise. A plain field rewrite (no validation that
 * {@code otherActorId} resolves to a live actor — the caller, Play mode's impersonation
 * picker, only ever passes ids resolved via the same {@code ActorPicker} the click-to-select
 * panel already trusts).
 */
public final void setActAs(int otherActorId) {
    this.identity = new Persona(identity.trueId(), otherActorId);
}
```

**Callers of `presentedId` this pass wires** (the "prove the seam" requirement):

- `InspectorText.describe`: after the existing `"type:   " + actor.typeId().key()` line, adds
  `"presents: " + (disguised ? presentedActor.typeId().key() : "(self)")` — following the
  exact existing convention the Job line already uses (`"job: X presents: Y"`, always shown,
  no toggle). Requires threading `ActorRegistry` (already a parameter) to resolve
  `registry.get(actor.identity().presentedId())` when `actor.identity().isDisguised()`.
- `InspectorRenderer.drawPanel`: currently computes `typeKey` from
  `registry.get(selectedActorId()).typeId().key()` for the face lookup
  (`faces.hasFaceFor(typeKey)`, `faces.draw(..., selectedActorId(), typeKey, ...)`). Changes
  to resolve `typeKey` **and the id passed to `faces.draw`** from the presented actor when
  disguised: `Actor presented = actor.identity().isDisguised() ? registry.get(actor.identity().presentedId()) : actor;` then use `presented.typeId().key()` and `presented.id()` as the
  `(actorId, archetypeId)` pair `FaceGen.compose` seeds from. This makes the portrait
  literally become the impersonated actor's deterministic face (same seed formula,
  `FaceGen.java`'s existing `(worldSeed, actorId, archetype)` contract, untouched) — exactly
  the Bledhreft/Senator-Harris canon example, made visible.
- `ActorsSystem.wielderId()` is **already correct** and untouched — confirmed by this session's
  reading, not modified.
- `EventLogTracker.tag` is **deliberately left untouched** (§2's table) — the observer's
  god-view debug log, distinct from the future in-world witness log.

## 6. Determinism, save/load, and ArchUnit

- `Actor.playerMoveTargetCell` (new plain `int` field, `NONE` default) and
  `StatusBit.PLAYER_CONTROLLED` (new bit, `1 << 9`, still fits `short`) are both plain
  primitive scalars — satisfies `ActorPurityTest`'s `NO_FLOATING_POINT_FIELDS`/
  `NO_HASH_CONTAINER_FIELDS` rules trivially (same shape as every existing field).
- `StatusBit.PLAYER_CONTROLLED` rides the **existing** `statusBits` short field, which already
  round-trips through TROJSAV (`ActorsSystem`'s existing serialize/deserialize of
  `statusBits` — no format change, no `formatVersion` bump, unlike the HELD/EXECUTED
  addendum which needed one for its own new fields).
- **New thaw-repair rule** (the same shape as the existing "invalid goal target at thaw →
  `goalState` SELECTING" repair, ACTORS-SPEC §2.9.3): on every load/thaw,
  `StatusBit.PLAYER_CONTROLLED` is unconditionally cleared for every actor, and
  `playerMoveTargetCell` is reset to `NONE`. Rationale: a fresh app launch never has a live
  human reattached to a specific actor; without this repair, an actor saved mid-Play-mode
  would permanently win `PolicyStack.selectIndex` (score 2000 > 0) with `act()` doing nothing
  useful (no target ever arrives again) — frozen forever, a real correctness bug. The repair
  makes "you must re-enter Play mode after a load" the deliberate, correct UX rather than an
  accident.
- `playerMoveTargetCell` is **not** added to the TROJSAV `ACTR` record — deliberately: it is
  per-frame input intent (same category as an in-flight, unconsumed mouse click), not
  simulation state; dropping it at a save boundary is correct, not lossy.
- Movement determinism: player input is an **external command stream**, the same category as
  the world seed or (eventually) a recorded input log for replay — the sim consumes it through
  the exact same `stepToward`/`isWalkable`/leash/speed-accumulator path every NPC's movement
  already goes through (§5.2), so nothing about *how* a step resolves becomes non-deterministic
  or actor-specific; only *when* a step is requested (an external, human-timed event) is new,
  exactly as it would be for any lockstep/command-driven simulation. No new RNG stream, no new
  randomness anywhere (`setActAs`/`PlayerControlPolicy` are both draw-free).
- ArchUnit: `ACTOR_SUBCLASSES_ARE_THIN` and `ACTOR_HIERARCHY_IS_DEPTH_TWO` are unaffected —
  every type-stack edit adds an element to an existing static field initializer, not a new
  field or method; `setActAs()`/the new policy/the new status bit all live on `Actor`/in the
  `com.trojia.sim.actor` package itself (never in a `type/*` subclass), matching every prior
  addendum's shape (`heldUntilTick`, `offenseCount`, `HeldPolicy`, `ExecutedPolicy`).

## 7. Existing tests/behavior at risk (enumerated)

- **`CameraInputTest`** — tests only the pure `panDelta(left,right,up,down)` helper, not
  `poll(...)`. Changing `poll`'s signature to add a `panEnabled` boolean does not touch
  `panDelta` at all — **no test change needed**, but confirm no other call site of
  `CameraInput.poll` exists besides `ObserverApp.render()` (confirmed: it's the only one).
- **`HeldDominatesWholeStackTest`** (`heldWinsSelectionOverEveryNeedAndJobBandPolicy`,
  `executedWinsSelectionOverHeldItself`) — both build a `Wastrel` via `Wastrel.STACK`
  directly (through `registry.spawn(Wastrel.TYPE, ...)`), which will now include
  `PLAYER_CONTROL` in the stack. Neither test sets `StatusBit.PLAYER_CONTROLLED`, so
  `PlayerControlPolicy.score()` returns 0 in both — **behavior unchanged, no test edit
  needed**, but worth a new adversarial-parity test (not required this pass, flagged for the
  implementer) proving `HELD`/`EXECUTED` still dominate even when `PLAYER_CONTROLLED` is also
  set, mirroring this test class's own existing pattern.
- **`ActorPurityTest`** (`ACTOR_SUBCLASSES_ARE_THIN`, `ACTOR_HIERARCHY_IS_DEPTH_TWO`,
  `NO_FLOATING_POINT_FIELDS`, `NO_HASH_CONTAINER_FIELDS`) — must still pass after the new
  field/policy/enum entries; reasoned through in §6, no rule needs relaxing.
  `ArchitecturePurityTest` (project-wide twin) likewise unaffected — no new third-party
  dependency, no float/double, no `Map`/`Set`/`HashMap`/`HashSet` field.
- **`ActorPickerTest`** — exercises `ActorPicker.pickAt` in isolation; reused as-is for the
  impersonation picker with zero changes (it is called with the exact same signature).
- **A14 `actrSection_roundTrip_allFields`** (ACTORS-SPEC test list) — round-trips every `ACTR`
  field byte-identical; since `PLAYER_CONTROLLED` rides the existing `statusBits` short and no
  new field is added to the record, this test needs no changes **provided no test fixture
  accidentally leaves `PLAYER_CONTROLLED` set** — safe by construction since nothing sets that
  bit except the new, unexercised-by-existing-tests code path.
- **A18 `deferenceTable_immutable_presentedIdentity`** — already covers "presented ≠ Wielder
  (disguise seam) → APPREHEND eligible; clamp floors hold." `setActAs()` writes the exact same
  `Persona` shape this test already exercises (via whatever helper it uses to construct a
  disguised Persona) — **should continue passing unchanged**; worth re-running explicitly
  since this pass is the first code to call a method literally named `setActAs`.
- **`JobBinderTest`**, **`ActorRegistryTest`**, **`HomeInventoryRelationshipTest`** — none
  assert exact `PolicyStack` contents or size; unaffected by the stack edits.
- **`InspectorTextTest`** (confirmed exists, `client-observer/src/test/java/com/trojia/client/
  inspect/InspectorTextTest.java`) — asserts via substring `contains(...)` on the joined panel
  text (e.g. `text.contains("serf.laborer")`, `assertFalse(text.contains("(secret)"))`), never
  an exact line count — adding a new "presents:" line is low-risk for this file specifically
  (no test should break by construction), but re-run it explicitly since `describe(...)`'s
  signature/behavior is changing. `InspectorStateTest` (selection/follow state only) is
  unaffected — it doesn't touch `describe()`.
- **No test hard-codes `PolicyId` enum ordinal count or exact `Wastrel.STACK`/other stacks'
  `.size()`** (confirmed by grep) — appending `PLAYER_CONTROL` is safe.

## 8. Open design questions left for the Author phase

None. Every mechanism above (keybinds, sentinel score, field shapes, save/thaw repair,
inspector/face wiring, excluded actor types) is a final decision, not a placeholder. The one
explicitly-deferred ambiguity in the wider system (exactly how `JobBehaviors.checkArrestExposure`
/ the witness scan should resolve `presentedId` once disguise has real consequences) is
correctly out of scope for this pass and is called out as such in §2's table, not left dangling
as something this pass half-implements.

## 9. Implementation notes (what actually shipped)

Recorded after implementation, for future readers reconciling this spec against the code:

- Shipped exactly as specced in §5, with one naming addition: `PlayModeState` (new,
  `client-observer/.../inspect/PlayModeState.java`) tracks `active`/`playedActorId`/
  `impersonatePickArmed` explicitly (rather than reading `InspectorState.selectedActorId()`
  live every frame) so `PlayModeInput` can detect the one frame the ordinary click-to-select
  path reassigns the selection out from under Play mode (§3 item 1) and transfer
  `StatusBit.PLAYER_CONTROLLED` to the newly selected actor instead of leaving the old one
  permanently pinned.
- `PlayModeInput.poll` runs *before* `InspectorInput.poll` each frame and returns whether it
  consumed the frame's left click (impersonation-pick fired); `ObserverApp` skips the
  `InspectorInput.poll` call for that frame when it did, so a click meant to choose a disguise
  target never also reselects the played actor.
- `PlayModeInput.applyMovement(PlayModeState, ActorRegistry, dx, dy)` is factored out as a
  public, GL-free entry point separate from the `Gdx.input` read, so both the real WASD poll
  and a debug/verification caller can drive the exact same code path — mirroring the existing
  `--debug-select` "bypass the input device, exercise the same code" convention.
  `ObserverApp`/`ObserverLauncher` gained matching `--debug-play-mode`, `--debug-move=dx,dy`,
  `--debug-act-as=id` flags for headless/screenshot verification (§ below), all off by default.
- Movement is genuinely walkability-gated, as designed: in the docks fixture proof run, a
  played Serf held one direction for 40 ticks (20 possible steps at `speedTicksPerStep=2`)
  moved only as far as the nearest wall before `stepToward`'s deterministic no-op took over —
  expected behavior, not a bug (§5.2's wall-slide/leash-ignoring path is exercised, not
  bypassed).
