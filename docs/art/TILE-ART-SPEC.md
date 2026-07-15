# TILE-ART-SPEC — placeholder tile art & the art-swap seam (v0)

**Status:** authoritative for `client-observer` art resolution and for the placeholder atlas.
Parent spec: `ARCHITECTURE.md` §3 (client-observer package entry), §10 (raws / color stops),
§1.1 #17 (one material + form), #19 (light 0–31). Where this document and ARCHITECTURE.md
disagree, ARCHITECTURE.md wins.

Owned files: this spec + `content/art/placeholder/art-mapping.json` (procedural fallback pack)
+ `content/art/kenney/art-mapping.json` (the shipped real Kenney 1-bit pack; §11 vocabulary,
§12 cosmetic variants).

---

## 1. Seams and responsibilities

Per ARCHITECTURE.md §3, art resolution is split across a GL-free / GL boundary:

| Type | Side | Responsibility |
|---|---|---|
| `TileArtResolver` (iface) | GL-free | `(materialId, form, appearanceBucket) → regionIndex` — pure index math, unit-testable headless |
| `JsonTileArtResolver` | GL-free | Implementation backed by `art-mapping.json`; resolves ALL fallbacks at load into a dense lookup table; render path is a branch-free array read |
| `AtlasRegionTable` | GL | Owns the loaded `TextureAtlas`; interns region **names → int indices** at boot; hands the GL-free side only ints |
| `ZPeekResolver` | GL-free | Decides which z-level shows through OPEN tiles and at what peek depth `d` (0..3) |
| `LayeredWorldRenderer` / `RenderPass` | GL | Applies the tint pipeline (§5) as batch color multiplies |

The **art-swap proof** (PLAN-v0 M7) is: replace the entire placeholder look by editing
`art-mapping.json` (and pointing `atlas` at a new pack) — zero code changes, no new
`TileArtResolver` implementation required for a pure reskin.

## 2. Keying model

A drawn tile is keyed by exactly three inputs:

