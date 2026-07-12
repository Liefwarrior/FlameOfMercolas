# Locked Design Decisions — Granadad: The Darkstreets

Locked by Eli 2026-07-12 ("DECISIONS LOCKED — proceed to plan + build. Quality over speed, no shortcuts.")
Engine/sim decisions live in ARCHITECTURE.md; this file records the GAMEPLAY layer.

| # | Area | Decision |
|---|---|---|
| — | Title | **Granadad: The Darkstreets** — playing Gabri in Trojia, Ptolus-style urban dark fantasy |
| — | Canon | Read `..\LordOfTrojia-MVP` before inventing; placeholders clearly marked where the book is silent |
| — | MVP world | One capital district + 2-3 level starter dungeon; top-down tile grid, Moonring style; CC0 art behind the asset-swap layer |
| 1a | Defense | **Hybrid per-part armor**: slots head/torso/arms/hands/legs/feet/shield/cloak/jewelry. All worn pieces contribute coverage-weighted to ONE aggregate AC used for hit/miss. On hit: roll body location; that location's piece mitigates damage and takes durability wear. Bare locations are weak spots. **Spec with worked examples + unit tests BEFORE implementation.** |
| 2b | Combat flow | Separate dedicated combat screen entered on encounter contact (Skald-style), decoupled from the exploration screen |
| 3a | Progression | Full Morrowind use-based skills — skills rise through use; this powers the weak→demigod systems-exploitation curve (north star) |
| 4c | Faces | Hand-authored text-art faces for named NPCs; a LITE version of Warsim: Realm of Aslona's layered-part face generator for generic NPCs, styled to our UI |

Spec documents (produced by the gameplay spec track, live in this folder):
- `COMBAT-SPEC.md` — defense math, hit location, durability, worked examples, unit-test list
- `PROGRESSION-SPEC.md` — skill list, use-XP formulas, attribute interplay, exploit seams
- `COMBAT-SCREEN-SPEC.md` — encounter transition, screen layout, action economy, UI
- `FACES-SPEC.md` — face file formats, generator part model, determinism, examples

Sequencing: specs first (this track), implementation lands as the G-milestones after the
sim-core F-milestones provide the world the game plays in. The "no gameplay in v0" scope
contract applies to the SIM core's F1-F8; the G-track builds the game layer on top of it.
