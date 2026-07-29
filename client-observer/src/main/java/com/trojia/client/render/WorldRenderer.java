package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.trojia.client.art.TileArtResolver;
import com.trojia.client.atlas.TileAtlas;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.render.TilePlan.BaseTilePlan;
import com.trojia.client.render.TilePlan.FluidOverlay;
import com.trojia.sim.fluid.FluidRegistry;
import com.trojia.sim.material.MaterialRegistry;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.World;

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
 * {@link TilePlan#cosmeticVariant} with the form argument pinned to
 * {@link TilePlan#FLUID_FORM_SALT}
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
 * several interchangeable sheet cells, each tile picks one via
 * {@link TilePlan#cosmeticVariant} — a
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
 * <p><b>Day/night lighting</b> (presentation-only, like everything here): the 4-arg
 * {@link #draw(SpriteBatch, MapCamera, int, AmbientLight)} multiplies every scene draw —
 * base tiles, fluid overlays, and both air-depth look-down draws — by the frame's
 * {@link AmbientLight}, per-cell lifted toward warm lamplight where the precomputed
 * {@link LampGlowMap} says a static lamp/brazier reaches (gated by
 * {@code AmbientLight.lampFactor()}, so pools only appear as the light fails). Night water
 * additionally sits slightly darker and cooler than land ({@link #WATER_NIGHT_COOL_R}/
 * {@code _G} scale the fluid overlay's ambient by nightness). At
 * {@link AmbientLight#NEUTRAL} every multiply is the identity — the daytime scene is
 * pixel-identical to the pre-cycle renderer, and the 3-arg {@link #draw(SpriteBatch,
 * MapCamera, int)} keeps exactly that behavior. HUD/UI draws happen after this pass with
 * the batch colour restored to white, so they are never tinted.
 *
 * <p>Reuses one {@link TileCursor} across the whole draw call, per {@code World.cursor()}'s
 * "callers keep and reuse it" contract — no per-tile cursor allocation. The look-down probe
 * moves that same cursor down the z-column and the next tile's {@code moveTo} resets it.
 */
public final class WorldRenderer {

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

    /** Faint cool (blue-ward) haze added per depth level, capped by {@link #COOL_MAX}
     * (package-private so {@link DepthVision#shade} shares the one curve). */
    static final float COOL_PER_DEPTH = 0.012f;

    /** Cap on the cumulative cool haze, so the tint stays a hint of depth, never a blue cast. */
    static final float COOL_MAX = 0.10f;

    /** FLUID-lane depth mask (Tile.java: depth bits 0–2) — {@link TilePlan}'s own. */
    private static final int FLUID_DEPTH_MASK = TilePlan.FLUID_DEPTH_MASK;

    /** Q8 alpha denominator: {@code alphaQ8 / 256f} is the batch alpha. */
    private static final float ALPHA_Q8_ONE = 256f;

    /** How much the fluid overlay's ambient red is pulled down at full night (water reads
     * a touch darker + cooler than the lamplit land around it; identity by day). */
    public static final float WATER_NIGHT_COOL_R = 0.18f;

    /** The paired (smaller) green pull-down — together with the red pull this cools the
     * night water blue-ward without ever brightening anything. */
    public static final float WATER_NIGHT_COOL_G = 0.10f;

    private final World world;
    private final MaterialRegistry materials;
    private final FluidRegistry fluids;
    private final TileArtResolver artResolver;
    private final TileAtlas atlas;
    private final TileCursor cursor;
    private final LampGlowMap lamps;

    /** Per-cell lit-ambient scratch written by {@link #lit} (single render thread). */
    private float litR;
    private float litG;
    private float litB;

    /**
     * Convenience constructor with no lamp map ({@link LampGlowMap#EMPTY}) — night scenes
     * get the ambient cycle but no lamp pools.
     */
    public WorldRenderer(World world, MaterialRegistry materials, FluidRegistry fluids,
                          TileArtResolver artResolver, TileAtlas atlas) {
        this(world, materials, fluids, artResolver, atlas, LampGlowMap.EMPTY);
    }

    /**
     * @param world       the world to read tiles from
     * @param materials   resolves a tile's raw MATERIAL-lane id back to its raws string id
     * @param fluids      resolves a tile's raw FLUID-lane fluid id back to its raws string id
     * @param artResolver resolves (materialId, form, bucket) to an atlas region name, and
     *                    fluid ids to overlay region/alpha/tint
     * @param atlas       the built atlas the region names are looked up in
     * @param lamps       the precomputed static-lamp influence map for Dusk/Night pools
     */
    public WorldRenderer(World world, MaterialRegistry materials, FluidRegistry fluids,
                          TileArtResolver artResolver, TileAtlas atlas, LampGlowMap lamps) {
        this.world = world;
        this.materials = materials;
        this.fluids = fluids;
        this.artResolver = artResolver;
        this.atlas = atlas;
        this.cursor = world.cursor();
        this.lamps = lamps;
    }

    /**
     * Draws every visible tile of z-level {@code z} at {@link AmbientLight#NEUTRAL} — the
     * exact pre-day/night-cycle look, kept for headless proofs and any caller with no clock.
     */
    public void draw(SpriteBatch batch, MapCamera camera, int z) {
        draw(batch, camera, z, AmbientLight.NEUTRAL);
    }

    /**
     * Draws every visible tile of z-level {@code z} within {@code camera}'s current
     * viewport, lit by {@code ambient} (see the class javadoc's day/night section). Caller
     * owns {@code batch}'s begin/end and projection matrix; this method assumes a standard
     * y-up, bottom-left-origin projection sized to the viewport (the libGDX default), and
     * converts {@link MapCamera}'s y-down, top-left screen coordinates into that space per
     * tile.
     */
    public void draw(SpriteBatch batch, MapCamera camera, int z, AmbientLight ambient) {
        float lampF = ambient.lampFactor();
        // Touch of night water: the fluid overlay's ambient sits slightly darker + cooler
        // than the land's as nightness rises (identity at lampFactor 0).
        float waterAmbR = ambient.r() * (1f - WATER_NIGHT_COOL_R * lampF);
        float waterAmbG = ambient.g() * (1f - WATER_NIGHT_COOL_G * lampF);
        float waterAmbB = ambient.b();
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
                    // exactly as before (blur level 0, no depth shade), in the frame's light.
                    if (hasBaseTile) {
                        lit(ambient.r(), ambient.g(), ambient.b(), lampF, tx, ty, z);
                        BaseTilePlan plan = baseTilePlan(cursor, tx, ty, z);
                        setTint(batch, plan.materialTintRgb(), litR, litG, litB);
                        batch.draw(atlas.region(plan.regionName(), plan.variant(), 0),
                                drawX, drawY, span, span);
                    }
                    // Fluid overlay pass: over the base tile, or alone on a fluid-bearing OPEN
                    // cell (the harbor's water column shows a surface on every z-slice it
                    // occupies, not just where it touches a floor).
                    FluidOverlay overlay = TilePlan.fluid(fluidBits, tx, ty, z, fluids, artResolver,
                            atlas);
                    if (overlay != null) {
                        lit(waterAmbR, waterAmbG, waterAmbB, lampF, tx, ty, z);
                        setOverlayColor(batch, overlay.tintRgb(), overlay.alphaQ8(),
                                litR, litG, litB);
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
                // Depth dim + faint blue-ward haze, via the ONE shared curve the actor
                // depth pass also applies ({@link DepthVision#shade} — extracted verbatim,
                // same expressions in the same order, so this stays pixel-identical).
                DepthVision.Shade shade = DepthVision.shade(depth);
                float shadeR = shade.r();
                float shadeG = shade.g();
                float shadeB = shade.b();

                cursor.moveTo(PackedPos.pack(tx, ty, foundZ));
                TileForm lowForm = cursor.form();
                int lowFluidBits = cursor.fluidBits();
                if (lowForm != TileForm.OPEN) { // solid form -> has a base tile (never VOID here)
                    // The look-down cell sits in the same scene light — ambient (with any
                    // lamp pool at the *found* cell) times the existing depth shade.
                    lit(ambient.r(), ambient.g(), ambient.b(), lampF, tx, ty, foundZ);
                    BaseTilePlan plan = baseTilePlan(cursor, tx, ty, foundZ);
                    setShadedTint(batch, plan.materialTintRgb(),
                            shadeR * litR, shadeG * litG, shadeB * litB);
                    batch.draw(atlas.region(plan.regionName(), plan.variant(), blurLevel),
                            drawX, drawY, span, span);
                }
                FluidOverlay overlay = TilePlan.fluid(lowFluidBits, tx, ty, foundZ, fluids,
                        artResolver, atlas);
                if (overlay != null) {
                    lit(waterAmbR, waterAmbG, waterAmbB, lampF, tx, ty, foundZ);
                    setShadedOverlayColor(batch, overlay.tintRgb(), overlay.alphaQ8(),
                            shadeR * litR, shadeG * litG, shadeB * litB);
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
     * Resolves the base tile of the cell the cursor is positioned on through the shared
     * {@link TilePlan#base} chain — material lane &rarr; raws key &rarr; region name &rarr;
     * cosmetic variant + secondary tint. Used verbatim by the top layer, the air-depth
     * look-down AND the first-person view, so a cell resolves identically no matter which
     * camera is looking at it (only the blur level and depth shade differ at the draw). The
     * caller guarantees a base-tile-bearing cell (a solid, non-VOID form).
     */
    private BaseTilePlan baseTilePlan(TileCursor cur, int tx, int ty, int z) {
        return TilePlan.base(cur.form(), cur.materialId(), tx, ty, z, materials, artResolver,
                atlas);
    }

    /**
     * Writes the scene-lit ambient for one cell into the {@link #litR}/{@code G}/{@code B}
     * scratch: the frame's ambient channels lifted toward the cell's precomputed lamp-glow
     * colour by {@code glowStrength * lampFactor} (the day/night pass's core lerp). With
     * lamps out ({@code lampFactor == 0}) or no lamp reaching the cell, this is just the
     * ambient — and at {@link AmbientLight#NEUTRAL} exactly {@code (1, 1, 1)}, the
     * pre-cycle identity.
     */
    private void lit(float ambR, float ambG, float ambB, float lampFactor,
            int x, int y, int z) {
        float r = ambR;
        float g = ambG;
        float b = ambB;
        if (lampFactor > 0f) {
            int glow = lamps.glow(x, y, z);
            if (glow != 0) {
                float s = ((glow >>> 24) & 0xFF) / 255f * lampFactor;
                float warmR = ((glow >> 16) & 0xFF) / 255f;
                float warmG = ((glow >> 8) & 0xFF) / 255f;
                float warmB = (glow & 0xFF) / 255f;
                r += (warmR - r) * s;
                g += (warmG - g) * s;
                b += (warmB - b) * s;
            }
        }
        litR = r;
        litG = g;
        litB = b;
    }

    /**
     * Sets the batch color for a fluid-overlay draw: the fluid's optional secondary tint
     * (white for {@link TileArtResolver#NO_TINT}) times the cell's lit ambient, at the
     * per-depth alpha {@code alphaQ8 / 256} — the default SpriteBatch alpha blending does
     * the rest, so deeper water covers the tile beneath more opaquely (TILE-ART-SPEC
     * section 5.3).
     */
    private static void setOverlayColor(SpriteBatch batch, int tintRgb, int alphaQ8,
            float litR, float litG, float litB) {
        float a = alphaQ8 / ALPHA_Q8_ONE;
        if (tintRgb == TileArtResolver.NO_TINT) {
            batch.setColor(litR, litG, litB, a);
            return;
        }
        float r = ((tintRgb >> 16) & 0xFF) / 255f;
        float g = ((tintRgb >> 8) & 0xFF) / 255f;
        float b = (tintRgb & 0xFF) / 255f;
        batch.setColor(r * litR, g * litG, b * litB, a);
    }

    /**
     * Multiplies the batch by a material's {@code 0xRRGGBB} tint (the secondary adjustment
     * described above) times the cell's lit ambient; {@link TileArtResolver#NO_TINT} (the
     * common case in the colored pack) draws at the lit ambient alone — which is white,
     * exactly as authored, whenever the light is neutral.
     */
    private static void setTint(SpriteBatch batch, int rgb,
            float litR, float litG, float litB) {
        if (rgb == TileArtResolver.NO_TINT) {
            batch.setColor(litR, litG, litB, 1f);
            return;
        }
        float r = ((rgb >> 16) & 0xFF) / 255f;
        float g = ((rgb >> 8) & 0xFF) / 255f;
        float b = (rgb & 0xFF) / 255f;
        batch.setColor(r * litR, g * litG, b * litB, 1f);
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
}
