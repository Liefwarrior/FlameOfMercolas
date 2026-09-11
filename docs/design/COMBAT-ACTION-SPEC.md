# COMBAT-ACTION-SPEC — Oblivion-inspired real-time action combat (v1)

**Status:** APPROVED and BUILDING. Eli's veto pass (2026-09-02) is folded in below; this
document is the build contract, spec-plus-veto assembled into one authority. Where an earlier
draft and the veto pass disagreed, the veto wins and the text here is already the veto's.

**Ruling (Eli, 2026-09-02, verbatim, binding):** *"combat, entirely scrap the JRPG vibe of it
and replace it with a combat system heavily inspired by Oblivion, where you have a dedicated
button for your spells and a swing/block/hold to swing hard action combat for now."* Recorded
at DECISIONS.md:63. "For now" is in the ruling — §10 names the ceiling this v1 deliberately
stops short of.

**Supersedes:** the Decision-2b paused-combat-screen register wherever the two conflict
(COMBAT-SCREEN-SPEC.md's screen half, DECISIONS.md:15's flow). The brawl line (DECISIONS.md:37)
**survives as the legality layer**; COMBAT-SPEC.md §1–§6 resolution math survives **as the v2
ceiling** (§10), not as v1 scope.

**The four binding veto rulings (2026-09-02), which OVERRODE the first draft and are already
folded into the sections below:**

1. **Targeting is a SIGHTLINE RAYCAST, not a cone (§2.1).** The player hits the first body on
   the look-ray down the crosshair — a sightline, not a 120° arc. A bystander eats a blow only
   when the player was looking straight at them. The draft's cone is DEAD.
2. **Everyone can die, including case-critical notables (§4.4).** No plot armor, no classifier
   shield. A case whose party is dead parks with an honest casebook line.
3. **Intent-by-verb (§4.2).** No intent menu, no toggle. Default Subdue; the player's first hard
   swing in a fight sets Harm; hard-swinging a bloodied man trips B3 and goes Lethal bare-handed.
4. **The Daggerfall justice system is a SEPARATE build AFTER combat (§10, and its own charter).**
   Combat v1 lands only the crime + heat + arrest + `Sentence::Condemned` hook. No court, no
   trial, no execution here.

**Laws this spec is written under:** integer math in sim, no floats in state; twin-run
determinism over the 60-steps-per-second spine (one sim step per rendered frame, engine tick
once per 60 steps); one actor per cell, A*, S7 corridor predicate; same-roll discipline (no new
draw is ever added — bands are carved off the roll the caller owns; the charge tier is a
PARAMETER not a draw, and the raycast is DRAW-FREE); draw-free queries (S9: no rolls in seeing,
thieving, or targeting); the never-remove actor convention; terminal register + the four UI
conventions (DECISIONS.md:61); device-aware prompts; NO new art, NO font changes, NO map/content
edits. **The population baseline `0x2646C1AAA2BA38DF` must not move** (no WardActor edits
anywhere); the tavern/gate-workload baseline moves exactly once, declared at lane-SIM's landing.

---

## 0. The model in one paragraph

Combat happens **in the world, in real time, in first person, inside the tick stream** — no
pause, no screen, no turns, no menus. One button **swings** what the hand holds (tap) or **swings
hard** (hold ≥ 250 ms, release); one button **holds a guard**; one dedicated button **casts the
equipped crafting**. A swing hits **the first body on the sightline down the crosshair** — where
you look is who you hit, and friendly fire is your aim's fault. All resolution runs through the
shipped `strike()` one-roll discipline, the shipped wind pool, and the shipped MGT reader. The
brawl classifier keeps ruling what the fight *is* — fists-and-Subdue stays a brawl with all its
floors; steel out, Kill meant, or a bloodied man beaten becomes **Lethal**: same presentation,
same room, but blows can now kill, the Watch has a cause, and the player's own death routes
through the respawn/nemesis-promotion ruling. The 2026-08-04 refusal line "BLADE OUT - THIS IS
NOT A BRAWL." is deleted and the resolution it refused is built in its place.

---

