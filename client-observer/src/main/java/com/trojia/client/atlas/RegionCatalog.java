package com.trojia.client.atlas;

/**
 * The GL-free half of {@link TileAtlas}: everything a <em>draw plan</em> needs to know about
 * an art pack without touching a texture. Which region names exist, how many interchangeable
 * cosmetic cells back each one, and how a tile picks among them (TILE-ART-SPEC section 12).
 *
 * <p>Split out so the tile-art resolution chain
 * ({@link com.trojia.client.render.TilePlan}) can be shared by every view of the world —
 * the top-down {@code WorldRenderer} and the first-person
 * {@code com.trojia.client.fpv.FirstPersonPlanner} — and unit-tested with a plain map, with
 * no GL context and no {@code TextureRegion} anywhere. {@link TileAtlas} extends this and
 * adds the texture lookups; a headless caller depends on this narrower type instead.
 *
 * <p>Two views resolving a tile through two <em>copies</em> of that chain would eventually
 * disagree about which variant cell or which fallback region a tile draws — the exact
 * "the two views must agree" failure the first-person work is gated on. One interface, one
 * chain, one answer.
 */
public interface RegionCatalog {

    /** Whether {@code regionName} has a cell in this pack. */
    boolean contains(String regionName);

    /**
     * How many interchangeable cells back {@code regionName}: {@code >= 1} for a known name
     * (one for a single-cell pack), {@code 0} for an unknown name.
     */
    int variantCount(String regionName);

    /**
     * How a tile picks which variant cell of {@code regionName} to draw (TILE-ART-SPEC
     * section 12). Defaults to {@link VariantPattern#HASH} — the position scatter every
     * existing pack uses — so a single-cell pack (whose regions report
     * {@code variantCount == 1} and always draw variant 0 regardless) needs no override.
     */
    default VariantPattern variantPattern(String regionName) {
        return VariantPattern.HASH;
    }
}
