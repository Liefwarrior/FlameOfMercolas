# Elder Scrolls Mechanics Reference

What this is: a distilled mechanics reference for how Daggerfall (DF), Morrowind (MW) and
Oblivion (OB) actually work under the hood — formulas, tunables, and data shapes, verified
against UESP / OpenMW source / Daggerfall Unity source. It exists to serve the six-step
program: (1) this research → (2) first-person combat & movement feel → (3) interfaces →
(4) Daggerfall-style chargen → (5) audio → (6) district build-out.
Each section ends with a **Granadad today** block: what this repo already has (checked
against the tree, not guessed) and the honest gap to the reference bar.
STANDING RULE: Trojian canon — the novel, DOCKS-GAZETTEER.md, MAGIC-CANON.md — always wins
over any TES convention it conflicts with. TES is the mechanical vocabulary, not the setting.

---

## 1. First-person feel (movement, fatigue, melee)

Units: MW/OB use Gamebryo units, 64 units ≈ 1 yard (~0.91 m). All named GMSTs below are the
actual engine tunables with vanilla defaults — treat them as the config surface.

### 1.1 Movement speed economy

**Morrowind** (OpenMW-exact):
```
WalkSpeed = fMinWalkSpeed(100) + 0.01 * Speed * (fMaxWalkSpeed(200) - fMinWalkSpeed)
WalkSpeed *= 1 - fEncumberedMoveEffect(0.3) * (encumbrance / maxEncumbrance)   // clamp >= 0
RunSpeed  = WalkSpeed * (fBaseRunMultiplier(1.75) + 0.01 * Athletics * fAthleticsRunBonus(1.0))
SwimSpeed = landSpeed * (fSwimRunBase(0.5) + 0.01 * Athletics * fSwimRunAthleticsMult(0.1))
FlySpeed  = fMinFlySpeed(5) + 0.01*(Speed + LevitateMag) * (fMaxFlySpeed(300) - fMinFlySpeed)
encumbrance > max  =>  speed = 0 (immobile, no "walk slow" tier)
```
Speed 50 / Athletics 5 starter ≈ 2.1 m/s walk, 3.9 m/s run; a maxed character moves ~1.9×
a starter. Every speed is attribute-derived and grows visibly.

**Oblivion:** `BaseSpeed = 90 + 0.4 * Speed`; run = walk × (3.0 + Athletics/100) → 3–4×
walk. Weapon drawn slows you (`fMoveNoWeaponMult = 1.1` sheathed); encumbrance scales down
continuously (to ~-40%); over-encumbered = immobile. Running is effectively free (flat
10 pts/s fatigue regen), so run is the de-facto mode and Athletics levels passively.

**Daggerfall** (DFU PlayerSpeedChanger, classic-accurate):
```
drag = 0.5 * (100 - max(Speed, 30))
Base = Speed + 150 - drag           // ÷ 39.5 for m/s; Speed 50 walk ≈ 4.4 m/s (arcade DNA)
Run  = Base * (1.35 + Running/200)
Swim = Base * Swimming/200 + Base/4
```
Running, Swimming, Climbing, Jumping are all use-leveled skills — the seed of the whole
"use it → improve it" line.

### 1.2 Fatigue as the universal currency (the mechanic to copy)

**Morrowind:** `MaxFatigue = STR + WIL + AGI + END`.
`FatigueTerm = fFatigueBase(1.25) − fFatigueMult(0.5) × (1 − current/max)` → **1.25 full,
0.75 empty**, multiplied into melee hit chance, evasion, block, jump height, and most skill
checks. One bar; everything reads it. Drains: run `5 + enc*2`/s; jump 5; attack
`2 + weaponWeight * attackStrength * 0.25` (heavier weapon + fuller charge cost more).
Regen ≈ `2.5 + 0.02*END`/s while not draining. Fatigue ≤ 0 → collapse, helpless.
Hand-to-hand damages **fatigue** until collapse, then health — brawling is a nonlethal
knockout loop.

**Oblivion:** same pool; fatigue enters melee damage as `(Fatigue/Max + 1)/2` → 0.5–1.0×
only (can never boost past 1.0). Power attacks/jumps/blocked hits drain; 0 = collapse.

**Daggerfall:** drains from running/swinging over real time, restored by sleep; empty = 
knockout (fatal in dungeons). Coarse but ever-present.

### 1.3 Jumping and falling

**MW jump velocity** (OpenMW-exact): `x = 128 + (min(Acro,50)/15)^4 + 3*max(Acro−50,0)*4`,
+64/pt of Jump spell, × `(0.5 + 1.0*(1 − normEncumbrance))` × 1.0-if-running × FatigueTerm.
Every jump costs 5 fatigue and trains Acrobatics — hop-everywhere growth is the point.
Fall damage: each Acrobatics point removes 1.5 units of counted fall height; damage further
scaled by `fFallAcroBase + fFallAcroMult*(100-Acro)`. Mid-air control = f(Acrobatics).

**OB:** `JumpHeight = 64 + (164−64) * Acrobatics/100` (0.9 → 2.3 m). Perks: 50 dodge-roll,
75 jump-while-falling / −50% jump fatigue, 100 water-surface jump.

### 1.4 Movement-mode state machine

All three treat water/air as movement-mode switches, not surfaces: {walk, run, sneak,
jump/air, swim, levitate, climb(DF)}, each with (speedFn, fatigueDrainFn, entry/exit
conditions). Breath meter underwater, then health drain. MW levitation magnitude is a
speed input — weak levitate = slow crawl.

### 1.5 Camera

MW/OB: permanent center-crosshair mouselook, activation ray from screen center. MW has
**no head bob** and no visible body — embodiment is the weapon/hands model plus audio.
Lesson: stable camera + animated hands + footstep/breath audio beats procedural bob.
Instant acceleration, no inertia — deterministic 1:1 control is what makes low speeds
tolerable.

### 1.6 The three melee models

**Daggerfall — gesture combat:** hold attack, drag mouse; attack fires when drag travel
> 5% of longest screen dimension within a 1 s buffer; drag angle → 8 directions:

| Swing | Damage | To-hit |
|---|---|---|
| Thrust (up) | −4 | +10 |
| Horizontal slash | 0 | 0 |
| Down-right diag | −2 | +5 |
| Down-left diag | +2 | −5 |
| Vertical chop | +4 | −10 |

Damage = `rand(min,max) + swingMod + materialMod + (STR−50)/5`. Hand-to-hand:
`min = skill/10+1, max = skill/5+1`. To-hit ≈ `weaponSkill + bodyPartArmorMod +
(atkLUC−defLUC)/10 + (atkAGI−defAGI)/10 − dodge/4 + crit − 50`, clamped 3–97%.