## 1. The verbs and their state machine

### 1.1 Input contract

No new core button. Hold-to-swing-hard **consolidates into Attack**, per the Action enum's own
law (verb #14 must consolidate — controls.hpp). The shipped bindings stand:

| Verb | Keyboard/mouse | Pad | Edge semantics |
|---|---|---|---|
| Attack (swing / hard swing) | MouseLeft | PadWest (X) | **moves from down-edge to release-edge**; down starts the hold clock |
| Block | MouseRight | PadLeftTrigger (LT) | held state, no latch (shipped) |
| Cast | C | PadRightTrigger (RT) | press-edge, quick-casts the equipped crafting (shipped) |

Pad triggers are already synthesized into pressed/released edges at the deadzone crossing, so
held LT and tapped/held X need nothing new. Hold measurement copies the QuickWheel template
exactly (record `stepClock` at press, compute duration at release): **charge is measured in
integer sim steps, so charge tiers are deterministic by construction.** A page-consumed press
must not fire a phantom swing on release — the release-edge is guarded by an "attack armed"
tracker set only in the world `pressed()` path (the QuickWheel self-guard shape).
`static_assert(kHardSwingHoldSteps == render::HoldToggle::kTapSteps)` at file scope in main.cpp
ties the hold boundary to the one tap/hold number the whole game uses.

### 1.2 The player combat state machine (sim-owned, stepped per movement step)

A new per-step machine in the Tavern (`Tavern::stepPlayerCombat()`), advanced once per movement
step from the same loop that drives `stepMovement`. All fields integer, all hashed.

```
IDLE ──Attack down──> CHARGING ──release < kHardSwingHoldSteps──> SWING resolves ──> RECOVERY(kSwingRecoverySteps)
                        │
                        └──release ≥ kHardSwingHoldSteps──> HARD SWING resolves ──> RECOVERY(kHardSwingRecoverySteps)
RECOVERY ──timer 0──> IDLE
```

**Named constants (all at the 60-steps-per-second spine):**

| Constant | Value | Meaning |
|---|---:|---|
| `kHardSwingHoldSteps` | **15** | 250 ms. Below = swing, at/above = hard swing. Equal by design to `render::HoldToggle::kTapSteps = 15` so every tap/hold boundary in the game is one number; a client-side `static_assert` ties them. |
| `kSwingRecoverySteps` | **36** | 600 ms lockout after a swing. Attack edges during RECOVERY are dropped, not buffered. |
| `kHardSwingRecoverySteps` | **54** | 900 ms lockout after a hard swing. Minimum hard cycle = 15 hold + 54 recovery = **69 steps = 1.15 s**, pinned to Barony's measured swing-to-swing cycle. |
| `kSwingChargeQ8` / `kHardSwingChargeQ8` | **256 / 512** | Damage scale passed to `strike()` (§2.2): a hard swing doubles the rolled weapon damage. |
| `kPunchFatiguePoints` | 4 (shipped) | Wind cost of a swing, paid on release. |
| `kHardSwingFatiguePoints` | **10** | Wind cost of a hard swing, paid on release. 2.5× a swing; a full base pool (160) carries 16 hard swings. |

**Rules of the machine, stated as laws:**
- The swing **resolves on the release step**, instantly — no windup delay on a tap (we have no
  viewmodel to animate; recovery, not windup, carries the cadence).
- **Holding is free and unbounded**; the cost lands on release. Charge past 15 steps changes
  nothing further — v1 has exactly two tiers (the ruling names two: swing, swing hard).
- If Attack goes down during RECOVERY, the edge is dropped — no queue-buffered instant hards.
- **Winded refuses the hard swing** ("TOO WINDED TO SWING HARD." — edge-latched once per stretch).
  A winded tap still swings; the fatigue whiff band is already its penalty.
- CHARGING is cancelled (no cost, no swing) by a Cast press, any page opening, talking, picking.
  The hand does one thing.
- Every state/timer field is sim state, hashed beside `playerHp_`.

### 1.3 Block — the held guard, Oblivion-priced

The shipped guard survives verbatim as mechanism: held state derived every step, never a latch;
`blockedDamage()` argues what a landed blow is *worth* (60% kept at shieldwall 0 down to the 20%
floor, min 1), never whether it lands; every softened blow trains `shieldwall`. Two additions:

1. **A caught blow costs the blocker wind:** `kBlockCatchFatiguePoints = 2` drained per softened
   blow. Turtling under a rain of blows empties the pool that powers the counterattack. Holding
   the guard itself stays free in v1.
2. No timing window, no parry — v1 keeps the guard a held state (a latched or timed guard is
   named ceiling, §10).

Bare arms block: `shieldwall` covers it, no weapon requirement.

### 1.4 The spell button

`Action::Cast` **is** the ruling's "dedicated button for your spells", already shipped: press →
`playerCastEquipped()` — refusals first and out loud, wind paid before the roll, WIT on the check
and cooldown, harmful touch classified as assault *before* the harm lands. **v1 changes nothing
about the cast pipeline** except the two lethal-path consequences in §4: touch-cast targeting
adopts the same sightline raycast as the swing (one targeting rule, two verbs), and the vitality
floor is lifted under Lethal class exactly as the fist floor is. No hard-cast in v1.

### 1.5 NPC cadence — the last JRPG residue, retired

NPC retaliation leaves the 1 Hz `advanceSecond` exchange and moves to per-step timers in a new
`Tavern::stepBrawl()`:

| Constant | Value | Meaning |
|---|---:|---|
| `kNpcSwingIntervalSteps` | **66** | 1.1 s between a standing brawler's swings. |
| `kNpcSwingStaggerSteps` | **30** | Initial timer offset at fight-join = `actorId % 30`, so a crowd never metronomes. |
| `kNpcSwingRetrySteps` | **12** | Timer re-arm when the timer expires out of reach (0.2 s re-check while closing). |

Timer decrements every step the actor is standing; on expiry within `kMeleeReach` the blow
resolves and the timer resets to 66; out of reach it resets to 12 and the actor keeps closing via
the existing `setDestination` → A* path. The NPC swing draw is re-keyed to a **per-actor
`npcSwingSeq_`** (replacing the per-tick shared `drawIndex` — per-actor streams are
order-independent and survive the cadence change cleanly). The 1 Hz tick keeps what belongs at
1 Hz: re-classification, disengage checks, bouncer warnings, Watch behavior.

**Hash honesty:** moving NPC blows out of the 1 Hz loop changes the tavern draw stream and adds
hashed fields; this **moves the twin-gate workload baseline once, declared, at lane-SIM's
landing**. The **population baseline `0x2646C1AAA2BA38DF` must not move** — nothing in v1 touches
`WardActor`; the tavern roster is outside the population baseline by construction.

---

## 2. Hit resolution

### 2.1 Target selection: the SIGHTLINE RAYCAST — the first body on the look-ray (VETO 1)

**The draft's 120° swing cone is DEAD and is not built.** Eli's veto: the player hits the **first
body on the look-ray** — a sightline down the crosshair, not a wide arc. A bystander only eats a
blow when the player was looking straight at them.

The player's yaw is sim state (pushed from the body each step, hashed beside position);
`angle.hpp` owns `forward_x_q16 / forward_y_q16`. The projection is **draw-free integer** (the S9
no-rolls law applied to targeting; no atan2, no sqrt):

```
dx, dy   = target - player            (Q8)
fx, fy   = forward_x_q16(yaw), forward_y_q16(yaw)   (Q16, angle.hpp)
along    = (fx*dx + fy*dy) >> 16       (int64 → forward distance down the ray)
perp     = (-fy*dx + fx*dy) >> 16      (signed offset from the ray)
on-line  ⇔  0 < along ≤ kMeleeReach  AND  |perp| ≤ kBodyHalfWidth
target   = the on-line body with the SMALLEST `along`  (first body the crosshair passes through)
```

- `kBodyHalfWidth` (**128** Q8 = half a cell) is the one tuning knob, and it is a **DISTANCE,
  not an angle** — the beam is the same width point-blank and at reach. That is what "you were
  looking right at them" means.
- **The rat-vs-person species preference is RETIRED for player swings.** The crosshair picks the
  body, not a species rule: a rat on your line and the man off it means you hit the rat. (NPC
  swings keep radial-nearest against the player — they face him already; a raycast for NPCs buys
  nothing in v1.)
- **Friendly fire is real and it's your aim's fault** (veto 1): the first body on the line catches
  the blow whoever they are, joins the fight, and `Deed::Struck` spreads.
- Touch-cast targeting adopts the same sightline (one targeting rule, two verbs).

### 2.2 The blow: `strike()` extended by one defaulted parameter

The entire resolver survives (brawl.cpp): one caller-owned roll; 1-in-8 whiff on bits 0-2;
fatigue's second whiff band on bits 32-37; variance `(roll>>3)%3` = base..base+2; the Evictor's
head band 24/256 on bits 8-15 (`crowned`); MGT bonus `(MGT-40)/15` floored so a blow is a blow.
The extension is one **trailing defaulted parameter**:

```
strike(weapon, target, roll, damageBonus = 0, fatigueTermQ8 = kFatigueTermFullQ8, chargeQ8 = kSwingChargeQ8)
damage = max(1, ((base + variance) * chargeQ8 >> 8) + damageBonus)
```

At `chargeQ8 = 256` the function is **bit-identical** to what it replaced (test-swept, the
`test_fatigue.cpp` equivalence pattern). A hard swing passes 512, doubling the rolled weapon
damage before the bonus:

| Weapon | Swing (base..+2) | Hard swing | vs `kActorHealth` 24 |
|---|---|---|---|
| Fists (3) | 3–5 (+MGT) | 6–10 (+MGT) | 5–8 taps, or 3 hard blows |
| Improvised (5) | 5–7 | 10–14 | |
| Blunt / Evictor (7) | 7–9 | 14–18 | 3 taps, or 2 hard blows |
| Edged (11) | 11–13 | 22–26 | 2 taps, or 1 hard blow — steel is a decision |

The whiff carving is identical at both tiers (a hard swing is not a truer swing). The Evictor's
crowned band rides the same roll at both tiers and **always downs, never kills** (§4.4) — even
under Lethal rules a crowned blow puts a man OUT, not open.

### 2.3 RNG streams — nothing new draws

- Player swings and casts: `drawForPlayerAction()`. Unchanged; the charge tier is a **parameter,
  not a draw**; the raycast is **draw-free**.
- NPC swings: re-keyed to `rng_.draw(actorId, npcSwingSeq_)` with a per-actor monotonic sequence
  (replacing the per-tick shared `drawIndex`). Declared part of the same one-time baseline move
  as §1.5.

### 2.4 Unchanged, by name

`baseDamage` table, `kStrikeVarianceMax`, `weaponSheetLine`, `kMeleeReach = 320`,
`kShoveImpulse = 96`, `blockedDamage` 60→20 min 1, bloodied = exact quarter, downed = hp ≤ 0,
downed-recovery +1 hp/s stand-at-quarter (Brawl class), vermin stay down, `meleeDamageBonus`,
`grantPlayerWeapon`/`kEvictorWeaponId`, fatigue pool sizing/regen/winded hysteresis, cast
check/cooldown/WIT scaling. **No hit-skill, no armor, no location table in v1** (§10 ceiling).

---

## 3. Defense and stamina — the wind ledger

One pool, everything physical reads it (`2*VIG + MGT + AGI` points, Q8 fine units):

| Act | Cost | Where |
|---|---:|---|
| Swing | 4 pts | shipped `kPunchFatiguePoints` |
| Hard swing | **10 pts** | new `kHardSwingFatiguePoints` |
| Caught blow while guarding | **2 pts** | new `kBlockCatchFatiguePoints` |
| Cast | 5 pts | shipped `kCastFatiguePoints` |
| Holding the guard, holding a charge | 0 | v1 ruling: states are free, acts cost |

Winded penalties: sprint refused; **hard swing refused** (§1.2); the fatigue term already widens
the whiff toward ~34% and scales the cast chance down toward ×0.6. No collapse.

---

## 4. The legality layer — the classifier survives; the screen it fed is gone

### 4.1 How `classifyFight` composes now

`classifyFight`/B1/B2/B3, the `Weapon` ordering with `kFirstLethalWeapon`, `Intent`, bloodied —
all survive **verbatim**. What changes is what the answer *means*:

- **`FightClass::Lethal` stops meaning "route to the screen" and starts meaning "lethal RULES":
  blows can kill, the Watch has a cause, and the fight keeps resolving right here.**
  `resolvesInWorld()` is retired **as a venue** (every fight resolves in world now); its meaning
  becomes "resolved with brawl/non-lethal rules", and call sites read `fight == FightClass::Lethal`
  to select the rules, not the venue. The function itself is retained because live callers read
  it; only its meaning changes.
- **Live read, latched consequences:** the class is computed before every player blow and harmful
  cast and before every NPC blow, plus the 1 Hz sweep. `escalation_`/`noteEscalation` survive as
  the **once-per-fight social latch** — `Deed::DrewSteel`, witness spread, `Offence::Brawled`, the
  house ladder fire once at the flip and are not un-rung by a dropped knife; resolution always
  follows the live class.
- The flip speaks once, edge-latched: **"STEEL OUT. THE ROOM STANDS BACK."** replacing both
  refusal strings.

### 4.2 Player intent without a menu — INTENT-BY-VERB (VETO 3)

No intent UI is added. The verbs carry it:

- Default `Intent::Subdue` — a tap-swing fist fight is a bar fight forever.
- The player's **first hard swing in a fight sets `Intent::Harm`** (reset to Subdue when the fight
  ends). Hold-to-swing-hard *is* meaning it — so hard-swinging a **bloodied** man trips B3 and the
  fight goes Lethal with never a blade out. Beating a man to death with fists is possible, is
  Lethal, and is murder.
