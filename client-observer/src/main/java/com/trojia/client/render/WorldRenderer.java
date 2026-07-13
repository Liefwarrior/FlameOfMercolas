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
 * <p><b>Per-material tint</b> (DECISIONS.md "Luminous-on-black" register): each tile's
 * quad is multiplied by the material's {@link TileArtResolver#materialTintRgb} before it
 * draws and the batch colour is restored to white afterward — the same shared-glyph-times-
 * per-type-tint trick {@link ActorRenderer} uses. That lets one tintable grayscale wall or
 * floor sprite in the {@link TileAtlas} glow a different colour per material (granite blue-
 * grey, oak amber, glowstone red…) with no per-material atlas cell. A pack that ships
 * pre-coloured cells instead (the procedural placeholder) simply lists no tints, so
 * {@code materialTintRgb} returns {@link TileArtResolver#NO_TINT} and the cell draws
 * untinted.
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
                String materialId = materials.get(cursor.materialId()).key();
                String formToken = form.name().toLowerCase(Locale.ROOT);
                String regionName = artResolver.regionName(materialId, formToken, APPEARANCE_BUCKET);
                TextureRegion region = atlas.contains(regionName)
                        ? atlas.region(regionName)
                        : atlas.region(artResolver.missingRegionName());

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
     * Multiplies the batch by a material's {@code 0xRRGGBB} tint (glowing-on-black), or
     * leaves it white for {@link TileArtResolver#NO_TINT} — a pre-coloured pack draws its
     * cell as authored.
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
}
