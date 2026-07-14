package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.art.TileArtResolver;
import com.trojia.client.atlas.TileAtlas;
import com.trojia.client.camera.MapCamera;
import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.World;

import java.util.Locale;

/**
 * Draws one z-level of a {@link World} as atlas tiles (M1 Behavior 2). Culls to
 * {@link MapCamera}'s {@code visibleTile*} bounds so only on-screen cells are drawn —
 * those bounds are already clamped to world bounds, so the draw loop needs no further
 * bounds-checking of its own. {@link TileForm#VOID} cells are always skipped;
 * {@link TileForm#OPEN} cells carry no material and draw no base tile (the background
 * clear color shows through), but both OPEN and solid-form cells still receive the fluid
 * overlay pass below when their FLUID lane holds pooled depth.
 *
 * <p><b>Fluid overlay pass</b> (TILE-ART-SPEC section 5.3; GRANADAD art spec section 5):
 * after (or, for OPEN cells, instead of) the base tile, any cell whose FLUID lane carries
 * depth &gt; 0 gets the fluid's overlay region drawn over the same quad at a per-depth
 * alpha — deeper reads darker/more opaque via the mapping's monotone {@code depthAlphaQ8}
 * curve. The raw FLUID-lane fluid id resolves to its raws key through
 * {@link FluidRegistry} (mirroring the MATERIAL-lane path through
 * {@link MaterialRegistry}), then to a region/alpha/tint through the same
 * {@link TileArtResolver} seam as tiles — an art-pack swap changes water's look with zero
 * renderer changes. A pack that maps no entry for the fluid reports alpha 0 at every
 * depth, which this pass treats as draw-nothing. Overlay variants reuse
 * {@link #cosmeticVariant} with the form argument pinned to {@link #FLUID_FORM_SALT}
 * (out-of-band of every real {@link TileForm} ordinal) so the water surface's variety
 * never correlates with the floor beneath it. Z-order: terrain, then water, then actors —
 * this pass runs inside the tile loop, before {@code ActorRenderer}, so actors read as
 * standing in the water rather than under it.
 *
 * <p>Appearance bucket is pinned to 0 for every tile in v0 — real charge-bucket lookups
 * (chromatis fill level, etc.) land with F5 light; this pass only wires up materials and
 * forms. Unknown/unmapped regions fall back to {@link TileArtResolver#missingRegionName()}
 * rather than throwing, matching the resolver's own no-crash contract (belt-and-suspenders
 * since the atlas is always built from the same resolver's referenced region set).
 *
 * <p><b>Per-material tint</b> (DECISIONS.md art register, Eli 2026-07-13, reversing the
 * 2026-07-12 "Luminous-on-black" ruling): the shipped Kenney pack is now the full-colour
 * sheet, so each region's cells already carry their own baked colour and the default is to
 * draw them as authored — most materials list no {@code tint} at all, and for those this
 * multiply is a no-op (batch colour stays white). A minority of materials still carry a
 * {@code tint} in {@code art-mapping.json} as a deliberate <em>secondary</em> adjustment —
 * never the primary colour source — used only where the shipped palette has no cell in the
 * needed hue, or where two materials would otherwise share one region's cells and look
 * identical (see art-mapping.json's per-material {@code notes} for the reasoning in each
 * surviving case). Tinting is a batch colour multiply, so it can only darken/re-hue a
 * texel, never lighten it past the sprite's own baked colour. Each tile's quad is
 * multiplied by {@link TileArtResolver#materialTintRgb} before it draws and the batch
 * colour is restored to white afterward — the same shared-glyph-times-per-type-tint trick
 * {@link ActorRenderer} uses, unchanged mechanically from the monochrome-pack era even
 * though it now runs against real colour instead of grayscale.
 *
 * <p><b>Cosmetic tile variants</b> (TILE-ART-SPEC section 12): when a region name backs
 * several interchangeable sheet cells, each tile picks one via {@link #cosmeticVariant} — a
 * pure hash of its world position (and material/form) modulo the variant count. Same map,
 * same look, every run; presentation-only, so sim-core and the determinism machinery are
 * untouched. This axis is orthogonal to the appearance bucket (the gameplay charge-stop
 * axis the resolver keys on, pinned to 0 here) — variety never changes which region name
 * resolves, only which of its cells is drawn.
 *
 * <p>Reuses one {@link TileCursor} across the whole draw call, per {@code World.cursor()}'s
 * "callers keep and reuse it" contract — no per-tile cursor allocation.
 */
public final class WorldRenderer {

    /** Appearance bucket used for every tile in v0 (F5 will read real charge state). */
    private static final int APPEARANCE_BUCKET = 0;