1. **`materialId`** — canonical raws id string (registry interns to short; resolver works on the ordinal).
2. **`form`** — `TileForm` enum value (ARCHITECTURE §1.1 #17: fill ⇒ `(fillMat, form)`; floor ⇒ `(floorMat, FLOOR)`; else OPEN, which is never drawn). The v0 placeholder pack covers two form tokens: the default solid form (token omitted) and `floor`. Unknown/new forms fall back per §7.3 — adding a `TileForm` value never blocks on art.
3. **`appearanceBucket`** — **color-stop ordinal 0..3**, served by `AppearanceQuery` (ARCHITECTURE §10: "Appearance bucket = color-stop ordinal (0..3); art mapping keys `byAppearance` on it"). Definition: the index of the material's active `colorStops` entry — the first stop whose `uptoPct` ≥ current charge percent. Materials without a `chargeable` feature are always bucket 0. Bucket changes arrive observer-side via `ChargeStopChangedEvent` / `ChunkRevisions`; the resolver itself is stateless.

Chromatis is the normative example: 3 stops ⇒ buckets 0 (silver-blue, ≤60%),
1 (orange, ≤95%), 2 (gold, ≤100%). The 0..3 range allows a 4-stop material later.

**Clamping rule:** if `AppearanceQuery` reports bucket `b` but the mapping entry defines
fewer regions, the resolver uses `byAppearance[min(b, len-1)]`. Clamp-high, never wrap —
an over-charged material saturates visually, it does not cycle.

## 3. Atlas region naming convention

```
regionName := <materialId> [ "." <form> ] [ ".a" <bucket> ]
```

- `<materialId>` — the canonical raws id, **verbatim**, including derived-material ids
  (`trudgeon_wood@getilia_soak`). Region names are free strings inside a libGDX `.atlas`
  file; `@` is legal there and in the JSON. (Caveat for future packs built from per-region
  PNG files via TexturePacker: avoid `@` and trailing `_<digits>` in *filenames* — the
  mapping JSON is the shim that aliases any pack-side name back to the canonical key,
  e.g. point `trudgeon_wood@getilia_soak` at a region named `trudgeon_wood_treated`.)
- `<form>` — lowercase `TileForm` name. **Omitted for the default solid form** (`block`).
- `<bucket>` — appearance bucket ordinal. **Omitted for bucket 0.** Segment order is
  always material, then form, then bucket.

Examples: `granite` · `granite.floor` · `chromatis` · `chromatis.a2` ·
`chromatis.floor.a1` · `trudgeon_wood@getilia_soak.floor`.

Reserved prefixes: `fx.` (fire/smoke/overlay effects, post-v0), `ui.` (HUD/brush icons).
The region `missing` (magenta/black checker) is reserved as the universal fallback.

**Deterministic packing:** the placeholder atlas is NOT produced by TexturePacker. The
generator lays regions out row-major in **ascending ASCII byte order of region name** on a
fixed 128×128 px sheet (8 columns × 16 px cells) and writes `placeholder.png` +
`placeholder.atlas` (standard libGDX atlas text format) directly. Generator output must be
**byte-deterministic** (same rule as the Tiled importer, ARCHITECTURE §9) — no timestamps,
fixed PNG encoder settings. The gdx-tools `:packArt` task is reserved for real art packs,
where byte-determinism is not required.

## 4. The 16 px grid

- Every tile region is exactly **16×16 px**. World rendering is pixel-snapped: nearest
  filtering, integer zoom levels only (1×, 2×, 3×…), camera translation snapped to whole
  screen texels. Under those rules a 0-padding atlas cannot bleed.
- If a future pack wants linear filtering or free zoom, repack via `:packArt` with
  `padding 2` + `duplicatePadding` — a pack/mapping change, not a code change.
- Multi-tile art (2×2 furniture etc.) is out of scope for v0; the naming grammar leaves
  room (`<id>.<form>.a<b>.<part>`) but nothing consumes it yet.

## 5. Render tint pipeline

Final texel color for a tile drawn at light level `L` (0..31) and peek depth `d` (0..3):

```
rgb_out = (((rgb_region * lightTintQ8[L]) >> 8) * zPeekDimQ8[d]) >> 8      (per channel)
```

Both tables ship in `art-mapping.json` so a pack swap can re-mood the world without code.
All factors are Q8 (256 = 1.0). Applied as a single batch color multiply; the glyph is
part of the region texels and dims with everything else.

### 5.1 Light-level tinting (0–31)

`L = effectiveBrightness` from `LightQuery`: `max(block, (sky * celestial) >> 5)`,
celestial fixed-point 0..32 (ARCHITECTURE §1.1 #19). The curve is quadratic with a floor —
dark areas stay legible because the observer is a god-view debugging client, not a stealth
game:

```
lightTintQ8[L] = 36 + floor(220 * L² / 961)        // L in 0..31; [31] = 256 exactly
```

| L | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| tint | 36 | 36 | 36 | 38 | 39 | 41 | 44 | 47 | 50 | 54 | 58 | 63 | 68 | 74 | 80 | 87 |

| L | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 | 26 | 27 | 28 | 29 | 30 | 31 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| tint | 94 | 102 | 110 | 118 | 127 | 136 | 146 | 157 | 167 | 179 | 190 | 202 | 215 | 228 | 242 | 256 |

Optional per-material `minLight` (0..31) in the mapping clamps `L' = max(L, minLight)` for
that material's tiles only — a cosmetic safety valve for emissive materials (glowstone
ships `minLight: 8`). It never feeds back into sim light values.

### 5.2 Z-peek dimming

`ZPeekResolver` (GL-free) walks down from the camera z through OPEN tiles and tags each
drawn tile with peek depth `d` = number of z-levels below the camera level. Dimming:

```
zPeekDimQ8 = [256, 168, 112, 76]        // d = 0..3; maxPeekDepth = 3
```

Below depth 3 nothing is drawn; the cell renders as flat `voidColor` (`#0E0E12`).
Values invented for v0 balance, need Eli's blessing (they only affect the observer).
Dimming multiplies **after** light tint (formula above); no desaturation pass in v0.

### 5.3 Fluid overlay (water)

Pooled fluid lives in the FLUID lane (depth 0–7) and renders as an overlay quad of the
`water` region on top of the floor tile, alpha-scaled by depth, then tinted by the same
light/peek pipeline:

```
depthAlphaQ8 = [0, 96, 120, 144, 168, 192, 216, 240]      // index = depth 0..7
```

Ice is NOT a fluid render: `FluidFrozenEvent` → PhaseTransition places grid material
`ice`, which resolves like any other material (§6 table).

### 5.4 Per-material tint: secondary adjustment, not the color source

**Current rule (Eli 2026-07-13, full-color Kenney art register).** Each region's sheet
cells already carry their own baked color. `WorldRenderer` still multiplies every tile's
quad by `TileArtResolver.materialTintRgb(materialId)` before drawing (§5's formula runs on
top of whatever this multiply produces), but for most materials in
`content/art/kenney/art-mapping.json` that field is simply **absent**, which resolves to
`TileArtResolver.NO_TINT` and leaves the batch color white — the cell draws exactly as
authored. A minority of materials still carry a `tint`, used only as a deliberate
**secondary** adjustment where it earns its keep: the shipped palette has no cell in the
hue a material's lore demands (`phorys` teal, `chromatis_melt` gold), or two materials
share one region's cells and would otherwise be visually identical (`steel` vs
`reman_concrete`, both on `wall_masonry`; `dirt`/`ash`/`glowstone`, all on `wall_rubble`).
Every surviving tint's reasoning is recorded in that material's `notes` field in the
mapping — read those before adding a new one. Because batch-color tinting is strictly
**multiplicative**, a tint can only darken or re-hue a texel; it can never lighten it past
the sprite's own baked color (multiplying by white, `#FFFFFF`, is a no-op, and that is the
ceiling). A material that wants to look *paler* than its region's native cell (the `ice`
case) cannot get there via tint — it needs a different, already-paler sheet cell instead.