- B1 and B2 continue to cover steel and NPC Kill intent (the nemesis ladder — Fists/Subdue → Harm
  at 1 win → Blunt at 2 → Edged+Kill at 3 — is unchanged and now cashes out as a real fight
  instead of a refusal).

### 4.3 Two rule sets, one presentation

| | Brawl class | Lethal class |
|---|---|---|
| NPC at 0 hp | Downed; +1 hp/s; stands at quarter (shipped) | **Dead** (§4.4) — except a `crowned` Evictor blow, which downs |
| Player floor | `kPlayerBrawlFloor = 1`; `applyDefeat` → nemesis promotion, coin, quay revive (shipped) | **floor removed**; 0 hp → death (§4.5) routed through the same `applyDefeat` |
| Spell vitality floor | 1 (shipped `kVitalityFloor`) | **removed** — a killing link kills |
| The Watch | not their business (house handles brawls) | **their business** (§4.4 murder law) |

### 4.4 Death — everyone can die, costed honestly (VETO 2)

Nothing in the build dies today (floors everywhere). v1 builds the smallest real death that
honors every standing convention:

- **A corpse is a Downed that never stands.** New actor terminal state `Activity::Dead`:
  `advanceSecond` never heals it, schedules never resume, `nearestTo` skips it, conversation
  refuses it. **Never removed from the roster** (the never-remove convention; the tavern roster is
  outside the population baseline by construction). Presentation is the Downed presentation
  generalized — no new art exists and none is needed.
