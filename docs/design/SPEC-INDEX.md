# SPEC-INDEX — G-track gameplay specs (Decisions 1a / 2b / 3a / 4c)

Status: all four specs verified (math recompute + canon citation audit) and reconciled
2026-07-12. Single sources of truth: **COMBAT-SPEC §2.3/§2.4** own all combat coefficients
and every `combat.*` draw name; **PROGRESSION-SPEC §8** owns evasion; **FACES-SPEC §4.2**
owns the facegen mixer. North star holds in every doc: PRS static 100, Streetwise (née
Gutter-Ken) information-only, no levels, use-based growth, weak-start Gabri flagged as pure
invention.

## The four specs

| Spec | Decision | Owns | Tests |
|---|---|---|---:|
| `COMBAT-SPEC.md` | 1a hybrid per-part defense | slot/coverage model, aggregate AC, hit roll (`hitQ16 = clamp(32768+(ACC−DEF)·328, 3277, 62259)`), block, location/mitigation/wear (+Harness terms), weak spots, damage, fall damage (§5.3), the full combat draw schedule (§2.4: attack-scoped subDraws 0–7, encounter-scoped init/flee/morale/aiTarget) | **42** |
| `PROGRESSION-SPEC.md` | 3a use-based skills | 16-skill list, grains/aptitude XP model, satiation, exploit seams E1–E3, attributes (PRS static 100), evasion + armorBulk, trainers/books/meditation, Flame track, worked grind goldens | **28** |
| `COMBAT-SCREEN-SPEC.md` | 2b dedicated combat screen | encounter transition + `CombatEntryState`, screen layout/log, ranks, action economy, aimed attacks, flee, morale, return consequences + Wielder-immunity law hook | **16** |
| `FACES-SPEC.md` | 4c text-art faces | 15×9 frame + silhouette contract, named-face file format (Devin, Minister John), FaceGen LITE parts/archetypes/overlays, pinned SplitMix64 mixer, reference vectors | **19** |

Total unit/golden tests specced: **105**. Test names are unique across all four docs.

## Cross-doc contracts (reconciled)

- Hit formula: one formula, COMBAT-SPEC §2.3; PROGRESSION §8/§9.1 and its tests are computed
  against it (skill feeds ACC 1:1). The old permille hook is gone.
- Draw names: COMBAT-SPEC §2.4 is the registry; PROGRESSION §3.4 and COMBAT-SCREEN §4.7
  consume its names and keying verbatim (`combat.hit/block/location/damage/weakspot/logline/
  readblow` + `combat.init/flee/morale/aiTarget`). One purpose per slot; no sub-bands.
- DEF shape: `DEF = evasion + AC_agg` (COMBAT-SPEC §2.2); `evasion = max(0, AGI − Σ armorBulk)`
  (PROGRESSION §8).
- Fall damage: COMBAT-SPEC §5.3; PROGRESSION seam E2 prices against it.
- Harness: mitigation/wear formulas normative in COMBAT-SPEC §4.1; grants in PROGRESSION §8.1.
- Faces: generated from `(worldSeed, npcId)` only, zero combat draws (FACES §4.2,
  COMBAT-SCREEN §2.4).

## Open placeholder items needing Eli's blessing

Every invented name/number is tagged `(placeholder)` inline in its spec; the rows below are
the collected decision-needing items (spec § in parentheses).

