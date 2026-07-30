# Blessing Queue — canon & balance decisions

Status legend: **RULED** = engineering decision made under ARCHITECTURE.md authority (veto anytime) ·
**ASKED** = put to Eli interactively · **DEFAULT** = v0 value adopted, batch-veto whenever.

## Engineering rulings (RULED)

1. **Raws/asset location**: canonical home is `content/src/main/resources/trojia/**` (raws, art
   mapping) so content ships on the classpath in the content jar; the current `content/raws/` and
   `content/art/` trees relocate there during the F2 main wave; `content/maps/` stays a source tree
   (only baked TROJSAVs become resources). Loaders read the classpath; tools may read the repo path.
2. **Invented raws shapes blessed as spec** (loader implements exactly): `features.emissive{lightLevel,tint}`,
   `shatterOnSpike{spikeCuPerTick,shattersTo,radiusChebyshev}`, `contactReactive{reagentTag}`;
   treatment semantics overrides=absolute, scaleQ8=floor(base*v/256), addTags=append; reaction raw
   fields trigger/expansion/wear*/pulse as shipped.
3. **Cross-registry refs**: meltsTo/freezesTo may cross material↔fluid registries; the loader
   validates against the united substance-id namespace (ice↔water blessed). `liquid`-tag⇒boilsTo is
   NOT binding for fluids in v0 (water.boilsTo null OK — vapor is the reserved steam seam).
   `chromatis_melt` ships phase LIQUID without the `liquid` tag (phorys does not react with molten
   metal in v0 — documented limitation); stored charge on melt converts to heat.
4. **steel.meltK = null in v0** (no steel_melt id; forge-grade temps out of scope until needed).

## Canon interpretation (ANSWERED by Eli, 2026-07-12)