**Morrowind — commitment + dice:** press-and-hold windup, `attackStrength =
clamp((held − min)/(max − min), 0, 1)` (~1 s full charge); release commits an
uncancellable swing. Direction from movement keys at press (stand=chop, strafe=slash,
fwd/back=thrust); each weapon has per-direction min–max damage (spear thrust ≫ slash);
"Always Use Best Attack" option is the official admission the mapping was friction.
`dmg = lerp(min,max,strength) × (50+STR)/100 × conditionRatio`. Hit roll at contact frame:
`hit% = (weaponSkill + AGI/5 + LUC/10)*FatigueTerm + FortifyAttack − Blind
      − (AGI/5 + LUC/10)*FatigueTerm − Sanctuary`, d100. Miss = swish sound, zero effect.
**Passive block** (shield equipped, no input): `(Block + 0.2*AGI + 0.1*LUC) ×
(enemyStrength + 1) × FatigueTerm`, ×1.25 if not backpedaling, clamped **10–50%**.
**Armor:** `mitigated = dmg * max(dmg/(dmg + AR), 0.25)`; struck body part (weighted
random) picks which piece soaks and degrades. Stagger on any health damage (stunlock
source); knockdown if `dmg ≥ AGI * 0.5` plus a roll.
Verdict: visually-connecting swings whiff with no player mitigation — a stat simulator in
an action game's clothes. Take the commitment arc and fatigue coupling; replace the to-hit
die with collision, or convert hit chance into a damage/graze multiplier.

**Oblivion — always-hit + active defense:** swing connects if the animation hitbox
overlaps within reach. Skill moved into damage and defense:
`Damage = BaseDmg * 0.5 * (0.75 + STR*0.005) * (0.2 + Skill*0.015) *
((WeapHealth/Max)+1)/2 * ((Fatigue/Max)+1)/2 * max(SneakMult, PowerMult) * ArmorRed *
Resist` — every term a ~0.5–1.0 normalized multiplier. Armor = flat % reduction cap 85%.
**Power attacks** = hold ~0.3 s: standing 2.5× from Novice; directional per skill tier
with riders (Journeyman side 5% disarm, Expert back 5% knockdown, Master fwd 5% paralyze);
big fatigue cost — the fatigue economy gates burst, not access. **Active block** (hold):
up to 75% reduction shield / 50% weapon at Master; **recoil rule** — a timed block (skill
≥ 25, low attack stagger score) bounces the attacker into a punishable opening; blocking a
power attack at low skill staggers the blocker. This creates the attack/block/counter
rhythm OB combat is remembered for. Stagger states to implement: hit flinch, block-recoil
(attacker), block-stagger (defender), knockdown, paralyze — animation lockouts of
increasing length.

### 1.7 Weapon reach & speed

Both live on the weapon record. Reach multiplies a global melee range (MW
`fCombatDistance ≈ 128` × reach; spears ~1.8 genuinely outrange). Speed multiplies
animation rate. OB representative (longsword = 1.0/1.0): dagger 1.3/0.6, shortsword
1.2/0.8, claymore 0.85/1.3. DPS roughly flat across classes; the real trade is fatigue
cost, reach, stagger weight.

### 1.8 Hit feedback vocabulary

Distinct audio for whiff / blocked / armor hit / flesh hit; victim flinch interrupting
their action; attacker recoil on block; screen-edge red flash + grunt on player damage;
condition ticking on weapon and struck armor; knockdown with get-up animation. MW proves
the negative case: when the whiff swish contradicts the visible blade-through-torso,
players read it as broken — **feedback must agree with the collision the player saw**.

**Granadad today:** `native/include/granadad/sim/player.hpp` is already the right chassis —
sim-authoritative continuous body, Q8 integers, BAM facing, fixed movement steps, axis-
split collision with substepping, auto-mantle, fall curve; gaits are real-world constants
from `human_scale.hpp` (walk 1.5 / jog 5.1 / sprint 7.0 m/s). `controls.hpp` has the 12
core actions incl. the in-flight Cast and Block (Block is held-only, read by
`Tavern::tickBrawl`, scaled by shieldwall); `brawl.hpp` pins the real-time-brawl vs
dedicated-combat-screen rule. The fatigue build closed the biggest gap this section used
to name: `sim/fatigue.hpp` is the §1.2 one-bar economy adapted to MGT/AGI/VIG/WIT and
integer Q8 (max = 2·VIG+MGT+AGI; FatigueTerm 320→192 Q8 applied as degradation-only onto
the existing brawl whiff and cast check, same-roll, no collapse in v1), the four
attributes have their runtime readers (MGT→melee damage, AGI→gait scale + climb costs,
VIG→pool/regen, WIT→cast check + cooldown recovery), and skyrunning/grit nudge climb
cost/regen. Remaining gaps: no encumbrance, no swim state, no weapon records
(reach/speed), no armed melee model yet — COMBAT-FEEL-REFERENCE.md and the combat screen
specs are where §1.6's choices get made.

---

## 2. Magic & spellmaking

### 2.1 Magicka economy

Pool: DF `SpellPoints = INT × mageryMult` (default 0.5×; Increased Magery buys up to 3.0×,
Sorcerer's 3× paired with no-regen). MW `INT × (1.0 + race/birthsign mults)` (Altmer +1.5,
Atronach +2.0 with stunted regen + 50% absorb). OB `2×INT + race + birthsign`.

Regen: **MW has none** — rest only, `0.15 × INT` per hour slept. **OB regens
continuously**: `regen/s = 0.01 × (0.75 + 0.02 × WIL) × MaxMagicka` — full refill is
constant wall-time regardless of pool size (~36 s at 100 WIL), runs in combat.
Design fork: MW's rest-only regen = per-expedition budget; OB's %-pool regen =
per-fight throughput. Pick per pacing goal.

### 2.2 Casting flow

MW: three exclusive hand stances (weapon/magic/down); ready-magic lowers the weapon,
attack casts the selected spell. Mode-switch friction. OB: dedicated Cast button fires the
equipped spell instantly, even with weapon drawn — one spell equipped, swapped via
menu/hotkeys. DF: open spellbook UI, click spell. OB's button model is the one to copy
(and Granadad already did: C = cast equipped).

### 2.3 Failure and gating

- **MW probabilistic:** `chance% = (Skill×2 + WIL/5 + LCK/10 − SpellCost − Sound) ×
  (0.75 + 0.5 × fatigue/max)`, d100 per cast. Fizzle costs no magicka, grants no XP.
