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

## Canon interpretation (ASKED — see answers recorded below when given)

5. Chromatis orange stop: fill-band (as shipped) vs canon's discharge-heat orange.
6. Thatch roofs + roofs as walkable FLOOR form.
7. `granite` as the id for canon's unnamed generic stone.
8. Untreated trudgeon flammable; whether getilia_soak also bumps hardness to stone-tier (canon:
   "hard as stone").
9. Phorys scope: contact-reaction only in v0; wearCapacity 1200 → ash; powder-explosion mode
   (Devastator) and lightstone self-drain/flicker deferred.

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