5. Chromatis: **HYBRID** — colorStops are a pure fill ramp silver→pale-gold(#E3CE7A)→gold; orange
   #E8842A is the heat-glow overlay rendered only while actively discharging or saturation-heating
   (`heatGlowTint` in art-mapping; renderer work lands with F5 light). Raws + art patched.
6. Roofs: **KEEP** thatch + walkable FLOOR form (rooftop traversal is desired for the Wielder).
7. `granite`: **BLESSED** as the id for canon's unnamed generic stone.
8. **BLESSED both**: untreated trudgeon burns (slow, fuelTicks 4050); getilia_soak now also
   overrides hardness→6 (granite-tier, canon "hard as stone"). Treatment raw patched.
9. Phorys: **NO WEAR-OUT in v0** (Eli overrode the wear-1200 recommendation) — inexhaustible;
   wear* fields removed from the shipped raw and become optional-reserved in the reaction schema
   (absent = no wear). Powder-explosion mode and lightstone self-drain remain deferred.

## Balance numbers (DEFAULT — batch-veto anytime)

10. Water/ice: freeze only at depth 7, evapMinK 333, evapChanceQ16 2048, ice hardness/opacity/value
    as shipped.
11. Flammability 0-3 severity scale (loader may collapse to zero/nonzero).
12. Opacity picks: lightstone 12, shards 6, ash 4, ice 4.
13. Fixture geometry: torch luminance 26/31, water depths, ignition target = the oak table,
    street = brick; RAMP unused in fixtures (exemplar comes with the first district map).
14. Art placeholders: legibility color overrides over hash colors; z-peek dims [256,168,112,76];
    glowstone tint raws #C43A2F vs art #B22D2D (art wins on screen until unified); water/ice
    display colors provisional.

## Simple Magic — public-shelf craftings (items 16 and 18 ANSWERED 2026-07-30; the rest OPEN)

Full dossier with line cites: `docs/lore/MAGIC-CANON.md`. Nothing below blocks the build; all of
it is shipped behind a flag in the raws' own `provenance` fields, the chromatis §1.1 convention.

15. **A commoner can open a BRIDGED link.** L459 says a crafting "requires a link between the two
    entities, normally this would be an arm or a blade" and that making one without a bridge
    takes the gift, which "almost nobody" has. This pass reads that as: the GIFT is for the
    UNBRIDGED link; an arm or a blade is the ordinary case. The entire premise rests on this
    reading. **Highest-value ruling in this queue.**
16. ~~**The imperial edict opening the Granadad libraries.**~~ **ANSWERED by Eli, 2026-07-30 —
    WITHDRAWN, not blessed.** Eli declined the invention and took canon's own mechanism instead:
    books come in two grades, and Gerik's public-issue gathering "was issued to the public because
    it was overly general and skimmed over most of the battles and details" (L2472) before he
    climbs a chair for the real edition off the top shelf. **That** is why common magic is weak —
    the public shelf teaches the shallow edition — and it needs no edict at all. The edict is gone
    from `package-info`, `SpellVerb`, `SkillChecks`, the skills raws provenance, PROGRESSION-SPEC
    and `MAGIC-CANON.md`. Canon's real libraries are left where canon put them and are NOT the
    source of the ward's craftings: the **Library of the Runemasters** in *Mercia* inside the Du
    Vron Dezdant (L2478), and the monastery library of the **Divine Light Cathedral** in Granadad
    (L2412), which belongs to Gabri's order. Nothing left to bless.
17. **"Linkcraft" as the skill's name.** Invented. Canon's word is *crafting* (collides with
    smithing in a skill list — the novel itself jokes about the collision at L442); LoT2's
    academic name for the field is *Thectrochanics*. Rename freely; nothing else depends on it.
18. **"The seven systems." ANSWERED by Eli, 2026-07-30, in three parts — and the third one is a
    SECRET.** (a) The canon seven is the novel's **six Steward systems** — Trojja/the Source,
    Mercan/flesh, Vervan/lightstuff, Fran/wards, Rema/creation, Firra/will-shaping (L726, L2468,
    L1479; WB L742 "Six vs seven Luxerne → six") — **plus the Flame of Mercolas**, which WB
    L273/L407 name explicitly as "NOT a Luxerne power… Distinct system" and which is Gabri's own
    religious office. That framing invents nothing. (b) **They are a HIERARCHY, not peers: the
    Flame is the strongest and sits at the top.** (c) **The seventh is a SECRET, and in-world the
    answer is six.** The public, scholars, Mercolan experts and the Runemasters all believe six —
    which is *why* the novel and the WorldBible say six, so the count was never an error. The
    Flame has never died, is continuous rather than hereditary, and lends its power to **Wielders**
    (canon's own word for Gabri, WB L273). **The `Lore\SECRET-MATERIALS-PROPER-NOUNS.html` §7
    "Seven Powers" table stays DISCARDED** on the two grounds already ruled here, and note it is
    not this seven anyway — it demotes the Flame to one peer school among seven, which is exactly
    backwards. Written up with cites in `MAGIC-CANON.md` §5, **flagged there as out-of-world
    knowledge**, with a hard content rule (§5.4: every in-world surface says six, no NPC hints,
    no knowing barks) and a prohibition (§5.5: the Flame gets no spell row, no shelf entry and no
    spell-bar button — the absence is the design). The shipped craftings stay on the **Source**.
    **Canon correction on the record:** the commissioning brief glossed *Y'marr* as the bearer of
    a power; canon uses it for the Beast's corruption-mirror, the *opposite* of a bearer (L1487,
    L1170, WB L120–123). Eli's "its Y'marr has never been seen before" survives the correction
    and is strengthened by it — see `MAGIC-CANON.md` §5.3.
18b. **Book 2 (Eli): the Flame is absorbed entirely by Gabri or Devon.** Recorded in
    `MAGIC-CANON.md` §5.8 as **UNPUBLISHED AUTHOR INTENT, NOT YET CANON**. Nothing is built
    toward it and nothing of it appears in content; it is on the record only so a future sprint
    does not design against it.
19. **Warmth pays REST.** Being warm is restful; canon is silent. **Resized 2026-07-30** from 1
    REST per deci-Kelvin per 50 ticks (which was half a serf's whole natural REST decay — too
    strong for a documented nudge) to 1 per 5 deci-Kelvin, i.e. 3 points per cadence against a
    decay of 30. Today it only ever touches the played actor. Cut freely.
20. **Balance numbers** (DEFAULT — batch-veto anytime): warmth ±15/-20 deci-Kelvin for 600 ticks,
    harm 1–3 hp, attribute nudges ±1, base resists 0–14, cooldowns 200–700 ticks, the linkcraft
    check family at base 500 / floor 50 / ceiling 900, and `SpellCost` at 4 per tile / 1 per
    transfer point / 6 per area tile / 20 for an unbridged link / one held period per 300 ticks.
    Nothing clamps magnitude on the temperature axis — the cost model is what keeps an unreviewed
    spell weak — but TWO rules are structural, one per axis that could otherwise reach past the
    system: no crafting may take a body below 1 hp (calibrated on L567: twenty full crafters
    pooled make one lit torch flare), and a live attribute modifier is bounded at ±2 in the only
    code that can report one, because that axis is folded into the single function every check in
    the game reads.
21. **Duration is now priced on held craftings** (`SpellCost.HELD_PERIOD_TICKS = 300`). Canon
    prices magnitude and distance explicitly (L452) and dose count implicitly (L457); nothing in
    the novel prices a hold, so the constant is invented. It exists because without it a held
    effect cost the same at one tick as at ten thousand, which made the documented claim that an
    unreviewed spell cannot be accidentally strong false for the mode every temperature and
    attribute crafting uses. Shifts the shipped list's resists by 1–3 points. Veto the number
    freely; the principle is not really optional.
22. **`close_the_cut`** (TOUCH mend, Lv 3) — a v0 content addition, `knit_the_skin` turned
    outward. Added because **nothing in this sim regenerates hit points**, so before it the only
    mending row was SELF-target and harm done to a neighbour could never be undone. Canon-anchored
    on L445 (Gerik knitting skin with borrowed heat) across L459's bridged link.
