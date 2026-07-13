# FACES-SPEC — Decision 4c: text-art faces

**Status:** spec-first deliverable for DECISIONS.md row 4c. Consumes `FACEGEN-RESEARCH.md` (the
Warsim dossier). Implementation lands in the G-track; nothing here touches sim-core F-milestones.
All invented names/numbers are **(placeholder)** unless marked canon-cited. Determinism rules are
binding per ARCHITECTURE.md §6 (integer-only math, named draws, canonical iteration order).

Two systems, one frame:

1. **Named-NPC faces** — hand-authored files, one per canon character. Always win over the generator.
2. **FaceGen LITE** — Warsim-style band-stacked generator for generic NPCs (architecture inspired
   by Warsim: Realm of Aslona; **zero Warsim art strings** — see §8 Licensing).

---

## 1. Canonical frame

One canonical face size for the whole game: **15 columns × 9 rows**, monospace (placeholder,
adopted from dossier §C1; wider than Warsim's ~11–13 to give headgear room).

### 1.1 Band layout

Bands stack top to bottom. Band 6 (jaw) is two rows tall — beards and jaw wraps need vertical
room; every other band is one row. Row heights are **fixed forever** (goldens depend on them).

| Band | Rows | Content | Notes |
|---|---|---|---|
| 0 | r0 | crown — hair / helm top / hood peak / bald | |
| 1 | r1 | brow / helm rim / hood shadow / hairline | |
| 2 | r2 | eyes | the soul band — biggest pool, most authoring budget |
| 3 | r3 | nose + ears | ear glyphs only on `BARE`-gear variants (§4.4) |
| 4 | r4 | upper lip / moustache / stubble | |
| 5 | r5 | mouth | |
| 6 | r6–r7 | jaw / chin / beard / cowl wrap | **2 rows** |
| 7 | r8 | neck / collar — gorget, robe collar, jerkin, bare | |
| — | — | overlay pass | post-compose single-cell substitutions (§4.6) |

### 1.2 Silhouette contract (the anti-mush rule)

Warsim's "parts must line up" rule, machine-checked instead of eyeballed (dossier §A5.1, §C3.1).
The v0 contract `HUMAN_BASE` (placeholder) pins outline glyphs to fixed cells; the validator
rejects any part whose outline drifts:

| Row | Required cells |
|---|---|
| r1 | col 1 = `/`, col 13 = `\` |
| r2–r5 | col 1 = `\|`, col 13 = `\|` (band-3 `BARE` variants may additionally place `)` at col 0 and `(` at col 14 — ears outside the outline) |
| r6 | col 2 = `\`, col 12 = `/` |
| r7 | col 3 = `\`, col 11 = `/` |
| r0, r8 | free-form within cols 1–13 (crown and collar own their own shape) |

Named faces must satisfy the same contract (keeps named and generated NPCs visually one species).

### 1.3 Charset and style constraints

- **Whitelist:** printable ASCII `0x20`–`0x7E` only. No tabs, no box-drawing, no Unicode, **no
  emoji**. Enforced by validator and unit test. (Warsim's "no periods" rule does not apply —
  our part-id channel never shares a line with art; dossier §A2 lesson.)
- Grit palette guidance (authoring, not enforced): outlines `/ \ | _` · texture `~ = # ' " : ;` ·
  features `0 o - * ( ) , .` · metal `= [ ] +`. Prefer sparse over dense; a face should read at
  a glance in a dim terminal-style UI.
- **Ink balance:** parts within one band pool should have comparable character density
  (authoring guideline, spot-checked via the golden face sheet, §7-T16).
- Faces are monochrome glyphs. Optional `tint.*` metadata keys are hints consumed by the
  art-mapping layer (same asset-swap philosophy as tiles; renderer may ignore them in v0).

---

## 2. Named-NPC face format

**Path:** `content/faces/named/<npc_id>.face` (relocates with the F2 raws move to
`content/src/main/resources/trojia/faces/named/` per BLESSING-QUEUE ruling 1; loaders read the
classpath).

**Format:** UTF-8 text (content must pass the ASCII whitelist), LF line endings.
Header of `key: value` lines, a `---` separator, then **exactly 9 art lines of exactly 15
characters** (trailing spaces significant — width is exact, not "up to").

```
id: devin
name: Devin
provenance: WB §Bestiary "Devin — Gabri's apprentice. Dark-skinned, white hair (Flame side-effect), foundling. Mace + repeater crossbow."; novel L2413 (born across the sea, chosen as next Wielder)
tint.hair: white
tint.skin: dark
---
<9 art lines>
```

Recognized header keys (placeholder set): `id` (must equal filename stem), `name`, `provenance`
(required — canon cite or needs-blessing flag, same convention as raws), `tint.*` (optional).
Unknown keys = loader hard-fail (raws-loader discipline).

Lookup rule: if `content/faces/named/<npcId>.face` exists for an NPC's id, it is used and the
generator is **never invoked** (no draws burned — named faces don't perturb anything).

### 2.1 Authored face: Devin

Canon: dark-skinned, white hair (Flame side-effect), foundling, born across the sea (WB
§Bestiary; novel L2413). Young, earnest, nervous around authority (novel L2413, L2716).
Wields mace + repeater crossbow — not visible in a face. Invented visuals: short tight white
curls, wide alert eyes, apprentice robe collar with clasp (placeholder).

```
   0123456789ABCDE   (column ruler, not part of file)
  ┌───────────────┐
r0│  .+~+~+~+~+.  │  white curls (tint.hair: white)
r1│ /~+~+~+~+~+~\ │  low young hairline
r2│ |  0     0  | │  wide alert eyes
r3│)|     )     |(│  ears out, small straight nose
r4│ |           | │  smooth — too young for stubble
r5│ |    \_/    | │  earnest half-smile
r6│  \         /  │
r7│   \_______/   │
r8│  __/  |  \__  │  apprentice robe, center clasp
  └───────────────┘
```

File art block (verbatim, 9 × 15):

```
  .+~+~+~+~+.  
 /~+~+~+~+~+~\ 
 |  0     0  | 
)|     )     |(
 |           | 
 |    \_/    | 
  \         /  
   \_______/   
  __/  |  \__  
```

### 2.2 Authored face: Minister John

Canon: "the old man was studiously poring over a manuscript"; minister of the Granadad
monastery/cathedral of the Divine Light; dry, hard to read ("his lips twitched, Devin couldn't
tell if it was irritation or amusement"); strained history with Devin (novel L2413–L2418).
Canon gives no physical detail beyond "old" — **all visuals below invented (placeholder)**:
bald dome, temple tufts, wrinkled brow, reading squint, heavy grey moustache over a downturned
mouth, full beard, high vestment collar.

```
   0123456789ABCDE
  ┌───────────────┐
r0│    _______    │  bald dome (placeholder)
r1│ /= ~~~~~~~ =\ │  temple tufts, brow wrinkles
r2│ | (-     -) | │  reading squint, crow's feet
r3│)|    _)     |(│  long drooping nose (placeholder)
r4│ | ========= | │  heavy moustache (tint.hair: grey)
r5│ |  '-----'  | │  downturned mouth
r6│  \ vvvvvvv /  │  full beard
r7│   \_vvvvv_/   │
r8│ __|=======|__ │  high Divine Light vestment collar
  └───────────────┘
```

File art block (verbatim, 9 × 15):

```
    _______    
 /= ~~~~~~~ =\ 
 | (-     -) | 
)|    _)     |(
 | ========= | 
 |  '-----'  | 
  \ vvvvvvv /  
   \_vvvvv_/   
 __|=======|__ 
```

---

## 3. FaceGen LITE — content model

### 3.1 Part files

**Path:** `content/faces/parts/<category>/*.facepart` — one part per file, `<category>` is the
band name: `crown/ brow/ eyes/ nose/ lip/ mouth/ jaw/ collar/` plus `overlay/` (§4.6).
Data-driven: adding a `.facepart` file adds the part; no code change.

Format mirrors §2 (header, `---`, art lines — **row count must equal the band's height**: 1 line
everywhere, 2 for `jaw`):

```
id: eyes_glint
band: eyes
weight: 20
tags: hooded
gear: HOOD,COWL
---
 |  *     *  | 
```

| Key | Meaning |
|---|---|
| `id` | unique across ALL parts; must equal filename stem; `[a-z0-9_]+` |
| `band` | must match the directory |
| `weight` | base integer weight ≥ 1 |
| `tags` | comma list, `[a-z_]+` — feeds archetype multipliers (§3.3) |
| `gear` | comma list of headgear classes this part is valid under, or `*` = all (§3.2) |
| `tint.*` | optional, as §2 |
| `provenance` | optional for generic parts (they are style, not canon) |

### 3.2 Headgear classes (cross-band coherence)

Warsim gets coherence by authoring within a race; we need explicit gating because a hood or
helm spans multiple bands (dossier §C3.2 — guard kettle-helm vs cultist cowl is the point of
Decision 4c).

`HeadgearClass ∈ { BARE, HOOD, OPEN_HELM, CLOSED_HELM, COIF, COWL }` (placeholder set).

- The class is drawn **once** (draw index 0) from the archetype's class-weight table, then
  filters every band pool: a part is eligible iff `gear` contains the drawn class or `*`.
- Ears (band 3): glyphs `)(` at cols 0/14 appear only on `gear: BARE` variants; every covered
  class uses earless variants. Authoring rule, enforced by validator tag-check (placeholder).
- Validator invariant: for every archetype × reachable gear class × band, the filtered pool is
  non-empty (§7-T10). No runtime fallbacks — holes are a content build error.

### 3.3 Archetypes

**Path:** `content/faces/archetypes/<archetype>.json`. v0 set (per Decision 4c crew brief):
`guard, cultist, monk, noble, thug, laborer` (placeholder list; dossier's "commoner" folds into
laborer).

An archetype is a **weighted view over the one shared part library** — tag multipliers, never a
private pool (dossier §A5.4/§C3.3: partitioned outcomes, shared authoring cost):

```json
{
  "id": "cultist",
  "gearWeights": { "HOOD": 70, "COWL": 25, "BARE": 5 },
  "tagMultipliers": { "hooded": 3, "robed": 3, "grim": 2, "guard": 0, "armored": 0 }
}
```

- Effective weight = `part.weight × Π tagMultipliers[t]` over the part's tags (missing tag → ×1).
  All integer. Multiplier `0` excludes the part (a cultist never rolls a gorget); small nonzero
  keeps it **rare, not never** — rare is where charm lives (dossier §C3.3: a noble *can* roll a
  thug's broken nose).
- A cultist skews hooded via `gearWeights` (HOOD+COWL = 95%) *and* `tagMultipliers.hooded = 3`;
  a guard the mirror image (`OPEN_HELM` heavy, `hooded: 0`). Monk: bald/coif skew; noble:
  `fine` up, `scarred` down; thug: `scarred`/`grim` up; laborer: near-uniform (all placeholder).

---

## 4. FaceGen LITE — composition algorithm

### 4.1 Pipeline

```
input: worldSeed (long), npcId (long), archetype (id)
1. if named face exists for npcId → return it (no draws)
2. draw 0            → headgear class (archetype gearWeights)
3. draws 1..8        → band 0..7 part pick (filtered, weighted pool)
4. stack rows: band0(1) + band1(1) + ... + band6(2) + band7(1) = 9 lines × 15 cols
5. draw 9            → overlay count (weights 13/2/1 for 0/1/2, total 16 — placeholder)
6. draws 10+2i,11+2i → overlay i: (pick, anchor); apply or deterministically drop
7. return composed grid (pure value; never stored in save state)
```

### 4.2 Determinism (binding)

- A face is a **pure function of `(worldSeed, npcId)`** — no tick term, no stored face state;
  stable for the NPC's lifetime, reproducible in any run of that world seed (dossier §C5).
- **Draws are archetype-independent** (the archetype shapes the *pool*, not the draw): the same
  NPC keeps its facial "genes" — e.g. if an id's archetype label is ever reclassified, the eyes
  stay the eyes where pools overlap. (Verified property, §7-T2.)
- Named draw function (**pinned — ruling, veto-able**): facegen does not consume the engine
  `RandomSource` counter stream; it pins its own stateless mixer so face goldens never depend on
  engine internals:

  ```
  mix64 = SplitMix64 finalizer:
      z ^= z >>> 30;  z *= 0xBF58476D1CE4E5B9L;
      z ^= z >>> 27;  z *= 0x94D049BB133111EBL;
      z ^= z >>> 31;  return z;

  base    = mix64(mix64(worldSeed ^ FACEGEN_SALT) + npcId)
  draw(k) = mix64(base + k)

  FACEGEN_SALT = 0x4641434547454E31L   // "FACEGEN1" (placeholder)
  ```

- **drawIndex map is append-only** (never renumber — same rule as the tick-phase enum):
  `0` gear class · `1–8` band picks · `9` overlay count · `10+2i / 11+2i` overlay pick/anchor.
  Future features (age pass, warpaint) append indices ≥ 14.
- **Weighted pick:** pool sorted by part id (ASCII ordinal — never file-discovery order), integer
  cumulative-weight table, `r = Long.remainderUnsigned(draw(k), totalWeight)`, first entry with
  `r < cum`. All integer; no floats anywhere in facegen (ArchUnit-enforced, §7-T15). Modulo bias
  is irrelevant at these pool sizes (u64 vs totals < 10^4) — accepted, documented.
- Facegen lives in a pure, GL-free package; golden face-sheet tests are byte-exact (§7-T16).

### 4.3 Row stacking

Bands never overlap: composition is pure line concatenation in band order (Warsim's model,
dossier §A1). Width alignment is trivially guaranteed because the validator rejects any part
line ≠ 15 chars — there is no runtime padding/clipping code path.

### 4.4 Cross-band consistency

All cross-band coherence comes from exactly two mechanisms — gear-class gating (§3.2) and the
silhouette contract (§1.2). No part may "reach into" another band's rows. If a future feature
needs multi-band art (e.g. a full-face tattoo), it is an overlay-pass feature, not a part.

### 4.5 (reserved)

Reserved: sub-template system (Warsim's race-template analogue) if archetype count outgrows tag
weighting. Empty in v0 so §4.6 numbering never shifts.

### 4.6 Overlay pass

Scars, brands, pox, warpaint — post-compose **single-cell substitutions** at fixed anchors
(dossier §C2 overlay row), rare by weight.

- Overlay parts: `content/faces/parts/overlay/*.facepart` with `glyph:` (one whitelisted char)
  instead of art lines. v0 sample set (placeholder): `ovl_scar` `/` w3 · `ovl_pock` `:` w2 ·
  `ovl_brand` `+` w1.
- Anchor table (fixed, append-only, placeholder): `A0=(r3,c4)` left cheek · `A1=(r3,c10)` right
  cheek · `A2=(r2,c7)` between eyes · `A3=(r6,c7)` chin. Anchor pick =
  `remainderUnsigned(draw, anchorCount)`.
- **Apply rule:** substitute only if the target cell currently holds a space; otherwise the
  overlay is **dropped, deterministically — no reroll** (rerolls would make output depend on
  pool contents in a second way; a dropped scar is a cheap price for a fixed draw map).

---

## 5. Rendering

- **One canonical size, everywhere.** 15×9 glyphs rendered in the UI monospace font; size on
  screen varies only by font point-size, never by re-authoring or algorithmic scaling
  (down-scaled text art is exactly the mush we're avoiding). No size variants in v0 (ruling).
- **Combat screen** (COMBAT-SCREEN-SPEC.md owns layout): the enemy panel shows the opponent's
  face at combat-font size; the face is the "you are fighting a person" beat on encounter entry
  — Warsim's "unique faces seen only by you" as a narrative device (dossier §A5.6). Multi-enemy
  encounters show the currently-targeted enemy's face only (placeholder rule).
- **Dialogue panels:** same 15×9 face beside the name/speech box at dialogue-font size.
- Faces are generated on demand and may be cached by `(worldSeed, npcId)`; the cache is an
  optimization, never a source of truth, and never serialized (a face is derivable, so TROJSAV
  carries nothing).
- Optional `tint.*` hints pass through to the art-mapping layer (legibility overrides win, same
  as tile art; BLESSING-QUEUE §14 philosophy).

---

## 6. Worked examples — four generated faces

Hand-verifiable trace using the pinned mixer (§4.2), `worldSeed = 0x5EEDF00D` (placeholder),
NPCs `4101` and `7777`, archetypes `guard` and `cultist`. Pools are the **spec sample library**
(illustrative subset, placeholder art/weights; real library is content-authored). Sample
archetypes as §3.3's cultist plus:
`guard = { gearWeights: {OPEN_HELM:60, COIF:25, BARE:15}, tagMultipliers: {guard:3, grim:2, armored:2, hooded:0, robed:0} }`.
Pool windows below are cumulative-weight ranges over the id-ordinal-sorted filtered pool. These
hex values are normative reference vectors for §7-T11.

Note how NPC 4101 keeps its draw values across both archetypes — only the pools change (§4.2).

### 6.1 npcId 4101, guard

| k | draw (hex) | mod | r | pick |
|---|---|---|---|---|
| 0 gear | `FD3822BBF6FC1C57` | 100 | 43 | **OPEN_HELM** |
| 1 crown | `D9E2E0A9F3FF074C` | 60 | 52 | crown_kettle `[0..59]` |
| 2 brow | `086D572606E931AF` | 60 | 15 | brow_rim `[0..59]` |
| 3 eyes | `EF79995D40258C0C` | 60 | 4 | **eyes_narrow** `[0..29]` · eyes_round `[30..49]` · eyes_weary `[50..59]` |
| 4 nose | `61537233FDBD6E70` | 30 | 4 | **nose_broken** `[0..9]` · nose_plain_c `[10..29]` |
| 5 lip | `C3278D19407C5F88` | 45 | 24 | lip_clean `[0..19]` · **lip_mstache** `[20..29]` · lip_stubble `[30..44]` |
| 6 mouth | `6B3FC942F5BA81A9` | 80 | 41 | mouth_frown `[0..29]` · **mouth_grit** `[30..49]` · mouth_line `[50..69]` · mouth_smirk `[70..79]` |
| 7 jaw | `4074137C375E362F` | 45 | 33 | jaw_beard `[0..14]` · **jaw_clean** `[15..34]` · jaw_scar `[35..44]` |
| 8 collar | `BC3CEF21BF98A125` | 155 | 146 | col_bare `[0..14]` · col_gorget `[15..134]` · **col_jerkin** `[135..154]` |
| 9 ovl# | `81B792CE85FDDAF4` | 16 | 4 | 0 overlays |

```
  _/=======\_  
 /===========\ 
 |  -     -  | 
 |    ~)     | 
 | ========= | 
 |  |-|-|-|  | 
  \         /  
   \_______/   
  __/|   |\__  
```

A kettle-helmed veteran — broken nose, moustache, gritted teeth. Reads "guard" from across the room.

### 6.2 npcId 4101, cultist (same draws, different pools)

Gear draw 43 lands in **HOOD** (`BARE[0..4]` `COWL[5..29]` `HOOD[30..99]`). Bands 1–2 pick the
only hood-gated parts (crown_hood, brow_shadow); eyes mod becomes 120 (glint ×3 joins the pool)
→ 4 → **eyes_glint** `[0..59]`; nose/lip/mouth/jaw picks match 6.1 (same windows); collar mod
215 → 46 → **col_robe** `[35..214]`. 0 overlays.

```
   ___/\___    
 /## ## ## ##\ 
 |  *     *  | 
 |    ~)     | 
 | ========= | 
 |  |-|-|-|  | 
  \         /  
   \_______/   
  _/  \_/  \_  
```

Same "genes" (broken nose, moustache, gritted teeth), now glinting from under a hood. This is
§4.2's archetype-independence property, visible.

### 6.3 npcId 7777, guard

| k | draw (hex) | mod | r | pick |
|---|---|---|---|---|
| 0 gear | `F0057133BC5B1A22` | 100 | 78 | **OPEN_HELM** |
| 1 crown | `037D5E965ED37139` | 60 | 37 | crown_kettle |
| 2 brow | `5F7D9D7AE7BB34B7` | 60 | 27 | brow_rim |
| 3 eyes | `DD905223DA220A48` | 60 | 36 | **eyes_round** `[30..49]` |
| 4 nose | `7DEDD0AE9D1D3DF5` | 30 | 21 | **nose_plain_c** `[10..29]` |
| 5 lip | `A6320D05BA8B3E03` | 45 | 39 | **lip_stubble** `[30..44]` |
| 6 mouth | `9544238D40879B6E` | 80 | 62 | **mouth_line** `[50..69]` |
| 7 jaw | `DEB85C046B8ACB96` | 45 | 5 | **jaw_beard** `[0..14]` |
| 8 collar | `B75565414018D22F` | 155 | 114 | **col_gorget** `[15..134]` |
| 9 ovl# | `A6D1F772A40043FF` | 16 | 15 | **2 overlays** |
| 10/11 | `A839A9ADEB500851` / `6F542FABD6DF7520` | 6 / 4 | 5 / 0 | ovl_scar `/` @ A0=(r3,c4) → **APPLIED** |
| 12/13 | `0F654D7E6FBF6C49` / `0AD8C63EF0D8C8AC` | 6 / 4 | 1 / 0 | ovl_pock @ A0 → **DROPPED** (cell no longer blank — §4.6 no-reroll rule) |

```
  _/=======\_  
 /===========\ 
 |  0     0  | 
 |  /  )     | 
 | ' ' ' ' ' | 
 |   -----   | 
  \ vvvvvvv /  
   \_vvvvv_/   
 __|=======|__ 
```

Bearded, stubbled, gorget-wearing, cheek-scarred. The dropped second overlay is the
deterministic-drop rule working as specced.

### 6.4 npcId 7777, cultist (same draws)

Gear 78 → **HOOD**; crown_hood, brow_shadow; eyes mod 120 → 96 → **eyes_round** `[90..109]`
(even a cultist rolls plain eyes sometimes); nose/lip/mouth/jaw as 6.3; collar mod 215 → 19 →
**col_jerkin** `[15..34]` (a hood over a laborer's jerkin — a street recruit, not a robed
initiate; rare-not-never); same scar applied, same pock dropped.

```
   ___/\___    
 /## ## ## ##\ 
 |  0     0  | 
 |  /  )     | 
 | ' ' ' ' ' | 
 |   -----   | 
  \ vvvvvvv /  
   \_vvvvv_/   
  __/|   |\__  
```

---

## 7. Unit-test list (facegen module + content validator)

Reference vectors for T11 are §6's tables. Golden face sheet (T16) = headless-generated grid of
N seeded faces per archetype, committed as a review artifact (dossier §C6).

| # | Test | Asserts |
|---|---|---|
| T1 | `sameSeedSameNpc_byteIdenticalFace` | two independent generator instances, same `(worldSeed, npcId, archetype)` → identical 9×15 grids, byte-exact |
| T2 | `sameNpcDifferentArchetype_sharesDrawValues` | draw(k) sequences identical across archetypes for one npcId; only pool resolution differs (§4.2 property) |
| T3 | `facegenIsPure_noStateAcrossCalls` | interleaved calls for many NPCs in shuffled order → same results as isolated calls |
| T4 | `composedFace_everyLineExactlyWidth15` | for a sweep of seeds × archetypes, every output line length == 15, exactly 9 lines |
| T5 | `composedFace_charsetWhitelistOnly` | same sweep: every char in `0x20..0x7E` |
| T6 | `partValidator_rejectsWrongWidth` | 14- and 16-char art line → content build error naming file + line |
| T7 | `partValidator_rejectsIllegalChar` | tab / non-ASCII / emoji in art → error |
| T8 | `partValidator_rejectsWrongRowCount` | 1-row jaw part, 2-row eyes part → error |
| T9 | `partValidator_rejectsSilhouetteDrift` | part with `\|` off the §1.2 contract cells → error; all shipped parts pass |
| T10 | `archetypeValidator_gearClassBandCoverage` | every archetype × reachable gear class × band → filtered pool non-empty |
| T11 | `weightedPick_matchesReferenceVectors` | §6 hex draw values → exact picks, incl. a draw with the high bit set (`remainderUnsigned` path) |
| T12 | `zeroTagMultiplier_excludesPart` | `guard: 0` cultist never selects gorget/kettle across full seed sweep of the sample library |
| T13 | `overlayOnNonBlankCell_droppedNoReroll` | §6.3 vector: second overlay dropped, grid identical to single-overlay composition |
| T14 | `namedFace_overridesGenerator` | id with a `.face` file → file contents verbatim; zero draws consumed |
| T15 | `archUnit_noFloatsNoHashMapInFacegen` | facegen package: no float/double, no `java.util.HashMap` iteration (ARCHITECTURE §6) |
| T16 | `goldenFaceSheet_byteExact` | fixed seed + npcId set × all archetypes → committed face sheet, byte-compared; bless only via `--bless` |
| T17 | `poolOrder_isIdOrdinal_notDiscoveryOrder` | shuffling part-file enumeration order changes nothing (canonical sort by id) |
| T18 | `duplicateDetection` | duplicate part id anywhere, or byte-identical art within one band pool → content build error |
| T19 | `namedFaceLoader_rejectsBadFile` | wrong grid size, unknown header key, id ≠ filename, missing provenance → hard fail |

---

## 8. Licensing note — what we took from Warsim, and what we did not

Per the dossier's verified ruling (FACEGEN-RESEARCH.md §A6): Warsim: Realm of Aslona has **no
formal license** covering its face parts; the only grant found is an informal itch.io comment by
Huw Millward ("You have my full permission to use it as you please") — personal, scopeless,
addressed to another developer, and covering generator *output*, not the part libraries.

**What we took (ideas — not copyrightable, and independently reimplemented):** the band-stacking
composition model, the one-line-per-part-per-band content shape, the partitioned/weighted-pool
coherence insight, and the "silhouette must line up" lesson (which we hardened into a
machine-checked contract).

**What we did not take:** zero Warsim art strings, part files, or generator code enter this
repo — every part glyph sequence in this spec and in `content/faces/**` is original. If we ever
want actual Warsim parts, the path is a direct written OK from Huw Millward first (dossier §A6
ruling). Sources: dossier Part B, items 1, 2, 8.

---

## 9. Open items / needs-blessing

1. Frame 15×9 and the `HUMAN_BASE` contract cells (§1.2) — placeholder geometry, freeze before
   first golden bless (T16), because every part ever authored depends on it.
2. `FACEGEN_SALT` value and the pinned-SplitMix64 ruling (§4.2) — engineering ruling, veto-able;
   if vetoed in favor of the engine mixer, §6 vectors regenerate mechanically (procedure
   unchanged).
3. Headgear class set, archetype list, all sample weights/multipliers — placeholder.
4. Minister John's entire appearance — invented; flag for Eli's blessing alongside the next
   canon batch.
5. Overlay anchor table and 13/2/1 rarity — placeholder.
6. Multi-enemy combat panel rule (targeted-enemy face only) — coordinate with COMBAT-SCREEN-SPEC.md.