| # | Spec | Item | Note |
|---|---|---|---|
| C1 | COMBAT | `leather` + `cloth` material raws (hardness 2/0, durability 60/40) (§1.2, §9) | new raws, `invented for v0 balance` provenance |
| C2 | COMBAT | chromatis armor no-wear (§4.3) | extrapolated from never-dull blades L1257; late-game exploit amplifier |
| C3 | COMBAT | weak-Gabri statline & timeline (§6.1) | standing flag — canon Gabri has no growth arc |
| C4 | COMBAT | `gladius` as the free short-sword name (§5.1) | cover-art-only slot L4 — bless or rename |
| C5 | COMBAT | layered armor (canon kit L2209) | deferred to v2; slot model leaves room |
| C6 | COMBAT | called shots deferred; random location in v1 (§9) | aimed attacks live on the combat screen instead (SCREEN §4.4) |
| C7 | COMBAT | Flame/charge damage riders (§2.4 reserved subDraw 7) | separate spec after FLAME systems design |
| C8 | COMBAT | ranged rules (reload/range/cover) split (§9) | SCREEN owns action economy |
| C9 | COMBAT | `chromatis_blade` AP 8 (§5.1) | extrapolated from the chromatis BOW 3-inch-steel feat L1246 |
| C10 | COMBAT | "Reman" repeater manufacture attribution (§5.1) | inferred — bless or drop |
| C11 | COMBAT | fall damage constants (2×drop², (Grit+Skyrunning)>>2, no-armor ruling) (§5.3) | invented curve for seam E2 |
| P1 | PROGRESSION | skill names + aptitude assignments (§2) | **RESOLVED 2026-07-12** (skill-list rulings, below): Saxework+Repeaterwork merged ("Sidearms" placeholder name); Shadow-Wait+Skylining merged as **Deftness** (Eli-named), **renamed Skyrunning 2026-07-13** to match the new Skyrunner villain archetype; The Draw → **Dire Bows**; Gutter-Ken → **Streetwise**; 18 → 16 skills. Follow-ups: Sidearms name awaits Eli; Skyrunning aptitude Favored needs-confirmation |
| P2 | PROGRESSION | weak-start Gabri numbers (skills ~0–5, Flame locked) | north-star invention, zero canon |
| P3 | PROGRESSION | seam E1 "Wielder's Bruise" tone check (§4) | goading thugs to strike the Wielder |
| P4 | PROGRESSION | Flame unlock rite + ability ladder names (§7) | Grit gate, vigils, Candleflame…Declaration |
| P5 | PROGRESSION | satiation constants (3,000-tick decay, 25% floor, 4 tiers) (§3.3) | |
| P6 | PROGRESSION | trainer constants (trainMod 1,000 cp, 25% social floor, 2×attr cap) (§6) | |
| P7 | PROGRESSION | Streetwise (née Gutter-Ken) information-only fence (§2 #15) | confirm it reads as intended in play |
| P8 | PROGRESSION | evasion shape + per-material armorBulk table (§8) | invented to make DEF derivable |
| S1 | SCREEN | all placeholder tuning (light tiers, press penalties, aim-penalty shape, flee formula, cartridge size, morale threshold, pursuit window) (§6.1) | |
| S2 | SCREEN | weak-Gabri v0 Flame powers (Flicker/Dazzle/Declare) (§4.6) | feeble-but-present from fight one |
| S3 | SCREEN | DEATH = plain game over in v0 (§5) | no Flame intervention — confirm |
| S4 | SCREEN | Devin DOWNED-not-dead (§5) | no companion permadeath in v0 — confirm |
| S5 | SCREEN | whole-side flee, no abandoning Devin (§4.5) | confirm |
| S6 | SCREEN | aiming-overrides-location-roll ruling (§4.4) | COMBAT-SPEC co-sign needed (owns the table) |
| S7 | SCREEN | guards mopping up Gabri's fled enemies as intended exploit (§5.3) | confirm feature, not bug |
| F1 | FACES | 15×9 frame + `HUMAN_BASE` silhouette cells (§1.2) | freeze before first golden bless |
| F2 | FACES | `FACEGEN_SALT` + pinned-SplitMix64 ruling (§4.2) | veto-able; §6 vectors regenerate mechanically |
| F3 | FACES | headgear classes, archetype list, sample weights (§3) | |
| F4 | FACES | Minister John's entire appearance (§2.2) | canon gives only "old" |
| F5 | FACES | overlay anchors + 13/2/1 rarity (§4.6) | |
| F6 | FACES | targeted-enemy-face-only panel rule (§5) | coordinate with SCREEN |

## Blessing resolutions (Eli, 2026-07-12 — docket round 3)

- **SANCTIONED EXPLOITS (all three confirmed as features)**: Wielder's Bruise (P3), guard
  mop-up (S7), chromatis armor no-wear (C2). These are load-bearing design, protected from
  future "balance fixes" without a new ruling.
- **S3 RULED**: death in v0 = plain game over. No Flame intervention.
- **P4 + S2 BLESSED**: Flame track as specced (Grit-gated rite, vigils, Candleflame→Declaration
  ladder; starter powers Flicker/Dazzle/Declare).
- **S6 RULED + COMBAT-SPEC CO-SIGN**: aiming overrides the location roll (with accuracy
  penalty). COMBAT-SPEC §3's location table gains the aimed-attack bypass note at F-implementation.
- **C4 BLESSED**: `gladius` is the common short-sword.
- **F4 BLESSED**: Minister John's invented appearance stands.
- **P1 PARTIAL** *(superseded — see skill-list rulings below)*: Eli reviewing the 18 skill
  names (Shadow-Wait pending); remaining batch (C1, C3/P2, C5-C11, P5-P8, S1, S4-S5, F1-F3,
  F5-F6) queued for a full walkthrough docket next downtime — NOT batch-blessed.

## Blessing resolutions (Eli, 2026-07-12 — docket round 4, full queue cleared)

- **C3/P2 CONFIRMED**: weak-start Gabri's canonical statline is the numbers already used in
  COMBAT-SPEC §6.1's worked example — shortblade skill 15, gladius, DEF 20, Flame locked.
  No separate invention needed; the worked example IS the spec.
- **C9 + C10 BLESSED via new lore**: chromatis-working is canonically widespread (Rema
  included), not Trojia-exclusive, with an unspecified "dark cost" — see
  docs/lore/MATERIALS-CANON.md §1.1 GAME-CANON-ADDITION. AP 8 is retired as a bespoke
  `chromatis_blade` stat and reborn as a material-derived rule (COMBAT-SPEC §5.1:
  `AP = max(weaponBaseAP, materialHardness)` for `hardness ≥ 8`), generalizing to any future
  chromatis weapon. The Reman-attributed repeater (C10) needs no separate extrapolation once
  Rema is canonically a chromatis-working civilization.
- **C11 BLESSED**: fall-damage curve `2×drop² − (Grit+Skyrunning)>>2` stands (Skyrunning
  supersedes the old Skylining reference post skill-merge).
- **S4 + S5 CONFIRMED**: Devin is downed-not-dead (no companion permadeath in v0); fleeing
  is whole-side only — no abandoning him mid-fight.
- **P7 LOOSENED**: the Streetwise fence widens from information-only to information +
  economic/tactical edge — black-market price shading and patrol/raid timing knowledge join
  rumors/routes/informants. Deference and combat draws remain permanently outside the fence
  (binding, unchanged). Applied to PROGRESSION-SPEC §2 row 15 + its north-star guard, and to
  ACTORS-SPEC's Watch/Wastrel deference rows.
- **ACTORS budget ACCEPTED**: 8.2ms worst-case tick (was 7.8ms pre-ACTORS) stands as the new
  reference line; 8ms was always a soft target, not a hard contract.
- **ACTORS architecture fork RESOLVED**: WORK/PATROL/VEND behavior folds into the §10
  Job-goal cycle — one source of truth for actor schedules, consistent with "Job = why
  you're here." Structural edit to ACTORS-SPEC §1.3/§4/§10 in progress.
- **FINAL BATCH BLESSED**: C1 (leather/cloth raws), C5 (layered armor, deferred v2),
  C6 (called shots, deferred — aiming lives on the combat screen instead), C7 (Flame/charge
  damage riders, deferred to a future Flame spec), C8 (ranged reload/range/cover, owned by
  COMBAT-SCREEN-SPEC), P5 (satiation constants), P6 (trainer constants), P8 (evasion +
  armorBulk table), S1 (all combat-screen tuning), F1-F3 (face frame/salt/archetype weights),
  F5-F6 (overlay rarity, targeted-face-only panel). All confirmed as designed; every value
  remains individually vetoable later since these are all plain data files.

**Every item in the original 32-row blessing queue is now resolved.** No open design
questions remain in the G-track specs as of 2026-07-12.

## Skill-list rulings (Eli, 2026-07-12 — P1 RESOLVED)

Four binding rulings, applied across PROGRESSION/COMBAT/COMBAT-SCREEN the same day; the
skill list is now **16 skills** (was 18):

1. **MERGE Saxework + Repeaterwork** → one skill covering saxe blades, knives held & thrown,
   and the repeater crossbow. Eli did not name it; **"Sidearms" is a placeholder name
   awaiting Eli**. Aptitude-clean merge (both parents AGI/Trained). AGI weight = sum of
   parents (24+20 = 44); §9.1 worked-example arithmetic unchanged (same aptNum 20, same
   ACC hook) except the AGI side-effect note (weight 20 → 44).
2. **MERGE Shadow-Wait + Skylining** → **Skyrunning** (Eli-named). Covers sneak, pickpocket,
   takedown, jumps, climbs, rooftop runs. AGI weight = sum of parents (28+36 = 64).
   Aptitude set to **Favored** (Skylining's; preserves the walkable-roofs/Wielder theme) —
   Shadow-Wait was Trained, so this choice **needs-confirmation** from Eli.
3. **RENAME The Draw → Dire Bows** (Eli-named; coverage and canon anchors unchanged).
4. **RENAME Gutter-Ken → Streetwise** (Eli-named; the information-only north-star fence
   remains verbatim in force — PROGRESSION §2 guard, now on skill #15; P7 confirm-in-play
   stays open).

Attribute-weight invariant intact post-merge (every row still sums to 128; PROGRESSION §5
shows the sums; test 26 updated).

## Verification-pass fix log (2026-07-12)

Applied: hit-formula unification (PROGRESSION §8/§9.1/tests recomputed on COMBAT §2.3);
draw-name registry extended and adopted across all three combat docs; fall damage added to
COMBAT §5.3; Harness mitigation/wear given integer insertion points in COMBAT §4.1;
`combat.readblow` allocated subDraw 6, `combat.logline` subDraw 5; K4→E1/E3 cross-refs;
Ex2 guard-lethality prose corrected (65.0%, ~8–9 swings); §9.1 bolt ledger recomputed
(3,080 shots, 420 bolts, 2,100 cp); §9.2 fall count restated by pacing (25–46, floor ≤ 60);
Shadow-Wait relabeled placeholder-name; Reman attribution flagged inferred; emperor-exception
cite L2414→L2967; bloodletter L1828 flavor reworded; blade-AP transfer flagged; 10 new test
rows (COMBAT +7, PROGRESSION +4 incl. one replacement); 328-constant wording and §5
shift-precedence corrected.
