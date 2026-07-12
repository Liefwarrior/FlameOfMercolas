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
