package com.trojia.client.render;

import com.trojia.client.art.TileArtResolver;
import com.trojia.client.atlas.RegionCatalog;
import com.trojia.client.atlas.VariantPattern;
import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.TileForm;

import java.util.Locale;

/**
 * <b>The one tile-art resolution chain, shared by every view of the world.</b> Material lane
 * &rarr; raws key &rarr; region name &rarr; cosmetic variant &rarr; secondary tint, and the
 * matching FLUID-lane chain for the water overlay. Pure functions of their arguments — no
 * world, no cursor, no GL — so they unit-test headlessly and answer identically no matter who
 * asks.
 *
 * <p><b>Why it is here and not inlined in a renderer.</b> {@link WorldRenderer} owned this
 * chain privately while there was one view. There are now two — the top-down tile view and
 * the first-person view ({@code com.trojia.client.fpv}) — and the first-person view is gated
 * on agreeing with the tile view about what is where and what it looks like. A second copy of
 * "which of this region's four cobble cells does tile (91, 60, 19) draw" would drift on the
 * first change to either copy, and the drift would be invisible until someone pressed the
 * switch key and the street changed pattern under them. So: one chain, both callers, and the
 * variant hash below is the single definition of a tile's cosmetic identity.
 *
 * <p>Everything here is presentation-only and is never read by {@code WorldHasher} — but it
 * <em>is</em> deterministic (a pure position hash, no RNG, no stored state), so the same map
 * looks the same every run and on every machine.
 */
public final class TilePlan {

    /** Appearance bucket used for every tile in v0 (F5 will read real charge state). */
    public static final int APPEARANCE_BUCKET = 0;

    /**
     * The {@code formOrdinal} argument {@link #cosmeticVariant} receives for fluid-overlay
     * variant picks: one past {@code TileForm.STAIR.ordinal()} (the last real form), so no
     * real form can ever collide with it and water variants never correlate with the
     * variant of the floor tile beneath (GRANADAD art spec section 5, pinned).
     */
    public static final int FLUID_FORM_SALT = 6;

    /** FLUID-lane unpacking (Tile.java: depth bits 0-2, fluidId bits 3-5, SETTLED bit 6). */
    public static final int FLUID_DEPTH_MASK = 0x7;
    private static final int FLUID_ID_SHIFT = 3;
    private static final int FLUID_ID_MASK = 0x7;

    private TilePlan() {
    }

    /**
     * One cell's resolved base-tile draw: which region cell to draw and the material's optional
     * secondary tint ({@link TileArtResolver#NO_TINT} when the pre-colored cell draws as
     * authored). The blur level and any depth shade are applied at the draw site, not here.
     */
    public record BaseTilePlan(String regionName, int variant, int materialTintRgb) {
    }

    /**
     * One cell's resolved fluid-overlay draw: which region cell, at what tint and Q8
     * alpha. {@code tintRgb} is {@link TileArtResolver#NO_TINT} when the pack's fluid
     * region is pre-colored and draws as authored (the shipped packs' water).
     */
    public record FluidOverlay(String regionName, int variant, int tintRgb, int alphaQ8) {
    }

    /**
     * Resolves a cell's base tile — material lane &rarr; raws key &rarr; region name &rarr;
     * cosmetic variant + secondary tint. The caller guarantees a base-tile-bearing cell (a
     * solid, non-{@link TileForm#VOID}, non-{@link TileForm#OPEN} form); an unmapped region
     * falls back to {@link TileArtResolver#missingRegionName()} rather than throwing, matching
     * the resolver's own no-crash contract.
     *
     * <p>Variant pick is the deterministic position hash of TILE-ART-SPEC section 12: a
     * single-cell region always draws variant 0, a {@link VariantPattern#PERIODIC} region a
     * fixed laid-paver weave, otherwise the material/form-salted position hash.
     */
    public static BaseTilePlan base(TileForm form, int materialLane, int tx, int ty, int z,
            MaterialRegistry materials, TileArtResolver artResolver, RegionCatalog catalog) {
        String materialId = materials.get(materialLane).key();
        String formToken = form.name().toLowerCase(Locale.ROOT);
        String regionName = artResolver.regionName(materialId, formToken, APPEARANCE_BUCKET);
        if (!catalog.contains(regionName)) {
            regionName = artResolver.missingRegionName();
        }
        int variantCount = catalog.variantCount(regionName);
        int variant = pickVariant(catalog, regionName, variantCount, tx, ty, z, materialLane,
                form.ordinal());
        return new BaseTilePlan(regionName, variant, artResolver.materialTintRgb(materialId));
    }

