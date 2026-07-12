# TILE-ART-SPEC — placeholder tile art & the art-swap seam (v0)

**Status:** authoritative for `client-observer` art resolution and for the placeholder atlas.
Parent spec: `ARCHITECTURE.md` §3 (client-observer package entry), §10 (raws / color stops),
§1.1 #17 (one material + form), #19 (light 0–31). Where this document and ARCHITECTURE.md
disagree, ARCHITECTURE.md wins.

Owned files: this spec + `content/art/placeholder/art-mapping.json`.

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
| chromatis (bucket 1) | `#E8842A` | `C` | black | **canon** — orange when hot |
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
transitions, per-biome variants.

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
