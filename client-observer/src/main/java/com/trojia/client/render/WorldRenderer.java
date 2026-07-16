package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
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
import java.util.function.IntPredicate;

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
 * <p><b>Air-depth "look-down" pass</b> (Eli 2026-07-15). Empty-air cells at the camera's
 * z-level are no longer left black. Instead of the two {@code continue} cases below (a
 * {@link TileForm#VOID} cell, or a dry {@link TileForm#OPEN} cell with no pooled fluid), the
 * renderer walks downward — {@code z-1, z-2, …} up to {@link #MAX_LOOKDOWN} levels — for the
 * first cell that <em>would</em> draw something (a base tile or a fluid). If found at depth
 * {@code d = z - z'}, that lower cell is drawn at the same screen quad through the identical
 * base-tile / fluid-overlay resolution as the top layer, but from a precomputed gaussian-blur
 * atlas level (deeper ⇒ blurrier, {@link TileAtlas#region(String, int, int)}) and multiplied
 * by a subtle depth-dim / cool-haze factor so it reads recessed. Nothing found within
 * {@link #MAX_LOOKDOWN} (or down to the world floor) stays black exactly as before. This pass
 * is presentation-only — like the rest of the renderer it never feeds {@code WorldHasher}, so
 * it carries no determinism constraint.
 *
 * <p>Reuses one {@link TileCursor} across the whole draw call, per {@code World.cursor()}'s
 * "callers keep and reuse it" contract — no per-tile cursor allocation. The look-down probe
 * moves that same cursor down the z-column and the next tile's {@code moveTo} resets it.
 */
public final class WorldRenderer {

    /** Appearance bucket used for every tile in v0 (F5 will read real charge state). */
    private static final int APPEARANCE_BUCKET = 0;

    /**
     * How many z-levels below the camera the air-depth pass searches for a tile to show
     * through empty air. Capped so a deep air column costs a bounded probe and never reads a
     * silly distance down; beyond this (or past the world floor) the cell stays black.
     */
    static final int MAX_LOOKDOWN = 8;

    /** Sentinel from {@link #findLookdownZ}: no drawable cell within reach (stay black). */
    static final int LOOKDOWN_NONE = -1;

    /** Per-depth brightness base: a look-down tile {@code d} levels down keeps {@code 0.90^d}. */
    private static final double DIM_BASE = 0.90;

    /** Floor on the depth-dim brightness so the deepest look-down never goes murky-dark. */
    private static final float DIM_FLOOR = 0.55f;

    /** Faint cool (blue-ward) haze added per depth level, capped by {@link #COOL_MAX}. */
    private static final float COOL_PER_DEPTH = 0.012f;

    /** Cap on the cumulative cool haze, so the tint stays a hint of depth, never a blue cast. */
    private static final float COOL_MAX = 0.10f;

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
                int fluidBits = form == TileForm.VOID ? 0 : cursor.fluidBits();
                boolean hasBaseTile = form != TileForm.VOID && form != TileForm.OPEN;
                boolean emptyAir = form == TileForm.VOID
                        || (!hasBaseTile && (fluidBits & FLUID_DEPTH_MASK) == 0);

                int screenXTopLeft = camera.tileToScreenX(tx);
                int screenYTopLeftDown = camera.tileToScreenY(ty);
                float drawX = screenXTopLeft;
                // MapCamera's y grows downward from a top-left origin; SpriteBatch's
                // default projection is y-up from the bottom-left, so flip per tile.
                float drawY = viewportHeight - screenYTopLeftDown - span;

                if (!emptyAir) {
                    // TOP LAYER — the cell at the camera's own z, drawn sharp and undimmed
                    // exactly as before (blur level 0, no depth shade).
                    if (hasBaseTile) {
                        BaseTilePlan plan = baseTilePlan(cursor, tx, ty, z);
                        setTint(batch, plan.materialTintRgb());
                        batch.draw(atlas.region(plan.regionName(), plan.variant(), 0),
                                drawX, drawY, span, span);
                    }
                    // Fluid overlay pass: over the base tile, or alone on a fluid-bearing OPEN
                    // cell (the harbor's water column shows a surface on every z-slice it
                    // occupies, not just where it touches a floor).
                    FluidOverlay overlay = fluidOverlay(fluidBits, tx, ty, z, fluids, artResolver,
                            atlas);
                    if (overlay != null) {
                        setOverlayColor(batch, overlay.tintRgb(), overlay.alphaQ8());
                        batch.draw(atlas.region(overlay.regionName(), overlay.variant(), 0),
                                drawX, drawY, span, span);
                    }
                    continue;
                }

                // AIR-DEPTH LOOK-DOWN — this cell is empty air, so peer down the z-column for
                // the nearest cell that would draw something and show it blurred + dimmed.
                final int fx = tx;
                final int fy = ty;
                int foundZ = findLookdownZ(z, MAX_LOOKDOWN, zPrime -> {
                    cursor.moveTo(PackedPos.pack(fx, fy, zPrime));
                    return cellDrawsSomething(cursor);
                });
                if (foundZ == LOOKDOWN_NONE) {
                    continue; // nothing within reach: stay black, exactly as before
                }
                int depth = z - foundZ;
                int blurLevel = blurLevelFor(depth);
                float dim = depthDim(depth);
                float cool = Math.min(COOL_MAX, COOL_PER_DEPTH * depth);
                // A faint blue-ward haze: pull red down most, green half as much, leave blue.
                float shadeR = dim * (1f - cool);
                float shadeG = dim * (1f - 0.5f * cool);
                float shadeB = dim;

                cursor.moveTo(PackedPos.pack(tx, ty, foundZ));
                TileForm lowForm = cursor.form();
                int lowFluidBits = cursor.fluidBits();
                if (lowForm != TileForm.OPEN) { // solid form -> has a base tile (never VOID here)
                    BaseTilePlan plan = baseTilePlan(cursor, tx, ty, foundZ);
                    setShadedTint(batch, plan.materialTintRgb(), shadeR, shadeG, shadeB);
                    batch.draw(atlas.region(plan.regionName(), plan.variant(), blurLevel),
                            drawX, drawY, span, span);
                }
                FluidOverlay overlay = fluidOverlay(lowFluidBits, tx, ty, foundZ, fluids,
                        artResolver, atlas);
                if (overlay != null) {
                    setShadedOverlayColor(batch, overlay.tintRgb(), overlay.alphaQ8(),
                            shadeR, shadeG, shadeB);
                    batch.draw(atlas.region(overlay.regionName(), overlay.variant(), blurLevel),
                            drawX, drawY, span, span);
                }
            }
        }
        // Restore so downstream draws in the same batch (actors, HUD) are untinted.
        batch.setColor(Color.WHITE);
    }

    /**
     * Whether the cell the cursor is currently positioned on would draw anything in the top
     * layer — the exact same "not one of the two {@code continue} cases" test the main loop
     * applies at the camera z, reused by the air-depth look-down probe. A {@link TileForm#VOID}
     * cell draws nothing; any solid form draws a base tile; a {@link TileForm#OPEN} cell draws
     * only when its FLUID lane carries pooled depth.
     */
    static boolean cellDrawsSomething(TileCursor cur) {
        TileForm form = cur.form();
        if (form == TileForm.VOID) {
            return false;
        }
        if (form != TileForm.OPEN) {
            return true;
        }
        return (cur.fluidBits() & FLUID_DEPTH_MASK) != 0;
    }

    /**
     * Walks the z-column downward from just below {@code viewZ} for the first level whose cell
     * draws something, capping the search at {@code maxLookdown} levels and never probing below
     * the world floor ({@code z' >= 0}). Pure over its {@code drawsAt} predicate — the renderer
     * passes a lambda that repositions the cursor and calls {@link #cellDrawsSomething}, and the
     * headless test passes a synthetic column — so it unit-tests with no world or GL.
     *
     * @param viewZ       the camera's z-level (the empty-air cell's level)
     * @param maxLookdown how many levels down to search, {@code >= 0}
     * @param drawsAt     tests whether the cell at a given z' would draw something
     * @return the nearest z' at or above the floor whose cell draws, or {@link #LOOKDOWN_NONE}
     */
    static int findLookdownZ(int viewZ, int maxLookdown, IntPredicate drawsAt) {
        int floor = Math.max(0, viewZ - maxLookdown);
        for (int zPrime = viewZ - 1; zPrime >= floor; zPrime--) {
            if (drawsAt.test(zPrime)) {
                return zPrime;
            }
        }
        return LOOKDOWN_NONE;
    }

    /**
     * The blur-pyramid level for a tile {@code depth} z-levels below empty air:
     * {@code clamp(depth-1, 0, atlas.blurLevelCount()-1)}. So the nearest look-down
     * ({@code depth 1}) still draws the sharp cell (level 0) and only its depth-dim recesses it,
     * and each level deeper steps one blur level up until the pyramid is exhausted. A pack with
     * no blur pyramid (placeholder / test fakes report {@code blurLevelCount() == 1}) always
     * clamps to 0.
     */
    private int blurLevelFor(int depth) {
        int last = atlas.blurLevelCount() - 1;
        int level = depth - 1;
        if (level < 0) {
            return 0;
        }
        return level > last ? last : level;
    }

    /**
     * The depth-dim brightness for a look-down tile {@code depth} levels down:
     * {@code max(DIM_FLOOR, DIM_BASE^depth)}. Subtle and monotone — deeper reads hazier /
     * recessed — with a floor so the deepest tiles never fall to murky darkness.
     */
    static float depthDim(int depth) {
        float f = (float) Math.pow(DIM_BASE, depth);
        return Math.max(DIM_FLOOR, f);
    }

    /**
     * Resolves the base tile of the cell the cursor is positioned on — material lane &rarr;
     * raws key &rarr; region name &rarr; cosmetic variant + secondary tint — into a GL-free
     * plan. Shared verbatim by the top layer and the air-depth look-down so a lower cell
     * resolves identically to how it would draw at the camera's own z (only the blur level and
     * depth shade differ at the draw). The caller guarantees a base-tile-bearing cell (a solid,
     * non-VOID form).
     *
     * <p>Variant pick is the same deterministic position hash as before (TILE-ART-SPEC section
     * 12): a single-cell region always draws variant 0, a PERIODIC region a fixed laid-paver
     * weave, otherwise the material/form-salted position hash — all pure functions of world
     * position, presentation-only, never read by {@code WorldHasher}.
     */
    private BaseTilePlan baseTilePlan(TileCursor cur, int tx, int ty, int z) {
        TileForm form = cur.form();
        int materialLane = cur.materialId();
        String materialId = materials.get(materialLane).key();
        String formToken = form.name().toLowerCase(Locale.ROOT);
        String regionName = artResolver.regionName(materialId, formToken, APPEARANCE_BUCKET);
        if (!atlas.contains(regionName)) {
            regionName = artResolver.missingRegionName();
        }
        int variantCount = atlas.variantCount(regionName);
        int variant = pickVariant(regionName, variantCount, tx, ty, z, materialLane,
                form.ordinal());
        return new BaseTilePlan(regionName, variant, artResolver.materialTintRgb(materialId));
    }

    /**
     * One cell's resolved base-tile draw: which region cell to draw and the material's optional
     * secondary tint ({@link TileArtResolver#NO_TINT} when the pre-colored cell draws as
     * authored). The blur level and any depth shade are applied at the draw site, not here.
     */
    record BaseTilePlan(String regionName, int variant, int materialTintRgb) {
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
     * Like {@link #setTint} but for the air-depth look-down: the material tint (white for
     * {@link TileArtResolver#NO_TINT}) times the per-channel depth shade
     * ({@code shadeR/G/B} are {@code depthDim} with a faint cool bias), so the lower tile draws
     * dimmed and slightly cooled on top of its own colour. Full opacity — the blur level, not
     * alpha, carries the softness.
     */
    private static void setShadedTint(SpriteBatch batch, int rgb,
            float shadeR, float shadeG, float shadeB) {
        float r = 1f;
        float g = 1f;
        float b = 1f;
        if (rgb != TileArtResolver.NO_TINT) {
            r = ((rgb >> 16) & 0xFF) / 255f;
            g = ((rgb >> 8) & 0xFF) / 255f;
            b = (rgb & 0xFF) / 255f;
        }
        batch.setColor(r * shadeR, g * shadeG, b * shadeB, 1f);
    }

    /**
     * Like {@link #setOverlayColor} but for a fluid seen through empty air: the fluid tint
     * (white for {@link TileArtResolver#NO_TINT}) times the per-channel depth shade, at the
     * fluid's own per-depth alpha. So water glimpsed several z-levels down blurs and dims by the
     * same depth factor as the terrain beneath it, keeping the look-down layer coherent.
     */
    private static void setShadedOverlayColor(SpriteBatch batch, int tintRgb, int alphaQ8,
            float shadeR, float shadeG, float shadeB) {
        float a = alphaQ8 / ALPHA_Q8_ONE;
        float r = 1f;
        float g = 1f;
        float b = 1f;
        if (tintRgb != TileArtResolver.NO_TINT) {
            r = ((tintRgb >> 16) & 0xFF) / 255f;
            g = ((tintRgb >> 8) & 0xFF) / 255f;
            b = (tintRgb & 0xFF) / 255f;
        }
        batch.setColor(r * shadeR, g * shadeG, b * shadeB, a);
    }

    /**
     * Chooses the cosmetic-variant cell index for one base tile (TILE-ART-SPEC section 12),
     * dispatching on the region's {@link com.trojia.client.atlas.VariantPattern}:
     *
     * <ul>
     *   <li>{@code variantCount <= 1} &rarr; {@code 0}: a homogeneous single-cell region (the
     *       smooth-surface default) always draws its one clean tile.</li>
     *   <li>{@link com.trojia.client.atlas.VariantPattern#PERIODIC} &rarr; {@code (x ^ y) & 1}
     *       folded into the count: a fixed 2-tone laid-paver weave (the sidewalk / civic
     *       flagstone), a regular pattern a random hash cannot produce.</li>
     *   <li>otherwise &rarr; the material/form-salted position hash: scattered variety, the
     *       intended look for deliberately-rough surfaces (dirt, rubble) and moving water.</li>
     * </ul>
     *
     * Every branch is a pure function of world position — presentation-only, never read by the
     * {@code WorldHasher}, byte-identical every run.
     */
    private int pickVariant(String regionName, int variantCount, int tx, int ty, int z,
            int materialLane, int formOrdinal) {
        if (variantCount <= 1) {
            return 0;
        }
        if (atlas.variantPattern(regionName) == com.trojia.client.atlas.VariantPattern.PERIODIC) {
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
