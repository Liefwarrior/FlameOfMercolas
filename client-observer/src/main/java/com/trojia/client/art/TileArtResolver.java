package com.trojia.client.art;

/**
 * GL-free art resolution seam: {@code (materialId, form, appearanceBucket) -> region name}.
 *
 * <p>Per {@code docs/art/TILE-ART-SPEC.md} section 1, art resolution is split across a
 * GL-free / GL boundary. Implementations of this interface are pure index math over data
 * loaded once at boot and must be usable headless (no libGDX graphics context). In v0 the
 * resolver returns atlas region <em>names</em>; the GL-side {@code AtlasRegionTable}
 * (not yet built) interns names to int indices and {@code TextureRegion}s at boot.
 *
 * <p>Keying model (TILE-ART-SPEC section 2):
 * <ul>
 *   <li>{@code materialId} — canonical raws id string, verbatim, including
 *       treatment-derived ids such as {@code trudgeon_wood@getilia_soak}.</li>
 *   <li>{@code form} — lowercase {@code TileForm} token; {@code "block"} is the default
 *       solid form. Implementations normalize case so {@code TileForm.name()} may be
 *       passed directly once sim-core ships the enum.</li>
 *   <li>{@code appearanceBucket} — color-stop ordinal 0..3 served by
 *       {@code AppearanceQuery}; materials without a {@code chargeable} feature are
 *       always bucket 0.</li>
 * </ul>
 *
 * <p>Contract: {@link #regionName} never returns {@code null}; every unresolvable input
 * falls back (at load time, per TILE-ART-SPEC section 7.3) to {@link #missingRegionName()}.
 * Implementations are immutable after construction and all methods are deterministic
 * pure functions of the constructor input.
 */
public interface TileArtResolver {

    /**
     * Sentinel returned by {@link #heatGlowTintRgb(String)} when a material has no
     * heat-glow overlay tint.
     */
    int NO_TINT = -1;

    /**
     * Resolves the atlas region name for a drawn tile.
     *
     * @param materialId       canonical raws material id (verbatim, case-sensitive)
     * @param form             {@code TileForm} token, case-insensitive; {@code "block"}
     *                         is the default solid form
     * @param appearanceBucket color-stop ordinal; {@code 0..3} accepted, values above
     *                         the material's defined regions clamp high (never wrap —
     *                         an over-charged material saturates visually); values
     *                         above 3 also clamp high
     * @return the region name; {@link #missingRegionName()} for unknown materials or
     *         forms with no {@code block} fallback — never {@code null}
     * @throws IllegalArgumentException if {@code appearanceBucket < 0} or
     *                                  {@code materialId}/{@code form} is null or blank
     */
    String regionName(String materialId, String form, int appearanceBucket);

    /**
     * The universal fallback region name (magenta/black checker in the placeholder
     * pack). Reserved name {@code "missing"} in v0.
     */
    String missingRegionName();

    /**
     * Cosmetic per-material light clamp {@code L' = max(L, minLight)} from the mapping
     * file (TILE-ART-SPEC section 5.1). Never feeds back into sim light values.
     *
     * @return 0..31; 0 (no clamp) for materials without the field or unknown materials
     */
    int minLight(String materialId);

    /**
     * The material's heat-glow overlay tint as packed {@code 0xRRGGBB}, or
     * {@link #NO_TINT}.
     *
     * <p>Canon (BLESSING-QUEUE.md ruling 5, Eli 2026-07-12): the chromatis fill ramp is
     * silver &rarr; pale-gold &rarr; gold via appearance buckets; orange {@code #E8842A}
     * is the {@code heatGlowTint} rendered as an overlay only while the tile is actively
     * discharging or saturation-heating. This resolver only exposes the tint; the
     * when-to-render decision belongs to the GL renderer (lands with F5 light).
     */
    int heatGlowTintRgb(String materialId);
}
