# DOCKS-GAZETTEER — The Docks of Granadad, single-district bible for the MVP

**Status:** design bible, spec-first. Deep enough to author every Tiled map from.
**Authority chain:** novel wins (`C:\repositories\LordOfTrojia-MVP\Lore\Lord of Trojia (indexable).txt`, cited `L<line>`); WorldBible.md cited `WB §n`; everything else here is invention and is marked **(placeholder)**.
**Governing rulings (DECISIONS.md, Eli 2026-07-12):** MVP world = ONE district, THE DOCKS, plus a 2–3 level starter dungeon beneath it; the game opens with Gabri investigating there; the rest of Granadad is off-map narrative context. Actor roster = the eight locked groups (§4). Art register = luminous-on-black.
**IP boundary:** the harbor-ward *archetype* (categories of shop/NPC/street/building, how a docks ward breathes) is structural inspiration from Ptolus/ptol.us. No names, prose, NPCs, establishments, or layouts are copied; every concrete thing below is original invention.
**Canon honesty note (binding):** the novel never shows a harbor, dock, ship, pier, or sailor anywhere (canon-dossier grep: zero hits). The entire ward is **(placeholder)**, justified as structurally inevitable: Granadad is the largest city in the world (L2327) on the point of the Trojian peninsula with sea on multiple sides (L2326–2328), and sea crossings demonstrably happen — Devin was born "in the lands across the sea" and the monks brought him back to Granadad (L2406). Where canon *does* speak (suburbs squalor, sewers, Divine Light, Wielder immunity, passports, materials), this document obeys it and cites it.

---

## 1. Context — Granadad as seen from the Docks (one page)

Granadad covers the point of the Trojian peninsula: a fortified **Inner City** of "the finest buildings and merchandise that one could ever hope to see" (L2327) ringed by suburbs that "stretched for miles in either direction… like the sea beating on an island when compared to the Inner City" (L2328). The suburbs are unwalled — Zradist walks in off the open coast with no gate mentioned (L2330) — and they are squalid: trash dumped in the streets, "shit and piss filled the alleys, and blood running down the roads was a common sight" (L2328), air "stale… like moldy bread" (L2330).

The Docks are the wettest, lowest hem of those suburbs **(placeholder — ward itself invented)**: the strip where the peninsula's north shore meets deep water, where everything the largest city in the world eats, wears, and taxes comes ashore. From the quays, looking up-slope through the fog, the player sees three things that are never maps, only backdrops:

- **The Inner Wall**, somewhere up-slope to the south, with its gate where a guard checks passports — even the Wielder presents one (L2410). Off-map exit label: **"Saltgate Road — to the Inner City" (placeholder name)**. This is the district's single land chokepoint: customs point, watch-post, and MVP map edge in one.
- **The palace**, "standing tall at the far end of the city" (L2326) — a silhouette on the skyline, nothing more.
- **The suburb sprawl** east and west along the shore: miles of the same squalor, referenced by role only — "the Shambles inland" **(placeholder label)** (the poor fighting over dumped trash is canon, L2328), "the abbey road south" (the alms-abbey a mile south of the walls, L2420), "the coast road" Zradist walked (L2326). Off-map exits carry those narrative labels and lead nowhere in the MVP.

**What ships bring:** grain, timber, wine, cloth, salt, foreign luxuries, foreign passengers — and, once, whatever this story is about **(placeholder cargo list; canon shows only that fresh salmon reaches the palace kitchens, L2351)**. **What ships take:** Trojian steel, worked goods, soldiers' pay flowing back out to a fifteen-year war (Gunthred launched it fifteen years prior, WB §5, L1220). **Why the empire watches its harbor:** tariffs are the visible hand of the off-map city — the posted rates at the customs house explain the whole smuggling economy without rendering another district **(tariff regime placeholder; passports as state control documents are canon, L2410)** — and because the deeper fear is doctrinal: "no bloodletter can enter the Trojian territory" (L2422). The border is theological. The harbor is the border. That is why a Wielder investigating the waterline is the most natural opening this setting can stage.

---

## 2. District geography

### 2.1 Shape and scale (in tiles)

**Surface map: 192 × 128 tiles, 16 z-levels** = 6 × 4 × 2 chunks (48 chunks of 32×32×8, ARCHITECTURE §8) — comfortably inside the ACTIVE bubble budget (7×7 focus columns, §7). Long axis east–west, water along the full north edge.

**Z-profile (local z 0–15):**

| z | Content |
|---|---|
| 15 | air / gull line |
| 13–14 | second stories, roofs (thatch inland, tile near the tar yard — see §7 fire map) |
| 12–13 | first-story interiors up-slope; quayside roofs. The street plane itself climbs up-slope: Saltgate Rise rises from z11 at the water to z13 at the south map edge |
| **11** | **quayside street level** — the baseline walking plane |
| 10 | high-tide water surface; quayside cellars; under-pier mudflats at low tide; **dungeon L1** (smuggler cellars/undercellars) |
| 9 | low-tide water surface; **dungeon L2** (sewer outfall junction) |
| 8 | harbor bottom; **dungeon L3** (drowned structure) |
| 0–7 | solid substrate (granite/dirt fill, no authored content) — exists so the map is a whole number of z-chunks; the importer bakes it procedurally (§8.1). Shrinking to 8 z-levels (6×4×1 = 24 chunks) is a live alternative — see §9 |

The two-level tide swing (z10 ↔ z9) is the district's second clock: deep-draft loading, under-pier scavenging, and sewer-outfall passability all key off it (§7.4). **(Tide mechanics placeholder; sewers themselves are canon — Zradist dens in them via a street grate, L2345.)**

### 2.2 The waterfront line (north edge, west → east)

1. **The Long Quay (placeholder)** — stone seawall quay, 3 deep-water berths with mooring posts and one timber crane gantry. Granite seawall face, brick paving (canon road recipe: gravel base, bedrock, brick top, L2247 / WB §1).
2. **Pier Row (placeholder)** — four timber finger-piers on trudgeon-trunk pilings **(trudgeon is canon timber, L1082; its use here placeholder)**; the easternmost, **Wormwood Pier (placeholder)**, is condemned and rotten — rotten-pier collapse is on the environmental danger list (§4, §7).
3. **The Beaching Strand (placeholder)** — shingle beach where hulls are careened; shades into mudflats at low tide (mudlark territory).
4. **The Outfall (placeholder)** — the sewer mouth in the seawall's east end, barred by a corroded tide-grate. Passable at low tide, drowned at high. Dungeon L2 access (§6). **(Outfall invented; a sewer network under the city is canon, L2345.)**

