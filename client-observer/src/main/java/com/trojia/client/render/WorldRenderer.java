package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.art.TileArtResolver;
import com.trojia.client.atlas.TileAtlas;
import com.trojia.client.camera.MapCamera;
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
 * bounds-checking of its own. {@link TileForm#VOID}/{@link TileForm#OPEN} cells carry no
 * material and are left undrawn (the background clear color shows through).
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

    private final World world;
    private final MaterialRegistry materials;
    private final TileArtResolver artResolver;
    private final TileAtlas atlas;
    private final TileCursor cursor;

    /**
     * @param world       the world to read tiles from
     * @param materials   resolves a tile's raw MATERIAL-lane id back to its raws string id
     * @param artResolver resolves (materialId, form, bucket) to an atlas region name
     * @param atlas       the built atlas the region names are looked up in
     */
    public WorldRenderer(World world, MaterialRegistry materials, TileArtResolver artResolver,
                          TileAtlas atlas) {
        this.world = world;
        this.materials = materials;
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
                if (form == TileForm.VOID || form == TileForm.OPEN) {
                    continue;
                }
                int materialLane = cursor.materialId();
                String materialId = materials.get(materialLane).key();
                String formToken = form.name().toLowerCase(Locale.ROOT);
                String regionName = artResolver.regionName(materialId, formToken, APPEARANCE_BUCKET);
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

                int screenXTopLeft = camera.tileToScreenX(tx);
                int screenYTopLeftDown = camera.tileToScreenY(ty);
                float drawX = screenXTopLeft;
                // MapCamera's y grows downward from a top-left origin; SpriteBatch's
                // default projection is y-up from the bottom-left, so flip per tile.
                float drawY = viewportHeight - screenYTopLeftDown - span;
                batch.draw(region, drawX, drawY, span, span);
            }
        }
        // Restore so downstream draws in the same batch (actors, HUD) are untinted.
        batch.setColor(Color.WHITE);
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
