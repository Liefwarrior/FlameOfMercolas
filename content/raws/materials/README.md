# Material raws — v0 canonical set (17)

Schema: ARCHITECTURE.md §10 (Chromatis example is normative). Temps integer Kelvin, Q8 fixed-point,
charge in 16-bit-safe cu. Every raw carries `provenance` (canon citation or "invented for v0")
and `notes`; loaders ignore both. Sibling raws (treatments/getilia_soak, reactions/phorys,
fluids/water) are owned elsewhere — `trudgeon_wood@getilia_soak` is minted at load, not a file here.
(`ice.json` in this directory is owned by the fluids crew per their freeze/thaw ruling and is not
part of the 17-id canonical vocabulary below.)

Added after the original 14-id set was tabled: `cloth` and `leather` (already-blessed per
docs/design/SPEC-INDEX.md item C1 — COMBAT-SPEC.md §1.2's gear-material raws) and
`reman_concrete` (2026-07-13 Trojian Compounds housing ruling, docs/design/DECISIONS.md —
values below are still placeholders pending Eli's blessing, per that file's own provenance
field).

| id | phase | dens | hard | cond | cap | flammable (ignK / fuelTicks -> burnsTo) | melt | opac | features | valueCp | provenance |
|---|---|---|---|---|---|---|---|---|---|---|---|
| granite | SOLID | 2700 | 6 | 60 | 88 | no | — | 31 | — | 20 | invented (role: §11 "stone conducts only") |
| dirt | SOLID | 1500 | 1 | 24 | 64 | no | — | 31 | — | 1 | invented |
| oak | SOLID | 700 | 3 | 16 | 48 | 550 K / 1800 -> ash | — | 31 | — | 40 | invented (named fuel of §11 Tavern Fire) |
| thatch | SOLID | 150 | 1 | 8 | 16 | 450 K / 240 -> ash | — | 31 | — | 5 | invented (tinder: easier + faster than oak) |
| trudgeon_wood | SOLID | 900 | 4 | 20 | 56 | 650 K / 3400 -> ash | — | 31 | — | 60 | WorldBible §9 (untreated base; treatment fireproofs) |
| steel | SOLID | 7850 | 7 | 230 | 100 | no | — | 31 | — | 1200 | WorldBible §9 (chromatis punches through steel) |
| brick | SOLID | 1900 | 4 | 40 | 72 | no | — | 31 | — | 15 | invented |
| chromatis | SOLID | 6800 | 8 | 200 | 96 | no | 2600 K -> chromatis_melt ×7 | 31 | chargeable 60000 cu | 40000 | §10 verbatim; WorldBible §9 |
| chromatis_melt | LIQUID | 6800 | 1 | 210 | 96 | no | — | 31 | — | 5000 | derived (meltsTo target); invented numbers |
| phorys | SOLID | 2400 | 3 | 32 | 48 | no | — | 31 | contactReactive (liquid) | 8000 | WorldBible §9 (liquid -> pressurized gas) |
| lightstone | SOLID | 2600 | 5 | 48 | 64 | no | — | 12 | chargeable 5000 cu; shatterOnSpike 2000 | 15000 | WorldBible §9; decision #14 |
| lightstone_shards | SOLID | 2600 | 2 | 40 | 48 | no | — | 6 | — | 500 | derived (shatter product); invented numbers |
| glowstone | SOLID | 2500 | 5 | 44 | 64 | no | — | 31 | emissive 7, red | 6000 | WorldBible §9 (eerie red; U'mar shrine) |
| ash | SOLID | 300 | 1 | 4 | 16 | no | — | 4 | — | 1 | invented (terminal burnsTo target, §11) |
| cloth | SOLID | 250 | 0 | 6 | 16 | 400 K / 150 -> ash | — | 8 | fabric | 8 | SPEC-INDEX C1 (blessed hardness/durability); rest invented, needs-blessing |
| leather | SOLID | 860 | 2 | 14 | 40 | 700 K / 900 -> ash | — | 20 | hide | 45 | SPEC-INDEX C1 (blessed hardness/durability); rest invented, needs-blessing |
| reman_concrete | SOLID | 2400 | 7 | 80 | 90 | no | — | 31 | masonry, engineered | 200 | DECISIONS.md 2026-07-13 Compounds ruling; all values placeholder, needs-blessing |

## Loader invariants (ARCHITECTURE §10) — verified for every file above, including cloth/leather/reman_concrete

1. **conductivityQ8 ≤ 256** — max in set is still steel at 230 (cloth 6, leather 14, reman_concrete 80
   all clear it easily). ✓
2. **Per-material stability Σ(w/cap) ≤ ½ (min heatCapacity enforced)** — every heatCapacityQ8 ≥ 16,
   so even against a worst-case max-conductivity neighbor (w = 256/256 = 1 on all 6 faces):
   6·(256/256)/capQ8 ≤ 6/16 = 0.375 ≤ ½. With own conductivities the worst case is thatch:
   6·(8/256)/16 ≈ 0.0117. Minimum cap in set (16, tied by cloth) clears the enforced floor of 12.
   leather (3·14=42 ≤ 64·40=2560) and reman_concrete (3·80=240 ≤ 64·90=5760) both clear it too. ✓
3. **FLAMMABLE ⇒ ignitionK + fuelTicks (≤ 4095) + burnsTo** — now five flammables, all complete,
   all burnsTo = ash (which is itself non-flammable, so no burn cycles):
   thatch 450/240, oak 550/1800, trudgeon_wood 650/3400, cloth 400/150, leather 700/900. Max
   fuelTicks is still trudgeon_wood's 3400 ≤ 4095. Ordering: cloth ignites easier and burns faster
   than thatch (the previous fastest-burning solid); leather ignites harder than the structural
   woods but burns out faster than lumber, consistent with a thin worn-goods profile. ✓
4. **melt ⇒ meltsTo + meltYieldUnits** — only chromatis melts (→ chromatis_melt ×7, both fields
   present; target id exists in this directory). Steel/granite deliberately have meltK = null:
   no melt-product ids exist in the v0 vocabulary. ✓
5. **liquid tag ⇒ boilsTo** — no material carries the `liquid` tag (chromatis_melt is tagged
   `melt`, see its notes), so the implication is vacuously satisfied. ✓
6. **chargeable/spike values fit 16 bits** — chromatis 60000/600, lightstone 5000/150/2000,
   heat 20/0 deciK·tick⁻¹, equilibria 6000/2930 deciK: all ≤ 65535. ✓
7. **treatment targets exist** — getilia_soak targets trudgeon_wood (present). ✓
8. **derived-id collisions** — no raw file claims `trudgeon_wood@getilia_soak`; ids are unique
   (including `cloth`/`leather`/`reman_concrete`, which collide with nothing above). ✓
9. **reaction refs resolve** — phorys present and flagged contactReactive for the reactions raw. ✓
10. **color stops monotone** — chromatis 60 < 95 < 100; lightstone 5 < 60 < 100; both end at 100
    and use ≤ 4 stops (appearance bucket 0..3). ✓

## Open questions for Eli

- **Steel melt** omitted (needs a `steel_melt` id + fluid handling) — acceptable for v0?
- **chromatis_melt** avoids the `liquid` tag so phorys won't react with molten metal and no vapor
  id is required. Bless or revisit.
- Flammability scale used as 0–3 severity (0 = inert, 3 = tinder) — loader currently only needs
  zero/nonzero; bless the scale.
- Lightstone opacity 12 (translucent glass body) and ash opacity 4 (light floods burned-out rooms)
  are invented gameplay choices.
- Invented feature-JSON shapes: `emissive {lightLevel, tint}`, `shatterOnSpike {spikeCuPerTick,
  shattersTo, radiusChebyshev}`, `contactReactive {reagentTag}` — loader owner should confirm
  field names before M1.
- `ice.json` (fluids crew) has `meltsTo: "water"` — a fluid id, not a material id. The loader's
  "melt ⇒ meltsTo + yield" check must decide whether meltsTo may resolve into the fluid registry.
