# SPEC-INDEX — G-track gameplay specs (Decisions 1a / 2b / 3a / 4c)

Status: all four specs verified (math recompute + canon citation audit) and reconciled
2026-07-12. Single sources of truth: **COMBAT-SPEC §2.3/§2.4** own all combat coefficients
and every `combat.*` draw name; **PROGRESSION-SPEC §8** owns evasion; **FACES-SPEC §4.2**
owns the facegen mixer. North star holds in every doc: PRS static 100, Gutter-Ken
information-only, no levels, use-based growth, weak-start Gabri flagged as pure invention.

## The four specs

| Spec | Decision | Owns | Tests |
|---|---|---|---:|
| `COMBAT-SPEC.md` | 1a hybrid per-part defense | slot/coverage model, aggregate AC, hit roll (`hitQ16 = clamp(32768+(ACC−DEF)·328, 3277, 62259)`), block, location/mitigation/wear (+Harness terms), weak spots, damage, fall damage (§5.3), the full combat draw schedule (§2.4: attack-scoped subDraws 0–7, encounter-scoped init/flee/morale/aiTarget) | **42** |
| `PROGRESSION-SPEC.md` | 3a use-based skills | 18-skill list, grains/aptitude XP model, satiation, exploit seams E1–E3, attributes (PRS static 100), evasion + armorBulk, trainers/books/meditation, Flame track, worked grind goldens | **28** |
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
| C11 | COMBAT | fall damage constants (2×drop², (Grit+Skylining)>>2, no-armor ruling) (§5.3) | invented curve for seam E2 |
| P1 | PROGRESSION | all 18 skill names + aptitude assignments (§2) | incl. **Shadow-Wait** — placeholder NAME, craft canon L3030–3038 |
| P2 | PROGRESSION | weak-start Gabri numbers (skills ~0–5, Flame locked) | north-star invention, zero canon |
| P3 | PROGRESSION | seam E1 "Wielder's Bruise" tone check (§4) | goading thugs to strike the Wielder |
| P4 | PROGRESSION | Flame unlock rite + ability ladder names (§7) | Grit gate, vigils, Candleflame…Declaration |
| P5 | PROGRESSION | satiation constants (3,000-tick decay, 25% floor, 4 tiers) (§3.3) | |
| P6 | PROGRESSION | trainer constants (trainMod 1,000 cp, 25% social floor, 2×attr cap) (§6) | |
| P7 | PROGRESSION | Gutter-Ken information-only fence (§2 #17) | confirm it reads as intended in play |
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
- **P1 PARTIAL**: Eli reviewing the 18 skill names (Shadow-Wait pending); remaining batch
  (C1, C3/P2, C5-C11, P5-P8, S1, S4-S5, F1-F3, F5-F6) queued for a full walkthrough docket
  next downtime — NOT batch-blessed.

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
