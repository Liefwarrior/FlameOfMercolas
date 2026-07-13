# Map fixtures — Tiled authoring convention (v0)

Hand-authored source maps for the Tiled importer (`tools` module: `TmxReader`/`TiledValidator`/
`TiledWorldImporter`). Spec: `ARCHITECTURE.md` §1.1 #17 (fill/floor collapse), §3 "tools",
`docs/PLAN-v0.md` "Tiled multi-z convention". The client never reads these files — only the
importer's baked TROJSAV output. The importer never reads pixels; there is deliberately **no
tileset image yet** (tiles render blank in Tiled — turn on *View → Show Tile Object Outlines*
and read the tile *Class* label, e.g. `granite/WALL`).

## Files

| File | What |
|---|---|
| `src/materials.tsx` | Shared imageless tileset. One tile per (material, form) pair in use, plus fluid tiles. 16×16 px declared size. Includes `reman_concrete` (WALL/FLOOR) and `leather`/`cloth` (WALL) for the Compound typology (tile ids 31–34). |
| `src/tavern_fixture.tmx` | 48×32, z-levels −1/+0/+1. The Tavern Fire flagship stage (M1/M2 acceptance: imports byte-identical twice). |
| `src/ubend_fixture.tmx` | 24×16, z-levels −1/+0. Sealed U-bend duct for the M3 fluids pressure test. |
| `src/compound_block.tmx` | 128×128, z-levels +0/+1/+2. A DOCKS-GAZETTEER city-block slice built on the corrected **Trojian Compound** housing typology (canon: courtyard-farm ecosystem, not detached homes; see `docs/design/DOCKS-GAZETTEER.md` §2.5) — 1 Compound (12 dwelling units) + 2 businesses (K22, K24), populated at lore-derived full occupancy with ~64 actors (all 9 actor groups, all 13 civic jobs, all 6 relationship kinds — see `CompoundBlockPopulation`'s Javadoc for the canon troop-number derivation). Supersedes an earlier detached-"12 homes" prototype (`city_block.tmx`, removed — the corrected Compound typology replaced it before it was ever populated). |

## Map-level rules

- Orthogonal, `renderorder="right-down"`, `infinite="0"`, tile size 16×16.
- Tile layer data **must be `encoding="csv"`**, uncompressed (StAX reader contract).
- **No flipped/rotated tiles.** `TmxReader` masks the gid flip bits and warns; don't rely on it.
- One external tileset reference: `materials.tsx`, `firstgid="1"`. Do not embed tilesets.
- Group/layer *names* are the contract; ids and ordering are free (Tiled manages ids).
  Convention in these fixtures: groups ordered bottom-up (z:−1 first) so upper levels draw on top.

## Z-level groups

One Tiled **layer group per z-level**, named exactly `z:+0`, `z:+1`, `z:-1`, … (sign always
written, `+0` for street level). Group offsets must stay 0. Every z-level the map touches needs
a group, even if nearly empty.

Fixed sublayers inside each group (exact names):

| Sublayer | Kind | Required | Meaning |
|---|---|---|---|
| `terrain` | tile layer | yes | The *fill* of the cell: material **and** form (WALL/RAMP/STAIR_*/solid furniture). |
| `floor` | tile layer | yes (may be all-empty) | Floor material for cells with **no** fill. Form is forced to FLOOR by the importer; the tile's own form property is ignored here — use the material's FLOOR tile for sanity. |
| `fluids` | tile layer | optional | Initial pooled fluid: tiles carrying `fluid` + `depth` properties. Only legal on this sublayer. |
| `markers` | object layer | yes (may be empty) | Annotations: light sources, script anchors. |

**Collapse rule (authoritative, §1.1 #17):** per cell — fill present → `(fillMaterial, fillForm)`;
else floor present → `(floorMaterial, FLOOR)`; else `OPEN` (air). So: **OPEN is authored by
leaving the cell empty on both tile layers.** There is no OPEN tile. Painting floor under fill
is allowed and ignored (these fixtures do it for easy repainting).

## Tileset property contract (`materials.tsx`)

Every material tile carries string properties:

- `material` — a raws material id. Must resolve against `MaterialRegistry` at import;
  unknown names **fail the import** (with a nearest-name suggestion). v0 vocabulary:
  `granite, dirt, oak, thatch, trudgeon_wood, steel, brick, chromatis, chromatis_melt,
  phorys, lightstone, lightstone_shards, glowstone, ash, reman_concrete, leather, cloth`
  and the treatment-minted `trudgeon_wood@getilia_soak`.
- `form` — one of `WALL | FLOOR | OPEN | RAMP | STAIR_UP | STAIR_DOWN` (`TileForm`).
  No tile in the set uses `OPEN` (see collapse rule); the value exists in the enum only.

Fluid tiles instead carry `fluid` (string, v0: `water`) and `depth` (int, 1–7 — the FLUID
lane stores 3 depth bits). Provided depths: 7 (full), 4, 2; add more variants as needed.

Tile *Class* is set to `material/FORM` (e.g. `oak/STAIR_UP`) purely as an editor label; the
importer reads only the custom properties. To add a variant: append a new `<tile>` with the
next free id — **never renumber or delete existing tile ids**, existing maps reference them
by gid.

### Form semantics

- `WALL` — cell fully solid (also used for furniture: tables, bar, barrels are just solid
  tiles of their material in v0 — there are no item entities).
- `FLOOR` — walkable surface at the bottom of the cell; blocks fluid/creature passage downward.
- `RAMP` — walkable slope connecting to the z above; the cell directly above must be OPEN.
- `STAIR_UP` / `STAIR_DOWN` — vertical connection. **Pairing rule:** a passage between z and
  z+1 needs `STAIR_UP` at `(x, y, z)` and `STAIR_DOWN` at `(x, y, z+1)`, same column.
- Vertical fluid connectivity: fluid falls from a cell into the cell below when the upper
  cell has no fill and **no floor** (a true shaft — see the U-bend arms).

## Markers (object layer)

Point objects only (`<point/>`). Tile position = `(floor(x/16), floor(y/16))`; place points at
tile centers (`tileX*16+8, tileY*16+8`). The importer sorts annotations by `(z, objectId)` —
never hand-edit object ids to collide. Object `Class` (TMX `type` attribute) selects the kind:

| Class | Properties | Meaning |
|---|---|---|
| `light_source` | `luminance` int 0–31 | Static baked light (no heat — ruling §1.2 observer #9). |
| `script_anchor` | — | Named coordinate for scenario scripts (`ScriptedAction`/`SimCommand` targets). Names must be unique per map. |

## Fixture inventory

### `tavern_fixture.tmx` (48×32)

- **z:+0 street level.** Granite shell, footprint x 10–31 / y 6–24; two-tile street door at
  (16–17, 24) with granite threshold; brick street band y 26–31; dirt yard elsewhere. Oak
  floorboards, oak partition x=24 (doors at y=9, y=16), east-wing divider y=12 (door x=27),
  oak bar (x=21, y 9–13), six oak tables, granite hearth (15–16, 7). Four
  `trudgeon_wood@getilia_soak` beam posts at (14,13) (19,13) (14,21) (19,21) — these must
  survive the Tavern Fire golden. Stairs: oak `STAIR_UP` (28,8) → z:+1; granite `STAIR_DOWN`
  (28,20) → cellar. Markers: `torch_bar` (22,8), `torch_door` (15,23) — luminance 26;
  `ignition_point` on the oak table at (13,15) (the Tavern Fire script's tick-10
  `ExternalIgnition` target).
- **z:+1.** Thatch roof (form FLOOR) over the single-story common room x 10–21, with
  getilia-soaked trudgeon beam rows at y=13 and y=21 (directly above the z:+0 posts) and a
  granite chimney above the hearth. Upper story over the east wing x 22–31: granite shell,
  oak west wall, oak floor, oak `STAIR_DOWN` (28,8).
- **z:−1.** Solid granite except: cellar carve x 24–30 / y 16–23 (granite floor, three oak
  barrels, granite `STAIR_UP` (28,20)) and a 1-wide **sewer stub** at y=19, x 12–22, sealed
  from the cellar by a steel-grate WALL at (23,19), dead-ending in rock at x=11. Shallow
  water (depth 2) along x 12–21. Extend the city sewer westward from here later.

### `ubend_fixture.tmx` (24×16)

Sealed U-bend for the pressure test (`z < headZ` BFS). z:+0 is a solid granite slab with two
open shaft cells at (6,8) and (17,8) — no floor tiles there, so the shafts connect vertically.
z:−1 has the horizontal duct y=8, x 6–17 (granite floor, otherwise sealed in rock), prefilled
with water depth 7. The left arm is additionally primed with depth 7 at z:+0. Anchors:
`water_inlet` (6,8), `gauge_point` (17,8) — the M3 test injects at the inlet and asserts rise
at the gauge.

### `compound_block.tmx` (128×128 — 4×4 chunks)

DOCKS-GAZETTEER slice built on the corrected **Trojian Compound** housing typology (see
`docs/design/DECISIONS.md` "Trojian housing: Compounds" row and
`docs/design/DOCKS-GAZETTEER.md` §2.5): one large walled residential ecosystem — a
courtyard-farm ringed by condo/mansion units, built up over the courtyard's east wing, with
the rooftop of that taller wing walled off as informal slum housing — plus 2 street-frontage
businesses on the same block, outside the compound's walls. Supersedes an earlier prototype
that laid out "12 detached homes" ringing a shared plaza instead of one walled Compound
(`city_block.tmx` — removed once the Compound typology superseded it; a `home_NN` there would
have been one separate building, not a dwelling *unit* within a Compound as here).

- **Street + businesses (z:+0, y 0–31).** Brick street y 0–7. **K22 Netmenders' Arcade** —
  `thatch` shell x 8–47 / y 8–31, oak floor, door (26–27, 31). **K24 The Eel-Pots** —
  `trudgeon_wood` shell x 80–119 / y 8–31, dirt floor (open-air night stalls), door
  (98–99, 31). A dirt lane (x 48–79) between them drops south into the compound gate.
- **The Compound (z:+0, y 36–95).** Outer walls are `reman_concrete` throughout (the
  owning families' Reman-engineered construction); each unit's own perimeter wall doubles
  as the compound's ring wall, so no separate boundary wall is authored.
  - **courtyard_farm** — the namesake atrium, x 48–79 / y 54–77 (32×24), open-air `dirt`
    floor, no walls: the shared farm lot the whole Compound is organized around.
  - **mansion_01** — the whole west wing, x 8–47 / y 36–95 (40×60), single story: the
    owning family's mansion, deliberately far larger than any condo (the size variance
    *is* the wealth signal). Door (47, 64–67) opens straight onto the courtyard.
  - **condo_05** (x 48–63/y 36–53), **condo_06** (x 48–79/y 78–95) — smaller condo units
    closing the north and south sides of the ring, doors onto the courtyard.
  - **condo_02/03/04** — the east wing at ground level, x 80–119, split into three
    18–24-tall condos (y 36–53 / 54–77 / 78–95); condo_03's door opens onto the courtyard,
    condo_02's onto the gate corridor, condo_04's (the compound's outer corner, no
    courtyard-adjacent side) onto the outer yard to the south.
  - A 16-wide open gate corridor (x 64–79 / y 36–53, `dirt` floor, unwalled) is the
    Compound's one carted entrance, linking the street lane down into the courtyard.
- **Upper floor (z:+1, over the east wing only, x 80–119/y 36–95).** `condo_07/08/09`
  stand directly above `condo_02/03/04` — the same 3 ground units built up a second
  story (`reman_concrete` shell+floor), reached by `oak` `STAIR_UP`/`STAIR_DOWN` pairs at
  (85,40)/(85,60)/(85,82). The single-story wings (mansion, condo_05, condo_06) get a
  flat unpopulated `reman_concrete` roof deck at this level for visual completeness only
  (no dwelling units, same treatment as the tavern's thatch roof over its common room).
- **Roof slum (z:+2, x 80–119/y 36–95 — the footprint of the 2-story east wing only).**
  A `reman_concrete`-parapet deck, reached from condo_08 by an `oak` stair pair at
  (95,60). Three small, irregularly sized and placed tent/hut units —
  `roof_hut_10` (`leather`, 8×8), `roof_hut_11` (`cloth`, 10×12), `roof_hut_12`
  (`leather`, 8×10) — sit on `dirt` (mud) floors, deliberately off the tidy grid the
  condos below keep: the visual signal that this is informal, illegally-built housing,
  per canon "tents/mud huts... informal and flammable".
- Markers: `business_netmenders_anchor`, `business_eelpots_anchor`, `mansion_01_anchor`,
  `condo_02_anchor`…`condo_09_anchor`, `roof_hut_10_anchor`…`roof_hut_12_anchor` (12
  dwelling-unit anchors total), `courtyard_farm_anchor`; `lamp_compound_gate` /
  `lamp_courtyard_roof_deck` light sources (luminance 18 / 14).

## Provenance

Canon (novel wins; `LordOfTrojia-MVP/Documentation/WorldBible.md` §9): trudgeon trunks soaked
in getilia sap harden to stone-like density and become fireproof — hence the treated beams;
granite-shell/timber-interior tavern is genre-standard for Granadad. Layout geometry, torch
luminance 26, sewer/water depths, and the U-bend dimensions are **invented for v0 balance,
need Eli's blessing**. Nothing here fixes art: appearance mapping keys on the registry's
appearance buckets, not on these tiles.

`compound_block.tmx`'s Compound typology is canon (Eli, 2026-07-13 — see
`docs/design/DECISIONS.md` "Trojian housing: Compounds" and the
`canon-trojian-compounds` memory note): courtyard-farm atrium, condo/mansion ring in
Reman-engineered construction, rooftop slum in cloth/leather/tent construction over the
built-up wing. `reman_concrete`/`leather`/`cloth` raws (`content/raws/materials/`) are the
canon-mandated materials for that gradient. Its K22/K24 placement follows DOCKS-GAZETTEER
(novel wins); every other coordinate, the courtyard/ring/roof-slum layout, unit count split
(1 mansion + 8 condos at ground/upper + 3 roof huts = 12), and the specific dimensions are
**invented for v0, need Eli's blessing** — the gazetteer fixes establishments, canon fixes
the typology, but not this map's specific geometry. Folded back into DOCKS-GAZETTEER.md §2.5
as the canonical Compound reference (this map is cited there as the worked precedent).