### 2.3 Named streets and lanes (all names placeholder — canon names zero streets in Granadad)

| Street | Run | Character |
|---|---|---|
| **Tarwalk** | E–W along the whole waterfront, z11 | The working spine: quays on one side, warehouses/chandlers on the other. Crowded by day, fog-blind by night. |
| **Saltgate Rise** | N–S, from the Long Quay up to the south map edge (off-map: Inner Wall gate) | The chokepoint road. Watch-post, notice board, and gibbet cage at its top **(gibbet placeholder — law texture)**. Climbs z11→z13. |
| **Ropewynd** | E–W, one block up-slope, parallel to Tarwalk | Fitting trades: ropewalk, sailmakers, coopers, chandlery. Long straight run (a ropewalk needs one). |
| **Herring Lane** | Short N–S link, west end | Fish market, salting sheds, offal drain running open down its gutter to the sea (canon suburb register: filth in the streets, L2328). |
| **The Gullet** | Crooked alley-net east end, around the condemned warehouse | Wastrel territory. Narrow, unlit, half its doors nailed shut. The player learns to fear it at night. |

### 2.4 Landmark placement logic

- **Authority sits at the chokepoint:** Weighhouse + watch-post flank the foot and head of Saltgate Rise. The state exists where cargo funnels; it is absent everywhere else (dossier pattern; canon shows no street watch in Granadad at all — the watch is invented, equipped per Trojian infantry canon WB §8).
- **Fire and water self-sort:** the tar yard (Pitchfield) is pushed to the west map edge, downwind-of-nothing, surrounded by wet strand; the Divine Light mission sits mid-district where alms traffic is; taverns cluster where wages are paid (Tarwalk's center).
- **The east end decays:** condemned pier, condemned warehouse, the Gullet, the outfall — the whole dungeon seam is gathered at the east end so the investigation's surface trail walks the player naturally from busy west (market, Weighhouse) to dark east (the descent).
- **Verticality is legible:** up-slope = safer, brighter, closer to the law; down-slope = wetter, darker, closer to the water and the underworld. The z-gradient IS the danger gradient.

### 2.5 Housing typology: Compounds (DECISIONS.md, Eli 2026-07-13)

Trojian residential space is organized into walled **Compounds**, not detached single-family houses: one large lot with a courtyard/atrium farm at the center (crops for the resident families) ringed by condo-like apartments or mansions belonging to owning families, built up multiple stories where land is scarce. The rooftop of a tall compound is walled off and rented out as mass low-income housing — tents/mud huts, cheap and flammable. The wealth gradient rides the MATERIAL, not the floor plan: owning-family units are Reman-engineered (`reman_concrete` — the canon engineer-civilization's concrete-equivalent); rooftop-slum units are cloth/leather/mudbrick. Visual touchstone: "the big crab in Ald-Ruhn" (Morrowind) — one architectural shell holding a stratified community, not a suburb of identical lots.

**Social consequence, binding:** rooftops are unseemly for every Trojian except a presented Wielder (whose maxed social immunity exempts him alone) — this is *why* the poor live there and *why* burglars/assassins (**"Skyrunners"**) use the rooftop-slum layer as their highway, fleeing across compound roofs after breaking in through a ceiling.

**Applies to the Docks:** every K-site with resident staff (K01 Weighhouse clerks, K05 the Lantern Room's landlady, K17 the Mission's disciple dormitory, etc. — §3) houses its people in a Compound, not a standalone building; a "dozen homes" is a dozen dwelling UNITS inside one compound, never a dozen separate lots. Worked precedent: `content/maps/src/compound_block.tmx` (one mansion + 8 condo units + 3 rooftop huts around a courtyard farm, populated at lore-derived full occupancy with ~64 actors — owning-family mansion with six servant/dependent households, one household per condo, a canon-packed rooftop slum hiding a Skyrunner, a robber and two cutpurses under rooftop covers; all 9 actor groups, all 13 civic jobs and all 6 relationship kinds exercised, F2.5) — the pattern the eventual `docks_surface.tmx` residential blocks (§8.1) should reuse rather than re-deriving.

**Population density (lore-derived, `CompoundBlockPopulation`).** Canon fixes no Docks-ward population, so it is extrapolated from canon troop numbers (novel): a single harbour ward is garrisoned at internal-security scale — ~1 platoon (canon platoon ≈ 100 men, L2074; cf. the capital's palace household-guard ≈ 60, L2074), **not** the field army deployed to the fifteen-year six-front war (30–40k at one front, L2146; legions in the thousands, L1226). Applying the ruled **1 enlisted : 10 not-enlisted heads-of-household** ratio to ~100 enlisted heads gives 1,100 households; the canonical household-size distribution (`household.json` weights {1:20,2:35,3:25,4:15,5:5}, mean 2.5) yields a ward target of **~2,750 people (~1,100 households)** — sensitivity range ~1,650 (palace-guard anchor) to ~2,750 (platoon anchor). One fully-occupied Compound holds ~64, so the ward implies **~40 Compounds**; the MVP fixture realizes ONE (~2.3% of the ward), the rest being off-map narrative context. Every figure above the platoon anchor is **(placeholder)** — the derivation, not the number, is the canon-grounded part.

---

## 3. Establishments (keyed locations)

All names and proprietors **(placeholder)** unless cited. "Runs it" gives the §4 actor group; K# keys are the Tiled marker ids (§8). Investigation-relevant sites flagged ◆.

| K# | Name | One line | Runs it | Sim/investigation relevance |
|---|---|---|---|---|
| K01 ◆ | **The Weighhouse** (harbormaster + customs house) | Berth ledger, tariff scales, impound yard behind; the district's truth-database, and everyone in it lies to everyone except the Wielder | Harbormaster **Ottavan Crell (placeholder)** — Shopkeeper group, bureaucrat variant | Clue site 2 (§5). The Wielder-vs-bureaucracy stage: doors open, ledgers open, clerks seethe (immunity canon, L2414) |
| K02 | **Impound Yard** | Seized cargo behind a spike fence; a bored watchman and a dog | Militia Watch + Animal Keeper | Loot/economy texture; the dog notices things people don't |
| K03 ◆ | **The Gilded Gull** | Captains' tavern: charts on the walls, factors doing deals, the good wine | Shopkeeper (landlord **Master Venn**, placeholder) | Rumor tier: manifests, sailings, which ship came in the night of the killings |
| K04 | **The Bilge** | Sailor dive on the Tarwalk: hammocks upstairs, knives checked at the door, mostly | Shopkeeper | Pay-night economy; Wastrel/Serf mixing bowl; brawl seed generator |
| K05 ◆ | **The Lantern Room** | The neutral-ground house — crews and cliques don't fight here, by ancient unwritten law; the landlady hears everything | Shopkeeper (**Mother Sethra**, placeholder) | The information broker. Deference means she TELLS Gabri things she'd sell anyone else (§5.3) |
| K06 | **Harl's Yard** (shipwright) | Slipway, saw pit, timber pond; a hull up on blocks with something wrong about it | Shopkeeper (**Harl**, placeholder) | Hook: what the wrights found inside a hull under repair (dossier pattern) — seed, not MVP content |
| K07 | **The Ropewalk** | A 60-tile-long shed where cable is laid; longest interior in the district | Shopkeeper + Serf labor | Fire-risk chain (dry hemp); long-sightline interior for one great encounter space |
| K08 | **Brann's Chandlery** | Ship stores: lamp oil, biscuit, cordage, two sets of books | Shopkeeper (**Brann**, placeholder) | Smuggler-facing front (gray ledger); lamp-oil = fire accelerant economy |
| K09 | **Pitchfield** (tar & pitch yard) | Cauldrons, tar barrels, a permanent reek; the one place everyone fears fire | Shopkeeper + Serf | THE fire-risk site (§7.3). Flagship-adjacent: tar fire is the district's tavern-fire analog |
| K10 | **Dawnstalls** (fish market) | Dawn auction off the boats; fishwives, gulls, ice from nowhere the player asks about | Serf (fishwives) + Shopkeeper (auctioneer) | Dawn rhythm anchor; "what came up in the nets" rumor tier; salmon to the palace is canon-adjacent flavor (L2351) |
| K11 | **Salt Row** | Gutting sheds, salting tubs, smokehouses; offal gutter to the sea | Serf | Smell, gulls, Animals scavenging; smokehouse = contained-fire texture |
| K12 | **The King's Bond** (bonded warehouse) | Sealed pending tariff, royal padlocks; even the harbormaster knocks | Militia Watch guards it | Bonded vs gray goods teaches the tax regime in one image |
| K13 ◆ | **The Drowned Hold** — THE warehouse | Condemned, east end, half its floor sagging toward the water; officially empty for nine years, and lamplight has been seen under its doors | nobody — officially | **Clue site 3 and dungeon entrance (§5, §6).** Undercellars below the tide line |
| K14 | **Wrackhouse** (salvage broker) | Buys what the sea spits up, no questions; diving hooks and a bell on the wall | Shopkeeper (**Dagny**, placeholder) | Fence tier; mudlark patron; knows the outfall grate's secret (low tide) |
| K15 | **Fenner's Pawn** | Three brass balls of a sort; wage-advance usury in the back | Shopkeeper (**Fenner**, placeholder) | Stolen-goods laundering chain; economy sink |
| K16 ◆ | **The Drowned-Name Wall** (sailors' shrine) | A seawall niche of candle stubs and names scratched for those the sea kept; folk practice, no priest | nobody — Serfs and sailors tend it | **(placeholder folk practice — canon religion is the Divine Light only; this is explicitly non-doctrinal grief-custom, tolerated).** New names appear here before bodies are found: quiet clue surface |
| K17 ◆ | **Mission of the Flame** | Divine Light almshouse: soup, bunks, a disciple always awake; the white garb locals distrust | Priest of the Flame (**Father Maell**, placeholder) + Disciples | Canon-consistent (Divine Light runs alms deliveries in the suburbs, L2420; white garb "not a welcome sight" outside the walls, L2410). Gabri's daytime anchor and report point |
| K18 | **Squall's Bathhouse** | Copper boilers, steam, the only warm floor in the ward; everyone is unarmed and equal in the steam | Shopkeeper (**Squall**, placeholder) | Texture + a socially-flat rumor room (deference is awkward when nobody wears rank) |
| K19 | **The Rows** (flophouse) | Hammock-space by the night; the landlord remembers every face | Shopkeeper | Where crews sleep; where someone didn't come back to their hammock |
| K20 | **Merle's Boats** | Boathouse and waterman's hire; the floor over the water has a section that lifts | Shopkeeper (**Merle**, placeholder) | Smuggler infrastructure: false floor, night tide-runs; alternate late-game surface↔water route |
| K21 | **Saltgate Watch-Post** | The ward's one watch station, at the top of the Rise; notice board and gibbet cage outside | Militia Watch (**Sergeant Vess**, placeholder) | The state's only ambient voice. Watch = order-keepers, not investigators — they haven't looked at the bodies properly and defer the whole matter to Gabri on sight |
| K22 | **Netmenders' Arcade** | Old women, needles, and the district's collective memory under a leaky colonnade | Serf | Ambient-rumor surface; they saw the ship come in; they always see everything |
| K23 | **Cooper & Blockmaker's shed** | Barrels and pulley-blocks; sawdust ankle-deep | Shopkeeper + Serf | Fire chain (sawdust); barrel props for the smuggler cellars below |
| K24 | **The Eel-Pots** | Night food stalls on the Tarwalk, lantern-lit; the only honest light after dark | Serf | Night-rhythm anchor; lantern light profile (§7.2) |
| K25 | **Kennel Row** | Rat-catchers' yard: dogs, cages, the men the city calls when things gnaw | Animal Keeper (**Old Cobb**, placeholder) | The underworld's surface interface — rat-catchers are the natural dungeon guides; Cobb's dog refuses the Drowned Hold's door (clue texture) |

---

## 4. People — the eight actor groups (RULED BY ELI, 2026-07-12)

The spawning roster IS these groups; each becomes an `Actor` subclass (thin: composed behavior policies + raws-driven stats, DECISIONS.md Actors row). All group *behavior specifics* below are design targets for ACTORS-SPEC.md, **(placeholder)** throughout except where canon is cited. Identity note (DECISIONS.md Identity row): every deference reaction below reads the **presented** identity — Persona-shaped from day one.

### 4.1 Deference master table (reaction to the Wielder, presented identity)

Canon anchor: "complete lawful immunity, no door was locked to the Wielder of the Flame nor action forbidden" (L2414, L2967); but "the white garb of the Divine Light was not a welcome sight in these parts of the city" (L2410). The Docks stages BOTH: deference that opens doors, resentment that colors it.

| Group | To the Wielder | Texture |
|---|---|---|
| Militia Watch | Full deference: never HOSTILE toward Gabri (COMBAT-SCREEN-SPEC §5.3, binding), unlocks anything, answers anything — through gritted teeth | The bureaucrat-seethe surface: compliance logged, resentment accrued (narrative only — Presence is immutable, PROGRESSION-SPEC §5) |
| Serf | Awed, frightened, truthful; may flee the conversation after answering | They tell him things because refusing a Wielder is unthinkable — and then avoid him |
| Wastrel | Defers face-to-face, vanishes after; will NOT initiate crime in his sight; some sell information unprompted | Deference ≠ loyalty: they warn each other he's asking |
| Priest of the Flame | Warm, hierarchical deference; expects reports in return (Minister John pattern, L2414–2428) | The one group with claims ON Gabri |
| Disciple of the Flame | Reverent, eager, follows him around if allowed | Escort/witness resource; can be sent with messages |
| Shopkeeper | Open books, open back rooms, free samples, visible calculation | "No door locked" (L2414) applied to commerce: the gray ledger comes out only for him |
| Animal Keeper | Plain deference; the useful one — hires out dogs and knowledge cheap or free | Old Cobb is the descent's Virgil if asked |
| Animal | None. Animals do not defer. | **The north-star joke made flesh: deference is social; a dog, a tide, and a fire are not social** |

### 4.2 Group dossiers

Each: docks presence · needs/rhythm · idle behavior · reactions (fire/water/crime; Wielder covered above) · FACES-SPEC archetype mapping (v0 set: `guard, cultist, monk, noble, thug, laborer` — extensions flagged).

**MILITIA WATCH** — *(invented; no street watch shown in Granadad; equipment per Trojian infantry canon WB §8: leather jerkin, chainmail for the wealthier, hobnailed boots, spear-dominant)*
- Presence: 4–6 on shift. Static post at K21; one 2-man patrol looping Saltgate Rise–Tarwalk-west by day; they do not enter the Gullet, ever; night presence collapses to the post (dossier day/night pattern).
- Needs/rhythm: shift changes dawn/dusk; food from the Eel-Pots; boredom is the default state.
- Idle: lean on spears, check carts at the Rise chokepoint, feed the impound dog, harass Wastrels within sight of the post.
- Fire: respond, form bucket lines, conscript Serfs — competently for structures, uselessly for tar. Water/flood: rope off, don't enter. Crime: detain or beat per watch raws; brutality normalized, investigation nonexistent — they keep order, they don't solve anything.
- Faces: `guard`.

**SERF** — *(canon-adjacent: the suburb poor of L2328 are the parent texture; "serf" framing placeholder)*
- Presence: the crowd. 30–50 by day (stevedore gangs on the quays, fishwives at Dawnstalls, gutting crews on Salt Row, sweepers, water-carriers); drops to a handful at night.
- Needs/rhythm: paid daily in coin, spent nightly in the ward (dossier labor-market pattern): dawn muster → cargo/fish work → dusk pay-out → tavern → the Rows or a cellar bunk.
- Idle: queue, gossip at the Netmenders' Arcade, mend, scavenge the strand at low tide, tend the Drowned-Name Wall.
- Fire: flee, then loot the edges if it's someone else's. Water: they know the tide table in their bones; never caught by it. Crime: see nothing, say nothing — to anyone but the Wielder (§4.1 — this asymmetry IS the investigation mechanic, §5.3).
- Faces: `laborer`.

**WASTREL** — *(invented; Tora's alley-parasite register is the imported texture — "creeping in the alleyways… One would be strangled instantly if they left the safety of the crowd," L426 — that is Tora canon, applied here as placeholder)*
- Presence: 8–15, skewing night. Mudlark kids on the strand at low tide; pickpockets in the day crowd; toughs in the Gullet after dark; one crimp working the Bilge's drunks.
- Needs/rhythm: inverse of the Serfs — sleep by day (the Rows, condemned buildings), work the dusk pay-out and the fog.
- Idle: watch. Wastrels are the district's other information network, the bought one.
- Fire: first to loot. Water: mudlarks profit from low tide, know the outfall grate. Crime: they ARE the ambient crime — robbery, crimping, cellar-cracking; strictly night-and-fog, strictly away from the post.
- Faces: `thug` (adult), plus **extension: `urchin` archetype (placeholder)** for mudlark kids — thug tag-multipliers at child band if the part library supports bands, else fold into `thug`.

**PRIEST OF THE FLAME** — *(canon group: Divine Light ministry, monastery hierarchy, alms in the suburbs, L2410–2428)*
- Presence: exactly one — Father Maell at the Mission (K17). More priests exist off-map (the Cathedral is inside the walls, L2410–2413).
- Needs/rhythm: dawn office, alms at midday, doors barred an hour after dark (he knows what the suburbs are, L2410).
- Idle: ledger of souls fed, letters to the monastery, worry.
- Fire: sees the Flame in it — organizes help, fearless (doctrinally awkward to fear fire). Water: blesses departures. Crime: shelters victims, won't name culprits to the Watch — will to Gabri.
- Faces: `monk`.

**DISCIPLE OF THE FLAME** — *(canon-adjacent: seminary initiates chosen by the Flame, L2413; docks posting placeholder)*
- Presence: 2–3 at the Mission; one always awake (night soup, night door).
- Needs/rhythm: serve, study, sleep in shifts.
- Idle: chores, catechism, staring at the sea (most are suburb-born; one is terrified of it).
- Fire: runs TOWARD it (doctrine + youth). Water: normal fear. Crime: reports everything to Maell, breathlessly. **Special: a Disciple who spots bloodletter sign recognizes it from catechism and does exactly the wrong thing** (see seeds, §4.3).
- Faces: `monk` (young bands) — **extension: `acolyte` tag-multiplier profile under `monk` (placeholder)**.

**SHOPKEEPER** — *(invented; category per dossier)*
- Presence: one per K-site above (~14 active), living over or behind the shop.
- Needs/rhythm: open at dawn bell (Dawnstalls earlier), shutter at dusk, count coin by lamplight. Stock arrives by quay and cart; each shop has 1–3 supply dependencies (§7.1 economy graph).
- Idle: tend shop, gossip across thresholds, adjust prices when the economy layer says so.
- Fire: THE terror — they own the flammable capital. Bucket-chain instantly; will pay for prevention (fire-watch fees, an economy sink). Water/flood: cellar stock is the exposure; flood = ruin. Crime: protection-shaped silence toward the Watch; itemized grievance lists for the Wielder.
- Faces: **extension: `merchant` archetype (placeholder)** — `laborer` base with aproned/prosperous tag multipliers; `noble` reserved for off-map factors.

**ANIMAL KEEPER** — *(invented; invariant per Eli's ruling: always owns ≥1 Animal)*
- Presence: 3–4. Old Cobb (Kennel Row, rat-dogs), the impound watchman (yard dog), a carter (dray horse) on the Tarwalk day-loop, a goose-woman on Herring Lane.
- Needs/rhythm: animals set it — feeding, working the animal, stabling at dusk.
- Idle: talk to the animal; talk to anyone about the animal.
- Fire: saves the animal first, always — a scripted-feeling scene the sim generates for free. Water: horses balk at flooded fords; dogs swim. Crime: the dog reacts first (see Animal).
- Faces: `laborer`, weathered tags.

**ANIMAL** — *(invariant: always belongs to a Keeper; strays are a spawn-validation error, not a feature — ACTORS-SPEC must enforce)*
- Presence: 1 per Keeper minimum; dogs, a dray horse, geese; gulls are ambience/particles, not Actors **(ruling suggestion for ACTORS-SPEC)**.
- Needs/rhythm: food, the Keeper's proximity, sleep. Leash-follow behavior policy; bounded wander when idle.
- Idle: scavenge Salt Row, sleep in doorways, bark at the tide.
- Fire: flee, loudly — animals are the district's fire alarm. Water: dogs swim, horses panic, geese win. Crime/danger: **animals sense what people don't** — canon precedent: sensing is mutual and instinctive around bloodletters (the hair-prickling scream of instinct, L2339, L2344). A dog refusing a door is this district's canary **(system placeholder: Animal fear-aura response to tagged entities/sites)**. No deference: an Animal reacts to Gabri's smell, not his office.
- Faces: none (no face panel for animals; bestiary art seam instead — **flag for FACES-SPEC**).

### 4.3 Emergent-scenario seeds (group × group)

Sim-pressure seeds, not scripts — each is a state overlap the ACTORS-SPEC behavior policies should make *possible*, Streets-of-Rogue-style. **(all placeholder)**

| Pairing | Seeds |
|---|---|
| Wastrel × Watch | 1. Pickpocket bolts through the fish market; the pursuit knocks over braziers (fire seed). 2. The Watch beats a mudlark at the post; Serf crowd sours, work slows — Gabri witnesses institutional rot firsthand. 3. Crimp drugs a drunk in the Bilge; the "body" is carried past the patrol, and the patrol is paid not to look. |
| Wastrel × Shopkeeper | 1. Night cellar-cracking at Brann's hits the SMUGGLER goods — the loudest silence in the ward; nobody can report it. 2. Protection dispute: shutters smashed at dawn; Fenner knows who, sells it. 3. A mudlark fences salvage at the Wrackhouse that came off a corpse (§5 clue vector). |
| Animal × Keeper (slip) | 1. The dray horse bolts on the Tarwalk at a fire-smell; cargo, stalls, and a Serf's leg in its path. 2. Cobb's best ratter slips down the Drowned Hold breach; Cobb begs the Wielder to get her back — a heart-hook into the dungeon. 3. Geese loose in the Weighhouse: the ledger page count is suddenly relevant comedy. |
| Disciple × bloodletter sign | 1. The night-soup Disciple finds the tide-line body FIRST, recognizes the sign from catechism, and moves it to the Mission for rites — contaminating the scene before Gabri arrives. 2. A Disciple tails a "shadow" into the Gullet alone (canon: the sign means terror deliberately maximized, rent robes, claw wounds, L2337/L2341 — this should nearly kill them). 3. A Disciple preaches the breach openly at the Eel-Pots; panic rumor spreads; night streets empty; the Wastrel economy notices. |
| Serf × tide | 1. Cargo gang works a deep-draft ship against the falling tide; the last pallet is a drowning roll. 2. Strand-scavenging Serf finds §5's second body at low tide — a clock: the tide will take it back. 3. A flooded cellar bunk-room at spring tide; the Rows overflows; tempers at the Bilge. |
| Fire × everyone | 1. Pitchfield ember day: one spark event escalates group-by-group (Animals alarm → Serfs flee → Shopkeepers bucket-chain → Watch conscripts → Wastrels loot → Priest walks in). The district's whole social physics in one scenario. 2. Smokehouse flare on Salt Row blamed on a Wastrel who didn't do it. |
| Watch × Priest | 1. The Watch wants a culprit for the bodies (any culprit); Maell shelters the accused Wastrel in the Mission; standoff at the door only the Wielder can walk through. |
| Shopkeeper × Shopkeeper | 1. Chandlery vs Wrackhouse: salvage undercuts new stores; the gray ledgers compete; one of them tips the Watch about the other's night boats. |

### 4.4 Named NPCs (all placeholder; drawn from the groups)

| Name | Group | Hook |
|---|---|---|
| **Harbormaster Ottavan Crell** | Shopkeeper (bureaucrat) | Meticulous, furious, compliant: his ledger has one erased line, and he will show the Wielder even that — while making him ask for it item by item |
| **Sergeant Vess** | Militia Watch | Relieved, not resentful: dumps the whole bodies matter on Gabri with both hands and a salute |
| **Mother Sethra** | Shopkeeper (Lantern Room) | The neutral ground made flesh; trades in everyone's secrets and gives Gabri's to no one — tells him so to his face |
| **Father Maell** | Priest of the Flame | Runs the Mission; wrote three unanswered letters to the monastery about "something wrong on the water" before the first body |
| **Onna** | Disciple of the Flame | The night-soup disciple; found the first body; hasn't slept since; knows one detail she doesn't know matters |
| **Old Cobb** | Animal Keeper | Rat-catcher; his dogs won't pass the Drowned Hold's door and he's been drinking about it |
| **Tarry Jek** | Wastrel (mudlark) | Sold something off a corpse to the Wrackhouse; the ward's cheapest and best pair of eyes if Gabri thinks to hire what everyone else kicks |
| **Dagny** | Shopkeeper (Wrackhouse) | Bought Jek's find, no questions; her stock room holds clue-adjacent salvage and her silence has a Wielder-sized exception |
| **Harl** | Shopkeeper (shipwright) | Repairing the hull of the ship that came in that night; found scratch-marks inside the hold he's been paid not to mention |
| **Brann** | Shopkeeper (chandler) | Two ledgers; the gray one shows night-oil sold to nobody he'll name — except to the Flame |

---

## 5. THE OPENING INVESTIGATION (scene-level)

### 5.1 Canon frame and the honest adaptation note

Canon's breach arc: the Flame calls Gabri, event-driven and directional (L2420, "the call grew stronger…"); the impossible-breach thesis — "no bloodletter can enter the Trojian territory" (L2422); bloodletter sign as a readable forensic language (§5.2); the trail leads DOWN (Zradist dens in the sewers via a grate, L2345). The MVP opening **precedes and mirrors** the canon abbey beat rather than replacing it: something ELSE came in first, by sea, weeks before the novel's Part One — a lesser vector, not Zradist **(placeholder creature; preserves canon's timeline and the abbey scene for post-MVP content)**. Consistency guards: the Flame's guidance stays event-driven, never at-will divination (canon shows exactly one call on page — dossier §E note); the breach thesis is why a single body at a dock is a Wielder matter at all.

### 5.2 The hook

Two nights ago a body came up against the outfall grate at low tide — a stevedore, robes rent, claw-penetration wounds, bled far past what the wounds explain, face fixed in terror (bloodletter sign per canon: L2337, L2341 — terror deliberately maximized before feeding). Onna found it doing night rounds and the Mission took it in for rites. Last night the Flame stirred Gabri awake — a directional call toward the waterline, faint, gone by morning **(placeholder event, canon-patterned on L2420)**. Sergeant Vess, out of his depth and glad of it, posted the matter to the monastery; Gabri arrives at the Docks at dawn with a passport, white garb, and the only authority in the ward that everyone answers.

### 5.3 How deference gates information (the design law of this game's investigation)

**People TELL the Wielder things. The investigation is never persuasion — it is knowing WHERE to ask.** No dialogue-skill checks exist; the gate is geographic and social-topological: which door you've found, which group's knowledge-domain you've realized is relevant, which name you've heard. Serfs answer and flee; Shopkeepers open the gray ledger; the Watch unlocks the impound; Wastrels must be FOUND first (finding them is the puzzle — deference doesn't help you locate someone hiding from you); Animals can't testify but react (Cobb's dogs). The bureaucrat-seethe texture: information is never refused, but it can be surrendered maliciously literally — Crell answers exactly what is asked, so the player must learn to ask better questions **(interaction design placeholder for the G-track)**.

### 5.4 The three surface clue sites

1. **The Mission's back room (K17)** — the body. Readable sign (canon forensic set): claw penetration, catastrophic blood loss, rent clothing, terror rictus; and on the flagstones where it lay overnight, a residue trace — black, tacky ichor (dead-bloodletter marker, L2979) that shouldn't be there if the victim merely died. Onna's unknowing detail: the body was ABOVE the grate, inside the outfall — it came from the sewer side, not the sea. Points to: the outfall (but the grate is corroded shut… from outside).
2. **The Weighhouse ledger (K01)** — the ship. One arrival matches the timeline: a grain coaster from across the sea **(placeholder — sea trade canon-plausible per Devin's origin, L2406)**, berthed three nights, sailed on the dawn tide after the killing. The erased line: a cargo item struck from the manifest after inspection, initialed by a clerk who has since stopped coming to work. Crell surrenders everything, seething. Points to: where the struck cargo went — carted east, to a warehouse that is officially empty. Harl's yard corroborates (scratch-marks inside the repaired hold — something rode in that hull, L-pattern: entry without forced entry, slides through cracks "like a snake," L2335).
3. **The Drowned Hold (K13)** — the trail. Lamplight seen under the doors of a nine-years-condemned warehouse; Cobb's dogs refuse the threshold; Tarry Jek (if found — the Wastrel-locating sub-puzzle) saw men carry crates in by night and "a thin fellow walk in through the gap no man fits." Inside: smuggler occupation debris (recent, abandoned in a hurry), a second body — a smuggler this time, same sign, hung by the feet (the mutilation signature escalating toward the canon crypt-guardian display, L2706, deliberately quoted-by-the-monster) — and the sagging floor's breach into the undercellar. The descent begins.

**Structure note:** sites 1–2 are parallel (either first); both point to 3; 3 is gated only by finding it (the Gullet at night is the physical danger tax — fragile Gabri among Wastrels who don't recognize him in the dark: deference requires RECOGNITION, the north-star fence in miniature).

### 5.5 The descent

The undercellar breach (K13, z11→z10) is the surface/dungeon seam. The trail is physical and canonical in language: ichor traces, drag marks, terror-dead vermin, and cold — each level reads as "it went down." Every third building COULD do this (dossier lesson: descent as integrated infrastructure); the MVP scripts one entrance and hints at two more (Merle's false floor K20, the outfall grate K—§2.2) for post-MVP delving texture.

---

## 6. Starter dungeon (locked chain: smuggler cellars → sewer outfall → drowned structure)

Three z-levels under the east end, each ~one 32×32 chunk footprint expanding downward **(all placeholder; only sewers L2345 and the distant royal crypts L2703+ are canon undergrounds — the crypts are near the Inner City, NOT here; this structure only gestures toward them)**. Threat calibration: the canon ladder's bottom rungs — vermin, desperate men, environment, and at the very bottom one taste of the real ladder. Fragile Gabri: flee is a first-class verb (COMBAT-SCREEN-SPEC §4.5), surprise kills (§1.4), and the environment is the boss.

### L1 — The Undercellars (z10) — *men and rats*

- **Character:** smuggler-cut cellars linking three surface buildings' foundations (Drowned Hold, Brann's back-cellar, a collapsed third). Dry brick and dirt west, seeping wet east. Barrel mazes, a contraband cache (the struck cargo, breached and empty — whatever it held let ITSELF out), a cold firepit, bedrolls for six, five sets of gear.
- **Beats:** (1) entry shaft + rat skirmishes (tutorial combats, terrain tag `interior`); (2) the cache room — investigation beat: the crate is scratched from the INSIDE (mirrors Harl's hull); (3) the holdout — the smuggler crew's last man, half-mad, barricaded: the MVP's first human encounter and first deference subversion — he doesn't recognize the Wielder in the dark and attacks (or, approached with light, surrenders and TELLS everything: the player's first taught choice between light-as-safety and light-as-warning); (4) the tunnel down — smuggler-cut stair toward the sewer level, marked with their tide-chalk.
- **Threats:** rats (2–4), the holdout (1, weak, crossbow), rotten floor tiles (fall to L2).
- **Texture:** lantern-warm browns in the black; dry → damp gradient; first darkness deeper than street night.

### L2 — The Outfall Junction (z9) — *the tide is the boss*

- **Character:** brick sewer galleries converging on the seawall outfall (canon anchor: sewers under the city, grate-entered, L2345). The tide clock rules: at high tide the outfall arm floods to the crown (impassable, drowning-real); at low tide it drains to shin-deep. FLUID lane showpiece (§7.4).
- **Beats:** (1) junction chamber — three arms: outfall (west, tidal), city-drain (south, barred by ancient bars, faint air from beyond: the off-map sewers toward the Inner City — narrative pointer, permanently sealed in MVP); collapse arm (east, down to L3); (2) the den — a scraped nest above the waterline, fish and rat bones, ichor smears: it slept here (canon behavior quote: dens in sewers to "rest and recuperate," L2345); (3) the second-victim alcove — the missing customs clerk, sign complete; his satchel holds the struck manifest line (paper clue closing the Weighhouse thread); (4) the tide gauntlet — reaching the east collapse requires the low-tide window or a swim-and-pray.
- **Threats:** drowning (the honest killer), sewer vermin swarms, one crazed hermit-wastrel who lived down here until two weeks ago and now won't stop whispering about "the wet man" (information, or a knife in the dark if startled — surprise rules).
- **Texture:** black water with lantern-cone reflections (luminous-on-black flagship imagery); sound design hooks: drip, surge, the grate's boom at tide-turn.

### L3 — The Drowned Structure (z8) — *the crypt edge*

- **Character:** the collapse arm drops into something older than Granadad's port — cut-stone foundations, a sunken colonnade hall, half-flooded, of a predecessor settlement **(pure placeholder — canon is silent on pre-Granadad ruins)**. Materials shift: brick → granite ashlar; and a cold red glow from a cracked wall-socket — a single dying **glowstone** (canon material: "eerie red light," unattended for ages, L917; its presence here placeholder) — the level's only non-carried light.
- **Beats:** (1) the colonnade — flooded aisles, dry spine, drowned-hall dread; (2) the display — the thing's larder/warning: a corpse hung by the feet, mutilated in the crypt-guardian signature (jaw, ribs — L2706's language at minimum-viable horror); one reanimated drowned corpse as guardian that wakes at light/proximity (canon trap-guardian behavior, L2708–2713) — the MVP's hardest fight and fully fleeable; (3) the bottom — **the hook, not the resolution (placeholder, needs Eli):** a fresh-scratched mark on the oldest wall — the mark of the Y'marr (canon sign, L2421) — above a gap in the stones the size of a crawling man, breathing cold air from the dark toward the Inner City… and the crypts (L2703). Gabri can't follow (gap too small / flooded beyond — hard gate). What he takes up: the manifest page, the mark's rubbing, and the thesis that should be impossible: *something crossed the sea into Trojian territory* (L2422). Report to Father Maell → letter to Minister John → MVP narrative close, sequel hook armed.
- **Threats:** the guardian (1, slow, relentless, water-immune — flee across flooded aisles is the intended solve for a weak Gabri), cold-water exhaustion **(system placeholder)**, darkness itself.
- **Texture:** red glowstone vs Gabri's white-gold Flame light in black water — the art register's thesis statement. Materials on-palette: granite, brick, water, ash, glowstone (all in MATERIALS-CANON §1).

---

## 7. Sim hooks

### 7.1 Economy sites (macro layer)

- **Produces:** fish (Dawnstalls → Salt Row preserved), salt goods, cordage (Ropewalk), barrels/blocks (K23), tar/pitch (Pitchfield), repairs (Harl's), salvage (Wrackhouse).
- **Consumes:** timber (into Harl's, from off-map by sea), hemp (Ropewalk), grain/oil (Brann's, Eel-Pots), coin (the daily wage loop: quay → pay-out → taverns/Rows/Fenner's, a closed circulator the macro layer can run whole).
- **Interface flows (off-map):** imports up Saltgate Rise to the Inner City; tariff skim at the Weighhouse; smuggler leakage bypassing it (Merle's, the gray ledgers) priced ~10% under official (dossier pattern, placeholder rate).
- Chunk summaries carry these as the ECON section's site list **(mapping to ARCHITECTURE §9 ECON placeholder)**.

### 7.2 Light profile (the luminous-on-black star)

- **Ambient:** fog off the water most nights — depressed ambient tiers, shortened sightlines **(fog system placeholder)**; moonless canon-dark suits the register.
- **Sources:** Eel-Pot lanterns (warm points along the Tarwalk), tavern windows, the Weighhouse signal mast lamp, the Mission's night lamp (doctrinal: the Flame is kept lit), Watch-post brazier, Pitchfield's banked cauldron glow (ominous red), the drowned glowstone (L917 red, dungeon L3), and Gabri's own light — the Flame's blinding radiance is canon combat kit (L2339–2341).
- **Design law:** up-slope bright, waterline dim, Gullet black; the district teaches light = safety, then L1's holdout beat teaches light = announcement.

### 7.3 Fire risk map

| Tier | Sites | Notes |
|---|---|---|
| Extreme | Pitchfield (tar, pitch, open cauldrons) | The district's flagship-fire candidate; getilia-soaked trudgeon fence around it **(canon material behavior — fireproof, L1083; placement placeholder)** as the sim's own firebreak |
| High | Ropewalk (dry hemp), Cooper's shed (sawdust), Salt Row smokehouses (contained flame daily), Brann's (lamp oil) | The chained-ignition corridor along Ropewynd |
| Medium | Taverns, the Rows, thatch roofs up-slope | Standard oak/thatch raws (MATERIALS-CANON §1.10–1.11) |
| Low | Quays and strand (wet), stone Weighhouse, the Bond (brick), flooded cellars | Wet quays are the natural firebreak line — fire wants to run east–west along Ropewynd, not north into the water |

### 7.4 Fluid features

- **Tide:** harbor water surface oscillates z10 ↔ z9 on a fixed deterministic cycle **(period placeholder; must be save-stable)** — gates the outfall, the mudflats, under-pier scavenging, and dungeon L2.
- **Tide grates:** the Outfall (K–§2.2) and two storm-drain grates on Tarwalk (street grate = canon sewer access image, L2345).
- **Flooded cellars:** east-end cellars flood at spring tide; the Drowned Hold's undercellar east rooms stand shin-wet permanently (L1 texture); dungeon L3 is permanently part-flooded.
- **The offal gutter** (Herring Lane → sea): a surface fluid ribbon, filth texture per canon suburb register (L2328) — cosmetic fluid, sim-cheap.

---

## 8. Tiled authoring plan

### 8.1 File breakdown

| File | Size | z-levels | Content |
|---|---|---|---|
| `maps/docks_surface.tmx` | 192×128 | z8–z15 (8 layers) | The whole ward: terrain, buildings (1–2 stories), quays, water at both tide marks (low-tide lane baked as metadata, tide handled by sim) |
| `maps/docks_under_L1.tmx` | 64×64 | z10 | Undercellars (overlaps surface footprint east end; importer stitches by world offset) |
| `maps/docks_under_L2.tmx` | 64×64 | z9 | Outfall junction + sewer arms |
| `maps/docks_under_L3.tmx` | 64×64 | z8 | Drowned structure |
| `maps/interiors_*.tmx` | as needed | — | Only if the importer wants big interiors (Ropewalk, Weighhouse) as separate stamps; default: inline in surface file |

Importer contract: emits TROJSAV at tick 0, byte-deterministic (ARCHITECTURE §9). Underground files occupy the same world chunks as the surface file's low z-layers — **ruling needed: one merged 16-z file vs. the split above; split recommended** for authoring sanity, importer stitches.

Residential blocks within `docks_surface.tmx` follow the Compound typology (§2.5), not detached buildings — author them the way `content/maps/src/compound_block.tmx` already does (courtyard-farm center, condo/mansion ring, rooftop-slum layer), not as one-lot-per-household stamps.

### 8.2 Layer & marker conventions (extends F2's TMX reader conventions — reconcile with `docs/art/TILE-ART-SPEC.md`)

- **Tile layers:** `z<k>_material`, `z<k>_form` per z-level (material id = raws id string via tileset property).
- **Object layers:**
  - `establishments` — one rect per K-site, props: `key` (K01…K25), `name`, `group` (owner actor group), `door` points.
  - `npc_spawns` — points, props: `group` (one of the eight), `named` (optional NPC id from §4.4), `schedule_anchor` (home/work/night refs by K-key).
  - `clue_sites` — points/rects, props: `clue_id` (C1–C3 surface, D1–D6 dungeon), `stage_gate` (which prior clue unlocks visibility of the interaction, not the geometry).
  - `exits` — edge rects, props: `label` ("Saltgate Road — to the Inner City", "the coast road", "the Shambles", "the abbey road"), `enabled:false` (all MVP exits are narrative-only walls).
  - `fluids` — tide-zone polygons (`tide_low_z`, `tide_high_z`), flooded-cellar volumes, gutter ribbon.
  - `fire_risk` — optional polygons with `tier` for the macro layer's risk map (§7.3) if not derivable from materials alone.
- **Material palette per map:** granite, brick, dirt, oak, thatch, trudgeon_wood (+`@getilia_soak` at Pitchfield fence), steel (fixtures), water, ash, glowstone (L3 only) — all raws exist in MATERIALS-CANON §1; **no new materials required for the ward.**

### 8.3 Effort estimate (placeholder, for planning)

| Item | Estimate |
|---|---|
| Surface terrain + streets + waterline | 2–3 days |
| 25 establishment buildings + interiors | 4–6 days (biggest line item; reuse building stamps aggressively) |
| Dungeon L1–L3 | 2–3 days |
| Object layers (spawns, clues, exits, fluids) | 1–2 days |
| Importer round-trip + fixups | 1–2 days |
| **Total** | **~10–16 author-days**, one author, before art pass (Kenney 1-bit adaptation is the TILE-ART-SPEC seam) |

---

## 9. Open questions / needs-blessing (Eli)

1. The opening PREQUEL framing (§5.1): a lesser vector arrives by sea before the novel's abbey events, preserving canon timeline — bless the adaptation stance.
2. What the vector IS (never fully shown in MVP — sign only) and what the L3 bottom-hook mark implies — placeholder creature identity needs a canon-safe ruling.
3. The sailors' shrine (K16) as non-doctrinal folk practice — confirm it doesn't collide with Divine Light exclusivity.
4. Map split (8.1): surface + 3 underground files vs one 16-z file.
5. Tide period + spring-tide events — sim crew ruling, this doc only requires determinism.
6. FACES-SPEC extensions: `merchant`, `urchin`/`acolyte` profiles; Animals get no face panel.
7. Gulls/vermin as ambience vs Actor instances (recommended: ambience; Animals-as-Actors only when Keeper-owned, per the roster invariant).
8. Named-NPC list (§4.4) — bless names or rename freely; all placeholder.
9. Vertical extent: keep 192×128×16 (48 chunks) with z0–7 as importer-baked solid substrate, or shrink to 192×128×8 (24 chunks) — all authored content lives in z8–z15 either way (§2.1, §8.1); sim/importer crew's call.

### §9 RESOLUTIONS — all nine blessed by Eli, 2026-07-12

1. **BLESSED**: prequel framing stands (lesser vector by sea, before the novel's abbey events).
2. **RULED**: the vector is a **lesser bloodletter spawn**, off-screen in MVP — sign only; the L3 bottom hook implies it without showing it.
3. **BLESSED**: sailors' shrine stays as tolerated folk practice; Disciples quietly disapprove (tension intended).
4. **RULED (off-recommendation)**: ONE 16-z map file — §8.1 authoring plan updates to a single tavern-convention file with 16 z layer-groups; no split files.
5. **DELEGATED**: tide period + spring-tide events → sim crew picks deterministic values (F4 fluids milestone).
6. **BLESSED**: FACES extensions approved — merchant, urchin, acolyte profiles; Animals get no face panel.
7. **RULED (off-recommendation)**: ALL animals are Actors, including gulls/vermin. Reconciliation (Claude, veto-able): the Keeper-owned `Animal` type keeps its invariant; wild creatures become a new thin `Feral` subclass (no owner, scavenging policies, raws-driven population caps) — the add-a-type walkthrough's first real exercise. ACTORS-SPEC addendum required.
8. **BLESSED**: named-NPC list as shipped (§4.4).
9. **RULED**: keep 192×128×16 (48 chunks) with z0–7 solid substrate — digging headroom retained.