**Superseded rule, kept for history.** Before this ruling, the shipped pack
(`monochrome-transparent_packed.png`) was white-on-transparent grayscale and *every*
material carried a saturated `tint` as its **primary** color source — one shared grayscale
wall/floor sprite served many materials, each glowing its own flat color on the black void
(DECISIONS.md "Luminous-on-black" register, Eli 2026-07-12, itself reversed the next day).
Region names were shared across materials expecting the full-strength tint to fully repaint
them; several of those monochrome-era cell picks do not carry over unchanged to the colored
sheet (§11 explains why) because a cell chosen only for its grayscale *shape* can bake to an
unexpected color once the pack itself supplies one (e.g. the old `wall_stone` set included a
cell that bakes solid **blue** in the colored sheet, not beige — it moved to the new
`wall_crystal` region instead of staying put and fighting its own tint).

## 6. Placeholder art: flat color + glyph

Every placeholder region is generated, never drawn by hand:

- **Block (default form):** 16×16 solid fill of the material color, a 1 px outline of
  `color * 128 >> 8` (per channel), and the material glyph from a built-in 8×8 ASCII
  (0x20–0x7E) bitmap font, centered at (4,4), in the listed glyph color.
- **Floor:** fill and outline scaled by `184/256`; glyph blended 50 % toward the fill.
- **missing:** 8 px checker of `#FF00FF` / `#000000`, no glyph.
- **water:** solid fill, glyph `~`, drawn at full alpha in the atlas (depth alpha is
  applied at render time, §5.3).

### 6.1 Deterministic color mint

Default color for any material id (used automatically for ids not overridden below):

```
h    = FNV-1a-64(utf8(id))                    // offset 0xCBF29CE484222325, prime 0x100000001B3
hue  = h mod 360
sat  = 45 + ((h >> 16) mod 25)                // 45..69 %
lit  = 34 + ((h >> 32) mod 18)                // 34..51 %
color = HSL(hue, sat, lit) → sRGB, rounded
glyphColor = black if WCAG relative luminance > 0.30 else white
```

Hashing the **full id string** means treatment-derived materials
(`trudgeon_wood@getilia_soak`) get distinct colors for free. The values in the table below
are **normative once listed** — the hash is the mint rule for future ids, not a licence
for the generator to drift if its HSL rounding differs; the generator must reproduce this
table exactly (it is asserted by test against `art-mapping.json`).

### 6.2 Normative placeholder table (v0 vocabulary)

| materialId | color | glyph | glyph color | source |
|---|---|---|---|---|
| granite | `#A9328F` | `G` | white | hash |
| dirt | `#29679E` | `D` | white | hash |
| oak | `#24768F` | `O` | white | hash |
| thatch | `#D62971` | `H` | white | hash |
| trudgeon_wood | `#A6433A` | `T` | white | hash |
| trudgeon_wood@getilia_soak | `#805B2D` | `t` | white | hash |
| steel | `#461E94` | `S` | white | hash |
| brick | `#335899` | `B` | white | hash |
| chromatis (bucket 0) | `#9FB8D8` | `C` | black | **canon** — ARCHITECTURE §10 colorStops / WorldBible §9 silver-blue |
| chromatis (bucket 1) | `#E3CE7A` | `C` | black | HYBRID ruling (Eli 2026-07-12): fill ramp is silver→pale-gold→gold; orange `#E8842A` moved to the `heatGlowTint` overlay, rendered only while discharging or saturation-heating — that's canon's "orange when hot" |
| chromatis (bucket 2) | `#F5C542` | `C` | black | **canon** — gold at full charge |
| chromatis_melt | `#CEAC27` | `c` | black | hash (happily lands molten-gold) |
| phorys | `#2A6993` | `P` | white | hash |
| lightstone | `#51D530` | `L` | black | hash |
| lightstone_shards | `#A5B635` | `*` | black | hash |
| glowstone | `#B22D2D` | `g` | white | **canon override** — WorldBible §9 "eerie red light" (hash gave purple) |
| ash | `#B6BD28` | `A` | black | hash |
| ice | `#A9D4E8` | `I` | black | override, invented for v0 legibility, needs Eli's blessing (hash gave purple) |
| water (fluid) | `#3F6FB5` | `~` | white | override, invented for v0 legibility, needs Eli's blessing (hash gave olive) |

Full placeholder region inventory = the 16 grid materials above × {block, floor}
(chromatis × 3 buckets × 2 forms) + `water` + `missing` = **38 regions**, fits the
128×128 sheet (64 cells) with room to grow.

## 7. `content/art/placeholder/art-mapping.json`

### 7.1 Schema (schemaVersion 1)