- **Scope:** roster actors (staff, patrons, notables, vermin). **Only the player kills in v1**
  (NPC-vs-NPC lethality is ceiling, §10). Any roster actor can die, **including case-critical
  notables** (VETO 2): no plot armor, no invisible classifier shield. An authored line whose party
  is dead parks with an honest casebook line ("dead men close no cases").
- **Murder law:** a kill under the three-clause witness rule (range + same floor + line of sight,
  and the S9 notice rule beside them) records `Deed::Slew` with witness spread and raises
  **`kMurderHeat = 60`** — instant paper, warrant threshold met in one act; unwitnessed, it raises
  nothing (heat is what the Watch *heard*). Routed as its own path beside `noteCrime`'s six (murder
  is not guild work and moves no roof-mirror standing in v1). Arrest of a murderer lands
  `Sentence::Condemned` — the exact hook watch.hpp wrote for the day a death existed.
- **Deference is canon and absolute:** the Watch never goes hostile to a **presented Wielder** — no
  arrest-close, no joining a fight against him. Reads presented identity via the Persona seam. Not
  reachable in current play state (the player does not present as Wielder), so it lands as sim
  rule + test, honestly.
- **Loot/bodies:** v1 ships the corpse and nothing on it (no item entities exist to drop). Ceiling.

