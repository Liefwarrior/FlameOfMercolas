# PROGRESSION-SPEC — use-based skills (Decision 3a)

**Status:** spec-first, pre-implementation. Implements DECISIONS.md row 3a ("Full Morrowind
use-based skills") for the G-track game layer, on top of the F-track sim core.

**Binding constraints inherited:**
- ARCHITECTURE.md §6 determinism rules: integer/fixed-point only (Q8/Q16), no float in any
  state-affecting math; golden masters cover progression (grind loops are goldenable).
- ARCHITECTURE.md §1.1 #16 RNG: every random draw is a named stream through the one
  counter-based `RandomSource`; XP awards themselves are **deterministic** (no RNG) — RNG
  exists only where a success *check* exists, and each check names its stream here.
- DESIGN NORTH STAR: social power is maxed from day one (Wielder lawful immunity, novel
  L2414/L2967). PHYSICAL power starts near zero and grows through exploitable systems, never
  levels. Gabri starts barely able to kill one slum thug (**pure invention** — the novel shows
  no weak Gabri; every weak-start number below is (placeholder / needs-blessing)).
- Canon precedence: novel > WorldBible > nothing; `Lore\*.html` non-canon. Canon cites use
  `(novel L<n>)` per MATERIALS-CANON.md convention. Invented names/numbers carry
  **(placeholder)**.

Cross-references: `COMBAT-SPEC.md` owns hit/damage math, all coefficients, and every
`combat.*` draw name (COMBAT-SPEC §2.4); the hooks in §8 are the shared contract
(reconciled against COMBAT-SPEC in the 2026-07-12 verification pass — no second formula
survives here).
Canon doctrine for the whole system: *"Everyone has 'magic' though without training it will
stay passive… expressed as unnatural intelligence, or in strength, or reflexes"* (novel L98)
— growth-through-use is canon-blessed.

---

## 1. Integer representation (normative)

All progression state is integer. Three units:

| Unit | Meaning | Relation |
|---|---|---|
| **cp** (centipoint) | human-facing XP unit in this doc's tables | 1 use ≈ 100 cp baseline |
| **grain** | stored progress unit | **1 cp = 20 grains** |
| **level** | skill level 0–100 | threshold in grains, below |

Why grains: aptitude rationals {3/4, 1, 5/4, 4} and satiation percentages {100, 80, 60, 40, 25}
are all **exact** in grains — no division ever occurs on the award path:

- `awardGrains = baseCp × satFactor`, where satFactor ∈ {20, 16, 12, 8, 5}
  (= 100%, 80%, 60%, 40%, 25% of 20 grains/cp).
- `thresholdGrains(L) = (L + 1) × 100 × aptNum`, where aptNum ∈ {15, 20, 25, 80}
  (= aptitude ×3/4 Favored, ×1 Trained, ×5/4 Neglected, ×4 The Flame).

State per skill: `level` (int8, 0..100), `progressGrains` (int32, 0 ≤ p < threshold).
On award: `progressGrains += awardGrains` (saturating add at `Integer.MAX_VALUE`; can never
be reached in legal play — largest threshold is 101×100×80 = 808,000 — but the saturation
guard is mandatory, see tests). While `progressGrains ≥ thresholdGrains(level)` and
`level < 100`: subtract threshold, `level++`, emit `SkillLevelledEvent(actorId, skillId,
newLevel)`. Excess carries (multi-level in one award is legal and looped, not recursive).
At level 100 further XP is discarded (no banking).

Total 0→100 cost at Trained: `Σ_{L=0}^{99}(L+1)×100×20 grains = 10,100,000 grains`
(= 505,000 cp). Early levels are cheap, late levels crawl — the demigod curve's shape.

---

## 2. Skill list (18 skills)

Naming: canon terms cited; everything else **(placeholder)**. "Gov" = governing attribute
(§5); "Apt" = Gabri's aptitude (all aptitudes placeholder / needs-blessing).

| # | Skill | Covers | Gov | Apt | Canon anchor |
|---|---|---|---|---|---|
| 1 | **Saxework** (placeholder name; weapon canon) | saxe, knives held & thrown | AGI | Trained | saxe = the press blade (novel L2390); Hart's thrown knives (L3030) |
| 2 | **Bladework** (placeholder) | swords long & short | AGI | Neglected | Blademaster vocabulary, swan stance (L3002) |
| 3 | **Lancework** (placeholder) | lance, spear, javelin | MGT | Neglected | Stahl's Omission / Trojja's Fury forms (L1537) |
| 4 | **Heavy Arms** (placeholder) | axe, mace, club | MGT | Neglected | Trojian house-guard axe (L277); Devin's mace (L2714) |
| 5 | **Repeaterwork** (placeholder) | repeater crossbow | AGI | Trained | repeater crossbow, "two quick clicks" (L2713); "Reman" attribution (inferred — placeholder) |
| 6 | **The Draw** (placeholder) | bows incl. chromatis composite | WIT | Neglected | 160-lb sniper standard (L1245) |
| 7 | **Open Hand** (placeholder) | unarmed strikes, locks, throws | AGI | Trained | army "unarmed specialist"; windpipe strike (L183, L976) |
| 8 | **Shieldwall** (canon term, L2390) | block, shield use | VIG | Neglected | shieldman drill (L2221) |
| 9 | **Harness** (placeholder) | armor familiarity (all slots) | VIG | Trained | layered per-part kit canon (Gundleas, L2209) |
| 10 | **Grit** (placeholder) | toughness: pain, falls, burns | VIG | Trained | pain-teaches; Hordrar's 20-hour endurance flavor (L2213) |
| 11 | **Skylining** (placeholder) | jumps, climbs, rooftop runs | AGI | **Favored** | walkable roofs kept for the Wielder (BLESSING-QUEUE #6) |
| 12 | **Shadow-Wait** (placeholder name; the craft is canon — Hart's sentry kills, L3030–3038 — but the phrase appears nowhere in the novel) | sneak, pickpocket, takedown | AGI | Trained | Hart's sentry craft; "counted out five minutes" (L3037) |
| 13 | **Cracksmanship** (placeholder) | locks, traps | WIT | Trained | urban-dungeon invention; canon [SILENT] |
| 14 | **Kit-Keeping** (placeholder) | repair, maintenance | MGT | Trained | nicked dagger (L186); barbed arrows ruin shields (L2813) |
| 15 | **Mixtures** (canon-adjacent: "phorys mixture", L2908) | venoms, draughts, reagents | WIT | Trained | nighthawk venom (L3025); Revlin's mixtures (L1630) |
| 16 | **Channeling** (canon term — fill-only tier, L1257, L1399) | filling/recharging chromatis & lightstone | WIT | **Favored** | channelers fill the Reman key (L1399) |
| 17 | **Gutter-Ken** (placeholder) | streetwise: rumors, routes, black-market **access to information ONLY** | WIT | **Favored** | slums cloak-up texture (L2410) |
| 18 | **The Flame** | Gabri-unique Source track | — (see §7) | **×4** | Flame of Mercolas feats (L2341, L2710, L2980) |

**North-star guard on #17 (binding):** Gutter-Ken never modifies disposition, deference,
prices-as-respect, or any social outcome — social power is STATIC and maxed. It gates
*information*: rumor quality tiers, fence/map/route unlocks, informant availability. A
Gutter-Ken 0 Gabri is obeyed everywhere but knows nothing about the Darkstreets; that gap is
the skill.

No character classes. Aptitude is the fixed per-skill rational above (Gabri is a fixed
protagonist); companions (Devin) get their own aptitude rows when companion progression lands
(post-MVP, placeholder).

---

## 3. Use-XP model

### 3.1 Base awards (baseCp per qualifying use)

All values (placeholder / needs-blessing) — Morrowind-derived scale (MW 1.0 use = 100 cp).

| Skill | Qualifying use → baseCp |
|---|---|
| Saxework | melee hit landed 100 · thrown-knife hit 120 |
| Bladework | melee hit landed 100 |
| Lancework | melee hit landed 100 · thrown lance/javelin hit 150 |
| Heavy Arms | melee hit landed 110 |
| Repeaterwork | bolt hit on qualifying target 100 · hit at range ≥ 40 tiles 200 |
| The Draw | arrow hit 120 · hit at range ≥ 60 tiles 300 |
| Open Hand | strike landed 90 · lock/throw succeeded 200 |
| Shieldwall | successful block 250 |
| Harness | struck on a covered location (post-1a location roll) 100 |
| Grit | took ≥ 1 post-mitigation damage from blunt/burn 150 · from a fall 300 |
| Skylining | risky leap landed (gap ≥ 2 tiles or drop ≥ 2 z) 150 · climb completed 50 |
| Shadow-Wait | proximity check evaded 25 · pickpocket 200 · silent takedown 400 |
| Cracksmanship | lock picked 200 × lockTier · trap disarmed 300 × trapTier |
| Kit-Keeping | repair restoring ≥ 1 durability 40 |
| Mixtures | successful brew 200 · ingredient property identified (first time) 50 |
| Channeling | recharge action moving ≥ 100 cu into an item 500 · per additional 1,000 cu in one sustained fill 100 |
| Gutter-Ken | new rumor extracted 100 · new location/route learned 150 |
| The Flame | successful channel 300 (§7) |

**Flat-per-success is kept deliberately** (Morrowind K2): the cheapest-qualifying-success spam
is the *discoverable seam*; the economy prices the inputs, satiation floors the rate.

### 3.2 Qualifying-use gates (anti-null-input, all binding)

1. **Weapon skills:** target must be a live, aware-or-ambushed creature that is hostile or
   under a sparring contract. Shooting walls, corpses, or furniture awards 0.
2. **Harness/Grit:** the striking source must be capable of ≥ 1 pre-mitigation damage.
   Feather-tap farming with harmless sources awards 0 — but a real (if weak) thug's real
   blows qualify. This is the sanctioned seam E1 (§4).
3. **Shadow-Wait:** proximity checks require a live observer entity within 10 tiles
   (placeholder) actively able to detect. **No banked time, no AFK accrual** — the Morrowind
   AFK-sneak lesson (dossier A2).
4. **Movement:** no per-second locomotion XP at all — walking into walls teaches nothing
   (Morrowind Athletics lesson A3). Skylining pays only on discrete risky events.
5. **Channeling:** energy must actually move (ledgered through `ChargeCommandBuffer` /
   `ExternalChargeApplied` — ARCHITECTURE §5); the source is real (a fire, a fed link), so
   fuel burns in the sim.

### 3.3 Satiation (diminishing returns that still permit grinding)

Per `(actorId, skillId, contextKey)` an int tier 0–4:

- On each award: apply satFactor by tier — **tier 0 = 20, 1 = 16, 2 = 12, 3 = 8, 4+ = 5**
  (i.e., 100/80/60/40/25%) — then `tier = min(tier + 1, 4)`.
- Decay: tier decreases by 1 per **3,000 ticks** (placeholder; 5 min at 100 ms/tick) with no
  award in that context. Fully resets overnight rest.
- **Floor is 25%, never 0** — grinding is always live, just priced at a quarter rate.
  Variety pays full rate (fresh contextKey ⇒ tier 0).

contextKey per skill (placeholder set): weapon skills → target species id · Shieldwall/
Harness/Grit → attacker entity id · Skylining → tile-region id of the leap · Shadow-Wait →
observer entity id · Cracksmanship → lock/trap instance id (each instance also awards only
once per re-lock) · Kit-Keeping → item id · Mixtures → recipe id · Channeling → item id ·
Gutter-Ken → informant id · The Flame → constant key.

### 3.4 Determinism & RNG streams

XP awards are pure functions of the triggering event — **zero RNG draws**. Success checks
that *precede* awards use named streams via `RandomSource.draw(key, idx)` (ARCHITECTURE
§1.1 #16). The `prog.*` streams below use: systemSalt = stream-name hash, spatialKey =
actorId, drawIndex = per-actor check counter. **The `combat.*` streams are excluded from
this addressing** — they use COMBAT-SPEC §2.4's own scheme (spatialKey =
`(attackerId<<20)|defenderId`, drawIndex = `attackSeq*8 + subDraw`); COMBAT-SPEC owns all
`combat.*` names:

| Stream | Used by |
|---|---|
| `combat.hit` | weapon hit checks (owned by COMBAT-SPEC §2.4/§2.3) |
| `prog.sneakCheck` | Shadow-Wait proximity evasion |
| `prog.pickLock` / `prog.trapDisarm` | Cracksmanship |
| `prog.mixBrew` | Mixtures brew success |
| `prog.flameVigil` | Flame unlock rite (§7) |

Golden-master rule: a scripted grind session (worked example §9.1) is a golden — same seed,
same world, bit-identical skill/attribute state at every tick.

---

## 4. Intended exploit seams (designed, priced, shipped)

Ship-criteria applied (Morrowind dossier distillation): each seam composes ≥ 2 sensible
rules, consumes a market-priced resource, saturates at integer caps, is deterministic under
named draws, and leaves world fingerprints.

**E1 — The Wielder's Bruise** *(needs-blessing)*. Attacking the Wielder is unlawful (novel
L2414) — but hirelings and goaded slum thugs will do it for coin, and only Gabri can farm
being hit safely (no legal consequence attaches to *him*). Composition: Harness/Grit
pain-XP (rule 3.2#2) + lawful immunity + hireling labor market. **Soft limits:** damage is
real (HP, healing costs through the economy), hireling wages scale with what you ask of
them, satiation floors the rate at 25% per attacker, and dead trainers stop swinging.

**E2 — The Rooftop Drop Circuit**. Skylining leap-XP + Grit fall-XP (300 cp) + walkable
roofs (BLESSING-QUEUE #6). Engineer a circuit of survivable drops; every landing that costs
≥ 1 HP pays Grit. **Soft limits:** fall damage grows superlinearly with z-drop (COMBAT-SPEC
§5.3), death is permanent, healing costs, satiation per tile-region forces circuit variety.

**E3 — Recharge Arbitrage**. Channeling XP per recharge (500 cp) + chromatis charge economy
(A4) + refill-from-fire canon (link to a campfire, novel L505). Drain charged trinkets
through a heat-lamp device, refill from the tavern hearth, repeat; or fill items for resale
— gold and physical power interconvert and the Wielder is the best-placed arbitrageur.
**Soft limits:** the hearth burns real fuel (wood is market-priced; buying out firewood
moves `PriceModel`), satiation per item id, `maxSafeDischargePerTick` makes hasty draining
shatter the item (ARCHITECTURE §11 — the exploit has teeth).

Anti-ship rules (equally binding): no stat-read glitches (trainer prices read BASE skill,
§6), no UI-arcana exploits, no zero-input accrual (gates 3.2#3–4).

---

## 5. Attributes & derived stats

**Recommendation (binding for MVP): drop Morrowind's level-up multiplier minigame
entirely.** Justification: it is the community's #1-hated bookkeeping chore (NCGD/GCD
precedent — dossier §7.6); it rewards spreadsheet play *in menus*, and the north star wants
exploits to live *in the simulated world*. There are **no character levels** at all — no
level-up screen, no rest-to-level, no multipliers to park.

Attributes are a pure integer function of skills, recomputed on every `SkillLevelledEvent`
(same game-layer tick, deterministic order by attributeId):

`attr = 10 + ((Σ skill_i × w_i) >> 8)` — the shift is parenthesized deliberately: in Java,
unparenthesized `10 + Σ >> 8` would parse as `(10 + Σ) >> 8`. Weights per attribute sum to
128 (unit-tested invariant) ⇒ range 10..60.

| Attribute | Weights (placeholder) |
|---|---|
| **Might (MGT)** | Heavy Arms 40 · Lancework 32 · Kit-Keeping 24 · Bladework 16 · Grit 16 |
| **Agility (AGI)** | Skylining 36 · Shadow-Wait 28 · Saxework 24 · Open Hand 20 · Repeaterwork 20 |
| **Vigor (VIG)** | Grit 48 · Harness 32 · Shieldwall 28 · Skylining 20 |
| **Wits (WIT)** | Mixtures 28 · Channeling 28 · Cracksmanship 24 · Gutter-Ken 24 · The Draw 24 |
| **Presence (PRS)** | **STATIC 100.** Not derived, not trainable, not lowerable. This *is* the north star: the Wielder's social weight is a constant of the world. |

Derived stats (recomputed live on any attribute change — never banked, killing the
rush-Endurance meta; all coefficients placeholder):

- `maxHP = 25 + 3×VIG + Grit` (on change, `currentHP += max(0, delta)`)
- `maxStamina = 40 + 2×VIG + 2×AGI`
- `encumbranceCapDeciStone = 300 + 20×MGT` (canon flavor: snipers carry 80-lb packs "like ten", L1245)

Trainer cap rule (§6) reads the governing attribute; checks always read **current effective
values** — temporary fortifications legitimately raise caps (Morrowind A5: consistent rules
that produce exploits beat special cases that prevent them).

---

## 6. Trainers & books (canon-plausible accelerators)

**Trainers** — direct level purchase, 2 in-game hours per level:

- Price per level: `priceCp = baseSkillLevel × 1000 (placeholder trainMod) × socialPipe`,
  where socialPipe is the standard social price pipe (disposition/faction), **floored at
  25%**. Computed from **BASE skill, never temporarily-modified skill** — the vanilla-MW
  Drain-and-Train stat-read glitch is explicitly not shipped (OpenMW rule). The Wielder's
  static deference legally drives most trainers to the 25% floor: *cheap training everywhere
  is Gabri's sanctioned drain-and-train* — same fantasy, diegetic cause.
- Caps: trainer cannot raise a skill above the trainer's own level, nor above
  `2 × governing attribute` (placeholder ratio). Books and use ignore both caps.
- Canon roster (placeholder assignments): **Monastery of the Divine Light** (raised Devin,
  novel L2406) — Grit, Open Hand, The Flame's rite (§7). **Traveling Blademaster journeymen**
  (they pass through, L983) — Bladework, Shieldwall; rare, expensive, capped high.
  **Runemasters** (graduating-thesis scholars, L2479) — Channeling, Mixtures.
  **Darkstreets fixers** (placeholder) — Cracksmanship, Gutter-Ken, Shadow-Wait.

**Drunair meditation** (canon: Alir/relar discipline, "control the Alir and focus the
relar", L1899; melar tutelage L2013–2061): not skill points — a **Focus** state. After a
successful 1-hour meditation, the next qualifying-use window (6 in-game hours, placeholder)
treats the satiation floor as **50% instead of 25%** for ONE chosen skill. Meditation is the
grind-efficiency accelerator; it stacks with, and rewards, deliberate grinding.

**Books:** +1 level, once per title, ignores all caps, ~3 titles per skill (placeholder),
placed as exploration/black-market loot. Example titles (all placeholder): *On the Swan
Stance* (Bladework; stance canon L3002), *Stahl's Omission, Annotated* (Lancework, form
canon L1537), *The Channeler's Primer* (Channeling), *Counted Minutes* (Shadow-Wait; title from Hart's
"counted out five minutes", L3037).

---

## 7. The Flame track (Gabri-unique)

Canon shape: the Flame answers discipline and faith ("trust the Flame", L2717), dies under
emotional shock (L2729), and big workings exhaust (the blood-pact purge ends with both on
their knees, L2744–47). The empty-mind rule is Eric's too (L2952). Weak-start Gabri and any
Flame "levels" are **pure invention (needs-blessing)**.

- **Locked at start** (level 0, cannot gain XP, no abilities). Unlock rite **"Vigil of the
  Empty Mind"** (placeholder): prerequisites Grit ≥ 10 (placeholder); then overnight vigils
  at the Divine Light chapel. Each vigil: `pass ⟺ draw16 < 16384 + 983×Grit + 6553×calm`,
  clamped ≤ 58,982 (90%), where `draw16 = rng("prog.flameVigil", actorId, vigilOrdinal) &
  0xFFFF` and `calm = 1` iff no damage taken that day. **Three total passes** (not
  consecutive) → `FlameUnlockedEvent`, Flame = 1.
- **Aptitude ×4** (aptNum 80): threshold `(L+1) × 8,000 grains`. The whole track costs 4×
  a normal skill — the demigod curve's expensive spine.
- **XP:** successful channel = 300 cp, **only while calm** (no damage taken in the last 600
  ticks, placeholder — the empty-mind rule as mechanics); each channel costs stamina (below).
  Satiation contextKey is constant: the Flame always grinds at the floor after 4 casts per
  window — practice is deliberate, slow, monastic.
- **Ability grants** (names placeholder; feats canon-anchored): 1 **Candleflame** — ignite an
  adjacent flammable tile (routes an `ExternalIgnition` SimCommand; remote-ignition lineage
  L2980); stamina 15. 5 **Lightpalm** — blinding flash, AoE stagger (L2341); stamina 25.
  10 **Flameblade** — coat weapon, +fire damage, cool to allies (L2710, L2718); stamina 20 +
  upkeep. 15 **Emberward** — burn a door/barrier (L2718); 25 **The Declaration** — fear pulse
  vs. dark creatures (routing of L2344); 40+ reserved (rainbow-flame tier is endgame,
  L2723–25). Every ability keeps the calm gate: taking a hard emotional/damage shock while
  channeling snuffs the effect (L2729).

---

## 8. COMBAT-SPEC hooks (shared contract)

COMBAT-SPEC owns final coefficients and all `combat.*` draw names (COMBAT-SPEC §2.4); this
section states how skills feed those formulas. Placeholder markers as noted.

- **Hit chance is COMBAT-SPEC §2.3 verbatim** (no second formula in this doc):
  `hitQ16 = clamp(32768 + (ACC − DEF) × 328, 3277, 62259)`; roll `combat.hit` (16-bit)
  < hitQ16 ⇒ hit. Weapon skill feeds ACC 1:1: `ACC = weaponSkill + weapon.accBonus +
  situational` — so each skill level ≈ +0.5% hit.
- **Evasion (owned here):** `evasion = max(0, AGI − Σ armorBulk)` — integer; `armorBulk`
  per equipped piece (placeholder: cloth/leather/cloak 0, oak 1, steel 1, chromatis 0 —
  needs-blessing). COMBAT-SPEC §2.2's `DEF = evasion + AC_agg` is the single defense shape;
  there is no separate "defRating". (COMBAT-SPEC §6's example evasion values are direct
  placeholder inputs to incomplete statlines.)
- Weapon skill tier grants modify action economy/damage (per-skill tables §8.1).
- Fall damage is COMBAT-SPEC §5.3: `fallDmg = max(0, 2×drop² − fallMitigation)`,
  `fallMitigation = (Grit + Skylining) >> 2` (placeholder) — the E2 price curve.
- Harness: armor mitigation `MIT_final = MIT_eff × (1000 + 2×Harness) / 1000` and wear
  `max(1, wear_base × (200 − Harness) / 200)` — both formulas live in COMBAT-SPEC §4.1.

### 8.1 Full effect tables (4 skills; pattern for the rest)

Pattern for all skills: **linear main term** (each level feeds the skill's hook above) +
**technique tiers at 25/50/75/100** (graded technique tiers are canon — Trojja's Fury's
optional finisher that "many who practiced left out", L1537). Technique names placeholder.

**Repeaterwork** — main term: +1 ACC per level (skill feeds ACC 1:1, COMBAT-SPEC §2.3;
≈ +0.5% hit per level).

| Lv | Grant |
|---|---|
| 25 | **Second Click** — second bolt per attack action at **−20 ACC** (≈ −6,560 Q16 ≈ −10% hit; placeholder) (canon "two quick clicks", L2713) |
| 50 | **Cartridge Hands** — reload a spent cartridge in 1 action instead of 3 (canon: reload so slow Revlin discards the weapon, L2908) |
| 75 | **Eye-Level Nock** — no move penalty while aimed (L2703) |
| 100 | **Bolt-After-Bolt** — third bolt vs. adjacent-horde targets (L2714) |

**Harness** — main term: mitigation `MIT_final = MIT_eff×(1000+2L)/1000`; armor wear
`max(1, wear_base×(200−L)/200)` — integer formulas normative in COMBAT-SPEC §4.1.

| Lv | Grant |
|---|---|
| 25 | **Settled Straps** — armor move/stamina penalty −25% |
| 50 | **Second Skin** — sleep/sneak in armor without penalty |
| 75 | **Read the Blow** — on an unblocked covered-location hit, 10% chance (draw `combat.readblow`, dedicated subDraw 6 in COMBAT-SPEC §2.4) to shift the hit to an adjacent better-armored location |
| 100 | **Iron Habit** — bare-location weak-spot penalty halved |

**Shadow-Wait** — main term: +5‰ per level to evade detection (contested vs. observer).

| Lv | Grant |
|---|---|
| 25 | **Still Breath** — stationary bonus doubled |
| 50 | **Cloak-Trick** — cloaked movement in crowds unhindered (slums cloak-up canon, L2410) |
| 75 | **Counted Minutes** — after 5 undetected minutes adjacent to a sleeper, takedown auto-succeeds (Hart's method, L3030–38) |
| 100 | **The Sleeper's Throat** — silent kill leaves no witness alert for 300 ticks |

**The Flame** — main term: channel magnitude cap = L × 40 cu/action (placeholder; routed
through `ChargeCommandBuffer`); ability grants at 1/5/10/15/25/40 per §7.

---

## 9. Worked examples (exact arithmetic)

### 9.1 A week of deliberate crossbow grinding

Setup (all placeholder): Gabri, Repeaterwork **5** (progress 0), Trained (aptNum 20). He
buys a worn repeater (1,200 cp coin) and 200 bolts (5 cp each), and spends 7 days culling
rats in the Undervault (live hostile vermin = qualifying targets; the warden waves the
Wielder through, no fee — social power texture). Sustained sessions: **240 qualifying hits
per day**; contextKey (rat species) never changes, so after the 4-award ladder each day sits
at the 25% floor; overnight rest resets satiation.

Daily XP in grains: `100×(20+16+12+8) + 236×100×5 = 5,600 + 118,000 = 123,600 grains`
(= 6,180 cp). Week: `7 × 123,600 = 865,200 grains` (= 43,260 cp).

Levels: cumulative cost from 5 up to level L is `2,000 × Σ_{k=6}^{L} k` grains.
- to 29: `2,000 × 420 = 840,000` ✓ (≤ 865,200)
- to 30: `2,000 × 450 = 900,000` ✗

**Result: Repeaterwork 5 → 29**, with 25,200 grains banked toward 30 (needs 60,000).
Crossed the 25 tier: **Second Click** unlocked on day 6
(cost to 25: 2,000×Σ_{6..25} = 2,000×310 = 620,000 ≤ 5×123,600 = 618,000? No — 618,000 <
620,000, so 25 lands 1 hit into day 6).

Hit-chance delta via COMBAT-SPEC §2.3 vs. a slum thug (DEF 8, placeholder; repeater
accBonus +0, so ACC = skill):
- before: `hitQ16 = 32768 + (5 − 8)×328 = `**31,784** (48.5%)
- after: `hitQ16 = 32768 + (29 − 8)×328 = `**39,656** (60.5%)
- **+12.0 points absolute, +24.8% relative**, plus Second Click's second bolt at ACC 9
  (29 − 20) → hitQ16 = 33,096 (50.5%) — expected bolts-on-target per action goes
  0.485 → 0.605 + 0.505 = **1.110 (×2.29)**. One week of honest grind more than doubles
  ranged output: the curve is steep at the bottom by design.

Ledger: 240 hits/day at the week's average hit rate (~54.5%, rising 48.5%→60.5%) means
~440 shots/day ≈ **3,080 shots/week**; ~1,400 misses, of which ~30% unrecovered
(placeholder recovery rules) ≈ **420 bolts lost = 2,100 cp** — the starting 200 bolts
recirculate but cannot cover attrition, so **mid-week restocking is an explicit part of the
exploit's price** — plus weapon wear (Kit-Keeping seam).
Attribute side-effect: AGI weight 20/128 × 24 levels ⇒ AGI +1..2 (exact value depends on
other skills; deterministic).

Determinism note: with a fixed seed, fixed script, this entire week is a golden master —
`grindWeek_goldenMaster` (§10) asserts the exact end state (level 29, 25,200 grains).

### 9.2 First Flame skill unlock

State: fresh Gabri, Grit 4. The Flame is locked; the monks name the price: the Vigil needs
a body that has *known* hardship (Grit ≥ 10).

1. **Getting Grit 4 → 10** via seam E2 (rooftop drop circuit, 2-z drops, ~4 HP each):
   fall award 300 cp; cost 4→10 = `2,000×Σ_{5..10}k = 2,000×45 = 90,000 grains` (Grit is
   Trained). **Pacing determines the fall count.** All-out pace with two alternating
   circuits (two contextKeys, no time for tier decay): the two 4-award ladders pay
   2×16,800 = 33,600 grains in the first 8 falls, then the 25% floor (1,500 grains/fall)
   → (90,000−33,600)/1,500 ≈ 38 more → **≈ 46 falls**. Patient pace (≥ 3,000 ticks between
   visits to each circuit, so tiers decay and the average factor holds near 12 grains/cp
   = 3,600 grains/fall) → **≈ 25 falls**. Floor-only worst case ≤ 60. Call it 25–46 falls
   ≈ 100–190 HP of healing bought over ~3 days. The exploit works and the ledger shows
   what it cost.
2. **Three vigil passes**: at Grit 10, calm days: threshold `16384 + 983×10 + 6553 =
   32,767` of 65,536 → 49.99% per night. Deterministic draws `prog.flameVigil(actorId, n)`;
   expected ~6 nights for 3 passes (exact nights are seed-determined and golden-testable).
3. **Flame 1 → 2** (first practice evening, threshold `2×100×80 = 16,000 grains`, awards
   300 cp on the constant-key ladder): channels yield 6,000 / 4,800 / 3,600 / 2,400 grains —
   cumulative 6,000 / 10,800 / 14,400 / **16,800 ≥ 16,000 on the 4th channel** →
   **Flame 2**, 800 grains carried. Four Candleflame ignitions, 60 stamina, one evening,
   all while calm.

Total path: ~3 days of priced self-harm + ~6 nights of rite + one evening of practice.
The demigod spine starts as a candle.

---

## 10. Unit-test list (all named, minimum assertions given)

Determinism/overflow tests are golden-gating; run under `-ea` in CI from the first G
milestone.

1. `awardGrains_exactFactors_noDivision` — for every satFactor {20,16,12,8,5} × every baseCp
   in §3.1: awardGrains == baseCp×factor, computed with no division/rounding.
2. `threshold_matchesFormula_allAptitudes` — thresholdGrains(L, apt) == (L+1)×100×aptNum for
   L 0..100 × aptNum {15,20,25,80}.
3. `levelUp_excessCarries_multiLevel` — one 50,000-grain award at level 0 (Trained) lands
   exactly level 6 with 8,000 grains remaining (thresholds 2k+4k+6k+8k+10k+12k = 42,000
   consumed; 14,000 needed for 7).
4. `skill_capsAt100_discardsOverflow` — awards at 100 change nothing, no event emitted.
5. `progress_saturatingAdd_neverWraps` — award Integer.MAX_VALUE grains twice: progress
   saturates, level-loop terminates, no negative values (anti-overflow).
6. `satiation_floorIsFive_neverZero` — 10,000 consecutive same-context awards all pay
   ≥ baseCp×5 grains.
7. `satiation_tierDecay_deterministic` — tier 4, idle exactly 3,000 ticks ⇒ tier 3; 2,999 ⇒
   still 4.
8. `satiation_contextKeysIsolated` — awards in context A never move context B's tier.
9. `xpAward_noRngDraws` — instrumented RandomSource: a full grind day performs zero draws
   from any `prog.*` stream except declared checks.
10. `rngStreams_namedAndStable` — `prog.flameVigil` draw(actor, n) equals the value after
    save/load at TICK_END (ARCHITECTURE §6 save-equivalence).
11. `armorXp_requiresRealAttacker` — 0-damage-capable source ⇒ no Harness/Grit award; 1-damage
    thug ⇒ award (seam E1's gate).
12. `sneakXp_requiresLiveObserver` — no observer in 10 tiles ⇒ no award; no time-banking
    across observer absence.
13. `movementXp_noPerTickAccrual` — 10,000 ticks of walking ⇒ Skylining unchanged.
14. `trainer_priceReadsBaseSkill` — temporary skill modifiers (up or down) never change
    price or eligibility (anti Drain-and-Train).
15. `trainer_capsByTrainerAndAttribute` — cannot buy past trainer level nor 2×governing
    attribute; a temporary attribute fortification raises the cap (A5 rule).
16. `book_awardsOncePerTitle` — second read of same title ⇒ no level.
17. `attributes_pureFunctionOfSkills` — same skill vector ⇒ same attributes, independent of
    level-up order/history (recompute is stateless).
18. `derivedStats_liveRecompute_notBanked` — raising Grit late yields identical maxHP to
    raising it early (kills rush-Endurance meta).
19. `presence_immutable` — no code path mutates PRS from 100.
20. `flame_lockedGainsNothing` — pre-rite channels award 0 XP and emit no events.
21. `flameVigil_threeTotalPasses_unlocks` — scripted seed: exact pass nights match golden;
    unlock event fires on the 3rd pass only.
22. `grindWeek_goldenMaster` — §9.1 script from fixed seed ⇒ exactly level 29,
    progress 25,200 grains, Second Click unlocked, at every tick hash-chain-identical
    across two engines in one JVM (M0 twin-run pattern).
23. `flameFirstEvening_goldenMaster` — §9.2 step 3 ⇒ Flame 2, 800 grains carried, 60
    stamina spent.
24. `hitHook_matchesCombatSpec` — feeding weaponSkill through `ACC = skill + accBonus`
    into COMBAT-SPEC §2.3 reproduces §9.1 exactly: skill 5 vs DEF 8 ⇒ hitQ16 31,784;
    skill 29 ⇒ 39,656; Second Click's ACC 9 ⇒ 33,096 (single formula, no permille path).
25. `meditation_focusWindow_fiftyPercentFloor` — after a successful Drunair meditation,
    the ONE chosen skill's satiation factor floors at 10 (50%) instead of 5 for the
    6-in-game-hour window; other skills stay floored at 5; window expiry restores 5.
26. `attributeWeights_sumTo128_invariant` — every attribute's §5 weight row sums to
    exactly 128 (static content test; PRS excluded — it has no weights).
27. `flame_calmGate_600Ticks` — channel with damage taken 599 ticks ago ⇒ 0 XP, no
    event; exactly 600 ticks ago ⇒ full award (boundary is ≥ 600, unit-pinned).
28. `book_ignoresTrainerAndAttributeCaps` — a book read at skill == trainer cap and ==
    2×governing-attribute cap still grants +1 (test 16 covers once-per-title; this pins
    cap-ignorance).

---

## 11. Open blessing items (queue candidates)

1. All 18 skill names and aptitude assignments (§2) — placeholder set.
2. Weak-start Gabri numbers (skills ~0–5, Flame locked) — north-star invention, zero canon.
3. Seam E1 "Wielder's Bruise" — is goading thugs to strike the Wielder tonally acceptable?
4. Flame unlock rite shape (Grit gate + vigils) and ability ladder names (§7).
5. Satiation constants (3,000-tick decay, 25% floor, 4-tier ladder).
6. Trainer trainMod 1,000 cp, 25% social floor, 2×attribute cap ratio.
7. Gutter-Ken scope fence (information only) — confirm it reads as intended in play.
8. Evasion shape `evasion = max(0, AGI − Σ armorBulk)` and the per-material armorBulk
   table (§8) — invented to make COMBAT-SPEC's DEF inputs derivable; placeholder.