| Field | Type | Meaning |
|---|---|---|
| `schemaVersion` | int | must be `1` |
| `atlas` | string | path to the `.atlas` file, relative to the `content/` root |
| `tilePx` | int | must be `16` in v0 |
| `missingRegion` | string | universal fallback region name |
| `voidColor` | `#RRGGBB` | fill for cells beyond maxPeekDepth |
| `lightTintQ8` | int[32] | §5.1 table; each 0..256, monotone non-decreasing, last = 256 |
| `zPeekDimQ8` | int[] | §5.2 table; first = 256, monotone non-increasing; length = maxPeekDepth+1 |
| `materials.<id>.forms.<form>.byAppearance` | string[1..4] | region names indexed by appearance bucket |
| `materials.<id>.minLight` | int 0..31, optional | §5.1 cosmetic clamp, default 0 |
| `fluids.<fluidId>.region` | string | overlay region |
| `fluids.<fluidId>.depthAlphaQ8` | int[8] | §5.3; `[0]` = 0, monotone non-decreasing, each ≤ 256 |
| `placeholderGen` | object | generator input only (§6 colors/glyphs); **ignored by `JsonTileArtResolver`** |
| `provenance`, `notes` | string, optional | anywhere; ignored by loaders (same ruling as raws) |

Unknown fields are ignored (raws convention). Form keys are lowercase `TileForm` names;
`block` is the default form.

### 7.2 Load-time validation (boot fails)

- `schemaVersion` == 1; `tilePx` == 16; atlas file exists.
- Every region name referenced anywhere resolves in `AtlasRegionTable` (report the full
  list of missing names, not just the first).
- `byAppearance` arrays length 1..4; table shape/monotonicity rules of §7.1.
- Warnings, not errors: registry material with no mapping entry (renders as `missing`),
  mapping entry with no registry material (art may lead content).

### 7.3 Fallback resolution — at load, never at render

`JsonTileArtResolver` bakes a dense `short[materialOrdinal][formOrdinal][4]` table:

1. exact `materials[id].forms[form].byAppearance[min(b, len-1)]`
2. else `materials[id].forms["block"]` (same bucket clamp)
3. else `missingRegion`

The render path is one array read; all branching above happens once at load.

## 8. Swapping in a real pack

Candidate CC0-ish 16 px packs (do **not** vendor until licenses are re-verified):
**Kenney 1-bit Pack** (CC0) and **DawnLike** (commonly distributed CC-BY 4.0 — attribution
required, verify before shipping). Swap procedure:

1. Run the pack through `:packArt` (TexturePacker) into `content/art/<pack>/<pack>.atlas`.
2. Copy `art-mapping.json`, point `atlas` at the new file, rewrite the region names inside
   `byAppearance` to the pack's names (the JSON is the alias layer — pack names need not
   follow §3's grammar). Adjust `lightTintQ8` / `zPeekDimQ8` mood to taste.
3. Delete or keep `placeholderGen` — the resolver ignores it either way.

No Java changes. That is the M7 art-swap acceptance test.

## 9. Out of scope for v0

Fire/smoke overlays (`fx.` prefix reserved; Fire renders via ON_FIRE flag pass, art TBD),
brush cursors and HUD icons (`ui.`), animation frames, multi-tile art, autotile/blob
transitions.

**Re-scoped in (Eli, 2026-07-13):** cosmetic per-tile texture variety — several
interchangeable sheet cells per logical region, selected deterministically by tile position
— is now **in** scope and shipped in the Kenney pack. See §11 (the real-pack tile vocabulary)
and §12 (the variant mechanism). What stays out: this is *cosmetic* variety only, not
*per-biome* material substitution (a biome swapping which material a tile is made of is a
content/worldgen decision, not an art one) and not autotile/blob edge transitions or
animation frames — those remain out per the list above.

## 10. Open questions for Eli

1. **`ice` material id** — this spec and the mapping assume the materials crew ships grid
   material id `ice` (required by `FluidFrozenEvent` → PhaseTransition ice placement,
   ARCHITECTURE §5). Confirm the id.
2. **Hash-color legibility** — pure hash gives lore-blind hues (granite magenta, steel
   purple, oak teal). Canon forced two overrides (chromatis stops, glowstone red); water
   and ice got legibility overrides. Bless or extend the override list (mechanism: edit
   `placeholderGen` colors, one JSON change).
3. **z-peek numbers** — `[256, 168, 112, 76]`, maxPeekDepth 3, invented for v0.
4. **Lightstone buckets** — if the materials crew gives lightstone `colorStops` (it is
   chargeable: 5,000 cu / 2,000 spike), add `lightstone.a1`… regions; until then the
   resolver's clamp rule makes single-region lightstone correct by construction.