- **OB no roll**, two gates: mastery tiers on *unscaled* cost (Novice ≤25 / Appr 26–62 /
  Journ 63–149 / Expert 150–399 / Master 400+, requiring skill 0/25/50/75/100), and cost
  scaling `cost = Base × (1.4 − 0.012 × Skill)` (×1.4 at 0, ×0.2 at 100). Worn armor
  multiplies outgoing magnitude down (~−15% full heavy, removed at Master armor skill).
- **DF:** cast cost `(0.275 − 0.0025 × Skill) × goldCost`, min 5.
Cost-scaling feels strictly better than fail-chance moment-to-moment; keep fail-chance
only where failure itself is interesting content.

### 2.4 Spellmaker effect algebra

All three price a spell as **Σ over effects** of f(baseCost, magnitude, duration, area,
range).

**DF** (Mages Guild members, ≤3 effects): per effect three dials, each
`base + increment per N caster levels` (level-scaling makes cheap spells grow):
Duration, Chance%, Magnitude min–max. 5 delivery types. Gold price = deterministic sum of
dial numbers; casting cost derives from it via skill (§2.3).

**MW** (service NPCs; effect must be known from an owned spell): per-effect
`floor((2×AvgMag × (Dur+1) × BaseCost/40 + Area × BaseCost/40) × (1.5 if Target))`, min 1.
Linear — the famous exploit surface (1-sec 100-pt Fortify chains).

**OB** (Arcane University altar; effect known): `Cost = BaseCost × 0.1 × Mag^1.28 × Dur ×
max(1, Area × 0.15) × (1.5 if Target)`. Two totals: unscaled (fixes mastery tier — can't
even *make* above your tier) and scaled (real price, per-school skill). Superlinear
`Mag^1.28` = deliberate anti-nuke pressure MW lacks.

Range semantics everywhere: Self ×1.0, Touch ×1.0 (melee hit test), Target ×1.5
(projectile, travel time, can miss).

### 2.5 Schools, delivery, enchanting, acquisition

Six schools (Alteration/Conjuration/Destruction/Illusion/Mysticism/Restoration), each a
trainable skill that both gates and cheapens its own casting. Enchanting: soul trap →
filled gem (MW soul values 5–400; OB tiers Petty 150 → Grand 1600); item charge = soul
value; per-use drain reduced by Enchant skill; constant-effect needs soul ≥ 400 (MW).
**Acquisition is purchase-only in all three**: spell-vendor NPCs; spellmaking is
downstream of purchase because you must know an effect before composing with it — vendor
spells are an **effect-unlock catalog**, the real reason to buy weak utility spells.

Distilled implementation core: per-effect table `{id, school, baseCost, allowedRanges}`;
spell = list of `{effect, magMin, magMax, durSec, areaRadius, range}`; price via linear or
`Mag^1.28`; gate via tier thresholds on unscaled cost; runtime cost via
`(1.4 − 0.012×skill)`; vendors as effect-unlock catalogs feeding a spellmaker.

**Granadad today:** the spellmaker half is *built and canon-priced*: `spellbook.hpp` loads
the 11 authored spells from `content/raws/spells/spells.json` as modular components
(axis × mode × magnitude × duration × target × range — exactly the §2.4 shape);
`spellforge.hpp` is a working Daggerfall-style forge with MAGIC-CANON's own cost model
(kResistPerTile=4, kResistUnbridged=20, held-vs-delivered pairing table, forge ceiling by
linkcraft level, deterministic forged-spell ids, ForgeBench UI state, Grimoire
learned-vs-crafted split). The priest teaches from the raws (`teachableAt`), i.e. the
effect-unlock catalog already exists. The stated VERIFICATION GAP (S4) still holds:
**nothing casts** — no resolution, no magicka/cost pool decision (cooldownTicks stored,
read by nobody); `Session::castEquipped()` + the Cast key are the in-flight wiring. Canon
diverges from TES on purpose: no schools (linkcraft), 3 effect axes not dozens, Ranged
gated behind "the gift" — canon wins.

---

## 3. Daggerfall character creation

Sequence: Race → Gender → Class (pick / quiz / custom) → Biography (12 Qs or random) →
Name & Face → Attributes → Skills → Reflexes → summary. Verified against UESP + DFU
source.

### 3.1 Race

Gender cosmetic. Race's **only** gameplay effect is one special (the manual's attribute
tables were never implemented): Breton +30% magic resist; Redguard +Level/3 to-hit/damage;
Nord +30% frost; Dark Elf +Level/4 to-hit/dmg; High Elf paralysis immunity; Wood Elf
+Level/3 archery; Khajiit +30 climbing; Argonian swim/breath. Racial advantages override
class disadvantages of the same type — race trumps class on conflicts.

### 3.2 Class — three paths

**Predefined (18):** each is literally a saved custom-class record: primary/major/minor
skills, HP die (Mage 6 → Barbarian 25), advancement multiplier (Mage 1.04× → Barbarian
0.64×), specials, plus a fixed attribute sheet always summing to **400**. HP die,
advancement mult and specials are one coupled economy: Warrior's 20 HP / 0.75× is paid for
by zero advantages; Mage's 2×INT magery is paid with 6 HP and 1.04×.

**Quiz ("Generate Class"):** 10 of 40 authored scenarios, 3 answers each, every answer
tagged Warrior/Rogue/Mage (shuffled order). UX: three constellations brighten per answer —
the player watches their identity converge. Tally triple (summing to 10) looks up a
predefined class in a data table; player may accept or **decline** back to the manual
list — the quiz is never a trap. Authoring format: second-person youth vignettes with a
concrete prop (trapped animal, extra change, a bully, a flawed dagger); three defensible
answers each carrying a self-justifying rationale clause; Warrior = honor/duty/direct
action, Mage = knowledge/prudence/observation, Thief = pragmatism/profit/rule-bending;
recurring fixtures (the Armsmaster, your father, market town) imply one coherent
childhood.