### 4.5 Player death — the promotion event, with ceremony

At 0 hp under Lethal rules: **the respawn/nemesis ruling executes** through the shipped machinery
— `nemesis_.recordDefeat` + rise-world (rung, house, toll, ground: permanent), coin taken,
`armRivals`, quay-apron revive via the `settleDefeat` path. This is the same `applyDefeat` a brawl
KO routes through; under Lethal the only differences are that the player floor is lifted (0 hp is
reachable) and the **ceremony** is shown:

- The travel black-dip seam generalized: dip, then a `drawTextPlate` epitaph in the terminal
  register naming killer, weapon, place — held **`kDeathHoldSteps = 270`** (4.5 s), then the revive
  plate. All existing primitives; no new art. The ceremony is PRESENTATION's; the SIM exposes
  `kDeathHoldSteps` and routes the defeat.

---

## 5. Presentation — five channels, zero new primitives (PRESENTATION lane)

1. **The reticle is the weapon.** `drawAim`'s ticks retract toward center across the 15-step hold
   and take the warm accent at the hard threshold; they brighten on a valid sightline target.
2. **Rows, in the sheet's own idiom.** While CHARGING at the hard tier, a centred row via the
   `blockLine` pattern: **`HELD HARD -- CUDGEL 14-18`**. GUARD UP and CAST rows unchanged.
