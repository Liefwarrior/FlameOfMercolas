# COMBAT-SPEC — Decision 1a: Hybrid per-part defense + core combat math

**Status:** spec-first (per DECISIONS.md 1a: "Spec with worked examples + unit tests BEFORE implementation").
**Owner track:** gameplay specs (`docs/design/`). Implementation lands in the G-milestones, on top of the F-core.
**Binding constraints (ARCHITECTURE.md §6):** integer/fixed-point math only (Q8 = x/256, Q16 = x/65536); no float/double anywhere in combat state or resolution; every random decision is a **named deterministic draw** through the counter-based `RandomSource` (§1.1 #16). Golden-master tests cover combat; identical inputs must produce byte-identical outcomes on all platforms.
**Canon convention:** citations are `(novel L<line>)` into `LordOfTrojia-MVP\Lore\Lord of Trojia (indexable).txt`. Every invented number/name is marked **(placeholder)**. The weak-start Gabri capability curve is itself pure invention (canon Gabri is top-tier in his first scene) — the whole "starting Gabri" statline carries a standing **needs-blessing** flag.

North-star reminder: social power is maxed from tick 0 (Wielder lawful immunity, novel L2414, L2967); PHYSICAL power starts near zero. This spec is the arithmetic floor Gabri crawls up from. The math must be honest enough to be gamed (pain-teaches armor training, chromatis kit arbitrage — seams cross-referenced to PROGRESSION-SPEC.md).

---

## 1. Slot model

Nine equipment slots, **one piece per slot** in v1 (no layering; the canon layered kit — jerkin under chainmail under plate, novel L2209 — is deferred; see §9 Deferred).

### 1.1 Coverage weight table `COV_Q8`

Coverage weights are Q8 shares of the whole body silhouette. **They sum to exactly 256** (static invariant, unit-tested). Shield has coverage 0 — it defends by *block*, not by being worn over a body part (§3). Jewelry has coverage 0 — it is a hook slot, not armor.

| Slot | COV_Q8 | ≈% | Notes / hooks |
|---|---:|---:|---|
| head | 32 | 12.5% | helm slot |
| torso | 96 | 37.5% | largest target (canon: chest shots dominate the wound catalog) |
| arms | 40 | 15.6% | sleeves/pauldrons as one piece v1 |
| hands | 12 | 4.7% | gauntlets (canon: Adelia's gauntlet kill, novel L2991) |
| legs | 48 | 18.75% | greaves/trousers |
| feet | 16 | 6.25% | boots (canon: hobnailed boots, novel L2209) |
| cloak | 12 | 4.7% | LOW coverage by design. Hooks: identity/faction display (Gabri's white Wielder cloak, novel L2969; slum-cloaking beat L2410), concealment bonus on the exploration screen (out of combat scope), weather. |
| jewelry | 0 | 0% | ZERO coverage. Hooks: chromatis charge substrate (ring/amulet as portable `chargeable` item — ties to ChargeSystem, ARCHITECTURE §1.1 #14), social-recognition token. Never mitigates, never wears from combat. |
| shield | 0 | 0% | block mechanic (§3), not coverage |
| **Σ** | **256** | 100% | |

All weights **(placeholder)** — canon gives no coverage percentages (dossier §10).

### 1.2 Piece data

Each equipped piece is an item instance:

- `materialId` — **must be a `MaterialRegistry` id from `content/raws/materials/`**. Mitigation and wear read `hardness` from the registry (steel 7, chromatis 8, oak 3, granite 6 per shipped raws). Gear needs two NEW raws: `leather` (hardness 2 **(placeholder)**) and `cloth` (hardness 0 **(placeholder)**) — canon has leather armor everywhere (novel L186, L2208) but leather was deliberately left out of the v0 tile vocabulary (MATERIALS-CANON §3). **Raws addition needs-blessing.**
- `quality` Q ∈ 0..4: 0 crude, 1 common, 2 fine, 3 masterwork, 4 legendary **(placeholder scale)**.
- `maxDurability`, `curDurability` — integers. Caps by material **(placeholder)**: cloth 40, leather 60, oak 90, steel 240, chromatis ∞ (no-wear, §4.3).

---

## 2. Aggregate AC and the hit roll

### 2.1 Per-piece deflect rating

```
condQ8   = condition band factor (§4.2)
D_piece  = ((hardness*4 + quality*2) * condQ8) >> 8        // integer, Q8 rounding-down
```

Examples: pristine common leather D = (8+2)·256>>8 = **10**; pristine fine steel helm D = (28+4) = **32**; masterwork steel breastplate D = (28+6) = **34**; legendary chromatis plate D = (32+8) = **40**.

### 2.2 Aggregate AC

```
AC_agg = ( Σ over slots  COV_Q8[slot] * D_piece(slot) ) >> 8      // bare slot contributes 0
DEF    = evasion + AC_agg
```

- **Unarmored baseline:** AC_agg = 0; DEF = evasion alone. Evasion is attribute-derived, owned by PROGRESSION-SPEC §8: `evasion = max(0, AGI − Σ armorBulk)` (integer; treated as an input here). `DEF = evasion + AC_agg` is the single defense shape in both specs.
- Shield contributes **nothing** to AC_agg (coverage 0). Its entire defensive value is the block roll (§3). This is the hybrid split: worn coverage makes you harder to *wound*, the shield is an active *interception*.
- BROKEN pieces (durability 0) contribute 0 — the slot is bare (§4.2).

### 2.3 Hit chance (bounded 5%–95%)

```
ACC      = attackerWeaponSkill + weapon.accBonus + situational
hitQ16   = clamp( 32768 + (ACC - DEF) * 328 ,  3277 , 62259 )
```

- 328 ≈ 0.5% per point of (ACC−DEF) in Q16 (**rounded** from 65536/200 = 327.68; the integer constant 328 ≈ 0.5005%/pt is normative).
- Bounds: 3277 = ceil(5%·65536), 62259 = floor(95%·65536). **No attack is ever a sure thing or a lost cause** — this keeps the pain-teaches training seam (PROGRESSION-SPEC seam E1) alive at every gear gap.
- Roll: `r16 = draw16(combat.hit)`; **HIT iff `r16 < hitQ16`** (strict; equality = miss — boundary unit-tested).

### 2.4 Named draws — the combat draw schedule (owns ALL combat-layer draw names)

Per ARCHITECTURE §1.1 #16, one counter-based `RandomSource`: `mix64(worldSeed + K1*tick) ^ SALT_COMBAT + spatialKey + drawIndex`. This section is the single registry of combat-layer draw names; PROGRESSION-SPEC and COMBAT-SCREEN-SPEC consume these names verbatim. **One purpose per draw slot — never a "sub-band" of an existing draw.**

**Attack-scoped draws** — `spatialKey = (attackerActorId << 20) | defenderActorId` (actor ids < 2^20 **(placeholder width)**); `drawIndex = attackSeq * 8 + subDraw` — **absolute sub-indices**, so a *skipped* draw (no shield → no block roll) never shifts later draws. Determinism holds across code paths; golden twins are cheap.

| Named draw | subDraw | Consumed when | Extracted bits |
|---|---:|---|---|
| `combat.hit` | 0 | every attack | low 16 (`r16`) |
| `combat.block` | 1 | defender has usable shield (§3) | low 16 |
| `combat.location` | 2 | hit landed and was not blocked (skipped entirely on aimed attacks, COMBAT-SCREEN-SPEC §4.4) | low 8 (`r8`) |
| `combat.damage` | 3 | hit landed (blocked or not) | low 16, reduced by span modulo |
| `combat.weakspot` | 4 | struck location is bare | low 16 |
| `combat.logline` | 5 | every resolved attack — presentation-only log-line pick (COMBAT-SCREEN-SPEC §2.2); no game state ever reads it | low 16 |
| `combat.readblow` | 6 | defender has Read the Blow (Harness 75 grant, PROGRESSION-SPEC §8.1) and an unblocked hit landed on a covered location | low 16 |
| *reserved* | 7 | crit seam / Flame rider / poison (nighthawk venom, novel L3025) — future | — |

**Encounter-scoped draws** — consumed by the combat screen (COMBAT-SCREEN-SPEC), not per attack: `spatialKey = (encounterId << 20) | combatantId` (**placeholder width**); `drawIndex = round * 4 + p` with purpose ordinal `p` per row (offsets keep the four purposes collision-free under the shared `SALT_COMBAT`):

| Named draw | p | Consumed when |
|---|---:|---|
| `combat.init` | 0 | once per combatant at combat start (initiative; round = 0) |
| `combat.flee` | 1 | engaged flee attempt that round (COMBAT-SCREEN-SPEC §4.5) |
| `combat.morale` | 2 | morale check at that combatant's turn start (COMBAT-SCREEN-SPEC §4.8) |
| `combat.aiTarget` | 3 | enemy policy tie-break among equal targets (COMBAT-SCREEN-SPEC §4.8) |

---

## 3. Shield block

Canon: shieldman doctrine (novel L2390), shield roof vs volleys and barbed-arrow degradation (L2813), shield bash stagger (L561). Blademasters fight shieldless (L2220) — shields are a choice, not a default.

**Eligibility:** defender has a non-BROKEN shield equipped, is aware of the attacker (not surprised/asleep — Hart's canon kills, L3030), and the attack comes from the front 180° arc. Facing rules are owned by COMBAT-SCREEN-SPEC.

```
blockQ16 = min( 6554 + shieldSkill * 328 , 32768 )      // 10% base + 0.5%/skill, hard cap 50%
```

Roll `combat.block`; **BLOCKED iff `r16 < blockQ16`**.

On block:
- **No location roll** (the shield intercepted before the body). `combat.location` draw is not consumed (safe — absolute indices).
- Damage still rolls (`combat.damage`): `final = max(rawDmg - MIT_shield_eff, 0)` — a clean block can zero the damage entirely (**no chip minimum through a block** — this is the one full-negation path in the system, and the reason shields matter despite AC_agg 0).
- Shield takes **double wear**: `wear_shield = 2 * wearFormula(rawDmg, shieldHardness)` (§4.1). Canon fingerprint: shields degrade fast under sustained fire (L2813). Shield at 0 durability = BROKEN, useless until repaired.

All block constants **(placeholder)**.

---

## 4. Hit location, mitigation, wear

### 4.1 Location table `LOC_Q8` and mitigation

On an unblocked hit, roll `combat.location` (`r8` ∈ 0..255) against cumulative bands. **Weights sum to exactly 256** (unit-tested):

| Location | LOC_Q8 | r8 band | Weak-spot rider if bare (§4.4) |
|---|---:|---|---|
| head | 24 | 0–23 | DAZE (lose next action) — temple KO canon (novel L1165, L172) |
| torso | 100 | 24–123 | BLEED (1 dmg/round, 3 rounds **(placeholder)**) — gut/heart wounds (L563, L561) |
| arms | 44 | 124–167 | FUMBLE (drop held weapon) — severed sword-arm tendons end fights (L2697) |
| hands | 12 | 168–179 | DISARM — weapon knocked away |
| legs | 56 | 180–235 | KNOCKDOWN (prone) — knee kick topples (L169) |
| feet | 20 | 236–255 | SLOWED (half move next round) |

All weights/riders **(placeholder magnitudes, canon-anchored directions)**.

**The struck piece — and only that piece — mitigates and wears:**

```
MIT_piece = ((hardness*2 + quality) * condQ8) >> 8      // from MaterialRegistry hardness
MIT_eff   = max(0, MIT_piece - weapon.apPoints)          // armor piercing, floor 0
MIT_final = (MIT_eff * (1000 + 2*defenderHarness)) / 1000  // Harness main term (PROGRESSION-SPEC §8.1); integer division; Harness 0..100 ⇒ ×1.000..×1.200
```

Reference MIT (pristine, Harness 0): common leather **5** · fine steel **16** · masterwork steel plate **17** · oak shield (common) **7** · legendary chromatis **20**. **All §6 worked examples and §8 test values take defenderHarness = 0** (placeholder statlines), so MIT_final = MIT_eff throughout.

```
wear_base = max(1, (rawDmg * 4) / (hardness + 2))            // integer division; applied to struck piece only
wear      = max(1, (wear_base * (200 - defenderHarness)) / 200)  // Harness wear reduction (−L/2 % at level L, integer);
                                                                 // the min-1 floor RE-APPLIES AFTER the reduction —
                                                                 // struck armor always wears ≥ 1 except chromatis (§4.3)
```

Bare locations take no wear (nothing there). Blocked hits wear only the shield (×2, §3, applied to `wear`, i.e. after the Harness reduction).

### 4.2 Condition bands

`condPct = curDurability * 256 / maxDurability` (Q8):

| Band | condPct | condQ8 factor | Effect |
|---|---|---:|---|
| PRISTINE | 192–256 | 256 | full deflect + mitigation |
| WORN | 128–191 | 224 | −12.5% |
| BATTERED | 64–127 | 176 | −31% |
| FAILING | 1–63 | 112 | −56% |
| BROKEN | 0 | — | piece inert; **slot counts as bare** (weak spot!) until repaired |

Breakage is deterministic: durability reaches 0 → BROKEN. No random shatter roll for mundane gear. Both `D_piece` (§2.1) and `MIT_piece` (§4.1) use `condQ8` — worn armor is easier to hit through AND softer. All band numbers **(placeholder)**.

### 4.3 Chromatis exception — no wear

Chromatis pieces never wear and never break: `wear = 0` always. Canon: Blademaster chromatis blades "cooled right after forging so that they would not break, or dull" (novel L1257); precedent for the no-wear pattern already blessed for phorys (BLESSING-QUEUE #9). Extending never-dulls from blades to chromatis-reinforced *armor* is an extrapolation — **needs-blessing**. This is deliberate late-game exploit surface: chromatis kit is the only armor that is free to train against (PROGRESSION-SPEC seams E1/E3).

### 4.4 Bare location = weak spot

If the rolled location has no piece (or a BROKEN one):

```
final    = rawDmg + (rawDmg >> 1)                        // +50% damage, rounding down
riderQ16 = min( rawDmg * 4096 , 32768 )                  // rider chance: 6.25%/point of raw, cap 50%
```

Roll `combat.weakspot`; rider applies iff `r16 < riderQ16` (rider per location table §4.1). Zero mitigation, zero chip floor — the full boosted damage lands. This is the punishment for Gabri's bare head in Example 1, and the reason full-coverage kit is a survival project, not a stat stick.

---

## 5. Damage model

### 5.1 Weapon table

`rawDmg = dmgMin + (draw16(combat.damage) mod (dmgMax - dmgMin + 1))`. All numbers **(placeholder)**; names/kit canon-cited.

| Weapon id | Canon | dmgMin–dmgMax | AP | accBonus | Tags |
|---|---|---:|---:|---:|---|
| `unarmed` | unarmed specialist doctrine (L183) | 1–3 | 0 | +0 | — |
| `shiv` | slum weapon **(invented)** | 2–5 | 0 | +5 | concealable |
| `knife` | Lief's dual knives (L976) | 2–6 | 0 | +5 | concealable, throwable |
| `saxe` | Mercian press blade (L2390) | 4–9 | 1 | +5 | pressWeapon |
| `gladius` | **(placeholder name — free canon slot, cover-art only L4)** | 3–8 | 0 | +10 | — |
| `battle_axe` | Trojian house-guard pattern, slash or stab (L277) | 6–12 | 2 | +0 | longReach |
| `mace` | Devin's — one-hit jaw-crush through a bloodletter (L2976) | 6–11 | 3 | +0 | crush |
| `lance` | Eric's ground-work lance (L186) | 5–12 | 2 | +5 | longReach, throwable |
| `repeater_bolt` | repeater crossbow (L2703); "Reman" manufacture attribution **(inferred — placeholder)** | 5–10 | 2 | +0 | ranged, slowReload (L2908) |
| `chromatis_blade` | Blademaster sword (L1257) | 8–15 | *(material)* | +15 | noWear, chargeReservoir (hook) |

- `longReach`: −15 ACC **(placeholder)** when attacker and defender are in press/adjacent-grapple range — direct systemization of "No man would be able to swing a long blade in those conditions" (L2390). Range states owned by COMBAT-SCREEN-SPEC.
- `chargeReservoir`: chromatis blades as tap-able energy stores (L2213, L3002) — hook only; Flame/charge combat riders are a separate future spec (reserved sub-draws 5–7).
- Melee-as-energy-transfer (Gerik's lecture, L447–449) is the *fictional physics* behind AP and crush tags; v1 does not simulate transfer magnitudes.

**Material-derived AP (Eli ruling, 2026-07-12 — resolves C9 and C10):** chromatis is canon-widespread, not Trojia-exclusive — "many advanced civilizations, Rema included, can make chromatis; almost none know the dark cost of making it" (Eli, new GAME-CANON-ADDITION lore; the dark-cost half stays deliberately vague, a hook for later). Per Eli: *"treat it as a material type"* — AP 8 on `chromatis_blade` was never a bespoke stat on one named sword, it is the material's inherent edge, derived directly from the `hardness: 8` already sitting in `content/raws/materials/chromatis.json` (§1.2 already reads this registry — no new raws field needed). **Rule: `AP = max(weaponBaseAP, materialHardness)` whenever a weapon's `materialId` resolves to a material with `hardness ≥ 8`.** This generalizes automatically to any future chromatis weapon (dagger, spear, arrowhead) without a new placeholder stat each time, and retroactively legitimizes C10: if Reman engineers are among the "many advanced civilizations" who can work chromatis, Devin's Reman-attributed repeater crossbow needs no separate extrapolation — it is unremarkable competence, not a stretch. Both C9 and C10 close as **BLESSED** under this one material-typed rule.

### 5.2 Damage arithmetic (armored location)

```
final = max( rawDmg - MIT_final , chip )      where  chip = max(1, rawDmg >> 3)
```

(`MIT_final` = Harness-scaled `MIT_eff`, §4.1; equal to `MIT_eff` at Harness 0, which all §6 examples assume.)

**Minimum chip rule:** any hit that lands on an armored body location deals at least `chip` (1 for rawDmg ≤ 15). Rationale: armor in canon is never total (blunt trauma, joints, pressure); and the sim stays honest — plate makes you near-invulnerable to a slum knife (chip 1) but 60 chip hits still kill. Full negation exists only behind a shield block (§3).

Damage applies to an integer HP pool (`VIT`). VIT derivation is PROGRESSION-SPEC's; every VIT number in §6 is **(placeholder)**.

### 5.3 Fall damage (owned here; the E2 price curve)

Falls resolve through this spec so PROGRESSION-SPEC's seam E2 has a real hook:

```
fallDmg       = max(0, 2 * drop * drop - fallMitigation)   // drop = z-levels fallen, pure integer, no draw
fallMitigation = (Grit + Skyrunning) >> 2                     // (placeholder shape; skill semantics owned by PROGRESSION-SPEC; Skyrunning = merged Shadow-Wait+Skylining, Eli-named 2026-07-12)
```

Deterministic — **no RNG draw**, no location roll, no armor mitigation (a fall is not a strike; placeholder ruling). Superlinear in `drop` by design: 1z = 2, 2z = 8, 3z = 18, 4z = 32 (before mitigation) — survivable practice drops stay cheap while real falls stay lethal. Damage ≥ 1 from a fall qualifies for Grit fall-XP (PROGRESSION-SPEC §3.1/§4 E2). All constants **(placeholder)**.

---

## 6. Worked examples — full arithmetic

Draw values below are **illustrative** (chosen to exercise the paths); golden-master tests pin the actual `RandomSource` outputs for fixed `worldSeed=42, tick=1000` **(golden fixture values)**. Every line shows: named draw → extracted integer → comparison → result.

### 6.1 Example 1 — slum thug (shiv) vs starting Gabri: the bare-head punishment

**(entire scenario needs-blessing: weak Gabri is invented, dossier §7)**

Thug: shortblade skill 25, shiv (2–5, AP 0, +5) → **ACC 30**. VIT 14.
Gabri (start): evasion 12, VIT 20. Kit: leather jerkin/sleeves/trousers/boots (all common Q1, pristine), white Wielder cloak (cloth Q2), **bare head, bare hands**, no shield.

Deflects: leather D = (2·4+1·2)·256>>8 = **10**; cloak D = (0+4) = **4**.

```
AC_agg numerator = 96·10 + 40·10 + 48·10 + 16·10 + 12·4  (head 0, hands 0)
                 = 960 + 400 + 480 + 160 + 48 = 2048
AC_agg = 2048 >> 8 = 8          DEF = 12 + 8 = 20
hitQ16 = 32768 + (30-20)·328 = 36048   (55.0%)
```

**Attack #1** (attackSeq 0):
1. `combat.hit` → r16 = 21400. 21400 < 36048 → **HIT**.
2. `combat.block` — skipped (no shield); index 1 reserved, no drift.
3. `combat.location` → r8 = 7 → band 0–23 → **HEAD**. Bare → weak spot.
4. `combat.damage` → r16 = 3 → rawDmg = 2 + (3 mod 4) = **5**.
5. Weak spot: final = 5 + (5>>1) = 5+2 = **7**. Gabri VIT 20 → **13**.
6. `combat.weakspot` → riderQ16 = min(5·4096, 32768) = 20480 (31.25%). r16 = 11111 < 20480 → **DAZED**, Gabri loses his next action.

**Attack #2** (attackSeq 1, free swing on the dazed Wielder):
1. `combat.hit` → r16 = 30111 < 36048 → **HIT**.
2. `combat.location` → r8 = 77 → band 24–123 → **TORSO**. Leather jerkin, pristine: MIT = (2·2+1)·256>>8 = **5**, AP 0 → MIT_eff 5.
3. `combat.damage` → r16 = 6 → rawDmg = 2 + (6 mod 4) = **4**.
4. final = max(4−5, max(1, 4>>3=0)) = max(−1, 1) = **1** (chip). Gabri VIT 13 → **12**.
5. Jerkin wear = max(1, 4·4/(2+2)) = **4**. Durability 60 → 56 (condPct 56·256/60 = 238 → still PRISTINE).

One exchange with ONE thug has Gabri at 12/20 and dazed — the leathers hold on the body, and the bare head nearly ends him. First purchase in the Darkstreets: any helmet. That is the north star in arithmetic.

### 6.2 Example 2 — starting Gabri vs an armored guard: small fish

Gabri: shortblade skill 15, gladius (3–8, AP 0, +10) → **ACC 25**. Kit/DEF as §6.1 (DEF 20).
Guard: evasion 8, VIT 26, shield skill 40. Kit (pristine): steel helm Q2, steel breastplate Q3, leather sleeves Q1, steel gauntlets Q2, steel greaves Q2, leather boots Q1, oak roundshield Q1 (dur 90).

Deflects: helm 32 · breastplate 34 · sleeves 10 · gauntlets 32 · greaves 32 · boots 10.

```
AC_agg numerator = 32·32 + 96·34 + 40·10 + 12·32 + 48·32 + 16·10
                 = 1024 + 3264 + 400 + 384 + 1536 + 160 = 6768
AC_agg = 6768 >> 8 = 26         DEF = 8 + 26 = 34
hitQ16 = 32768 + (25-34)·328 = 32768 - 2952 = 29816   (45.5%)
blockQ16 = min(6554 + 40·328, 32768) = 19674          (30.0%)
```

**Attack #1**: `combat.hit` r16 = 28000 < 29816 → HIT. `combat.block` r16 = 40000 ≥ 19674 → not blocked. `combat.location` r8 = 100 → **TORSO** (masterwork plate: MIT 17, AP 0 → 17). `combat.damage` r16 = 5 → rawDmg = 3 + (5 mod 6) = **8**. final = max(8−17, max(1, 8>>3=1)) = **1** (chip). Guard 26 → 25. Plate wear = max(1, 8·4/(7+2)) = 32/9 = **3**. Durability 240 → 237.

**Attack #2**: `combat.hit` r16 = 52000 ≥ 29816 → **MISS**. (One draw consumed; nothing else.)

**Attack #3**: `combat.hit` r16 = 12000 → HIT. `combat.block` r16 = 15000 < 19674 → **BLOCKED**. No location roll. `combat.damage` r16 = 11 → rawDmg = 3 + (11 mod 6) = **8**. Shield MIT (oak Q1) = (3·2+1) = **7** → final = max(8−7, 0) = **1** leaks through the block. Guard 25 → 24. Shield wear = 2·max(1, 8·4/(3+2)) = 2·6 = **12**. Shield 90 → 78.

Expectation: ~0.455 hit rate × (~70% chip-1 + ~30% blocked-≤1) ≈ **0.4 damage per swing → ~65 swings** to drop the guard. Meanwhile the guard's saxe (4–9, ACC 50 vs Gabri's DEF 20 → hitQ16 = 32768 + 30·328 = 42,608 = **65.0%** hit): per landed hit, bare locations (head+hands, 36/256) average ~9.5 boosted damage and covered leathers (220/256, MIT_eff ≈ 4 after AP 1) average ~2.67 → **~3.6 VIT/hit ≈ 2.4 VIT/swing** — Gabri (VIT 20) drops in **~8–9 swings**, fewer once a daze or bleed rider lands on the bare head. Verdict the system teaches: against plate you do not fight, you *arrange things* (social power, terrain, poison, fire). Small fish, big pond.

### 6.3 Example 3 — late-game Gabri (chromatis-reinforced kit) vs a bloodletter

Gabri (late): blade skill 85, chromatis_blade "Flameborne **(placeholder name)**" (8–15, AP 8, +15) → **ACC 100**. Evasion 40, VIT 34. Kit: legendary chromatis-reinforced pieces in all six body slots (D 40, MIT 20 each, no-wear §4.3), Wielder cloak (D 4).

```
AC_agg numerator = (32+96+40+12+48+16)·40 + 12·4 = 244·40 + 48 = 9808
AC_agg = 9808 >> 8 = 38         DEF = 40 + 38 = 78
```

Bloodletter **(statline placeholder; tavern-assassin/Bledhreft tier, novel L1826–29, L2973–76)**: evasion 45, VIT 90, natural hide as implicit piece on all body slots (H5 Q3 → D 26, MIT 13; its wounds "did not bleed like a mortal man's" — inhuman-flesh flavor, L1828), claws 10–18 AP 4, ACC 90. No cloak: AC_agg = 244·26 >> 8 = 6344>>8 = **24**; DEF = 45+24 = **69**.

**Gabri attacks**: hitQ16 = 32768 + (100−69)·328 = **42936** (65.5%).
1. `combat.hit` r16 = 20000 < 42936 → HIT. No shield on the bloodletter.
2. `combat.location` r8 = 130 → **ARMS**. Hide MIT 13 − AP 8 (chromatis edge, L1246) → MIT_eff **5**.
3. `combat.damage` r16 = 6 → rawDmg = 8 + (6 mod 8) = **14**. final = max(14−5, max(1,14>>3=1)) = **9**. Bloodletter 90 → **81**. (~10 such hits to kill — a real fight, not a farm; canon needed three elites plus a flamethrower, L1829.)

**Bloodletter attacks**: hitQ16 = 32768 + (90−78)·328 = **36704** (56.0%).
1. `combat.hit` r16 = 30000 < 36704 → HIT.
2. `combat.location` r8 = 60 → **TORSO**. Chromatis plate MIT 20 − claw AP 4 → **16**.
3. `combat.damage` r16 = 7 → rawDmg = 10 + (7 mod 9) = **17**. final = max(17−16, max(1, 17>>3=2)) = max(1, 2) = **2** (chip floor governs). Gabri 34 → **32**. Chromatis wear = **0** (§4.3).

Contrast that lands the whole progression: the same 17-raw claw against §6.1 starting Gabri's leather torso (MIT 5 − AP 4 = 1) deals **16** — half his pool in one swipe; on the bare head, **25** — instant death. The chromatis kit converts a lethal predator into a 2-chip nuisance, and it never wears out. That kit was bought with systems exploitation, not levels. Exploit honestly earned, world visibly gamed.

---

## 7. Determinism & pipeline notes

- Combat resolution is pure integer over `(worldSeed, tick, attackerId, defenderId, attackSeq, statlines, gear)` → `AttackOutcome` record (primitives only, per event rules ARCHITECTURE §5).
- No `HashMap` in combat state; slot iteration is fixed enum order (head→jewelry); piece lists sorted by slot ordinal.
- Draw sub-indices are absolute (§2.4): skipped draws (no shield, no weak spot) never shift subsequent draws — determinism twins across code paths are structurally guaranteed, not tested-into-existence.
- Save/load mid-fight: combat happens on the dedicated combat screen (Decision 2b); combat state serializes at TICK_END only, same as everything (ARCHITECTURE §4 phase 10).
- North-star seam (cross-ref PROGRESSION-SPEC seam E1): being hit trains armor skill; attacking the Wielder is unlawful → goaded weak attackers are Wielder-exclusive safe training. Chip damage (§5.2) is the honest cost meter of that exploit; chromatis no-wear (§4.3) is its late-game amplifier.

## 8. Unit-test list (implementation contract)

Class/method names are binding; each row is the exact assertion.

**`CoverageTableTest`**
1. `coverageWeightsSumTo256` — Σ COV_Q8 over all 9 slots == 256.
2. `locationWeightsSumTo256` — Σ LOC_Q8 over 6 body locations == 256.

**`AggregateAcTest`**
3. `unarmoredBaselineIsZero` — actor with zero pieces: `acAgg() == 0` and `def() == evasion`.
4. `startingGabriKitAcIsEight` — exact §6.1 kit → `acAgg() == 8`.
5. `guardKitAcIsTwentySix` — exact §6.2 kit → `acAgg() == 26`.
6. `brokenPieceContributesZero` — §6.2 kit with breastplate durability 0 → `acAgg() == 13` ((6768−3264)>>8).
7. `conditionScalesDeflect` — breastplate BATTERED (condQ8 176): D = 34·176>>8 = 23 → `acAgg() == 22` ((6768−3264+96·23)>>8 = 5712>>8).

**`HitChanceTest`**
8. `hitChanceMidpointAtEqualStats` — ACC == DEF → `hitQ16 == 32768`.
9. `hitChanceClampsLowAtFivePercent` — ACC−DEF = −200 → `hitQ16 == 3277`.
10. `hitChanceClampsHighAtNinetyFivePercent` — ACC−DEF = +200 → `hitQ16 == 62259`.
11. `example1HitChance` — ACC 30, DEF 20 → `hitQ16 == 36048`.
12. `hitRollEqualityIsMiss` — r16 == hitQ16 exactly → miss (strict `<`).

**`BlockTest`**
13. `blockChanceBaseAtSkillZero` — shieldSkill 0 → `blockQ16 == 6554` (10% base, exact).
14. `blockChanceCapsAtFiftyPercent` — shieldSkill 100 → `blockQ16 == 32768`.
15. `blockedHitSkipsLocationRoll` — blocked attack: `combat.location` sub-draw never requested (probe RandomSource spy; drawIndex 2 of that attackSeq unused).
16. `blockedHitCanZeroDamage` — rawDmg 5 vs shield MIT 7 → final == 0 (no chip through block).
17. `blockedHitLeaksRemainder` — rawDmg 8 vs shield MIT 7 → final == 1.
18. `shieldWearIsDoubled` — rawDmg 8, oak H3, Harness 0 → shield wear == 12 (2·max(1,32/5)).

**`LocationRollTest`**
19. `locationBandBoundaries` — r8 ∈ {0,23}→HEAD; {24,123}→TORSO; {124,167}→ARMS; {168,179}→HANDS; {180,235}→LEGS; {236,255}→FEET (12 asserts, one per edge).

**`DamageRollTest`**
20. `damageRollSpanMapping` — `rawDmg = dmgMin + r16 mod span`: gladius (3–8, span 6): r16 0 → 3; r16 5 → 8; r16 6 → 3 (wrap); repeater_bolt (5–10): r16 65535 → 5 + (65535 mod 6) = 8 (full-range value, not just goldens).

**`MitigationTest`**
21. `steelMasterworkPlateMitigation` — H7 Q3 pristine → MIT == 17.
22. `armorPiercingFloorsAtZero` — MIT 5, AP 8 → MIT_eff == 0 (never negative).
23. `conditionBandsScaleMitigation` — leather base MIT 5: WORN → 4 (5·224>>8); BATTERED → 3 (5·176>>8); FAILING → 2 (5·112>>8).
24. `conditionBandBoundariesExact` — condPct 192→PRISTINE / 191→WORN; 128→WORN / 127→BATTERED; 64→BATTERED / 63→FAILING; 1→FAILING / 0→BROKEN (8 asserts, both sides of every band edge).
25. `harnessScalesMitigation` — MIT_eff 16, Harness 50 → MIT_final == 17 (16·1100/1000); Harness 0 → 16; Harness 100 → 19 (16·1200/1000, integer floor).
26. `chipDamageFloor` — rawDmg 8 vs MIT_final 17 → final == 1; rawDmg 20 vs MIT_final 17 → final == 3 (max(3, chip 2)).

**`WearTest`**
27. `wearMinimumIsOne` — rawDmg 1 vs steel H7: 4/9 == 0 → wear == 1.
28. `wearFormulaExact` — rawDmg 8 steel → 3; rawDmg 4 leather → 4 (Harness 0).
29. `harnessReducesWearAfterFloor` — wear_base 4, Harness 75 → wear == 2 (4·125/200); wear_base 1, Harness 100 → wear == 1 (min-1 re-applies after reduction).
30. `chromatisTakesNoWear` — chromatis piece, any rawDmg, any Harness → wear == 0, durability unchanged.
31. `pieceBreaksAtZeroAndSlotGoesBare` — piece dur 2, wear 3 → dur 0, state BROKEN; next attack resolving that location applies weak-spot rules (MIT 0, +50%).

**`FallDamageTest`**
32. `fallDamageSuperlinearAndMitigated` — §5.3: drop 2, mitigation 0 → 8; drop 3 → 18; drop 2, fallMitigation 10 → 0 (floor at 0, no negative healing); no RNG draw consumed.

**`WeakSpotTest`**
33. `bareLocationBonusDamage` — rawDmg 5 on bare location → final == 7 (5 + 5>>1), no wear anywhere.
34. `weakspotRiderChanceCapped` — rawDmg 12 → riderQ16 == 32768 (min(49152, 32768)).
35. `weakspotRiderBelowCapExact` — rawDmg 5 → riderQ16 == 20480 (uncapped branch, exact).
36. `riderTablePerLocation` — bare ARMS → FUMBLE rider id; bare LEGS → KNOCKDOWN rider id (table lookup, all six asserted).

**`CombatDeterminismTest`**
37. `twinRunsAreByteIdentical` — two fresh resolvers, same (worldSeed, tick, actorIds, statlines, attackSeq range 0..9) → identical serialized `AttackOutcome` sequences (determinism twin; golden-master seed 42).
38. `skippedDrawsDoNotShiftLaterAttacks` — outcome of attackSeq 1 is identical whether attackSeq 0 was blocked, missed, or landed a weak spot (absolute drawIndex; assert equality across all three constructed histories).
39. `drawNameSubIndexMappingIsStable` — attack-scoped combat.hit/block/location/damage/weakspot/logline/readblow ↔ 0–6; encounter-scoped purpose ordinals combat.init/flee/morale/aiTarget ↔ 0–3 (constant golden; a renumber fails the build).

**`WorkedExampleGoldenTest`** (fixtures pin real RandomSource draws at worldSeed 42; the illustrative r-values in §6 are replaced by the pinned actuals on first implementation, then frozen)
40. `example1ThugVsStartingGabri` — full trace: head weak spot then torso chip; Gabri VIT 20→12 shape (7-then-1 under §6 illustrative draws), DAZED applied, jerkin 60→56 PRISTINE.
41. `example2GabriVsArmoredGuard` — chip 1 on torso, plate 240→237, miss consumes only one draw, blocked hit leaks 1 and wears shield 90→78.
42. `example3LateGabriVsBloodletter` — Gabri deals 9 (AP through hide), takes 2 (chip floor over MIT 16), chromatis durability unchanged.

## 9. Open items / blessing queue additions

| # | Item | Recommendation |
|---|---|---|
| C1 | `leather` + `cloth` gear-material raws (hardness 2 / 0, durability caps 60 / 40) | add to `content/raws/materials/` with `invented for v0 balance` provenance — **needs-blessing** |
| C2 | Chromatis armor no-wear (extrapolated from never-dull blades L1257) | keep — it is the late-game exploit amplifier — **needs-blessing** |
| C3 | Weak-Gabri statline & timeline (canon Gabri has no growth arc, dossier §7) | standing flag; every §6.1 number placeholder |
| C4 | `gladius` as the free short-sword name (cover-art-only slot, L4) | bless or rename |
| C5 | Layered armor (canon kit L2209: jerkin under mail under plate) | **deferred to v2** — one piece per slot ships; slot model leaves room (layer list per slot) |
| C6 | Called shots / aimed attacks at chosen locations | deferred; location stays a pure weighted draw in v1 (assassin gameplay handles precision kills narratively via surprise rules, COMBAT-SCREEN-SPEC) |
| C7 | Flame/charge damage riders (Gabri's Flame, chromatis reservoir taps) | reserved sub-draws 5–7; separate spec after FLAME systems design |
| C8 | Ranged-specific rules (reload, range bands, cover) — repeater slowReload canon L2908 | COMBAT-SCREEN-SPEC owns action economy; damage/AP resolve through this spec unchanged |
| C9 | `chromatis_blade` AP 8 — extrapolated from the chromatis BOW three-inch-steel feat (L1246) to blades | keep the number, flag the transfer — **needs-blessing** |
| C10 | "Reman" manufacture attribution on the repeater crossbow (canon has Reman engineering L277/L1398 but never names the repeater's maker) | inferred — **needs-blessing** or drop the attribution |
| C11 | Fall damage §5.3 (2×drop² curve, (Grit+Skyrunning)>>2 mitigation, no-armor ruling) | all constants placeholder — **needs-blessing** |