**Custom (ClassMaker):** name + 3 primary/3 major/6 minor from 35 skills; everything else
optional, feeding one scalar shown as a dagger on a gauge:
```
P = hpAdjust + Σ advantages + Σ disadvantages           // clamped [-12, +40]
hpAdjust = (hp > 8) ? (hp - 8) : -2*(8 - hp)            // HP slider 4..30, default 8
AdvancementMultiplier = 0.3 + 2.7 * (P + 12) / 52       // 0.3x .. 3.0x skill-up cost
```
Attributes: base 50 each, zero-sum redistribution, clamp [10, 75]. Advantages (max 7):
Increased Magery 1.0–3.0×INT = 2/4/6/8/10 pts; Expertise 2; Resistance 5; Immunity 10;
Adrenaline Rush 4; Spell Absorption 8–14; regen/rapid-heal tiered by uptime (in light /
in darkness / general). Disadvantages refund: Critical Weakness −14; No SP regen −14;
Forbidden Armor Leather/Chain/Plate −1/−2/−5; Forbidden Material priced by **utility, not
lore** — Steel −10, Daedric −2; Phobia −4; Darkness/light-powered magery −7..−14. Enforce
mutual exclusion (immunity+weakness same element, bonus-to-hit+phobia same group) — classic
had exploitable gaps.

### 3.3 Biography — 12 questions

Class-keyed question sets (or randomize). Answers do three jobs: mechanical bonuses,
spliced prose "Background History", (unshipped) NPC hooks. Effect vocabulary: Skill +6
typical (+12 big commitment, +2/3/4 splits), Gold +100/200/500, specific item by material
tier, Reputation ±2..±10 with 5 social groups or a deity, and **mandatory-malus
questions** (pick your poison: PoisonRes −5/−10, DiseaseRes −5, MagicRes −5, Fatigue −5,
ReactionRoll −5, ToHit −5), plus pure-flavor questions. Data format is a flat opcode list
per answer (`#NNNN` splice text, skill ±N, `IT x y z` item, gold, rep, resistance) — one
BIOG file per class, trivially author-able.

### 3.4 Attributes, skills, reflexes

Attribute roll: class base sheet + 0–10 random per attribute + a 6–14 bonus pool spent
freely; reroll freely, **one saved roll** restorable — slot-machine UX with an undo.
Skills: Primary 25 / Major 15 / Minor 10 / misc 0; biography-touched skills add bonuses
with no roll, untouched get +3–6 random; player distributes 6/6/6. Reflexes five steps:
Very High → Very Low credit ×1.25/1.125/1.0/0.875/0.75 counted skill uses (combat-speed
tradeoff; the enemy-speed half was bugged in classic).

### 3.5 Downstream hooks

```
usesNeeded(skill) = floor(skill * perSkillMult * classAdvMult * 1.04^level * 2/5 + 1)
Level = floor((S - S0 + 28) / 15)    // S = 3 primaries + 2 best majors + best minor
                                     // S0 at chargen => starting skills set the level cap
Per level: 4-6 attribute points; HP = rand[die/2..die] + (END-50)/10
```
Systemic summary: chargen is one currency (difficulty points) with three skins — HP
slider, advantage shop, and fixed classes as pre-baked bundles — plus two authored
questionnaires: a 10-of-40 tally quiz that *selects* a bundle and a 12-question biography
that *perturbs* starting state via flat opcodes. Every knob lands in exactly one of four
sinks: advancement multiplier, starting skills (→ level cap), starting attributes,
persistent specials.

**Granadad today:** `sim/chargen.hpp` already implements the Daggerfall sheet shape —
3 Primary / 3 Major / 6 Minor slots over the real skills.json SkillTrack (the_flame
refused), start levels 30/15/5 (flagged placeholder), 24-pt attribute pool with a 15-pt
per-attribute cap, idempotent apply(). `render/creation.hpp` is the BG3-style origin flow:
DEVIN/GABRI fixed sheets from `content/raws/companions/*.json` (via companions.hpp), a
custom path over Chargen, appearance picker, all drawn through the shared
DialogueViewState widget. Gaps to the bar: no quiz (the 10-of-40 tally + constellation
feedback is cheap and high-charm), no biography opcode system, no
advantage/disadvantage point economy or advancement-multiplier coupling, no reflexes
knob, and starting skills don't yet feed any level-cap arithmetic (there is no character
level at all — see §5).

---

## 4. Interface & menu conventions

### 4.1 Morrowind's multi-panel menu mode

One modal menu mode (right-click toggle, world pauses visibly behind), **four windows
simultaneously**: Stats, Inventory/paper-doll, Magic, Map. Draggable/resizable, layout
persists. OpenMW's pinning (panel stays on HUD, input-transparent) is the refinement worth
copying. Load-bearing decisions: (a) all four visible so cross-referencing needs no mode
switch, (b) tooltips carry all secondary data so rows stay one line, (c) selection state
(readied spell, equipped gear) mirrors to a tiny always-on HUD indicator so closing the
menu loses nothing. Magic window rows show name + live cast-chance %; one click readies.

### 4.2 Daggerfall's set

320×200 full-screen replacements off a persistent HUD bar. Inventory: paper doll + two
item columns (remote container | player), 4 category filters, and **mode buttons**
(Remove/Equip/Use/Info) — modal verbs instead of drag-and-drop, low-res friendly.
The wagon: purchasable 750 kg overflow container accessible outdoors — loot-hauling never
requires a mid-dungeon town trip. Travel map: filter toggles + name search over ~15k
locations; three binary choices (cautious/reckless, foot/ship, inns/camp) → days + gold
preview (see §8.7). Rest: Rest X hours / Until healed / Loiter; city streets refuse rest;
wilderness rests roll spawn interruptions. Dungeon automap is 3D isometric — honest about
vertical topology, unreadable without the floor-slicing DFU added.

### 4.3 Journal models

**MW:** append-only chronological prose journal + (Tribunal) index layer: Topics tab
(every hyperlinked keyword → accumulated summaries) and Quests tab (filtered stage view).
**No quest markers** — directions live in the prose, so entries must be written navigable.
Implementation: journal = append-only `(day, questId, stageIndex, text)` log; index views
are filters. **OB:** per-quest log, Current/Completed tabs, exactly one Active quest
driving a compass marker through walls. For an investigation game MW's model is the
mechanically interesting one; OB's contribution is the Current/Completed split.

### 4.4 Barter math (MW, OpenMW-verified)

```
pcTerm  = (clamp(disp,0,100) − 50 + Mercantile + LUC/10 + PER/10) × pcFatigueTerm
npcTerm = (npcMercantile + npcLUC/10 + npcPER/10) × npcFatigueTerm
buyPrice  = base × 0.01 × (100 − 0.5 × (pcTerm − npcTerm))
sellPrice = base × 0.01 × ( 50 − 0.5 × (npcTerm − pcTerm))    // min 1 gold
```
Untrained buys at ~100%+, sells at ~50%−. Haggle slider: each 1% pushed in your favor
costs ~4% acceptance from a base ~50% (fBargainOfferBase=50, fBargainOfferMulti=−4),
offset by skill superiority. Success: disposition +1; failure −1 and refusal. Merchant
gold resets 24 h after last trade. Known wart: above ~70 Mercantile the curves cross so
disposition can worsen sell prices — keep both terms monotonic in the player's favor.

