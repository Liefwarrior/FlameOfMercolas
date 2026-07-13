# MATERIALS-CANON — provenance backbone for all v0 raws

**Status:** reference dossier for raws review. Every number in `content/raws/` must trace back to a row here, either as *canon-derived* or *invented-for-v0*.

**Sources & precedence** (per repo canon rule):
1. **Novel** — `C:\repositories\LordOfTrojia-MVP\Lore\Lord of Trojia (indexable).txt`. Cited below as `(novel L<line>)` = line number in that file. The novel wins all conflicts.
2. **WorldBible** — `C:\repositories\LordOfTrojia-MVP\Documentation\WorldBible.md`, cited `(WB §<n>)`. Audited v1.1+ against the novel; treated as reliable summary, novel still wins.
3. The `Lore\*.html` files are **NON-CANON** (AI fan summaries) and are cited nowhere in this document.

**Raws provenance convention** (loader-ignored fields, per shared ruling): every raw should carry
`"provenance": "<canon cite or 'invented for v0 balance, needs Eli's blessing'>"` and optionally `"notes"`.
Suggested cite forms: `"novel L1398: Chromatis holds exorbitant energy, cool until near-full"` · `"WB §9: trudgeon soaked in getilia sap is fireproof"` · `"invented for v0 balance, needs Eli's blessing"`.

Sim-side vocabulary referenced below is ARCHITECTURE.md: raws schema & loader invariants (§10), charge machinery (§1.1 #14, §3 `com.trojia.sim.reaction`, §11 Chromatis Experiment), phorys reaction (§1.1 #22, §5 `PressurePulseEvent`), treatments (§10 sibling files), FIELDS_RESERVED phase (§4 phase 5) and the `AETH` save section (§9).

---

## 1. Material-by-material canon dossier

### 1.1 chromatis — the most canon-constrained material in v0

**Canon facts (paraphrase + cite):**
- Unique among metals: "could also hold EXHORBANENT amounts of energy inside of it. It didn't even feel hot until it held enough energy to obliterate a pile of rocks the size of a house" (novel L1398). WB §9: "Holds enormous energy without feeling hot until saturated."
- Charge scale: filling a key-sized piece takes "over a hundred channelers, over a year"; a source-tier crafter (Eric/Vallech) fills it in seconds (novel L1399, L516).
- Color: uncharged is "a blueish silvery color"; at full charge turns "pure gold" (novel L2785–2786). A blade actively discharging as heat glows "bright orange" (novel L3004). Full stakes feel "warm to the touch"; empty ones "very cold to the touch, almost like ice" (novel L2782–2784).
- Filling sensation: "The Chromatis was like a void... no matter how much energy he put into it that it could always hold more" until warm (novel L2784) — capacity is enormous but finite (warmth signals full).
- Mechanical: bow alloy (steel+Chromatis) "as bendable as a fresh yew, but strong enough to shoot something over eight hundred yards" (novel L1246); broadheads "punch a hole through steel three inches thick" (novel L1246); Blademaster swords never break or dull when cooled right after forging (novel L1257).
- Blademaster blades are energy reservoirs: Hordrar fights twenty hours on "his Chromatis blade and the energy he had stored in it" (novel L2213); Droft "drew from the energy stored in his Chromatis sword and felt all of his weariness wash away" (novel L3002).
- Forging: "Blacksmiths have to keep it under intense flame for nearly a week before it can be shaped, but even then it never glows gold like this, just a coppery color" (novel L2786); Revlin's welding device could "forge Chromatis objects, without leaving them in a fire for a month" (novel L1399). So Chromatis softens/melts only under extreme, sustained heat.
- Violent discharge: Sarai's stake, released all at once, blows a "large gouge in the side of the plateau... made from the force of the energy inside the Chromatis stake" (novel L3048) and the residual flame "collapsed on itself... before exploding with a force one-hundred times what it had been" (novel L3053). Sudden bulk discharge = area destruction.
- Vallech's heartstone is "isolated inside the Chromatis head of my spear" (novel L2737) — Chromatis can contain even Y'marr-tier energy. Flavor only for v0.

**GAME-CANON-ADDITION (Eli, 2026-07-12):** chromatis-working is not Trojia-exclusive. Many advanced civilizations — Rema explicitly included — are capable of making chromatis; almost none know the *dark cost* of making it. The dark cost is deliberately left unspecified (a hook for later, not a v0 mechanic). This resolves two combat-spec placeholders in one stroke: it retires the "chromatis_blade AP transferred from a bow feat" extrapolation (COMBAT-SPEC §5.1 — AP is now a material-derived property, `AP = hardness` for any chromatis-forged weapon, reading the existing `hardness: 8` raw) and legitimizes the "Reman"-attributed repeater crossbow (COMBAT-SPEC C10) — unremarkable Reman competence, not a stretch, since Rema is now canonically named among the civilizations who can work the metal.

**Sim derivation:**
- `features.chargeable` with `capacityCu 60000`, `maxSafeDischargePerTick 600`, `saturationPct 95`, `saturationHeatDeciKPerTick 20`, `equilibriumDeciK 6000`, `colorStops` silver-blue → orange → gold — **fixed by ARCHITECTURE §10 (normative example) and §1.1 #14 (16-bit rescale)**. The *ratios* are canon-shaped (cool for most of the range, hot only near saturation, gold at full, huge capacity vs. discharge rate); the absolute numbers are invented rescales.
- `meltK 2600, meltsTo chromatis_melt, meltYieldUnits 7` — canon supports "melts only far above every ordinary fire" (week under intense flame); 2600 K itself and yield 7 are invented.
- High hardness (8), high density (6800), conductivityQ8 200, opacity 31 — invented, canon-consistent (dense premium alloy, opaque metal).
- ShatterSystem: sudden over-rate discharge causing Chebyshev ≤ 2 area destruction (§11) is a direct systemization of the stake gouge scene (novel L3048–3053).

**Interpretive step flagged for Eli:** the novel's *orange* is "actively discharging heat" (Droft's blade) and *gold* is "full". ARCHITECTURE's color stops use orange for the 60–95% band. Defensible (canon says it warms as it approaches full) but it is an interpretation, not a quote. → needs-blessing row in §2.

