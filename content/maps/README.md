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
| `src/materials.tsx` | Shared imageless tileset. One tile per (material, form) pair in use, plus fluid tiles. 16×16 px declared size. |
| `src/tavern_fixture.tmx` | 48×32, z-levels −1/+0/+1. The Tavern Fire flagship stage (M1/M2 acceptance: imports byte-identical twice). |
| `src/ubend_fixture.tmx` | 24×16, z-levels −1/+0. Sealed U-bend duct for the M3 fluids pressure test. |

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
  phorys, lightstone, lightstone_shards, glowstone, ash` and the treatment-minted
  `trudgeon_wood@getilia_soak`.
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

## Provenance

Canon (novel wins; `LordOfTrojia-MVP/Documentation/WorldBible.md` §9): trudgeon trunks soaked
in getilia sap harden to stone-like density and become fireproof — hence the treated beams;
granite-shell/timber-interior tavern is genre-standard for Granadad. Layout geometry, torch
luminance 26, sewer/water depths, and the U-bend dimensions are **invented for v0 balance,
need Eli's blessing**. Nothing here fixes art: appearance mapping keys on the registry's
appearance buckets, not on these tiles.