5. **Runtime packaging** — `content/art/placeholder/` sits beside `content/src/main/
   resources/`; whether the mapping+atlas are copied onto the classpath by the content
   build or read from the content dir by the observer is build wiring owned by the
   observer/build crew (this spec only fixes the file's schema and location).

## 11. Real-pack tile vocabulary (Kenney 1-bit)

**Current pack (Eli 2026-07-13 art register, full-color Kenney).** The shipped sheet is
`content/art/kenney/Tilesheet/colored-transparent_packed.png` — the same **49×22** grid of
**16 px** cells as the monochrome sheet (confirmed against `Tilesheet.txt` and by direct
pixel measurement: both are 784×352 px, i.e. 49×22 cells with no inter-tile spacing), but
full-color-on-transparent instead of white-on-transparent. Cell `[col, row]` occupies pixel
rect `(col*16, row*16, 16, 16)`, unchanged. Cells below were chosen by zoom-reading and
pixel-sampling the *actual colored sheet* (composited over a mid-gray background, cropped
into labelled bands), not assumed to carry over unchanged from the monochrome-era picks —
several do not (see the §11.1 palette note and the historical subsection below for why).

### 11.1 The sheet's palette is a small fixed set, not free color

Measuring the dominant opaque color of every cell in the terrain/architecture area (rows
0–21, all 49 columns) turns up almost the whole sheet baked from just **seven** flat
colors — this is a recolored 1-bit pack, not a painted-per-tile pack:

| swatch | hex | role on the sheet |
|---|---|---|
| beige/bone | `#CFC6B8` | the overwhelming default — most walls, floors, masonry, dungeon-UI props |
| brown/orange | `#BF7958` | wood: fences, planking, furniture |
| red/orange | `#E6482E` | brick, hazard/"missing" markers |
| blue | `#3CACD7` | water, ice/glass panels, a handful of blue creature sprites |
| green | `#38D973` | foliage, grass speckle, a green creature family |
| maroon/rose | `#7A444A` | rubble/dirt speckle, weave patterns |
| gold | `#F4B41B` | crowns, coins, torches — **item icons only**, no tileable gold surface exists |

This matters for material design: several lore-distinct materials that used to be told
apart purely by a saturated `tint` over identical grayscale cells (granite vs steel vs
reman_concrete vs ice vs chromatis, all "stone-family") now have to share one of seven
native hues, differentiated by **shape** (which region name) and, where that still is not
enough, a secondary tint (§5.4) — not by inventing colors the sheet does not ship. There is
no tileable gold-baked surface at all (the only gold cells are crown/coin/torch icons), so
`thatch` and `chromatis_melt` are the two materials that keep a tint purely to supply a hue
absent from the pack outright, not to differentiate from a sibling material.

Region names are **role-named categories**, not the §3 `<materialId>` grammar — legal per §8
("pack names need not follow §3's grammar; the JSON is the alias layer"). Each category is a
**variant SET** (§12); several materials can share a category, told apart by shape family,
by their (usually absent) `tint`, or both.

**Wall / `block`-form textures**

| region | baked color | look | cells `[col,row]` |
|---|---|---|---|
| `wall_brick` | red | brick/shelf coursing | (6,15) (7,15) (6,14) |
| `wall_stone` | beige | smooth dressed ashlar panel | (1,18) (19,18) (7,17) |
| `wall_rubble` | maroon | speckle + basket-weave rough texture | (10,0) (11,0) (16,0) (17,0) |
| `wall_hatch` | beige | crosshatch — wattle/plaster/veining | (1,4) (2,4) |
| `wall_plank` | brown | vertical picket fencing | (13,16) (14,16) |
| `wall_masonry` | beige | coursed ashlar with horizontal bands (reads as cast/engineered) | (19,13) (20,13) (21,13) |
| `wall_crystal` | blue | flat glassy/crystalline panel — **genuinely baked blue**, not a tint | (8,5) (9,5) (10,5) (11,5) |
| `wall_moss` | green | blobby natural mineral/moss chunk | (18,11) (19,11) (20,11) |

**Floor / `floor`-form textures**

| region | baked color | look | cells `[col,row]` |
|---|---|---|---|
| `floor_tile` | beige | fine tiled/checker floor | (1,17) (2,17) (19,17) |
| `floor_stone` | beige | smooth flagstone slab (shares `wall_stone`'s cells) | (1,18) (19,18) (7,17) |
| `floor_plank` | brown | horizontal decking + reused picket cells | (4,6) (7,5) (13,16) (14,16) |
| `floor_earth` | maroon | speckle (shares `wall_rubble`'s cells) | (10,0) (11,0) (16,0) (17,0) |
| `floor_cobble` | maroon | the denser basket-weave/grid subset of `floor_earth` | (16,0) (17,0) |
| `floor_crystal` | blue | shares `wall_crystal`'s cells | (8,5) (9,5) (10,5) (11,5) |
| `floor_moss` | green | sparse-to-dense grass speckle | (5,0) (6,0) (7,0) |

**Roof textures** (drawn on a `block`-form tile whose *material* is a roofing material —
there is no mechanical "roof" `TileForm`; a rooftop is a `WALL`/`FLOOR` tile of thatch /
cloth / leather per the Trojian-compounds canon, ARCHITECTURE/WorldBible)

| region | baked color | look | cells `[col,row]` |
|---|---|---|---|
| `roof_thatch` | brown | reuses the horizontal-decking wood cells; `thatch` tints it straw-gold (§5.4 — no gold tileable cell exists) | (7,5) (4,6) |
| `roof_tile` | beige | clay tiles/shingles from above (documented + available; not yet assigned to a material) | (10,18) (11,18) (10,17) |
| `roof_cloth` | beige | reuses `wall_hatch`'s crosshatch cells (reads as woven fabric); `leather`/`cloth` each tint it dim brown/tan | (1,4) (2,4) |

**Fluid & fallback**

| region | baked color | look | cells `[col,row]` |
|---|---|---|---|
| `water` | blue | irregular pond-edge and ripple blob shapes — distinct cells from `wall_crystal`'s flat panels, no tint needed | (8,4) (9,4) (10,4) (11,4) (12,4) |
| `missing` | red | framed X — reads as invalid/unmapped (happens to bake the same red family as brick; distinguishable by its X shape) | (13,18) |

Cells are deliberately shared across roles where one texture serves both (e.g. the smooth
beige panel serves both `wall_stone` and `floor_stone`; the crosshatch cells serve both
`wall_hatch` and `roof_cloth`). The current material→region assignment, including every
surviving `tint` and why, lives in `content/art/kenney/art-mapping.json`'s per-material
`notes`/`provenance`.

Not every terrain tile on the sheet is mapped — this is the useful, tileable subset. More
categories (fences, doors, windows, furniture, slope/ramp diagonals, stairs, the sheet's
character/monster/item area in the right ~25 columns) exist on the sheet and can be added
the same way when a consumer needs them.

### 11.2 Historical note: the monochrome-tint pass (superseded 2026-07-13)

Before the full-color ruling, this section described
`content/art/kenney/Tilesheet/monochrome-transparent_packed.png` — the same grid,
white-on-transparent — with region cells chosen purely for **grayscale shape**, since every
material's saturated `tint` was multiplied in as the primary color (§5.4 historical note).
Several of those cell choices do not carry over unchanged to the colored sheet: a cell
picked only because its silhouette looked like "smooth stone" can bake to an unrelated
color once the pack supplies one. The clearest example: the old `wall_stone` variant set
included cell `(9,5)`, chosen because it read as a plain white panel in monochrome — in the
colored sheet that exact cell bakes solid **blue** (it is one of the pack's water/glass
tiles wearing the same silhouette as the stone panels). It was moved to the new
`wall_crystal` region rather than kept in `wall_stone` fighting its own baked color. The old
`wall_brick` set mixed genuinely-red cells with beige ones that turned out to belong to the
`roof_tile` shape family instead; the old `floor_plank`/`roof_thatch` sets mixed brown-wood
cells with beige-stone ones for the same reason. The lesson generalized into §11.1's
palette table: a monochrome silhouette is not a reliable predictor of a colored pack's baked
hue, so every cell in the table above was re-verified by direct pixel sampling of the actual
colored sheet, not carried over from the superseded picks.

### 11.3 Fifth revision (2026-07-15): true-black void, ramp/stair, Roman civic facades

**DECISIONS.md Art register, FIFTH revision (Eli 2026-07-15).** Muse: Dwarf Fortress's own
visual language translated from ASCII glyphs to the same Kenney sheet used since §11.1 — no
second pack mixed in. Three concrete changes to `content/art/kenney/art-mapping.json`, all
pure content edits (no code, no schema change):

1. **`voidColor` → `#000000`.** Was `#180F14` (a warm plum-black, §11's fourth-revision
   mood target). True black per the explicit directive; fills cells beyond max z-peek depth
   only — the lit-but-dark dimming curve (§5.1/§5.2) is byte-identical.
2. **Ramp/stair regions, newly mapped.** The fourth revision never gave the Kenney pack a
   RAMP or STAIR form at all (only `custom` had one) — every material now also resolves
   `ramp`/`stair`, both pointing at two newly-picked cells: `slope_a` (three diagonal-corner
   slope cells, `(0,16)`/`(4,16)`/`(0,18)`, native beige) and `stair_rung` (`(21,1)`, the
   middle rung-row of a 3-cell ladder graphic — the top/bottom cells carry end-caps and don't
   tile alone). Caveat: `chromatis`/`lightstone`/`ice` have no baked-hue slope cell in this
   sheet, so their ramps/stairs render in `slope_a`/`stair_rung`'s native beige rather than
   their signature color (§5.4's tint-can-only-darken rule) — low impact, since none of the
   three are placed on the Docks surface map today.
3. **Civic facade regions + materials — the Rome cue.** Three new sheet cells read as
   genuine classical-order silhouettes (re-verified at 24× zoom, not assumed from
   `Preview.png`): `facade_cornice` `(6,4)` (banded cornice/window register over a column row
   over a base band, beige), `facade_colonnade` `(6,5)` (a pedimented mini-portico — arched
   cornice over four column shafts on a plinth, terracotta), `facade_baluster` `(7,5)` (the
   same shaft spacing without the pediment cap, terracotta). No fluted-shaft-and-capital
   asset exists anywhere in the extracted Kenney catalog; these three are the closest real
   thing the pack ships and read unambiguously as "columned facade." Three new **civic-only**
   material ids — `granite_facade`, `brick_facade`, `reman_facade` — are physical clones of
   `granite`/`brick`/`reman_concrete` (identical raws stats, `content/raws/materials/
   *_facade.json`, minted via the `add-material-raw` skill) that exist purely to carry a
   *different* wall art mapping. `gen_docks_surface.py` paints them onto exactly four
   street-facing frontage wall runs — the Weighhouse (K01) and Mission of the Flame (K17)
   north edges (`granite_facade`), the King's Bond (K12) north edge (`brick_facade`), and the
   Quayward Compound's (C1) east gate wall (`reman_facade`) — leaving every other
   granite/brick/reman_concrete wall in the district plain. This is the mechanism for
   "pillars on some buildings, not all": a distinguishing key has to live at the *material*
   level, since `WorldRenderer` keys art purely on `(materialId, form, appearanceBucket)` with
   no "which building" input, and adding one would be a render-layer regression, not a
   content one.

Actor sprites (`SpriteIndex`) and `FaceGen` are unchanged — still the custom MERCOLAS-24
sheet (a separate parallel workflow's territory; see DECISIONS.md's fifth-revision entry for
the full reasoning). Torch/brazier light pools (`content/art/kenney-light-masks/`) are a
flagged fast-follow: an additive `WorldRenderer` overlay pass over the existing multiplicative
light-tint pipeline, not required to land the void/facade changes above.

### 11.4 Sixth revision (2026-07-15): dominant-surface atlas swap to the Roguelike City Pack

**DECISIONS.md Art register, SIXTH revision (Eli 2026-07-15).** The fifth revision's
infrastructure (true-black void, the three civic-facade materials, the light/peek pipeline,
the cosmetic-variant hash) was correct, but its dominant-tile SELECTION off the Kenney 1-Bit
sheet read as an ugly low-contrast **icon-noise carpet**: the 1-Bit pack is fundamentally an
atlas of ~1024 tiny distinct glyphs, not a seamless terrain tileset, so a large granite/dirt/
brick surface tiled into a restless field of square-in-square icons rather than solid
masonry. This revision keeps every fifth-revision invariant and re-**skins** only which sheet
the dominant surfaces draw from, swapping `atlas` to Kenney's **Roguelike Modern City Pack**
(CC0, `content/art/kenney/CityPack/tilemap_packed.png`, `License.txt`; **37×28** @16 px, zero
spacing — the `SheetAtlasSpec` grid mechanism is unchanged, only the sheet and cell coords
differ, still zero Java change). That pack's Tilemap is a genuine seamless masonry terrain
set: solid low-frequency red-brick / grey-ashlar / warm-sandstone coursing walls and grey/tan
flagstone + tilled-earth + grass floors that tile into calm, weighty, architectural masses
(the Dwarf-Fortress-mass muse + Eli's gritty-Rome cue), not icon noise. Concrete changes, all
pure content edits in `content/art/kenney/art-mapping.json` (see its `regionsProvenance` /
per-material `notes` for exact cells and reasoning):

1. **`atlas` / `sheet` → City Pack, 37×28.** Every `wall_*/floor_*/roof_*/water/slope/stair`
   region re-pointed at the **interior (border-free)** cells of each City-Pack building block
   so coursing/flagstone tiles seamlessly (verified by tiling proofs into masses).
2. **Palette pushed to warm-gritty-Rome.** Granite — the district's *dominant* material —
   gains a warm travertine `#D8C6A2` multiply so the whole city reads as warm Roman dressed
   stone rather than monochrome grey; `dirt`/`ash`/the timber ids/`glowstone` retuned to keep
   the warm register coherent. `fluids.water` gains a `#4E9AB0` harbor-teal tint (honored by
   `JsonTileArtResolver.fluidTintRgb`).
3. **Facade regions renamed by stone.** The fifth revision's shared-hue
   `facade_cornice/facade_colonnade/facade_baluster` are replaced by stone-matched
   `facade_granite/facade_brick/facade_reman` — one grand multi-register **stone elevation
   block** per civic material (grey / red-brick / sandstone), since a single portico cell
   cannot serve three stone colours. These read as ornate windowed ashlar frontages, a
   stronger Rome cue than the 1-Bit portico glyph the fifth revision itself flagged as weak.
   No literal free-standing colonnade/arch cell exists in the City Pack (its vertical-rhythm
   cells are modern glass storefronts), so this is a "grand elevation, not free-standing
   columns" compromise — flagged, honest, palette-coherent.

**Deliberate compromise (flagged for Eli).** The City Pack ships **no seamless wood surface**
— every wood cell is a discrete crate/board on transparency that tiles into a pile of objects
with black gaps — so `oak`/`trudgeon_wood` route onto the solid sandstone coursing shape with
warm-brown multiplies, reading as solid **timber-framed masonry masses** rather than broken
crate cells. Trades literal wood-grain for solidity, the right call under the DF-mass muse +
single-atlas constraint, low-impact in this stone/brick-dominated Docks district.

`voidColor` (`#000000`), `lightTintQ8`, `zPeekDimQ8`, `tilePx` (16) are **byte-identical** to
the fifth revision. `SheetAtlasSpecTest.ShippedKenneyPack` updated for the new grid dimensions
(37×28) and City-Pack variant counts. §11.1–§11.3 above describe the *superseded* 1-Bit sheet
and are kept for history; the live pack is the City Pack described here.

## 12. Cosmetic tile variants (real-pack)

**Problem.** With one sheet cell per logical region, a large granite wall or dirt floor
draws the *same* 16 px sprite in every cell — a flat, obviously-repeating grid. Real
tilesets break this up with several interchangeable looks per surface.

**Schema.** In a sheet pack's `regions` map, a region value is **either** a single
`[col, row]` pair (one variant, the shorthand) **or** an array of pairs
`[[col,row], [col,row], …]` (several cosmetic variants). Parsed GL-free by `SheetAtlasSpec`;
every pair is bounds-checked against `sheet.columns`/`sheet.rows` at boot, aggregating all
defects into one `ArtMappingException` (same "boot fails" rule as §7.2). Order is preserved
and significant only in that it fixes each variant's index.

```
regions.<name> := [col, row]  |  [ [col,row], [col,row], … ]     (each pair 0-based, in-bounds)
```

**Selection.** `WorldRenderer` picks a variant per drawn tile:

```
count   = atlas.variantCount(regionName)                       // ≥ 1
variant = count ≤ 1 ? 0
        : floorMod( hash(x, y, z, materialLane, formOrdinal), count )
region  = atlas.region(regionName, variant)
```

`hash` is a MurmurHash3-style integer mix of the tile's **world position** plus a
material/form salt (`WorldRenderer.cosmeticVariant`). It is a **pure function** — no RNG, no
stored state — so:

- **Deterministic & reproducible.** The same map always looks identical, on every run and
  every machine. Re-rendering a fixture twice is pixel-for-pixel the same; there is nothing
  to seed or persist.
- **Presentation-only.** This lives entirely in the client render path. It does **not** touch
  sim-core, the FORM/MATERIAL lanes, or the `WorldHasher` / determinism-proof machinery — the
  tile's simulated state is unchanged; only *which of a region's interchangeable cells* is
  drawn changes. It is not a "gameplay determinism" concern.
- **Good local variety.** The hash avalanches between adjacent coordinates, so neighbouring
  tiles usually land on different variants (probability of a matching neighbour ≈ `1/count`),
  visibly breaking up the repeat.

**Unchanged by the full-color art register (§5.4, §11).** This mechanism does not care what
color a region's cells carry — it only ever picks *an index*. Under the colored pack, a
region's variants are typically several different-shaped cells of the **same baked hue**
(e.g. `wall_rubble`'s four maroon speckle/weave cells), so the variety a player sees is real
shape variety, not the old scheme's identical-grayscale-shape-under-one-flat-tint. Nothing
about `SheetAtlasSpec`, `WorldRenderer.cosmeticVariant`, or the hash itself changed for the
pack swap — only which cells the `regions` map points at (§11).

**Orthogonal to the appearance bucket (§2).** The variant axis is *not* `appearanceBucket`.
`appearanceBucket` is a **gameplay-meaningful** key — the material's active `colorStops`
ordinal (0..3), driven by `ChargeStopChangedEvent` for chargeable materials like chromatis
(charge level → glow colour). It selects *which region name* resolves and is served by the
GL-free `TileArtResolver`. The variant axis selects *which cell of that already-resolved
region* is drawn, is keyed on world position, and is resolved GL-side in the atlas. They
compose without interference: chromatis' charge visualization (`heatGlowTint`, per-bucket
regions) is completely untouched, and adding variants to a region never changes its bucket
behaviour.

**Atlas seam.** The `TileAtlas` interface carries the axis GL-side: `variantCount(name)` and
`region(name, variantIndex)` (indices folded mod count defensively). `SheetTileAtlas` slices
one `TextureRegion` per variant cell at boot; the single-cell placeholder pack
(`PlaceholderAtlas`) reports `variantCount == 1` and ignores the index, so the same renderer
drives both packs unchanged (§8).
