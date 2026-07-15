package com.trojia.client.atlas;

/**
 * How {@code WorldRenderer} picks which cosmetic-variant cell of a region to draw for a given
 * world tile (TILE-ART-SPEC section 12). The mode is declared per region in
 * {@code art-mapping.json}'s optional {@code variantPatterns} map; a region with no entry
 * defaults to {@link #HASH}, the original position-hash scatter.
 *
 * <p><b>The senior-level-design homogeneity rule (DECISIONS.md art register, Eli 2026-07-15).</b>
 * The hash mode avalanches between neighbours, so any region backing &ge;2 cells scatters a
 * different sprite in every cell — a restless, AI-generated-looking patchwork. That look is
 * correct only for deliberately-rough materials (dirt/rubble/mud, moving harbor water). Smooth
 * designed surfaces (house/shop/compound interior floors, building walls, roofs, civic facades)
 * are instead trimmed to a single cell in the mapping, so they render as one clean repeated tile
 * ({@code variantCount <= 1 ⇒ variant 0}, no pattern needed). The one surface that wants an
 * <em>obvious regular</em> look rather than either extreme — the paved sidewalk / civic
 * flagstone — uses {@link #PERIODIC}: a fixed 2-tone paver weave that reads as deliberately laid
 * pavement, which a random hash cannot produce.
 *
 * <p>Every mode is a pure function of world position — presentation-only, never read by
 * {@code WorldHasher} or sim-core, byte-identical every run (the same determinism contract the
 * hash already carries).
 */
public enum VariantPattern {

    /**
     * MurmurHash3-style scatter of world position + material/form salt
     * ({@code WorldRenderer.cosmeticVariant}). Neighbours usually differ — the intended look
     * for rough, organically-messy surfaces only.
     */
    HASH,

    /**
     * A fixed periodic function of world position — {@code (x ^ y) & 1} for a 2-cell region, the
     * classic alternating-paver checker — so the surface reads as a regular laid pattern (an
     * obvious sidewalk / civic flagstone), identical every run.
     */
    PERIODIC
}