### 1.2 phorys — reagent mineral

**Canon facts:**
- "a type of mineral he'd dubbed phorys. The mineral, when mixed with a liquid, would convert it into gas form. The gas would gain pressure inside of the iron canister" (novel L1630 — Revlin's flame bow; also flavor for *any* liquid, canon shows corn-oil mixture).
- Devastators: "They use a rock called phorys, which they grind into powder and light on fire to shoot them" (novel L2441). One round "splattered ten men into pieces" (novel L1267); forty of them crater the Dezdant walls (novel L2814, WB §1).
- Buried phorys line detonates when Jake falls onto it — "the ground erupted underneath of him" (novel L1271). Trigger ambiguous (impact? slow fuse?).
- Phorys is consumed by use: the flame bow "needed more phorys before it could be fired because he had used it" (novel L1646).
- Pressure build-up takes time ("would gain pressure... until it would have enough power", novel L1630) — canon support for the sim's **next-tick** `PressurePulseEvent` latency (ARCHITECTURE §5 calls this "documented canon latency").

**Sim derivation:**
- `ContactReactive` feature; reaction raw: trigger = contact with any `liquid`-tagged fluid, effect = `PressurePulseEvent` + wear counter increment. Fluids consumes the liquid units and emits `ReagentContactEvent` (§1.1 #22); reactions owns the pulse and the wear counter.
- `expansion 240, wear 5/unit` (ARCHITECTURE §10 sibling files) — **invented**; canon gives no ratios, only "pressurized gas" and "consumed by use". Wear→spent is canon-supported directionally (L1646).
- Non-flammable *as a solid* in v0 (only liquid-contact modeled). Canon also shows a second mode — ground powder ignited as explosive propellant (L2441) and the buried-line detonation (L1271). **v0 deliberately models only the liquid-contact mode**; heat-triggered powder explosion is a flagged gap.

### 1.3 lightstone (+ lightstone_shards)

**Canon facts:**
- Chala's gift: "This is a lightstone, if a crafter or Drunair is skilled enough he can make it shine brighter than a fire" (novel L2052). WB §6: "small glass sphere with metal core, woven with Alir to glow."
- Charging: energy fed in appears as "a web of lights... then they spread, and grew brighter until it shone so bright that he had to turn his head" (novel L2054) — brightness scales with charge.
- Discharge rule (the shatter canon): "When you let it go do not do it suddenly. You must let the energy drain away slowly, not all at once. If you do then you will break it" (novel L2056).
- Self-drain: "the light faded from the lightstone slowly" once she released it (novel L2056).
- Needs sustained feed for steady output: it "flickered from his lack of concentration" and "flashed a few times" under strain (novel L2892, L2897); Eric keeps "feeding the lightstone" while flying down the pit (novel L2868, L2872); casts a "yellow globe of light" (novel L2872).
- Rare: "her eyes flashed with a guarded pleasure... such an item was not a common find" (novel L2056).

**Sim derivation:**
- `features.chargeable` (capacity 5,000 cu, spike 2,000 — ARCHITECTURE §1.1 #14) + `ShatterOnSpike` + `Emissive` with brightness scaling on charge color stops. Shatter-on-spike is **direct canon** (L2056); both cu numbers are invented rescales.
- `LightstoneShatter` consumes `EnergyDischargedEvent(rate)` same phase — systemization of "released too suddenly = breaks".
- **lightstone_shards** — invented debris material (canon says "break it" but never describes fragments). Inert, non-flammable, low value, opacity low. Whole raw = invented-for-v0.
- Canon's slow self-drain and flicker-under-poor-feed are **not modeled** in v0 (charge persists until discharged). Flagged gap.

### 1.4 glowstone

**Canon facts:** "the only light showing was the glowstones, they cast their eerie red light on the pedestal U'mar stood upon" — deep catacombs beneath the Y'marr shrine, unattended, presumably burning for decades/centuries (novel L917). WB §9: "Eerie red light; lights the U'mar shrine. Different from lightstone."
**Sim derivation:** `Emissive` constant (not chargeable, no feed shown in canon), red tint, low-to-mid `lightLevel` ("eerie" = dim). Exact light level, opacity, density, value — all invented. Non-flammable, no melt in v0.

### 1.5 trudgeon_wood + treatment getilia_soak → trudgeon_wood@getilia_soak

**Canon facts:**
- Delta's palisade "made of the trunks of trudgeon trees. The trees were twice as thick as a man and, if left to harden, would eventually become as hard as stone when covered in the sap of the getilia tree" (novel L1082).
- The town's buildings "were soaked in the sap and were fireproof as well as being hard as stone" (novel L1083).
- WB §1: "Trees: trudgeon (giant, sap-hardened to stone), getilia (sap), pine, oak."
- Flagship contract: getilia-treated trudgeon "never enters the burning map" (ARCHITECTURE §11 Tavern Fire); macro acceptance excludes getilia-treated tiles from burnout (§12 M6).

**Sim derivation:**
- Treatment raw `getilia_soak` targeting `trudgeon_wood`, minting derived id `trudgeon_wood@getilia_soak` at load with `flammability 0` — **direct canon** (fireproof). Derived material should also get stone-tier hardness ("hard as stone") and plausibly higher density/conductivity shifts — magnitudes invented.
- Untreated trudgeon_wood: canon never shows it burning, but the entire point of the sap is fireproofing, so untreated wood is implied FLAMMABLE. IgnitionK/fuelTicks/burnsTo(ash) — invented (suggest ignition slightly above oak — massive dense trunks — and long fuelTicks).
- Getilia sap itself as a fluid is **not** in v0 vocabulary (treatment is applied at load, not simulated). Deferred, see §4.

### 1.6 steel

**Canon facts:** Trojian steel spearheads (WB §6); Revlin works in "steel, and copper" (novel L1397); Chromatis broadheads punch "through steel three inches thick" (novel L1246) — steel is the ordinary-strong benchmark metal that Chromatis exceeds.
**Sim derivation:** conventional metal: non-flammable, high conductivityQ8 (near cap 256 — metals are the fast conductors in the thermal kernel), meltK ~1700–1800 (real-world anchor; canon silent), meltsTo/meltYieldUnits — if melt is enabled, a `steel_melt` fluid/material would be needed which is NOT in the v0 vocabulary → **recommend `meltK: null` for v0** (no forge scenario needs molten steel). Hardness below chromatis (canon: broadheads pierce it). All numbers invented.

### 1.7 brick

**Canon facts:** standard roads are "a gravel base, then load bedrock on top of that before covering the very top with bricks" (novel L2247). Bricks exist as ubiquitous construction material (WB §1 "gravel + bedrock + brick everywhere").
**Sim derivation:** non-flammable, no melt, moderate conductivity (masonry ~ low), opacity 31, cheap. All numbers invented but low-risk.

### 1.8 granite

**Canon facts:** **none as a named material.** The only novel hit is a simile ("a granite mask over his features", novel L2145). Canon has generic stone, mountains (Glandraungs), bedrock road base, stone keeps. "Granite" as the terrain-stone id is a v0 naming choice.
**Sim derivation:** the generic terrain rock: non-flammable, no melt in v0, low conductivity ("stone conducts only" — Tavern Fire flagship, ARCHITECTURE §11), high heatCapacity (thermal mass), opacity 31. Id choice + all numbers invented (canon-consistent).

### 1.9 dirt

**Canon facts:** "the packed dirt that served as the road in Delta" (novel L1084); dug-up dirt lines on the battlefield (novel L1255). Generic ground everywhere.
**Sim derivation:** inert ground material; non-flammable, no melt, low conductivity, moderate capacity. Numbers invented, low-risk.

### 1.10 oak

**Canon facts:** named in the canon tree list (WB §1). No specific novel scene features oak as a burning material; the burning-buildings beats (Shan gate fire novel L277, blackened battlefield L2515) burn generic wood.
**Sim derivation:** the v0 flagship structural wood (Tavern Fire, ARCHITECTURE §11): FLAMMABLE ⇒ ignitionK (suggest ~500–560 K, real-world wood anchor), fuelTicks (≤ 4095; suggest high hundreds–low thousands so a tavern burns for minutes at 100 ms/tick), burnsTo `ash`. All numbers invented; the abstract burnout window [7200, 21600] ticks (§12 M6) is the macro-side constraint the numbers must respect.

### 1.11 thatch

**Canon facts:** **none.** No novel hit for thatch; WB doesn't list it. Peasant-roof material invented for v0 (setting-plausible: medieval villages, log cabins, huts in Frandia novel-adjacent).
**Sim derivation:** the fast-fire roof material: FLAMMABLE, lowest ignitionK of the solids (suggest ~450–500 K), short fuelTicks (fast flashover), burnsTo `ash`, low density/hardness, partial opacity optional. Entire raw = invented-for-v0.

### 1.12 ash

**Canon facts:** a jet of flame "vaporized a man into a pile of ash" (WB §5 Old Mage, prologue); "The entirety of the battlefield had been blackened" (novel L2515). Burnt residue is canon; ash as a *tile material* is a sim construct.
**Sim derivation:** universal `burnsTo` target: inert, non-flammable, no melt, low density/hardness, low value, low opacity or full — designer's call. Numbers invented, low-risk.

### 1.13 chromatis_melt

**Canon facts:** none directly — no scene shows molten Chromatis. Canon-adjacent: forging requires ~a week under intense flame (novel L2786), i.e., a softened/workable state exists at extreme heat.
**Sim derivation:** melt product of chromatis (`meltsTo`), required by loader invariant (melt ⇒ meltsTo + yield). Phase SOLID-as-goo vs FLUID is a sim decision (v0 vocabulary lists it as a material id, fluid list is water-only ⇒ model as a solid "melt" material). **Open ruling needed: what happens to stored charge on melt?** Recommend: charge overlay is dropped and converted to a one-off heat injection (conservation-friendly), needs blessing. All numbers invented.

### 1.14 water (fluid)

**Canon facts:** streams, rivers, rain, fords throughout (e.g., novel L847, L1630 "build a temporary ford"). Trivially canon; no scene constrains freezing/boiling behavior.
**Sim derivation:** fluid raws in Kelvin: freeze 273, boil 373 (real-world anchors; canon silent, but nothing contradicts Earth-normal water). Quench: fire extinguish via `FluidView` at depth ≥ threshold (ARCHITECTURE §1.1 #20 depth ≥ 4 substitution rule). Evaporation constants — invented. The Sewer Flood flagship is wholly invented scenario framing (canon has no sewer scene; Granadad slums where "blood literally runs in the streets" WB §1 is the closest flavor).

---

## 2. Gap table — every number v0 invents

`v0 chose` column: values fixed by ARCHITECTURE are cited; otherwise "raws-owner TBD" with the suggested range this dossier recommends. **Bless? Y** = Eli should explicitly approve; **y** = low-risk invention, batch-approve; **N** = canon-derived, no blessing needed.

| Material | Property | Canon says | v0 chose | Bless? |
|---|---|---|---|---|
| chromatis | capacityCu | "exorbitant"; 100 channelers × 1 yr per key-piece; Eric = seconds (L1398–99) | 60,000 cu (ARCH §1.1 #14, 16-bit rescale) | y (scale only) |
| chromatis | maxSafeDischargePerTick | sudden bulk release = explosion/gouge (L3048–53) | 600 cu/tick (ARCH §1.1 #14) | y |
| chromatis | saturationPct / saturation heat | cool until near-full, warm at full (L1398, L2782–84) | 95% / 20 deciK/tick, equilibrium 6000 deciK (ARCH §10) | y |
| chromatis | colorStops orange band 60–95% | silver-blue→gold endpoints canon (L2785); orange = *discharging heat* (L3004), not a fill band | orange as 60–95% stop (ARCH §10) | **Y** (interpretation) |
| chromatis | meltK 2600 / meltsTo / yield 7 | melts only under week-long intense flame (L2786); no temp given | 2600 K, chromatis_melt, 7 units (ARCH §10) | y |
| chromatis | density 6800, hardness 8, condQ8 200, capQ8 96, opacity 31, valueCp 40000 | premium alloy, pierces 3-in steel, rare (L1246, L1398) | ARCH §10 values | y |
| chromatis_melt | entire raw (phase, temps, behavior) | no canon scene | raws-owner TBD; solid "melt" material, inert, re-solidify out of v0 scope | **Y** |
| chromatis_melt | stored charge on melt | no canon | recommend: drop overlay → one-off heat injection | **Y** |
| phorys | expansion 240 (units gas-pressure per liquid unit) | "converts liquid to pressurized gas" — no ratio (L1630) | 240 (ARCH §10 sibling) | **Y** |
| phorys | wear 5/unit → spent | phorys consumed by use (L1646) | 5/unit (ARCH §10 sibling); spent product id TBD | **Y** |
| phorys | heat-triggered powder explosion mode | Devastator powder "lit on fire to shoot" (L2441); buried line detonates (L1271) | NOT modeled in v0 (liquid-contact only) | **Y** (scope cut) |
| phorys | thermal/mechanical raws (density, conductivity, hardness) | "a rock" (L2441) | raws-owner TBD, generic mineral range | y |
| lightstone | capacityCu 5000 / spike 2000 | brighter than a fire when fed; breaks on sudden release (L2052–56) | ARCH §1.1 #14 values | y |
| lightstone | brightness↔charge curve, tint | "web of lights... grew brighter"; yellow globe (L2054, L2872) | color stops monotone, yellow-white tint, raws-owner TBD | y |
| lightstone | self-drain / flicker-without-feed | fades slowly on release; flickers on lapse (L2056, L2892) | NOT modeled (charge persists) | **Y** (scope cut) |
| lightstone_shards | entire raw | canon: "you will break it", no fragment description (L2056) | invented debris material, inert | y |
| glowstone | lightLevel / tint | "eerie red light" in dark catacombs (L917) | red tint; suggest lightLevel 4–8 of 31 (dim-but-sole-source) | y |
| glowstone | powered-by / duration | unattended for ages, no feed shown (L917) | constant Emissive, no charge | N (cleanest read) |
| glowstone | thermal/mech raws | none | raws-owner TBD, stone-like | y |
| trudgeon_wood | ignitionK / fuelTicks / burnsTo | flammability implied only by fireproofing being valuable (L1082–83) | FLAMMABLE, suggest ignition ≥ oak, long fuelTicks, burnsTo ash | y |
| trudgeon_wood | density / hardness | trunks "twice as thick as a man" (L1082) | raws-owner TBD, dense hardwood range | y |
| trudgeon_wood@getilia_soak | flammability 0 | "fireproof" verbatim (L1083) | flammability 0, minted at load (ARCH §10) | **N** |
| trudgeon_wood@getilia_soak | hardness/density shift | "hard as stone" (L1082–83) | suggest hardness = granite-tier | N (direction) / y (magnitude) |
| steel | meltK | no canon | recommend null in v0 (no molten-steel scenario; avoids un-vocabularied steel_melt) | y |
| steel | conductivity/capacity/density | ordinary benchmark metal (L1246) | raws-owner TBD; condQ8 near-cap ≤ 256 | y |
| brick / dirt / granite / oak / ash | all numeric properties | generic setting materials; granite never named (only simile L2145), oak only in WB §1 tree list | raws-owner TBD, real-world-anchored ranges | y |
| granite | the id itself | canon says "stone/bedrock/mountains", never "granite" | keep id `granite` (v0 vocabulary is frozen) | y (naming) |
| thatch | entire raw | **no canon at all** | invented: lowest ignitionK, short fuelTicks, burnsTo ash | **Y** |
| oak | fuelTicks vs macro burnout window | none | must satisfy abstract tavern burnout ∈ [7200, 21600] ticks (ARCH §12 M6) | y (constraint, not canon) |
| water | freezeK 273 / boilK 373 / evap constants | Earth-normal implied, never stated | real-world anchors; evap rate invented | y |
| water | quench depth ≥ 4 substitution | fire+water never co-staged in a canon scene | ARCH §1.1 #20 ruling | y |
| all FLAMMABLE | fuelTicks ≤ 4095, ignition+burnsTo present | — | loader invariant (ARCH §10), not canon | N (engineering) |

**Blessing shortlist for Eli (the Y rows):** chromatis orange-band interpretation · chromatis_melt behavior + charge-on-melt · phorys expansion 240 + wear 5 + spent-product · phorys powder-explosion scope cut · lightstone self-drain scope cut · thatch existing at all.

---

## 3. Canon materials/substances deliberately NOT in v0

Recorded so future vocab extensions cite this list instead of re-mining the novel.

| Substance | Canon cite | Why deferred |
|---|---|---|
| onyx | J'harra's armor & curved sword; Tower of Transmodim fixtures (WB §5, §9) | cosmetic/armor material, no sim behavior needed |
| copper | Revlin's workbench (novel L1397); "coppery color" of forge-heated chromatis (L2786) | no scenario uses it |
| getilia sap (as fluid) | soaking process (L1082–83) | treatment applied at raws-load, not simulated; a "soak" process sim is post-v0 |
| nighthawk venom | "one-tenth of a drop could kill a horse" (novel L3025) | combat/macro commodity, not a tile material |
| corn oil / coal dust | flame-bow mixture components (L1630) | subsumed under phorys reaction abstraction |
| blood (lake of) | Wyr'fen lair underground lake (L2875–80) | set-piece fluid; water is the only v0 fluid |
| gravel / bedrock / seashells | road construction (L2247) | rendering/import palette concern, map to dirt/granite/brick |
| pine, wheat/grass, leather, ice-as-terrain | WB §1, various | ice exists only as fluids-freeze output in v0 (FluidFrozenEvent → PhaseTransition ice placement, ARCH §5); a dedicated ice material raw is the fluids/raws crew's call |
| incendiary arrows | Sniper incendiary into tent (L1261–63) | ignition delivery, covered by Ignite command |

---

## 4. Future pillars — the crafting energy rules, mapped to reserved machinery

Canon's energy system (the Source / crafting / links) is the game's long-term physics pillar. v0 ships only its Chromatis-shaped shadow (ChargeSystem). This section is the mapping contract so nothing v0 does forecloses the real system.

### 4.1 The canon rules (Gerik's lecture + practice scenes)

1. **Conservation.** Energy is transferred, never destroyed: spent body energy "transfers to him" on contact (novel L447); crafting moves energy between self, opponents, environment (L452).
2. **Dissipation with size and distance.** "The more you transfer the more is lost to nature, also the further it travels the more is lost... it would be less taxing to do lots of tiny transfers than one big one" (novel L454–457). Loss scales with transfer magnitude AND link length; many small links beat one big link.
3. **Links need a bridge, with material affinity.** A link requires touch, a held weapon, or a sensed source; "With living things it's easy, they kind of draw you into them. It's much harder with things that aren't alive like a rock or a sword" (novel L503). Affinity ordering: living > dead organic > inert rock/metal.
4. **The Source is an external reservoir.** A pocket "directly in his chest... couldn't identify it as heat energy" (novel L505), effectively unbounded at Eric/Vallech tier (L516: energy "as strong as a thousand men"), refillable from environment (link to campfire, L505), with overdraw shutdown (L2517).
5. **Discharge modes.** Into Chromatis = storage (Reman key L513–516, stakes L2784); into flesh = burst/heart-pop; into ground = fire funnels (L2514–15); uncontrolled leakage burns nearby men "without Eric even opening a link" (L2512) — saturation radiates as heat.
6. **Aether/uether.** Two extraplanar energies moving between dimensions; lightstuff pulled from the aether, uether flows detectable as "a hole in the aether" (novel L2458; WB §2). Entirely post-v0.

### 4.2 Mapping to reserved sim machinery

| Canon rule | v0 embodiment | Future home |
|---|---|---|
| Source injections (rule 4) | `InjectCharge` SimCommand → `ExternalChargeApplied` → ChargeCommandBuffer (ARCH §4 phase 0, §5). External energy is ledgered as explicit inflow — conservation audits stay exact because Source inflow is *declared*, mirroring FluidLedger discipline | a `SourceEmitters` analog of FluidEmitters when NPC crafters exist |
| Chromatis filling (rule 5) | ChargeSystem + CHARGE overlay, 16-bit cu, stops/saturation (ARCH §1.1 #14, §11) | unchanged — this IS the canon mechanic |
| Dissipation with distance/size (rule 2) | *not modeled* — v0 charge injection is point-local | **FIELDS_RESERVED (phase 5)**: a LinkSystem owning a link graph (endpoints as PackedPos pairs). Per-link transfer loss = f(distance, magnitude), superlinear in magnitude, and the lost fraction is **injected as heat along the path via HeatCommandBuffer** — "lost to nature" literally becomes ambient warmth, keeping `Σcap·T + sink` audits exact. The many-small-links rule falls out if loss is superlinear per-link. |
| Material link affinity (rule 3) | not modeled | a future raws field (e.g. `linkAffinityQ8`) keyed off existing tags (`living` > organic > `metal`/mineral). Reserve nothing in v0 beyond loader tolerance for unknown-but-ignored fields (provenance/notes ruling already establishes the pattern — but any NEW ignored field needs its own ruling) |
| Saturation leakage (rule 5) | `saturationHeatDeciKPerTick` — canon's "energy leaking out... burning the closest men" (L2512) at material scale | crafter-actor leakage when agents exist |
| Reman key (white-hot tip on demand, L277 stud-press; L514 tip grows white-hot when fed) | approximated by chromatis charge + `Ignite` command; scenario scripts can stage "key" beats | a Device layer: controlled discharge-to-heat converter (charge → HeatCommandBuffer at a chosen rate ≤ maxSafeDischarge) |
| Stake detonation (sudden bulk discharge, L3048–53) | EnergyDischarged(rate) → ShatterSystem Chebyshev ≤ 2 (ARCH §3, §11) | scale blast with releasedCu when weapons become items |
| Blademaster reservoir blades (L2213, L3002) | out of tile-sim scope | macro commodity (`chromatis` valueCp already prices it); item-level charge when inventory exists |
| Aether/uether (rule 6) | `FIELDS_RESERVED` phase 5 empty slot; `AETH` TROJSAV section reserved-empty; "aether = a later lane registration" (ARCH §3 world, §9) | aether lane + Lief-tier field effects; uether/Slip corruption effects. Goldens don't renumber — that's the whole point of the reserved slot |
| Veil/time-flow (WB §2) | nothing | out of sim scope indefinitely; tick-rate is engine-fixed |

**Guardrail:** any future LinkSystem must route ALL its mutations through the existing intake buffers (HeatCommandBuffer / ChargeCommandBuffer) — the §6 sole-writer table stays intact without amendment.

---

## 5. Open questions for Eli (consolidated)

1. Orange color-stop = 60–95% fill band, vs. novel's orange = "discharging heat". Keep ARCHITECTURE's read?
2. chromatis_melt: solid goo or future fluid? Stored charge on melt: drop-to-heat (recommended) or preserved?
3. Phorys numbers: expansion 240 gas-units per liquid unit, wear 5/unit, and what the spent phorys becomes (inert rock id? reuse `granite`? new id post-v0?).
4. Phorys scope cut: powder-ignition explosion mode (Devastator/mine behavior) deferred — OK?
5. Lightstone scope cut: no self-drain/flicker in v0 — OK?
6. `thatch` has zero canon. Bless as setting-plausible, or swap the roof material to something canon (e.g., trudgeon shingle)?
7. `granite` as the id for canon's generic "stone" — bless the naming.
8. Untreated trudgeon burns in v0 (implied by canon, never shown) — confirm.
