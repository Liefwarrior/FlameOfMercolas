# COMBAT-SCREEN-SPEC — Decision 2b: dedicated combat screen (Skald-style)

**Status:** spec-first deliverable for the G-track (gameplay layer). No implementation exists yet.
Implements Decision **2b** (DECISIONS.md): *separate dedicated combat screen entered on encounter
contact, decoupled from the exploration screen.*

**Binding companions:**
- `COMBAT-SPEC.md` (parallel spec, this folder) — owns ALL resolution math: aggregate AC, the hit
  roll, the body-location table, per-location mitigation, durability wear, damage. This document
  consumes those rules and never redefines them. Where this spec names a draw or a table, the name
  in COMBAT-SPEC wins; align at review before either spec is declared final.
- `FACES-SPEC.md` — enemy/companion portrait rendering (Decision 4c hook, §3.4 below).
- `PROGRESSION-SPEC.md` — use-XP awards emitted from combat actions (§4.6).
- ARCHITECTURE.md §4–§6 — tick pipeline, event rules, determinism rules (integer/fixed-point only,
  counter-based named RNG draws). Golden-master tests will cover combat; every rule here is written
  to be goldenable.
- Canon dossier: the combat-canon research dossier (novel = `LordOfTrojia-MVP\Lore\Lord of Trojia
  (indexable).txt`, cited `L<line>`; the novel wins; `Lore\*.html` non-canon).

**North star (binding):** Gabri's social power is maxed from tick zero (Wielder lawful immunity,
L2414, L2967-2968); his PHYSICAL power starts near zero — barely able to kill one slum thug — and
grows only through exploitable systems. Weak-start Gabri is a game invention (canon Gabri has no
growth arc — dossier §7); every weak-Gabri number below is **(placeholder / needs-blessing)**.
The combat screen must make small-fish play viable: fleeing, aiming at weak spots, terrain/light
abuse, and letting the law do your killing are first-class tools, not afterthoughts.

Skald reference: *Skald: Against the Black Priory* — dedicated combat screen entered on contact,
side-vs-side rank layout, visible initiative queue, menu-driven actions, and a verbose flavor-heavy
combat log. We borrow the screen flow and log-forward feedback, but design for **solo + one
companion (Devin)** vs 1–6 enemies, not a full party.

---

## 1. Trigger and transition IN

### 1.1 Trigger

Combat begins when, on the exploration grid, an **encounter contact** occurs:

- a hostile NPC group's engagement zone (Chebyshev radius, per-group raws, placeholder default 1
  tile = adjacent contact) includes Gabri's tile, **and** that group's disposition is HOSTILE
  toward Gabri at that moment; or
- Gabri initiates: player attacks a visible NPC from the exploration screen (this sets surprise,
  §1.4).

Non-hostile NPCs never trigger combat by proximity. Guards are HOSTILE-capable toward everyone
**except Gabri** (Wielder immunity, §5.3) unless he attacks the emperor (canon sole exception,
L2967) — out of v0 scope, recorded for completeness.

### 1.2 What freezes

**The sim engine pauses for the duration of combat.** `SimulationDriver` stops stepping the
engine; no ticks elapse between the entry snapshot and the return transition. Combat is a
game-layer state machine, not a sim phase — it never appears in the ARCHITECTURE §4 tick pipeline
and owns no lanes.

Justification:
- Determinism: the combat replay contract (§4.7) needs a closed initial state. A live sim under
  the combat screen would make ambient light / fire spread part of combat state, coupling the
  combat golden to the world golden.
- ARCHITECTURE §4: saves are legal only at TICK_END; pausing between ticks keeps the world in a
  legal, hashable state for the whole combat.
- Skald precedent: the world waits; combat is a room of its own.

*Post-v0 option (recorded, not designed):* re-sample ambient light per combat round from a live
sim for burning-building fights. Requires re-snapshotting rules; deferred.

### 1.3 State handoff — `CombatEntryState`

One record, built at transition, is the **complete** input to combat (with the world seed and the
action list, per §4.7). Fields:

| Field | Source | Notes |
|---|---|---|
| `encounterId` | game layer | unique per encounter instance; part of every RNG draw key (§4.7) |
| `worldSeed` | engine | same seed the sim uses (ARCHITECTURE §1.1 #16) |
| `entryTick` | engine | tick at which the sim paused; used for RNG keying and the return commands |
| `participants[]` | game layer | Gabri, Devin (if present), 1–6 enemies. Per participant: `combatantId`, side, stat block ref, **equipment snapshot** (all Decision-1a slots: head/torso/arms/hands/legs/feet/shield/cloak/jewelry, each with material, quality, current durability), weapon(s) + ammo counts, current HP/wounds, skill values (PROGRESSION-SPEC) |
| `surprise` | game layer | `NONE`, `PLAYER` (player amushed them), or `ENEMY` (§1.4) |
| `terrainTags` | exploration tile | tags of the tile the **contact occurred on** (the defender's tile), e.g. `street`, `rooftop`, `sewer`, `interior`, `wet`, `blood-slick` (placeholder tag list; owned by the map/tile raws). Drives footing and flee modifiers (§4.5, §2.3) |
| `ambientLight` | sim LIGHT lane | `LightQuery.effectiveBrightness` (0–31, ARCHITECTURE §1.1 #19) at the contact tile, read once at entry. Drives the visibility tier (§2.3) |
| `noiseContext` | game layer | whether guards/witnesses are within earshot radius at entry (feeds return consequences §5) |

Everything else (full map, economy, other NPCs) is invisible to combat. If it isn't in
`CombatEntryState`, combat may not read it — enforced by test T3.

### 1.4 Surprise

- `PLAYER` surprise: player attacked from exploration an enemy that had no awareness of Gabri
  (awareness model owned by the exploration/stealth spec, placeholder until it exists: attacked
  from outside the group's engagement zone while unseen). Effect: **Gabri's side takes one free
  round** (each player-side combatant acts once) before initiative is rolled.
- `ENEMY` surprise: group contacts Gabri while unseen by the player (placeholder: contact from a
  tile outside Gabri's current FOV). Effect: enemy side takes the free round.
- Canon anchor for ambush lethality: Hart's entire craft is killing the unaware (L3030-3038);
  J'harra dies only asleep. Surprise must be strong — it is the small-fish killing tool.

---

## 2. Screen layout

Monospace mockup, 80×30 target (final grid is the UI crew's; the **information contract** — what
must be visible — is binding, pixel positions are not).

```
+------------------------------------------------------------------------------+
| TURN ORDER >> [Thug B] [GABRI] [Thug A] [DEVIN] [Thug C]        Round 3      |
+-------------------------------------------------+----------------------------+
|                 BACK        FRONT               |  TARGET: Thug A            |
|  ENEMY   [Thug C ]      [Thug A ]  <- acting    |   .---.                    |
|          (sling  )      (club   )               |  ( o_o )   <- FACES-SPEC   |
|                         [Thug B ]               |   \_-_/    generic face    |
|                         (saxe   )               |  "Rat-faced cutpurse"      |
|                                                 |                            |
|  - - - - - - - - - - - - - - - - - - - - - - -  |  Cond: Bleeding (arm)      |
|                                                 |  Armor: none visible       |
|  YOU     [GABRI  ]      hp ####------  12/30    |  head   -- BARE            |
|          (knife  )      arm: leather(worn)      |  torso  leather  [####-]   |
|          [DEVIN  ]      hp ########-- 24/28     |  legs   -- BARE            |
|          (r.xbow 6/8)   FRONT/BACK: Devin back  |                            |
+-------------------------------------------------+----------------------------+
| > Attack   Aim...   Defend   Item...   Flame...   Move rank   Flee           |
+------------------------------------------------------------------------------+
| The saxe opens Gabri's forearm to the bone. Blood sheets over his wrist.     |
| Devin's bolt takes the thug under the collarbone; he folds, wheezing.        |
| [Street, night: gloom - ranged -2]  [Cobbles slick with blood: footing -1]   |
+------------------------------------------------------------------------------+
```

### 2.1 Regions (binding information contract)

1. **Turn-order bar** (top): full initiative queue for the round, acting combatant highlighted,
   round counter. Order is fixed for the whole combat (§4.1) so the player can plan.
2. **Battle field** (left): two sides, each with FRONT and BACK rank slots. Player side: Gabri +
   Devin (2 slots used of a 4-slot companion seam — the layout supports up to 4 player-side rows
   so future companions need no screen rework). Enemy side: up to 6 (max 3 per rank). Each row
   shows: name, wielded weapon (+ ammo `loaded/cartridge` for repeaters), HP bar + numeric,
   worst-condition flag (bleeding/stunned/burning), rank position.
3. **Target panel** (right): currently targeted enemy — face (§2.4), one-line descriptor,
   conditions, and the **armor readout**: per-location piece + durability pips, with `BARE`
   loudly marked. Bare locations are the aim-exploit seam (Decision 1a: "bare locations are weak
   spots") — the UI must advertise them, because the north star wants the player gaming the
   location system, and an exploit the player can't see isn't a system, it's a secret.
4. **Action menu**: Attack · Aim… (submenu = body locations from COMBAT-SPEC's table, each showing
   its to-hit penalty and the target's armor there) · Defend · Item… · Flame… (Gabri only) ·
   Move rank · Flee. Grayed-out entries stay visible with the reason on hover/footer (e.g.
   "Flee — blocked: engaged in press").
5. **Combat log** (bottom, scrollback): resolution text (§2.2) plus one-time situational banners
   for light tier and terrain modifiers so every hidden modifier is disclosed once.

### 2.2 Combat log tone (canon-binding)

Register per dossier §6: **bodily, never poetic** — men beg, choke, stink (WB §10). Wound lines
are keyed by (location, damage class, severity) — table owned by this spec's content file
(`content/.../combat_log_lines` (placeholder path)), flavor seeded from canon:

| Location | Canon flavor source | Sample line (placeholder text, canon register) |
|---|---|---|
| head | mace/hilt pounding, "snot, blood, brains, and vomit" (L3005); brains on wall (L2714) | "The cudgel caves the temple. He drops, legs still moving." |
| eyes | socket stab "mini-explosion" (L2956) | "The point finds the socket. What comes out is not a scream." |
| throat | windpipe crush = silent kill (L183, L976); "bled like every other man" (L3038) | "A knuckle crushes the windpipe. He dies without a sound." |
| torso | sternum arrow, uniform turns red (L1264); gut wound survivable (L563) | "The bolt sits in his gut. He folds around it, alive and sorry." |
| arm | severed sword-arm tendons end the fight (L2697); joint-lock break (L979) | "The blade parts the cords of his sword-arm. The saxe clatters down." |
| leg | bolt through thigh (L2339); knee kick topples (L169) | "The knee gives sideways. He meets the cobbles face-first." |
| stagger | shield bash world-spin (L561) | "The shield's rim takes him across the jaw. The street tilts." |
| burning | writhing, crackling flesh, burnt-hair stench (L1829) | "He beats at the flames and the smell of burnt hair fills the alley." |

Log lines are **presentation only** — chosen by a dedicated named draw (`combat.logline`,
attack-scoped subDraw 5, COMBAT-SPEC §2.4) so goldens can assert them, but no game state ever
reads a log line (test T12).

### 2.3 Situational readouts

- **Light tier** from `ambientLight` (0–31): BRIGHT 22–31 (no modifier) · GLOOM 8–21 (ranged and
  aimed attacks −2 (placeholder)) · DARK 0–7 (all attacks −4, aimed attacks disabled, flee +4
  (placeholder)). The Darkstreets are dark at night: light is a weapon. Gabri's Flame light and
  carried lightstones raise the *effective* tier for combat (flat substitution, placeholder rule)
  — an intended exploit seam both ways (fight in the dark to escape; make light to aim).
- **Terrain tags** → flat modifiers, disclosed in the log banner: `wet`/`blood-slick` footing −1
  to melee attack and defense (canon: blood-slick footing, L562, L2509); `rooftop` flee +2 for
  Gabri only (Wielder rooftop traversal, BLESSING-QUEUE #6); `interior` press rule more likely
  (§4.3). All values placeholder.

### 2.4 Faces (FACES-SPEC hook)

Target panel face: named NPCs use hand-authored text-art; generic NPCs use the LITE layered-part
generator. **Determinism contract:** a generic face is generated from `(worldSeed, npcId)` only —
same thug, same face, every session and every replay. Face generation consumes **zero** combat
draws (it happens at NPC creation, not in combat). FACES-SPEC owns the format; this spec reserves
a 12-col × 6-row cell (placeholder) in the target panel and one-line descriptor beneath.

---

## 3. Participants and positioning

### 3.1 Ranks, not a grid

Each side has FRONT and BACK rank slots. No tactical grid in v0 — the exploration grid is where
positioning happens; the combat screen abstracts to ranks (Skald-adjacent simplification for a
2-vs-6 maximum). Rules:

- Melee attacks from FRONT reach enemy FRONT. Enemy BACK is reachable in melee only when the enemy
  FRONT rank is empty ("the wall is down").
- LONG weapons (spear/lance class, COMBAT-SPEC weapon classes) may strike enemy BACK over their
  own FRONT — the canon over-the-shoulder spear drill (L2221, L2390).
- Ranged weapons reach any slot.
- `Move rank` swaps a combatant between FRONT/BACK (a Minor action, §4.2). Moving out of FRONT
  while engaged gives each engaging enemy a free strike at −2 (placeholder) — the canon
  division-change vulnerability (L2392-2393).

### 3.2 Player side

Gabri and Devin are **both directly player-controlled** (no companion AI in v0 — full control is
the Skald model and the honest-systems model). Default: Gabri FRONT, Devin BACK (repeater
crossbow, L2703). The companion seam is an interface: the combat screen takes a `CombatAction` per
player-side combatant per turn regardless of who supplies it (player UI now, AI later).

### 3.3 Devin's kit (canon, dossier §8)

Repeater crossbow: 2 bolts per Attack action ("two quick clicks", L2713), cartridge of 8 bolts
(placeholder — canon gives no count), **reload = a full Major action** (canon: Revlin tosses the
weapon aside rather than reload mid-melee, L2908). Mace as sidearm (skull-splatter class, L2714);
weapon swap = Minor action. Devin's stat line: entirely (placeholder / needs-blessing).

### 3.4 Enemy roster

1–6 enemies, defined by encounter raws (game-layer content): stat block, equipment (drives their
armor readout and loot), weapon class, morale threshold (§4.8), face (named art or generator id).
The v0 ladder bottom rungs from dossier §4: slum thug < bandit < militia veteran — all numeric
stat lines (placeholder).

---

## 4. Turn structure and action economy

### 4.1 Initiative

Rolled **once** at combat start (after any surprise round): per combatant,
`initScore = SpeedStat + draw(combat.init) mod 8`, keyed (encounterId, combatantId) per
COMBAT-SPEC §2.4's encounter-scoped schedule (placeholder formula — final formula owned by
COMBAT-SPEC). Sort descending; ties break by (side: player first, then
`combatantId` ascending) — total order, no re-rolls, stable all combat. Rationale: fewer draws,
plannable UI, simpler goldens.

### 4.2 The turn: 1 Major + 1 Minor

On its initiative slot each combatant takes at most **one Major and one Minor action**, either
order (Skald-simple; no action points in v0):

| Action | Type | Notes |
|---|---|---|
| Attack | Major | standard attack, COMBAT-SPEC resolution: hit roll vs aggregate AC → location draw → mitigation → durability |
| Aimed attack | Major | §4.4 — chosen location, to-hit penalty |
| Flame power | Major | Gabri only, §4.6 |
| Use item | Major | flask, thrown vial, bandage (item list owned by the items spec, placeholder) |
| Reload | Major | repeater cartridge swap (§3.3) |
| Flee attempt | Major | §4.5 |
| Defend | Major | +4 AC (placeholder) until next turn; the canon duelist's stillness (L2694-2696) |
| Move rank | Minor | §3.1, free-strike rule applies |
| Swap weapon | Minor | sheathe/draw |
| Drop prone / stand | Minor | reserved (placeholder; interacts with flee and ranged, not fully specced in v0) |

### 4.3 The press rule (canon)

While a combatant in FRONT is **engaged** (any living enemy in the opposing FRONT), weapon length
matters: SHORT class (saxe, knife, mace) no penalty; MEDIUM (sword, axe) −1; LONG (spear, lance)
−4 (all placeholder). Canon: "No man would be able to swing a long blade in those conditions,
which is what made the saxe so deadly" (L2390). This makes the cheap slum knife an honest early
weapon and gives the player a reason to control engagement — exploit seam by design.

### 4.4 Aimed attacks vs the location table — RULING

**Aiming overrides the location roll.** An aimed attack:

1. takes a **to-hit penalty derived from the aimed location's weight in COMBAT-SPEC's location
   table** — `penalty = f(locationWeight)`, monotone: rarer location = bigger penalty (exact
   `f`, in integer/Q8 math, owned by COMBAT-SPEC; placeholder shape: head −6, arms/hands −4,
   legs −3, torso −1);
2. on a hit, **skips the `combat.location` draw entirely** and resolves mitigation/durability
   against the chosen location.

Justification:
- **One tuning surface.** The same table drives random-hit distribution and aim penalties; there
  is no second, independently-tunable aim table to drift out of balance.
- **It IS the Decision-1a exploit.** "Bare locations are weak spots" only matters if the player
  can choose to hit them. Aim-at-the-bare-head is the intended way a near-zero Gabri kills an
  armored opponent — honest, visible (armor readout §2.1.3), and skill-gated (the penalty shrinks
  as weapon skill rises, PROGRESSION-SPEC).
- **Determinism is unharmed.** The RNG is counter-based and *keyed*, not sequential
  (ARCHITECTURE §1.1 #16): skipping the `combat.location` draw cannot desync anything, because no
  other draw's value depends on how many draws came before it. Draw keys, not draw order, are the
  contract (test T6).

### 4.5 Flee — the small-fish survival tool

Fleeing must be **viable from minute one** (north star). Rules:

- Flee is a Major action by Gabri; if it succeeds, the **whole player side** exits combat
  (companion escapes with him — no leave-Devin-behind in v0).
- **Unengaged flee** (no living enemy in contact with the fleeing side's FRONT, or the fleeing
  side is entirely in BACK with own FRONT empty of enemies… simplified: no enemy adjacent by rank
  rules): **automatic success**, no draw. Walking away from a fight nobody has closed is free.
- **Engaged flee:** contested check —
  `draw(combat.flee) + GabriSpeed + modifiers  vs  fastest enemy Speed + 8` (placeholder;
  keyed (encounterId, combatantId), drawIndex from the round, per COMBAT-SPEC §2.4).
  Modifiers: light DARK +4, `rooftop` +2 (Gabri only), each downed enemy +1, each previous failed
  flee attempt this combat **+2 (cumulative)** — a failed flee is never a dead end, it's
  progress. On failure: each engaged enemy gets one free strike at −2 (placeholder), and Gabri's
  turn ends.
- Enemies never get a free strike on a **successful** flee (the escape already cost the Major
  action and the risk; punishing success would make flee a trap).
- Pursuit is an exploration-layer concern (§5.4).

### 4.6 Flame powers (Gabri only)

The `Flame…` submenu exists from the first fight — social/mythic identity is maxed from day one —
but its v0 contents are deliberately feeble (weak-start invention, needs-blessing, dossier §7):

| Power | Effect (all numbers placeholder / needs-blessing) | Canon seed |
|---|---|---|
| Flicker | raise effective light tier by one for 3 rounds | crafted blinding light (L2340) |
| Dazzle | one target: −2 to its attacks for 2 rounds; costs Gabri his next Minor action | palm light-blast, scaled to embers (L2341) |
| Declare | attempt to rout ONE low-tier enemy (morale check at +4) | "his declaration alone routs Zradist" (L2344), scaled down |

Costs: v0 uses a per-combat use count (placeholder: 2 uses total) instead of a resource pool;
the real Flame economy is PROGRESSION-SPEC's problem. Canon constraint carried forward: the Flame
is emotion-linked and fails under emotional shock (L2729) — reserved as a future condition hook,
not modeled in v0.

Use-XP: every combat action reports a `SkillUseEvent` (weapon skill on attack, Sneak on surprise
kill, etc.) to PROGRESSION-SPEC's accumulator at combat end — awards are part of `CombatResult`,
not applied mid-combat (keeps combat state closed, §4.7).

### 4.7 Determinism and the replay contract (TESTABLE, binding)

> **Contract:** a combat is a pure function.
> `CombatResult = resolve(worldSeed, CombatEntryState, List<CombatAction>)`
> Same three inputs ⇒ bit-identical `CombatResult`, including HP timelines, durability wear,
> ammo counts, morale flips, XP awards, and the chosen log-line ids. This is a golden-master
> surface exactly like the sim's flagship scenarios.

Rules that make it hold:

- All randomness via the engine's counter-based `RandomSource` (ARCHITECTURE §1.1 #16),
  addressed exactly as COMBAT-SPEC §2.4 specifies (that section owns every combat-layer draw
  name and key). The set this screen consumes: attack-scoped `combat.hit`, `combat.block`,
  `combat.location`, `combat.damage`, `combat.weakspot`, `combat.logline`, `combat.readblow`
  (keyed `(attackerId<<20)|defenderId`, drawIndex `attackSeq*8+subDraw`); encounter-scoped
  `combat.init`, `combat.flee`, `combat.morale`, `combat.aiTarget` (keyed
  `(encounterId<<20)|combatantId`, drawIndex `round*4 + purposeOrdinal`). No other randomness
  source exists in the combat layer (test T5).
- All math integer or Q8/Q16 fixed-point; no float/double anywhere in combat state or resolution
  (same ArchUnit-style purity test as sim-core, test T4).
- Combat reads nothing outside `CombatEntryState` (test T3) and writes nothing until the return
  transition (§5), which emits only `SimCommand`s and game-layer results.
- UI is a pure view: rendering, scrollback, hover text consume `CombatResult`/state and produce
  no draws and no state (test T12).

### 4.8 Enemy turns and morale

Enemy action selection in v0 is a deterministic policy (no draws for "AI" except where the policy
explicitly rolls: target choice among equals → `combat.aiTarget`, its own encounter-scoped
draw (COMBAT-SPEC §2.4), never a sub-band of another purpose; policy table is
encounter-raws data). Morale: when a group has lost ≥ half its members (placeholder threshold,
per-group raws), each remaining enemy checks `combat.morale` (keyed (encounterId, combatantId),
drawIndex from the round) vs its
morale stat at the start of its turn; failure = it spends its turns fleeing (exits combat after
one round unengaged). Canon: victory-fed troops break when bled (L2210-2211, L3069-3070); slum
thugs are not soldiers. A routed enemy is a live NPC on the exploration grid again (§5.4).

---

## 5. Return transition — outcomes and world consequences

Combat ends in exactly one of: **VICTORY** (all enemies dead or fled), **FLED** (player side
escaped), **DEATH** (Gabri at 0 HP — game over screen in v0; any Flame-death-intervention is
(placeholder / needs-blessing) and out of v0). Devin at 0 HP = DOWNED, not dead: he exits combat
unconscious and recovers after combat (placeholder rule; companion permadeath is a post-v0
decision).

On end, the game layer builds a `CombatResult`, unpauses the sim, and applies consequences **at
the next TICK_BEGIN via the InputGate** (SimCommands + game-layer entity updates) — the only
legal write path, per ARCHITECTURE §4 phase 0.

### 5.1 Bodies

Corpses and dropped equipment become game-layer entities on exploration tiles at/adjacent to the
encounter tile (deterministic placement: encounter tile first, then neighbors in Dir order).
The v0 *sim* has no item entities ("no agents, no item entities" — PLAN-v0 contract); bodies live
in the G-track entity overlay, not in sim lanes. Loot = enemy equipment snapshot minus durability
lost in the fight (Decision 1a wear carries out of combat — beating armor off a thug ruins the
loot; honest systems).

### 5.2 Noise and witnesses

A finished combat emits a game-layer `NoiseEvent(tile, radius)` (placeholder radius 12 tiles,
scaled by loudest weapon used). NPCs in radius gain awareness; witnesses set faction disposition
per the social layer's rules (future spec). **North-star fence (binding on that future spec):
witness/disposition bookkeeping may never move Gabri's social power — Presence, deference, and
lawful immunity are static constants of the world (PROGRESSION-SPEC §5 PRS, test
`presence_immutable`); disposition here is NPC awareness/hostility state only.**

### 5.3 Law — the Wielder immunity hook (canon, binding)

Guards responding to noise/bodies **never intervene against Gabri**: no arrest, no attack, no
blocking — complete lawful immunity, sole exception killing the emperor (L2414, L2967-2968;
"brushes palace guards aside"). Concretely:

- Guards arriving at a scene with Gabri present: they investigate, they defer, they do NOT enter
  combat against him or initiate any hostile disposition change.
- Guards **do** act against everyone else: surviving/fled enemies of Gabri's fight are valid
  targets for guard response (detain/kill per guard raws).
- **Intended exploit seam (north star):** a physically weak Gabri can start a fight, flee, and
  let the responding law mop up his enemies — or lure thugs into a patrol's earshot. The
  simulation must be honest enough for this to actually work; it is a feature, and test T11
  pins it.
- Reputation/witness texture: white Divine Light garb is "not a welcome sight" in the slums
  (L2410) — social-consequence hooks live in the future social spec, flagged here only.

### 5.4 Fled combats and routed enemies

- Player FLED: the enemy group persists on the exploration grid, state ALERTED, and pursues for N
  exploration turns (placeholder N=10). If it re-engages while ALERTED, the new combat's
  `surprise = NONE` at best (never PLAYER), and enemies get +2 initiative (placeholder). Alerted
  decays to normal after the pursuit window.
- Enemy routed/fled from combat: those NPCs reappear on the exploration grid fleeing away from
  the encounter tile; they remember Gabri (disposition HOSTILE-FEARFUL, placeholder).

### 5.5 State written back

`CombatResult` fields consumed by the game layer at return: per-participant HP/wounds/conditions,
equipment durability + ammo, corpse/loot manifest, XP award list (→ PROGRESSION-SPEC), noise
event, disposition changes (NPC-state only — never Gabri's social power, §5.2 fence), pursuit
state. Sim-side writes in v0: **none required** (no blood
fluid, no sim items); the SimCommand channel is the reserved path for future consequences
(fires started by combat Flame use, etc. — out of v0, the seam exists).

---

## 6. Open questions / needs-blessing (Eli)

1. All (placeholder) numbers above — light-tier thresholds, press penalties, aim penalty shape,
   flee formula + escalating bonus, Devin's cartridge size, morale threshold, pursuit window.
2. Weak-Gabri Flame power list (§4.6) — invented from scaled-down canon feats; bless the concept
   (feeble-but-present from fight one) and the three v0 powers.
3. DEATH = plain game over in v0 (no Flame intervention) — confirm.
4. Devin DOWNED-not-dead rule — confirm no companion permadeath in v0.
5. Whole-side flee (no abandoning Devin) — confirm.
6. Aiming-overrides-location-roll ruling (§4.4) — this spec's recommendation; COMBAT-SPEC must
   co-sign since it owns the location table.
7. Guards mopping up Gabri's fled enemies as an intended exploit (§5.3) — confirm it's desired,
   not an oversight to be patched later.

---

## 7. Unit / integration test list

Golden-master surface per §4.7; all tests integer-deterministic, cross-platform.

**Unit**

1. `T1_entrySnapshot_capturesLightAndTerrain` — build `CombatEntryState` from a fixture world:
   asserts `ambientLight` equals `LightQuery.effectiveBrightness` at the contact tile and
   `terrainTags` come from the defender's tile, not Gabri's.
2. `T2_surpriseRound_freeActionsBeforeInitiative` — PLAYER surprise: both player combatants act
   once before any `combat.init` draw is consumed; ENEMY surprise mirror-case.
3. `T3_combatReadsOnlyEntryState` — mutate world state (light, tiles, other NPCs) after entry;
   resolve the same action list; assert bit-identical `CombatResult`.
4. `T4_purity_noFloatsNoMaps` — ArchUnit-style: no float/double fields or math in combat state
   and resolution types; no `java.util.HashMap` in combat state; log-line choice via draw only.
5. `T5_allRandomnessIsNamedDraws` — instrument `RandomSource`: a full scripted combat consumes
   only the declared draw purposes (§4.7), each addressed exactly per COMBAT-SPEC §2.4
   (attack-scoped or encounter-scoped keying, no other key shapes).
6. `T6_aimedAttack_skipsLocDraw_noDesync` — two combats identical except one attack is aimed:
   assert the aimed branch consumes no `combat.location` draw AND every later draw in both runs yields
   identical values for identical keys (keyed-RNG independence proof).
7. `T7_aimPenalty_monotoneInLocationWeight` — for every location in COMBAT-SPEC's table, rarer
   location ⇒ penalty ≥ any commoner location's penalty; bare-location aimed hit resolves
   mitigation with no armor piece.
8. `T8_pressRule_weaponClassPenalties` — engaged FRONT: SHORT 0 / MEDIUM −1 / LONG −4
   (placeholder values read from raws, not literals); LONG from BACK over own FRONT unpenalized.
9. `T9_flee_unengagedAutoSucceeds_engagedEscalates` — unengaged flee: success, zero draws;
   engaged flee failed twice: third attempt's modifier includes +4 cumulative; free strikes on
   failure only, never on success.
10. `T10_moraleRout_deterministic` — 4-thug group loses 2: each survivor's morale check uses
    `combat.morale` keyed (encounterId, combatantId) with round-derived drawIndex
    (COMBAT-SPEC §2.4); same seed ⇒ same routs.
11. `T11_wielderImmunity_guardsNeverHostileToGabri` — integration: finish a loud combat with a
    guard patrol in noise radius; guards respond, engage the fled enemy NPCs, and never enter a
    HOSTILE disposition toward Gabri; the lure-thugs-to-guards exploit resolves with thugs dead
    and Gabri untouched by law.
12. `T12_uiIsPureView` — render every screen state of a scripted combat headlessly: rendering
    consumes zero draws and mutates nothing (hash combat state before/after).
13. `T13_goldenMaster_fullCombatReplay` — flagship: fixed (worldSeed, CombatEntryState fixture,
    30-action script) vs blessed `CombatResult` hash including HP timeline, durability wear,
    ammo, XP list, log-line ids; run twice in one JVM (twin-run) — identical.
14. `T14_returnTransition_consequencesViaInputGate` — integration: VICTORY writes corpse/loot
    entities deterministically (encounter tile, then Dir-order neighbors), emits NoiseEvent, and
    applies zero sim-lane writes in v0; sim hash unchanged by combat except tick pause/resume.
15. `T15_fledEncounter_persistsAndPursues` — FLED: enemy group ALERTED, pursues ≤ N turns,
    re-engagement grants no PLAYER surprise and +2 enemy initiative; decays after window.
16. `T16_devinDowned_recoversPostCombat` — Devin to 0 HP mid-fight: DOWNED, skipped in
    initiative, exits combat unconscious, recovers per rule; Gabri to 0 HP ⇒ DEATH outcome.

---

*Spec produced 2026-07-12 for the G-track. Every number tagged (placeholder) is unblessed; every
canon claim carries a novel line cite. COMBAT-SPEC.md owns resolution math and draw names —
reconciled in the 2026-07-12 verification pass: this screen consumes COMBAT-SPEC §2.4's draw
names and keying verbatim.*