    /**
     * The {@code formOrdinal} argument {@link #cosmeticVariant} receives for fluid-overlay
     * variant picks: one past {@code TileForm.STAIR.ordinal()} (the last real form), so no
     * real form can ever collide with it and water variants never correlate with the
     * variant of the floor tile beneath (GRANADAD art spec section 5, pinned).
     */
    static final int FLUID_FORM_SALT = 6;

    /** FLUID-lane unpacking (Tile.java: depth bits 0–2, fluidId bits 3–5, SETTLED bit 6). */
    private static final int FLUID_DEPTH_MASK = 0x7;
    private static final int FLUID_ID_SHIFT = 3;
    private static final int FLUID_ID_MASK = 0x7;

    /** Q8 alpha denominator: {@code alphaQ8 / 256f} is the batch alpha. */
    private static final float ALPHA_Q8_ONE = 256f;

    private final World world;
    private final MaterialRegistry materials;
    private final FluidRegistry fluids;
    private final TileArtResolver artResolver;
    private final TileAtlas atlas;
    private final TileCursor cursor;

    /**
     * @param world       the world to read tiles from
     * @param materials   resolves a tile's raw MATERIAL-lane id back to its raws string id
     * @param fluids      resolves a tile's raw FLUID-lane fluid id back to its raws string id
     * @param artResolver resolves (materialId, form, bucket) to an atlas region name, and
     *                    fluid ids to overlay region/alpha/tint
     * @param atlas       the built atlas the region names are looked up in
     */
    public WorldRenderer(World world, MaterialRegistry materials, FluidRegistry fluids,
                          TileArtResolver artResolver, TileAtlas atlas) {
        this.world = world;
        this.materials = materials;
        this.fluids = fluids;
        this.artResolver = artResolver;
        this.atlas = atlas;
        this.cursor = world.cursor();
    }

    /**
     * Draws every visible tile of z-level {@code z} within {@code camera}'s current
     * viewport. Caller owns {@code batch}'s begin/end and projection matrix; this method
     * assumes a standard y-up, bottom-left-origin projection sized to the viewport (the
     * libGDX default), and converts {@link MapCamera}'s y-down, top-left screen
     * coordinates into that space per tile.
     */
    public void draw(SpriteBatch batch, MapCamera camera, int z) {
        int span = camera.tileSpanPx();
        int viewportHeight = camera.viewportHeightPx();
        int minX = camera.visibleTileMinX();
        int maxX = camera.visibleTileMaxX();
        int minY = camera.visibleTileMinY();
        int maxY = camera.visibleTileMaxY();
        for (int ty = minY; ty <= maxY; ty++) {
            for (int tx = minX; tx <= maxX; tx++) {
                cursor.moveTo(PackedPos.pack(tx, ty, z));
                TileForm form = cursor.form();
                if (form == TileForm.VOID) {
                    continue;
                }
                int fluidBits = cursor.fluidBits();
                boolean hasBaseTile = form != TileForm.OPEN;
                if (!hasBaseTile && (fluidBits & FLUID_DEPTH_MASK) == 0) {
                    continue; // dry OPEN air: nothing to draw, as before
                }

                int screenXTopLeft = camera.tileToScreenX(tx);
                int screenYTopLeftDown = camera.tileToScreenY(ty);
                float drawX = screenXTopLeft;
                // MapCamera's y grows downward from a top-left origin; SpriteBatch's
                // default projection is y-up from the bottom-left, so flip per tile.
                float drawY = viewportHeight - screenYTopLeftDown - span;

                if (hasBaseTile) {
                    int materialLane = cursor.materialId();
                    String materialId = materials.get(materialLane).key();
                    String formToken = form.name().toLowerCase(Locale.ROOT);
                    String regionName =
                            artResolver.regionName(materialId, formToken, APPEARANCE_BUCKET);
                    if (!atlas.contains(regionName)) {
                        regionName = artResolver.missingRegionName();
                    }
                    // Pick a cosmetic variant deterministically from the tile's world position
                    // (plus material/form salt) so a large wall or floor shows real tile-to-tile
                    // variety instead of one repeated sprite — a pure function, identical every
                    // run and machine (TILE-ART-SPEC section 12).
                    int variantCount = atlas.variantCount(regionName);
                    int variant = variantCount <= 1 ? 0
                            : Math.floorMod(cosmeticVariant(tx, ty, z, materialLane, form.ordinal()),
                                    variantCount);
                    TextureRegion region = atlas.region(regionName, variant);

                    setTint(batch, artResolver.materialTintRgb(materialId));
                    batch.draw(region, drawX, drawY, span, span);
                }

                // Fluid overlay pass: over the base tile, or alone on a fluid-bearing OPEN
                // cell (the harbor's water column shows a surface on every z-slice it
                // occupies, not just where it touches a floor).
                FluidOverlay overlay = fluidOverlay(fluidBits, tx, ty, z, fluids, artResolver,
                        atlas);
                if (overlay != null) {
                    setOverlayColor(batch, overlay.tintRgb(), overlay.alphaQ8());
                    batch.draw(atlas.region(overlay.regionName(), overlay.variant()),
                            drawX, drawY, span, span);
                }
            }
        }
        // Restore so downstream draws in the same batch (actors, HUD) are untinted.
        batch.setColor(Color.WHITE);
    }