    /**
     * The GL-free plan for one cell's fluid overlay — what to draw and how — or
     * {@code null} when the cell draws no overlay. Pure function of its arguments
     * (deterministic across runs and machines).
     *
     * <p>Draw-nothing cases: depth 0; a FLUID-lane fluid id outside the registry (lane
     * garbage — nothing sane to resolve); an alpha of 0 at the cell's depth, which is how
     * a pack that maps no entry for this fluid opts out (the resolver reports 0 at every
     * depth for unmapped fluids). The SETTLED bit and any future high FLUID-lane bits are
     * ignored — presentation only cares about what is pooled, not whether it is still
     * flowing.
     *
     * @param fluidBits raw FLUID-lane bits (depth 0-2, fluidId 3-5, SETTLED 6)
     * @return the plan, or {@code null} to draw nothing
     */
    public static FluidOverlay fluid(int fluidBits, int tx, int ty, int z,
            FluidRegistry fluids, TileArtResolver artResolver, RegionCatalog catalog) {
        int depth = fluidBits & FLUID_DEPTH_MASK;
        if (depth == 0) {
            return null;
        }
        int fluidId = (fluidBits >>> FLUID_ID_SHIFT) & FLUID_ID_MASK;
        if (fluidId >= fluids.size()) {
            return null;
        }
        String fluidKey = fluids.get(fluidId).key();
        int alphaQ8 = artResolver.fluidDepthAlphaQ8(fluidKey, depth);
        if (alphaQ8 <= 0) {
            return null;
        }
        String regionName = artResolver.fluidRegion(fluidKey);
        if (!catalog.contains(regionName)) {
            regionName = artResolver.missingRegionName();
        }
        // Same deterministic position-hash variety as base tiles, but salted with the
        // raw fluid id and the out-of-band FLUID_FORM_SALT instead of material/form, so
        // the water surface's repeat pattern is independent of the floor's.
        int variantCount = catalog.variantCount(regionName);
        int variant = variantCount <= 1 ? 0
                : Math.floorMod(cosmeticVariant(tx, ty, z, fluidId, FLUID_FORM_SALT),
                        variantCount);
        return new FluidOverlay(regionName, variant, artResolver.fluidTintRgb(fluidKey), alphaQ8);
    }

    /** The pooled depth in a raw FLUID lane, {@code 0..7}. */
    public static int fluidDepth(int fluidBits) {
        return fluidBits & FLUID_DEPTH_MASK;
    }

    /**
     * Chooses the cosmetic-variant cell index for one base tile (TILE-ART-SPEC section 12),
     * dispatching on the region's {@link VariantPattern}:
     *
     * <ul>
     *   <li>{@code variantCount <= 1} &rarr; {@code 0}: a homogeneous single-cell region (the
     *       smooth-surface default) always draws its one clean tile.</li>
     *   <li>{@link VariantPattern#PERIODIC} &rarr; {@code (x ^ y) & 1} folded into the count:
     *       a fixed 2-tone laid-paver weave (the sidewalk / civic flagstone), a regular
     *       pattern a random hash cannot produce.</li>
     *   <li>otherwise &rarr; the material/form-salted position hash: scattered variety, the
     *       intended look for deliberately-rough surfaces (dirt, rubble) and moving water.</li>
     * </ul>
     *
     * Every branch is a pure function of world position — presentation-only, never read by the
     * {@code WorldHasher}, byte-identical every run.
     */
    public static int pickVariant(RegionCatalog catalog, String regionName, int variantCount,
            int tx, int ty, int z, int materialLane, int formOrdinal) {
        if (variantCount <= 1) {
            return 0;
        }
        if (catalog.variantPattern(regionName) == VariantPattern.PERIODIC) {
            return Math.floorMod((tx ^ ty) & 1, variantCount);
        }
        return Math.floorMod(cosmeticVariant(tx, ty, z, materialLane, formOrdinal), variantCount);
    }

    /**
     * A well-mixed integer hash of a tile's world position and material/form salt, used to
     * choose a cosmetic tile variant (TILE-ART-SPEC section 12). Pure and stateless — the
     * same tile always hashes the same value on every run and machine, so variant choice is
     * deterministic with no RNG and no stored state; it is presentation-only and never read
     * by sim-core or the {@code WorldHasher} (the tile's simulated state is untouched). The
     * MurmurHash3 mixing gives good avalanche between adjacent coordinates, so neighbouring
     * tiles usually land on different variants and the repeat pattern breaks up.
     */
    public static int cosmeticVariant(int x, int y, int z, int materialLane, int formOrdinal) {
        int h = 0x9E3779B9;
        h = mix(h, x);
        h = mix(h, y);
        h = mix(h, z);
        h = mix(h, materialLane);
        h = mix(h, formOrdinal);
        // fmix32 finalizer (MurmurHash3).
        h ^= (h >>> 16);
        h *= 0x85EBCA6B;
        h ^= (h >>> 13);
        h *= 0xC2B2AE35;
        h ^= (h >>> 16);
        return h;
    }

    private static int mix(int h, int value) {
        int k = value * 0xCC9E2D51;
        k = Integer.rotateLeft(k, 15);
        k *= 0x1B873593;
        h ^= k;
        h = Integer.rotateLeft(h, 13);
        return h * 5 + 0xE6546B64;
    }
}