### 4.5 Rest / wait

MW: one key, verb depends on legality — towns/owned interiors offer Wait only; wilderness,
dungeons, beds offer Rest (+Until Healed); recovery per hour health `0.1×END`, magicka
`0.15×INT`; sleep (not wait) is the level-up trigger; unsafe rests roll spawn
interruptions, waits never. OB: wait anywhere not-in-combat, sleep needs an owned/allowed
bed.

### 4.6 Persuasion

MW: visible disposition 0–100; Admire / Intimidate (temporary gain, permanent resentment) /
Taunt (lowers disposition, raises Fight — enough successes flip the NPC hostile **without
it counting as your crime**, the sanctioned make-them-swing-first tool) / Bribe 10/100/1000
(Mercantile-based, failure keeps the gold). All d100 vs
`(Speechcraft + LUC/10 + PER/10) × fatigueTerm` both sides, damped near disposition 0/100.
Every attempt trains Speechcraft. OB replaced rolls with the persuasion wheel minigame —
aged badly; the visible-chance roll aged well.

### 4.7 Containers, pickup, ownership

MW: world pickup is one click to inventory; containers open alongside your inventory,
transfer both ways (player storage in any non-respawning container). Vanilla MW gives
**no ownership warning** — theft is only theft if witnessed; **OB's red-hand cursor** on
owned items/beds is the convention to copy: an investigation sandbox turns on knowing
what's a crime *before* committing it. Weight/encumbrance always visible in every
transfer screen.

### 4.8 Low-res readability doctrine

One or two bitmap fonts, large x-height; fixed-size icon cells (32×32) with corner
overlays, never text inside icons; verbs as modes/buttons not gestures; tooltips as the
universal second layer; chunky 9-slice window frames; wide scrollbars. Diegetic/abstract
split is consistent: diegetic *framing* (book-shaped journal, paper doll, parchment map),
frankly abstract *data* — disposition, prices, cast chance and encumbrance are always
visible numerals. None of these games hide mechanics behind fiction.

**Granadad today:** `render/menu_view.hpp` is already the Morrowind tiled menu by explicit
brief — four simultaneous panels (Character / Map / Letters / Journal), focus ring instead
of pages, two type registers (chunky titles, dense bodies); the DialogueViewState widget
is the one panel vocabulary reused across seven surfaces; `casebook.hpp` is the
investigation journal (Bloodletter trail) and `letters.hpp` its readable documents —
the MW prose-journal direction, already chosen. `barter.hpp` runs haggling as rounds with
merchant patience (Ardenfall reference) over the SocialLedger. Gaps: no inventory system
at all (contraband slots are the only carry — no paper doll, no containers, no ownership
cursor), no rest/wait verb for the player (clockScale is a capture tool), no tooltips
layer, no pinning. Canon note: DOCKS-GAZETTEER 5.3 bans dialogue-skill checks — §4.6's
persuasion rolls are reference only; the gate here stays geographic/social-topological.

---

## 5. Skills & progression

### 5.1 The three models

| | Daggerfall | Morrowind | Oblivion |
|---|---|---|---|
| Skill gain | tally counter, batch-checked | progress points per use | XP per use |
| Level trigger | formula over tracked-skill sum | 10 major+minor ups, then sleep | 10 major-only ups, then sleep |
| Attributes | free pool | 3 picks, ×1–×5 banked mult | same |
| World scaling | partial | mostly static | aggressive (the pathology) |
| Discrete perks | none | none | at 25/50/75/100 |

### 5.2 Daggerfall tally

+1 per qualifying use; skill +1 when `tally > difficultyFactor × currentSkill` and ≥6
in-game hours since last increase (rate limiter). difficultyFactor = per-skill constant ×
class advancement multiplier. Level = `floor((S − S0 + 28)/15)` recomputed on every
increase — no ritual. **Cheapest correct use-XP implementation: O(1) integer per skill per
actor, checked lazily — NPC background leveling can literally be this.**

### 5.3 Morrowind points + the multiplier bank

Points L→L+1: `L × classMult(0.75 major / 1.0 minor / 1.25 misc) × specMult(0.8)` —
**identity as learning rate, not caps**: any actor can learn anything, class only changes
speed. Use values: weapon hit 1.0, block 2.5, armor-hit-absorbed 1.0, successful cast
≈1.0 (failed = nothing), pick ≈2.0, sneak ticks only near unaware actors, Athletics
per-second trickle, Acrobatics ≈3.0 for surviving a damaging fall (risk teaches),
Mercantile ≈0.4 per deal.
Level-up: 10 major/minor increases → sleep → pick 3 attributes; each gains by governed
skill-ups banked since last level: 0→+1, 1–4→+2, 5–7→+3, 8–9→+4, 10+→+5. Luck always +1.
HP = (STR+END)/2 start, +END/10 per level **non-retroactive**.
Pathologies to dodge: multiplier banking (spreadsheet sleep management), END-first opener
(non-retroactive HP), sleep-as-commit tension. If attribute growth is kept, derive it
continuously from lifetime skill totals.

### 5.4 Oblivion XP + the scaling trap

`required = fSkillUseMult × (0.35 × L)^1.5 × classMult(0.75/1.25) × spec(0.75)` — power-1.5
curve: cheap early points hook, slow late points prestige. Majors-only leveling + level-
scaled world inverted the design: optimal play picked majors you never use; casuals got
+2/+2/+2 levels vs a world tuned for +5/+5/+5 and literally got weaker by playing
naturally. **Never combine player-authored level timing with world scaling.** (Remastered
fix: all skills grant level XP, flat 12-point distribution, no banking.)

### 5.5 Mastery perks — the biggest feel addition

0/25/50/75/100 = Novice→Master, each a **discrete rule change**: Block 25 no fatigue cost,
75 knock-back counter, 100 disarm; Blade each rank a new power attack; Acrobatics 50
dodge-roll, 100 water-jump; Mercantile 50 sell anything to anyone, 75 invest in shops;
Security ranks reduce tumbler reset. Continuous formulas make growth real; breakpoint
perks make it *legible* — players remember "at 50 I could sell anything" forever. Steal
wholesale.

### 5.6 Trainers & books

