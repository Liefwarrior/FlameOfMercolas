# Face Generator Research Dossier — Warsim: Realm of Aslona → Granadad LITE facegen

Status: research notes feeding `FACES-SPEC.md` (Decision 4c). Produced 2026-07-12 from web research.
Everything in Part C is a PROPOSAL (placeholder numbers, veto freely); Parts A/B are sourced findings.

---

## Part A — How Warsim's face generator actually works (sourced)

### A1. Composition model: horizontal band stacking

Warsim composes a face from **7 stacked text files, one horizontal band each**, picking one random
line-part per band and printing them top to bottom. From the official modding wiki
([Modding: Race Faces](https://warsim-the-realm-of-aslona.fandom.com/wiki/Modding:_Race_Faces),
retrieved 2026-07-12; wikitext fetched via `?action=raw`):

> "Warsim's face generation system uses 7 text files to store each face part and then takes a
> random part from each file and combines them to produce a face."

Band semantics (same source):

| File | Band |
|---|---|
| `Face0.txt` | "the very top of the head" (hair/crown/headgear top) |
| `Face1.txt` | "the layer just below the top" (brow/upper outline) |
| `Face2.txt` | "typically the eye layer" |
| `Face3.txt` | "typically the nose and ear layer" |
| `Face4.txt` | "typically the upper lip layer" |
| `Face5.txt` | "typically the mouth layer" |
| `Face6.txt` | "typically the chin layer" |

Files live in `\Data\Faces` under the game root — the whole system is plain moddable text.

### A2. Line/part structure

Each **part is exactly one line of text**, prefixed by the race-template number and a dot:
`45.    ___!___`. All parts for race N in `FaceK.txt` are the band-K pool for that race; the
generator filters by prefix, picks one line, strips the prefix, prints it. Example parts from the
wiki (race id 45, band 0): `___!___`, `__\|/__`, `___-___`, `_=_=_=_`, `_|-x-|_` — all 7 chars of
art at identical columns. Race ids 1–43 were occupied at the time of writing (humans = 1).

Composed example from the wiki (7 lines, one per band):

```
   ___!___
  / == == \
 /  o   o  \
 |   ( )   |
 \         /
  \ ----- /
   |     |
```

Two structural rules called out by the wiki:

> "Generally speaking you want to ensure that the parts will all layer together and mesh fine,
> if all parts don't line up then some of the randomly generated faces could be broken."

> "Do not use '.' fullstops/dots/periods in your face parts as they will break save files"

(The second is an artifact of Warsim's `N.`-prefix parsing/save format — a lesson in keeping the
part-id channel out of the art charset.)

### A3. Race templates and prefix mutations

- Pools are **partitioned per race template** (race number). Human parts never mix into orc faces;
  each race's 7 pools are authored to share that race's silhouette.
- Race *prefixes* mutate faces by operating on **semantic bands**: "Many-Eyed" duplicates the eye
  band, "Blind" "removes eyes and doubles brow", "Nightmare" gives "three sets of teeth and four
  pairs of eyes", "Inbred ... removes a random part of their face", plus "double forehead" and
  "extra neck/lower head part" variants. Because band 2 *is* the eyes, such mutations are trivial
  row operations. Sources: [75 Million fantasy races (Medium)](https://medium.com/@huw2k8/the-indie-game-with-75-million-fantasy-races-inside-it-b9ee6ddf1102),
  [Exploring procedurally generated fantasy races (Medium)](https://medium.com/@huw2k8/exploring-the-depths-of-procedurally-generated-fantasy-races-in-warsim-the-realm-of-aslona-a000954227c5).
- Prefixes also change stats/population/skin colour, not just art — face and mechanics vary together
  (same Medium sources).

### A4. Combination math and pool sizes

- Steam store claim: "over 100 quadrillion different faces" across all races
  ([Steam page](https://store.steampowered.com/app/659540/Warsim_The_Realm_of_Aslona/)).
- Per-race totals published on [Warsim's Generator Toolbox (itch.io)](https://huw2k8.itch.io/warsims-generator-toolbox),
  which ships 41 standalone face generators. Sample: Man 179,571,730,319,982,000 · Mutant
  22,413,887,964,468,000 · Orc 65,307,875,400,000 · Goblin 30,543,693,649,920 · Elf 7,072,758,000 ·
  Vampire 2,244,483,072 · Ghost 102,930,128,321.
- Devlogs grow totals by adding parts: "Added 1'589'130'356'814'000 new possible human faces
  (1.5 quadrillion)" and 7 trillion new orc faces in
  [0.7.5.4](https://huw2k8.itch.io/warsim/devlog/89439/warsim-0754-gender-choice-quadrillions-of-faces-lots-of-improvement);
  Satyr rework 16.8 B → 89 B in [0.8.2.2](https://huw2k8.itch.io/warsim/devlog/183433/warsim-0822-crying-gnome-update-138-features);
  see also [3 Trillion Goblin Faces](https://huw2k8.itch.io/warsim/devlog/94003/warsim-0765-3-trillion-goblin-faces-and-a-bunch-of-other-stuff).
- **Our factor analysis** (inference, not a Millward statement): the totals factor cleanly into
  small-integer products consistent with "product of per-band pool sizes", e.g.
  Elf 7,072,758,000 = 2^4·3^8·5^3·7^2·11 → geometric mean ≈ **26 parts/band over 7 bands**;
  Vampire ≈ 22/band; Giant ≈ 33/band; Goblin ≈ 84/band; Man ≈ **290/band-equivalent** (the human
  generator multiplies in extras — hair/beard/gender variants — beyond the base 7 bands).
  Ghost's total (102,930,128,321) is **prime**, so at least some totals are *sums over sub-template
  products*, i.e. variety = Σ over sub-templates Π over bands |pool|. Takeaway: **7 slots ×
  20–90 hand-made parts each is the entire trick**; the astronomical numbers are just the product.

### A5. What makes them charming vs mush (analysis grounded in sources)

1. **Locked silhouette, variable interior.** Every part in a race's pool keeps the outline
   characters (`/ \ | _`) in the same columns; only the interior varies. The wiki's one warning is
   exactly this ("if all parts don't line up ... broken"). Mush = silhouette drift.
2. **Semantic bands.** Because band 2 is always eyes and band 5 always mouth, every output parses
   as a face at a glance; randomness never destroys the schema. Structure carries legibility,
   parts carry personality.
3. **Hand-authored parts, machine-authored combinations.** "To make the face system I wrote
   thousands upon thousands of lines of different face parts over the span of years"
   ([Medium](https://medium.com/@huw2k8/the-indie-game-with-75-million-fantasy-races-inside-it-b9ee6ddf1102)).
   Every individual band is human-approved; the machine only shuffles. Charm survives because no
   frame contains machine-drawn art.
4. **Partitioned pools.** Race templates never cross-contaminate; a demon eye-band cannot land on
   a halfling jaw. LITE corollary: archetype pools (guard/cultist/thug) must be curated subsets,
   not one big bucket.
5. **Surprise as a design goal, curated after the fact.** "With this system the vast majority of
   what I see is something I never imagined, planned, or designed" (same Medium article). Even
   bugs get promoted: "Broke the warsim face generator and ended up with faces that will now be
   used as children in the game"
   ([Millward on X, 2018](https://x.com/HuwMillward/status/1040252957177126913)).
6. **Uniqueness as fiction.** Steam pitch: throne-room faces are "unique faces seen only by you
   and never again" — the generator is a narrative device (everyone you meet is a person), not
   just decoration. That is exactly the Granadad use case: the Wielder looks commoners in the face.

### A6. License / reuse verification — RULING: inspiration only

- **Warsim's Generator Toolbox** (itch.io) is **free / name-your-own-price**, no formal license
  text anywhere on the page. In the page comments (retrieved 2026-07-12), user Nosferatube asked:
  "what is the licensing for the content created by the generator? Can it be used in other games?"
  Huw2k8 replied: **"You have my full permission to use it as you please mate! :)"**.
- Assessment: that is an informal, personal permission grant in a comment thread — addressed to a
  specific developer, with no scope, attribution, or revocation terms, and it covers *generator
  output*, not necessarily the part libraries as redistributable assets. It is **not a license**
  (no CC/OSI/SPDX identifier). The game's `Data\Faces` files are moddable but ship with a
  commercial game.
- In-game generators are the real dev tools, shared for fun: "I've always loved games that allowed
  you to play around ... so I wanted to share them"
  ([Steam discussion](https://steamcommunity.com/app/659540/discussions/0/2828702372993244102/)).
- **Ruling for Granadad: copy the ARCHITECTURE (band stacking, prefix-partitioned pools, template
  mutation), author 100% original part art.** Zero Warsim part strings enter our repo. If we ever
  want actual Warsim parts, the path is a direct written OK from Huw Millward first.

---

## Part B — Source list

1. Warsim wiki, "Modding: Race Faces" — https://warsim-the-realm-of-aslona.fandom.com/wiki/Modding:_Race_Faces (primary technical source; CC-BY-SA wiki, wikitext archived to scratchpad during research)
2. Warsim's Generator Toolbox, itch.io — https://huw2k8.itch.io/warsims-generator-toolbox (41 face generators, per-race totals, permission quote in comments)
3. H. Millward, "The Indie game with 75 Million fantasy races inside it", Medium — https://medium.com/@huw2k8/the-indie-game-with-75-million-fantasy-races-inside-it-b9ee6ddf1102
4. H. Millward, "Exploring the depths of procedurally generated fantasy races...", Medium — https://medium.com/@huw2k8/exploring-the-depths-of-procedurally-generated-fantasy-races-in-warsim-the-realm-of-aslona-a000954227c5
5. Warsim devlog 0.7.5.4 (Quadrillions of Faces) — https://huw2k8.itch.io/warsim/devlog/89439/warsim-0754-gender-choice-quadrillions-of-faces-lots-of-improvement
6. Warsim devlog 0.7.6.5 (3 Trillion Goblin Faces) — https://huw2k8.itch.io/warsim/devlog/94003/warsim-0765-3-trillion-goblin-faces-and-a-bunch-of-other-stuff
7. Warsim devlog 0.8.2.2 (Satyr face rework) — https://huw2k8.itch.io/warsim/devlog/183433/warsim-0822-crying-gnome-update-138-features
8. Steam store page, Warsim — https://store.steampowered.com/app/659540/Warsim_The_Realm_of_Aslona/ ("over 100 quadrillion different faces")
9. Steam discussion "Random Generators" — https://steamcommunity.com/app/659540/discussions/0/2828702372993244102/
10. H. Millward on X, children-faces bug — https://x.com/HuwMillward/status/1040252957177126913

---

## Part C — Granadad LITE facegen outline (PROPOSAL — all numbers placeholder)

### C1. Frame

- Monospace grid, **15 cols × 9 rows** (placeholder): 8 face bands + 1 collar band. Wider than
  Warsim's ~11–13 to give headgear room; fits a portrait panel in the combat/dialogue UI.
- Charset: printable ASCII 0x20–0x7E only (font-atlas safety; no Warsim-style forbidden-char rule
  because part ids never share a line with art in our format).
- Rendering is client-observer side; parts may carry an optional per-band tint key into the
  art-mapping layer (e.g. cultist crimson, guard steel) — same asset-swap philosophy as tiles.

### C2. Bands (part categories)

| Band | Content | Notes |
|---|---|---|
| 0 | headgear top / crown of head | helm crest, hood peak, bald, hair |
| 1 | brow / headgear rim | helm rim + eye-slit variants, hood brim shadow, hairline |
| 2 | eyes | the soul band — biggest pool |
| 3 | nose + ears | ears suppressed when headgear class covers them |
| 4 | upper lip / moustache | |
| 5 | mouth | |
| 6 | chin / jaw / beard | |
| 7 | neck / collar | gorget, cowl drape, robe collar, jerkin, bare neck |
| — | overlay pass | scars, brands, eyepatch, warpaint: post-compose single-cell substitutions at fixed anchor cells, rare |

### C3. The mush-killers (rules Warsim taught us)

1. **Silhouette contract per template**: each archetype template declares the outline column
   positions; a validator (tools module) rejects any part whose outline chars drift. This is
   Warsim's "must line up" rule made machine-checked instead of eyeballed.
2. **Headgear class is drawn ONCE and gates bands jointly** — class ∈ {BARE, HOOD, OPEN_HELM,
   CLOSED_HELM, COIF, COWL} (placeholder set). Class filters the pools of bands 0, 1, 3 (ears), 7.
   Warsim gets coherence by authoring within a race; we need cross-band gating because hood/helm
   spans multiple bands (city guard kettle-helm vs cultist cowl is the whole point of Decision 4c).
3. **Archetype = weighted pool subset over one shared band library.** Archetypes v0: city guard,
   cultist, monk, noble, thug, commoner (placeholder list). Tags on parts (`hood`, `helm-open`,
   `grim`, `fine`, `scarred`...); archetype file = tag filters + integer weights. Shared library
   keeps authoring cost sane; weights keep a noble from rolling a thug's broken nose (rare ≠ never
   — rare is where charm lives).
4. **Ink-balance review**: parts within a band pool should have comparable character density;
   authoring guideline, spot-checked in the golden face sheet (C6).

### C4. Pool sizes (placeholder targets)

Shared library ~30–40 parts/band; per archetype pool 8–16/band after tag filtering. Even the
floor (8^8 across 8 bands ≈ 16.7 M per archetype before headgear gating and overlays) dwarfs the
NPC count of one district. Warsim's data says variety is the cheap part; **budget authoring time
into eyes/mouth bands** (personality) and headgear (archetype legibility), not into raw counts.

### C5. Determinism (binding per ARCHITECTURE §6)

- Faces are pure functions of `(worldSeed, npcId)` — **no tick term, no stored face state**; a face
  is stable for the NPC's lifetime and reproducible in any run of the same world seed.
- Named deterministic draws via the one engine `RandomSource` discipline (§1.1 #16):
  `draw(k) = mix64(mix64(mix64(worldSeed ^ FACEGEN_SALT) + npcId) + k)` with fixed drawIndex map
  (placeholder): 0 = headgear class · 1–8 = band picks · 9 = overlay count · 10+ = overlay
  pick/anchor pairs. Adding bands/overlays later appends draw indices, never renumbers.
- Weighted pick: integer cumulative-weight table, `idx = Long.remainderUnsigned(draw, totalWeight)`,
  binary search. All-integer; no floats anywhere (golden-master safe).
- Named NPCs bypass the generator: id → hand-authored face file lookup wins (Decision 4c).
- Facegen lives in a pure, GL-free package with golden tests: fixed seed + npcId set → byte-exact
  composed face sheet.

### C6. File formats (feeds FACES-SPEC.md)

- `faces/bands/band2_eyes.txt` (placeholder path under content resources): repeated records of one
  header line `part <id> w=<weight> tags=<a,b,c> gear=<classes>` + exactly one art line. Id channel
  never shares a line with art (the lesson of Warsim's no-periods rule).
- `faces/templates/<archetype>.json`: silhouette columns, tag filters, per-band weight overrides,
  headgear class weights.
- `faces/named/<npcId>.txt`: full 9-line block, hand-authored.
- Tools-module validator: width/charset/silhouette/gear-class coverage (every class × band
  non-empty), duplicate art detection. Boot-fails like the raws loader.
- Golden face sheet: `headless`-generated grid of N seeded faces per archetype, committed as a
  review artifact — the "does it read as a guard from across the room" test.