    /**
     * The GL-free plan for one cell's fluid overlay — what to draw and how — or
     * {@code null} when the cell draws no overlay. Pure function of its arguments
     * (deterministic across runs and machines); the GL side of the pass is just a
     * {@code batch.draw} of the plan. Package-private so it unit-tests headless.
     *
     * <p>Draw-nothing cases: depth 0; a FLUID-lane fluid id outside the registry (lane
     * garbage — nothing sane to resolve); an alpha of 0 at the cell's depth, which is how
     * a pack that maps no entry for this fluid opts out (the resolver reports 0 at every
     * depth for unmapped fluids). The SETTLED bit and any future high FLUID-lane bits are
     * ignored — presentation only cares about what is pooled, not whether it is still
     * flowing.
     *
     * @param fluidBits raw FLUID-lane bits (depth 0–2, fluidId 3–5, SETTLED 6)
     * @return the plan, or {@code null} to draw nothing
     */
    static FluidOverlay fluidOverlay(int fluidBits, int tx, int ty, int z,
            FluidRegistry fluids, TileArtResolver artResolver, TileAtlas atlas) {
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
        if (!atlas.contains(regionName)) {
            regionName = artResolver.missingRegionName();
        }
        // Same deterministic position-hash variety as base tiles, but salted with the
        // raw fluid id and the out-of-band FLUID_FORM_SALT instead of material/form, so
        // the water surface's repeat pattern is independent of the floor's.
        int variantCount = atlas.variantCount(regionName);
        int variant = variantCount <= 1 ? 0
                : Math.floorMod(cosmeticVariant(tx, ty, z, fluidId, FLUID_FORM_SALT),
                        variantCount);
        return new FluidOverlay(regionName, variant, artResolver.fluidTintRgb(fluidKey), alphaQ8);
    }

    /**
     * One cell's resolved fluid-overlay draw: which region cell, at what tint and Q8
     * alpha. {@code tintRgb} is {@link TileArtResolver#NO_TINT} when the pack's fluid
     * region is pre-colored and draws as authored (the shipped packs' water).
     */
    record FluidOverlay(String regionName, int variant, int tintRgb, int alphaQ8) {
    }

    /**
     * Sets the batch color for a fluid-overlay draw: the fluid's optional secondary tint
     * (white for {@link TileArtResolver#NO_TINT}) at the per-depth alpha
     * {@code alphaQ8 / 256} — the default SpriteBatch alpha blending does the rest, so
     * deeper water covers the tile beneath more opaquely (TILE-ART-SPEC section 5.3).
     */
    private static void setOverlayColor(SpriteBatch batch, int tintRgb, int alphaQ8) {
        float a = alphaQ8 / ALPHA_Q8_ONE;
        if (tintRgb == TileArtResolver.NO_TINT) {
            batch.setColor(1f, 1f, 1f, a);
            return;
        }
        float r = ((tintRgb >> 16) & 0xFF) / 255f;
        float g = ((tintRgb >> 8) & 0xFF) / 255f;
        float b = (tintRgb & 0xFF) / 255f;
        batch.setColor(r, g, b, a);
    }

    /**
     * Multiplies the batch by a material's {@code 0xRRGGBB} tint (the secondary adjustment
     * described above), or leaves it white for {@link TileArtResolver#NO_TINT} — the common
     * case in the colored pack, where the cell draws exactly as authored.
     */
    private static void setTint(SpriteBatch batch, int rgb) {
        if (rgb == TileArtResolver.NO_TINT) {
            batch.setColor(Color.WHITE);
            return;
        }
        float r = ((rgb >> 16) & 0xFF) / 255f;
        float g = ((rgb >> 8) & 0xFF) / 255f;
        float b = (rgb & 0xFF) / 255f;
        batch.setColor(r, g, b, 1f);
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
    private static int cosmeticVariant(int x, int y, int z, int materialLane, int formOrdinal) {
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