3. **Say-row diet.** Per-blow lines retire; landed/taken/blocked speak through the shipped washes,
   the reticle, and audio. The row speaks events and refusals only: downs, crowned KOs, kills
   (`CANNIC DIES ON THE BOARDS.`), the steel-out flip, `NOBODY IN REACH.`, `TOO WINDED TO SWING
   HARD.`, every cast line.
4. **Wire the dormant audio.** SwordDraw on the Lethal flip; SwordClash beside the block pulse;
   Graze/Thud by damage band; HelmetHit on crowned; Whoosh on whiff; Punch* for fists.
5. **Camera impulse at the one seam.** ImpactPulse-driven pitch/yaw BAM offset composed inside
   `Session::camera()`, render-only floats, never written to sim yaw.

### The epitaph and revive ceremony (§4.5) is PRESENTATION's, driving `kDeathHoldSteps`.

---

## 6. AI response, v1 — the bouncer pattern generalized

Per step: swing timer (§1.5); close via `setDestination`/A* when out of reach; face the player.
Per second: re-classify; bouncer warning→ejection ladder; disengage when all down or the player
leaves; Watch pursuit gives up at 12 s out of sight (all shipped). New draw-free integer rules:
rout under Lethal for a bloodied non-professional; bouncers do not wade into steel; the Watch
closes on cause (a Lethal fight or corpse inside a watchman's sight), deference-gated per §4.4.
**No NPC blocking, no NPC hard swings, no NPC-vs-NPC lethality** — named ceiling, §10.

---

## 7. The scrap list, confirmed

**Dies:** COMBAT-SCREEN-SPEC.md's screen half (superseded banner pointing here); COMBAT-SPEC.md's
encounter-scoped draw rows; the "BLADE OUT - THIS IS NOT A BRAWL." refusal and its cast twin
"STEEL IS OUT. THIS IS NOT THE ROOM'S FIGHT ANY MORE." (replaced by lethal resolution + the flip
line "STEEL OUT. THE ROOM STANDS BACK."); every THERE-IS-NO-COMBAT-SCREEN comment reworded to the
new model; the draft's §2.1 120° cone (replaced by the sightline raycast, VETO 1). The §1–§6
math and attack-scoped rows survive as the §10 ceiling.

---

## 8. The build-lane plan

Three worktree lanes with **disjoint file footprints**, buildable in parallel once lane SIM's
headers land (SIM defines the API; INPUT and PRESENTATION build against it).

| Lane | Footprint | Delivers |
|---|---|---|
| **SIM** | `sim/{brawl,tavern,actor,fatigue,watch,crime,nemesis}.hpp`, `sim/{brawl,tavern,actor,nemesis}.cpp` (+ `social`/`crime`/`watch` pure-sim edits) | `chargeQ8` in `strike`; sightline raycast targeting; player state machine + NPC step cadence; wind costs; Lethal rules (floors lifted); `Activity::Dead` + corpse conventions; murder heat + Condemned; deference gate; death→defeat-seam wiring; comment rewording; `test_combat_action`, `test_fatigue` charge sweep, `test_tavern`/`test_nemesis` updates |
| **INPUT** | `client/main.cpp`, `render/controls.*` | Attack → release-edge with hold clock; edge routing; page-scope guards; `static_assert(kHardSwingHoldSteps == HoldToggle::kTapSteps)` |
| **PRESENTATION** | `render/{session,hud}.*`, audio wiring | `attackDown/attackUp`; blocking derivation; say-row diet; reticle charge; HELD HARD row; audio; camera impulse; death ceremony; scripted-arc rewrite |

**The cross-lane contract (fixed):** SIM Tavern methods `playerAttackDown()`,
`PlayerSwingResult playerAttackUp()`, `stepPlayerCombat()`, `stepBrawl()`, and const accessors
`playerCombatIdle()`, `playerChargeSteps()`, `playerChargeHard()`, `playerSightlineTarget()`,
`playerHeldWeapon()`. `PunchResult`/`playerPunchNearest()` keep working for existing call sites
(PRESENTATION repoints `Session::punch()` to a tap = attackDown then immediate attackUp). SIM
owns the charge counter (deterministic, hashed). `strike(..., chargeQ8)` extended by one trailing
default, bit-identical at 256.

**Ship criteria:** drive a real fight, both ways, twice; win/lose brawl and lethal; legality both
sides of the line; determinism (a scripted fight run twice byte-identical, the twin gate green on
the new declared baseline).

---

## 9. Register literals (veto-blessed)

"STEEL OUT. THE ROOM STANDS BACK." / "TOO WINDED TO SWING HARD." / "HELD HARD -- CUDGEL 14-18" /
"CANNIC DIES ON THE BOARDS." / the epitaph plate lines.

---

## 10. The ceiling, named ("for now" is in the ruling)

In priority order, none in v1: **the Daggerfall JUSTICE system — court / arraignment / plea /
sentence branch (fine, jail time-skip, execution = GAME OVER) — is its own scout+design+build
chartered AFTER combat ships** (VETO 4; combat v1 lands only the crime + heat + arrest +
`Sentence::Condemned` hook). Then: melee skill terms folded into the whiff/variance carving +
use-XP on swings; COMBAT-SPEC §1–§6 armor/AC/location/wear; timed block/parry and shields; NPC
blocking, hard swings, and NPC-vs-NPC lethality; aimed/ranged casting past the gift gate; loot on
corpses; street-population (WardActor) combat sheets; pitch-biased hit location; hitstop; the real
climax fight.

**The two player-end paths, binding canon for the justice build (do not conflate):** combat
defeat (lose a fight) → nemesis ruling, promoted-against, coin taken, quay revive — **you live**.
Court execution (the law catches and condemns you) → a **true game over**, NOT a revive — the one
place the player actually ends. Combat v1 builds the first path and the hook for the second.

---

*Assembled from the combat design lead's spec (2026-09-02) with Eli's veto-pass rulings folded
in, at the combat build's landing (2026-09-10). Line numbers in the source scout reports were
anchored at an older tip and are not repeated here; the SIM lane's re-anchor holds the current
targets.*