MW/OB trainer cost `currentSkill × 10` gold/pt. MW caps: trainer's own skill, and **can't
train past the governing attribute** (can't buy past your body). OB: 5 points per
character level (rationed gold sink), Master trainers behind favor-quests — the skill
system itself becomes content. Skill books: +1 first read only.

### 5.7 The shared formula shape

Morrowind's doctrine, one function written once:
`chance = (Skill + Attr/5 + Luck/10) × fatigueTerm ± situational − difficulty`, d100.
Applied to lockpicking (`− lockLevel`, ≤0 = "lock too complex" hard gate that still costs
a tool charge), run speed, jump, prices, taunt-to-lawful-kill, spellcasting. No flat
constants — every constant a designer would hardcode is instead f(skill, attribute,
fatigue). Why it feels rewarding: zero-friction attribution (the action *is* the XP
claim; surface every skill-up individually, never batch); skills gate outcomes players
perceive without UI (speed, arcs, prices, lock messages, provoked brawls).

**Granadad today:** `social.hpp` SkillTrack is real Morrowind-style use-XP over
`content/raws/skills/skills.json`'s 20 rows (the_flame excluded), with the "skills affect
everything" directive already honored in spots — lockpick feel level, barter, stealth,
spellforge ceiling all read skill. `attributes.hpp` wired the four attributes
(MGT/AGI/VIG/WIT) and parsed aptitudeTier (FAVORED/TRAINED/NEGLECTED), but —
stated in chargen.hpp — the aptitude→XP-cost ratio from PROGRESSION-SPEC.md §1 is
deliberately **not** wired into usesForLevel() yet (live-tuning blast radius). Gaps: no
character level, no attribute growth, no breakpoint perks (§5.5 is the cheapest big win),
no trainers/skill-book gold sinks, no fatigue term to multiply into checks (§1.2), and
ward actors don't level from daily life yet — the Morrowind directive's §5.2 tally model
is the cheap path to that.

---

## 6. Factions, quests, crime, dialogue

### 6.1 Guild rank ladders

**DF:** one universal 10-rank table (rank r needs rep ≥ 10r, one preferred skill ≥
~22+8r, another ≥ ~4+4r), checked on questgiver talk with a 28-day cooldown,
rank-skipping allowed; expulsion only below rep 0. **Rank = keyring of services, not stat
buffs**: Mages Guild R0 spellmaker, R2 library, R4 soul gems, R5 enchanter, R6 24-hour
access, R8 teleport network; Fighters `repairCost × (10−rank)/10`,
`reward × (10+rank)/10`. **MW:** per-faction 2 favored attributes + 6 favored skills +
faction rep (quests only, +5/+10 each); promotion instant on ask; top ranks gated behind
authored quests (usually deposing the head); crimes against guildmates → expulsion, one
reinstatement; hard exclusivity locks rival factions.

### 6.2 Daggerfall radiant quests (template mechanics)

Quest = QRC (numbered message blocks: offer/accept/decline/success/fail/rumor) + QBN
(resources + task logic); ~227 templates serve the whole world. Resources bound at
instantiation: Person (faction/social-type constrained, placed in a real building),
Place (region site tables), Item, Foe, **Clock** (base days ± random, auto-extended by
travel time; expiry fires the fail branch — most quests are failable by being slow).
Faction-owned pools filtered by membership + rank; same template elsewhere binds new
people/places and reads as a new job. **Standard rep contract:** success +5 giver, +2
allies, −2 enemies; failure −2/−1/+1 (nobles fail: −22 personal — catastrophic by
design); templates may carry third-party deltas. Active quests inject rumor text into the
region's "Any news?" pool — townsfolk gossip about the job while it runs. All rep drifts
1 point toward 0 per 112 days.

### 6.3 Inter-faction ripple

DF: stored graph (parent/child/ally/enemy), a change ripples exactly one hop (allies
+half, enemies −half); ally/enemy edges partly reshuffled annually. MW: **stateless
reaction matrix** (−3..+3, deliberately asymmetric) applied at dialogue time:
`dispositionAdjust = reaction × 3 × (1 + 0.5 × PCrank)` — rising in Guild A automatically
sours every member of rival Guild B, scaling with rank, no stored value touched.

### 6.4 Morrowind dialogue data model

One global DB: Topic → ordered Info records, each with response text + filter conditions
(speaker id/race/class/faction+rank, cell, disposition, PC sex/faction/rank, journal-stage
comparisons, variables); **first passing record wins**, specific-before-generic by
authoring order. Hyperlinked keywords grow the player's known-topic list. Quests =
numbered journal indexes tested by the same filters.

### 6.5 Daggerfall tell-me-about

Category (Location/People/Things/Work) × tone: Polite (Etiquette check), Blunt
(Streetwise), Normal (Personality). Answer quality probabilistic: never-heard-of →
verbal directions → **marks your map**. Active quest resources register with the town, so
ordinary citizens can direct you to them — radiant system and dialogue share state.

### 6.6 Crime & bounty

Detection first: an illegal act is a crime only when an NPC **detects and reports** (MW
per-NPC Alarm 0–100; homeowners often Alarm 0, guards 100). Bounties (MW/OB): theft
itemValue / 0.5×value; lockpick 5; pickpocket 25 (only failed attempts detected in MW);
assault 40 (aiming a swing counts); murder 1000; OB horse theft 250 (horses report).
Guard menu: **Pay** (stolen-flagged items confiscated to an Evidence Chest) / **Jail**
(1 day per 100 gold; −1 random skill/day, except Security/Sneak which *gain*) / **Resist**
(killing the guard while resisting isn't murder). MW ≥5000 = death warrant, no menu.
**MW stolen-goods rule (brutal, memorable):** ownership tracked by item *type* per victim
forever — steal Nalcarya's diamonds once and every diamond you ever hold is "hers";
offering a victim their own goods is an instant report; the victim *is* the fence
detector. OB: fences = merchants with Responsibility <30, the only buyers of flagged
goods; guild crime → suspension + penance quest, 3 strikes = expulsion. OB guard social
override: disposition ≥90 ∧ bounty <1000 → fine waived. **OB NPCs live inside the same
system**: low-Responsibility NPCs steal food when hungry and get cut down resisting
arrest — crime is a sim rule, not a player-only rule. DF: per-region legal rep with a
court scene (plead via Streetwise or Etiquette).

### 6.7 NPC schedules, honestly

MW: none — density + dialogue filters + faction reactivity carried it. DF: building
opening hours + after-hours = breaking & entering; that alone made time-of-day matter.
OB Radiant AI as shipped: per-NPC package stacks (Sleep/Eat/Wander/Travel/UseItemAt…)
with schedule windows; packages state goals not paths (Eat finds any food — own, buy, or
steal if Responsibility <30); four scalars (Aggression, Confidence, Energy,
Responsibility) drive the edges. The E3 dynamic-goal claims did not ship; pre-release
need-loops caused NPC murder sprees and were *removed*, not fixed. Lesson: **schedule
windows + owned beds/food + a crime system that applies to NPCs bought ~90% of perceived
aliveness; dynamic goal formation bought ~0%.**

**Granadad today:** this dimension is the deepest already-built stack: `faction.hpp` five
factions + ranks.json ladders; `crime.hpp` six-act ledger with witness detection;
`watch.hpp` warrants → actual arrest consequence; `nemesis.hpp` (killer rises in rank —
beyond TES); `legend.hpp` five ward-reputation tracks; `contract.hpp` radiant goods jobs
bound to real notables and the live economy; `questline.hpp` authored stage spines;
`social.hpp` per-actor deed memory + witness weights; `ward_actors` 661 people with
jobs/hours/beds (§6.7's shipped-OB recipe, already the design); `dialogue.hpp` topic
lists gated geographically per canon (no rolls — overrides §6.5's tone checks and §4.6's
persuasion). Known gap stated in `radiant_quest.hpp` itself: the #81 RadiantBoard
generator is real and tested but **nothing constructs one** — unwired. Other gaps:
rank = keyring service gating (§6.1) not yet a pattern, no inter-faction reaction ripple,
no jail/pay/resist menu (arrest exists, the three-verb surrender doesn't), no
stolen-goods type-flagging, no quest-rumor injection into ward_voice barks.

---

## 7. Audio

### 7.1 The layer model

4–5 independent buses, separate sliders, mixed additively: Music (2D stream), Ambient bed
(2D loop + chance one-shots), World SFX (3D point sources), UI SFX (2D, never ducked),
Voice. MW/OB give **footsteps their own slider** — the highest-repetition sound must be
independently tunable.

### 7.2 Ambient beds

MW (canonical minimal design): every exterior cell belongs to a Region holding
`[(soundId, chance 0–100)]`; **every 10 s** roll the list, play first success as a 2D
one-shot. That's the whole system. Each weather type owns a looping ambient, crossfaded
over the ~15 s weather transition; interiors get one loop per cell. OB adds weather
condition flags + time-of-day windows per entry, and per-object looping emitters
(waterfalls, fires). Steal: region ambient = `{loopId per weather, [(oneShot, chancePct,
weatherMask, hourMask)]}`, ticked every 10 s — reads as "alive" for almost no code.

### 7.3 Footsteps

DF: one sound per context class. MW: by boot armor class only, L/R alternation from
animation keyframes. OB: full **surface material (6: dirt/grass/stone/wood/snow/water) ×
armor overlay** matrix, 3–5 random variants each, pitch jitter. **Audio as stealth
input:** OB sneak detection consumes a movement-noise term (running > walking > sneaking,
heavy > light > bare) — footsteps and stealth share one "noise emitted" number; if you
have a detection sim, footsteps should *be* the emission event.

### 7.4 UI sound language (MW's semantic set)

Item handling typed by item class — `Item Weapon Up/Down`, `Item Gold` (coin jingle — the
single highest-value UX decision), `Item Book`, `Item Potion`… you hear what you grabbed
without looking. Generic Menu Click; skill-raise chime; OB splits quest-added/updated/
completed into three stings and plays a dedicated track on level-up. Five distinct spell-
failure sounds per school — failure is audible *and diagnostic*. Rule across all three:
every player-caused state transition gets a sound; passive stat changes get at most a
sting; nothing continuous plays from UI.

### 7.5 Combat audio

Swing whoosh by weapon class; hit sound chosen by **what was struck** (light/medium/heavy
armor of the struck body part, flesh, wall material); block clang *replaces* the hit
sound — the mix itself reports the outcome: whoosh = miss, thud = hit, clang = blocked.
**Combat is fully legible with eyes closed — that is the spec.** Pain barks from race×sex
voice sets; attacker taunts chance-gated with cooldown.

### 7.6 Music systems

DF: contextual playlists, no combat music, hard cuts. MW: two folders — Explore and
Battle — random track, gap, next; battle trigger = any actor in combat with the player.
OB: five folders (Explore/Public/Dungeon/Battle/Special), cell flags pick, combat
overrides; battle music **fades in ~1 s on combat detection — before the first blow, an
early-warning system players rely on** — and out over 4–5 s. Special recognizes exactly
three filenames: title, death, success (level-up). Soule pattern: one motif on the boot
screen only, quoted nowhere in play — hearing it means "the game", not "a place". Steal:
`enum MusicContext {EXPLORE, TOWN, DUNGEON, BATTLE, SPECIAL}` + folder playlists, 1 s
in / 4–5 s out / ~3 s post-combat hold.

### 7.7 Dungeon audio — designed absence

Base state near-silence, so every sound is signal. Random scare one-shots at random 3D
offsets around the player. **Enemies vocalize on detection, usually before line of
sight — enemy audio range > sight range turns audio into the threat radar** (species and
rough range identified by ear; MW cliff-racer screech; OB's "Who's there?" makes the
stealth state machine audible). Underwater = low-pass everything.

### 7.8 Minimal-but-right engine

~24–32 mono 3D voices + 4 dedicated 2D streams; steal the quietest-effective voice;
footsteps lowest priority. Sound record `{buffer, baseVolume, pitchJitter, minDist,
maxDist}`, linear rolloff. For a tile sim: distance in tiles + stereo pan from bearing is
90% of "3D"; add a wall-occlusion low-pass off the tile grid's line-of-sight. Random-
variant banks (2–5 buffers, pick ≠ last, ±5–10% pitch) are non-negotiable — the
difference between retro and cheap. The entire TES audio identity is five lookup tables
(`footstep[material][boot]`, `hit[struck]`, `block[material]`, `itemUpDown[class]`,
`bark[actorType][aiState]`) plus the ambient and music controllers. **The bark hook — AI
state transitions emitting a 3D bark with cooldown + chance — is the cheapest
high-value system on this list given an existing actor sim.**

**Granadad today:** no longer zero, but not wired: `native/audio/` has a real mixer
(`mixer.cpp` — 48 kHz float32 stereo, buses, 48 voices with stealing, constant-power pan,
per-sample gain ramps, tanh soft clip, plus **procedural** water-lap and wind layers since
the vendored Kenney CC0 set has no ambience recordings), a vorbis decoder, `sound_bank.hpp`
(manifest: SoundId → asset file), and `sound_ids.hpp` — a TES-restrained vocabulary
already shaped like §7.4 (footsteps per surface, WadeSplash layered on fluidDepth, a
quiet Ui* family, BookOpen/CoinHandle/DoorOpen diegetics). Strictly client-side; sim can
never observe audio. Gap: `audio_engine.hpp` is named in comments as "a later pass" and
does not exist — nothing in session.cpp or main.cpp plays a sound. Missing: the engine +
SDL device wiring, footstep trigger off the step cadence, the bark hook onto ward_voice,
music/ambient controllers, tile-grid occlusion.

---

## 8. World design & exploration

### 8.1 Vivec: the canton machine

9 repeated ziggurats, each the same 4-tier vertical stack behind hard load doors:
Underworks (sewers — outlaw layer, a mini-dungeon per canton) → Canalworks (storage, cheap
housing — burglary targets) → Waistworks (shops, guilds, taverns — the pedestrian circuit;
bridges connect cantons here) → Plaza (manors, temples — elite, guards concentrate).
**Altitude = class, enforced by content placement, not rules.** One template × N cantons =
huge city mass cheap; differentiation by tier contents and faction ownership, not
geometry. Documented failure: externally identical cantons → navigation misery. The fix
Bethesda didn't ship: one unique silhouette/banner per canton readable at distance.

### 8.2 Imperial City: precinct-and-hub

Ring wall, pie-slice districts behind gates, slum literally extramural (Waterfront).
**One master vertical landmark** at the topological center visible from everywhere —
orientation is free. Per district: central plaza with an identity object, ring street,
short radials — a district's plan fits in memory after one lap. Hard edges do triple
duty: load boundary, guard checkpoint (bounty dialogue at gates), ambience/music swap.
Crossing a gate is an *event*, never a gradient.

### 8.3 Landmark navigation vs map dependence

MW: **no quest compass** — verbal directions in dialogue, copied to the journal; works
because the world supplies the nouns: road signs at every junction, named roads, foyadas
as natural corridors, faction-distinct architecture readable at silhouette range. OB's
compass markers made players stop reading the world; signage content atrophied. If
exploration is the game: verbal route chunks (3–5 steps), signposts, distinct
silhouettes, no target marker. Test = a player who never opens the map can complete the
route; every landmark check must reference an object visible from its decision point.

### 8.4 Thresholds

Cheap TES mechanics for "you are now in a named place": region/cell name popup on
crossing; load-door gates with a stationed guard whose greeting names the place and its
rules; ambience/music swap; **monumental doorframes** (you climb or pass *through*
something, never walk past a line); threshold guard doubles as systemic checkpoint —
bounty, contraband, faction rank, or an *ability* as door key (Telvanni towers require
Levitation).

### 8.5 Secrets density / reward cadence

Seyda Neen's opening 5 minutes is the canonical spec: hidden ring, bowl in a stump, dead
tax collector, wizard falling from the sky, smuggler cave in sight of the dock. Rule: an
interactable oddity within ~30–60 s walk of any point; one story object per screen of
wilderness near roads. **Locked doors as deferred promises**: visible lock level 1–100 on
the crosshair is a bookmark — the player self-schedules the return trip. Every movement
skill needs hand-placed payoffs only it reaches (jumpable Balmora roofs with cached loot,
levitation routes to Plaza tiers). Ownership flags make *looking* legal and *taking* a
crime — the find is free, monetizing it is a heist. Budget per city district: 2–3
come-back-later doors, 1 vertical route, 1 basement/underlayer entrance, 3–5 micro-finds,
1 quest-hook object.

### 8.6 Day/night as schedule state

Business hours as hard gates: same interior flips from shop to burglary dungeon by clock.
Rank-gated after-hours access as a progression reward. Gate curfew (arrive late, you're
outside the walls). Night = empty streets = **no crime witnesses**; NPC asleep in bed
enables pickpocket/murder; tailing a target's daily loop *is* the investigation verb
(multiple OB quests are literally that). Spawn-table swap by hour: same spawn point,
time-keyed table.

### 8.7 Fast travel — networks, not teleports

MW runs 6 overlapping node networks, none reaching a dungeon, all NPC-vended from fixed
nodes — the last mile is always on foot, first-traversal never bypassable: silt strider
(~9 inland towns, cost `distance / fTravelMult(4000)`, disposition-adjusted, hours pass),
boats (coastal), Guild Guide (guild halls only, instant — **joining a faction upgrades
your transit map**), Intervention spells (eject-to-civilization, not go-anywhere),
Mark/Recall (ONE anchor slot — placement is a real decision), Propylon ring (keyed by a
collectathon). DF travel map: pay gold ∝ distance, choose cautious/reckless × foot/ship ×
inns/camp; time always passes, so timed quests make the triangle real; one modal screen,
deep choice. **OB's regression (avoid):** free instant travel to any discovered icon +
cities pre-unlocked → the road network stops being seen. If map travel ships: nodes
earned by arrival on foot, cost gold + time, never deliver past the settlement gate.

### 8.8 Underlayers

Vivec: a sewer mini-dungeon inside every canton (~1 underlayer : 1 district). IC sewers:
one connected network under all districts — a second traversal graph whose edges bypass
gates and guards, home to the crime factions. A town of ~30 doors hides 2–4 that go
*down*. Rules: entrances visible early, gated by lock/skill/faction not pixel-hunt; every
underlayer connects ≥2 surface points (a route, not a pit); underlayer ownership belongs
to the local crime layer, so using it is a social statement.

**Granadad today:** one district, deeply built: `docks.hpp` pins the baked
`docks_surface` world and its three walk bands (quayside z19 / mid-slope z20 / upper
z21 — the §8.1 altitude-stratification idea is already in the terrain), with the spawn
chosen by measurement (#79's 400-stand grid). DOCKS-GAZETTEER.md is the authored
gazetteer canon that outranks all of §8. 661 ward actors keep hours/jobs/beds
(§8.6's substrate); skyrunning + mantling are the vertical-route verbs; `lockpick.hpp`
lock levels are §8.5's deferred promises; `stealth.hpp` light/sound/sightline makes
night mechanically real; `contraband.hpp`/`watch.hpp` give thresholds something to check.
Gaps: no second district and no underlayer yet (undercroft only exists as an audio
comment), no region-name popup or threshold events, no signpost/verbal-route content, no
travel network (irrelevant until district #2 — but when it comes, §8.7's
earn-nodes-by-arrival rule applies), no business-hours door locking (people keep hours;
buildings don't yet refuse you).
