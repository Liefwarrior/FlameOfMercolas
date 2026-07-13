package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.trojia.client.camera.MapCamera;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.world.PackedPos;

/**
 * Draws the actors resident on one z-level over the tiles {@link WorldRenderer} already
 * laid down (ACTORS-SPEC.md §7.1: per-type glyph + tint, luminous-on-black). Each actor is
 * a single {@code BitmapFont} glyph — its type's {@code glyph}, tinted its type's {@code tint}
 * (both raws-authored, read off {@link ActorTypeStats}) — centered in its tile.
 *
 * <p><b>No staleness</b> (matching {@link WorldRenderer}'s tile read): positions are read
 * live from the {@link ActorRegistry} every frame — nothing is cached — so an actor walking
 * home under {@code RETURN_HOME} visibly moves tile-by-tile as the world ticks forward,
 * rather than snapping once at spawn. Only actors on the requested z-level and inside the
 * camera's visible tile box are drawn (the box is already clamped to world bounds).
 *
 * <p>Draws above tiles, below UI (§7.1). The caller owns {@code batch}'s begin/end and the
 * y-up, bottom-left projection sized to the viewport (the libGDX default) — the same
 * contract {@link WorldRenderer#draw} assumes.
 */
public final class ActorRenderer {

    private final ActorRegistry registry;
    private final GlyphLayout layout = new GlyphLayout();
    private final Color tint = new Color();
    private final char[] glyphBuf = new char[1];

    public ActorRenderer(ActorRegistry registry) {
        this.registry = registry;
    }

    /**
     * Draws every actor on z-level {@code z} within {@code camera}'s viewport, as a tinted
     * glyph centered in its tile. {@code font} is scaled to the camera zoom for the duration
     * of the call and restored to scale 1 / white afterward, so the caller's other font uses
     * are unaffected.
     */
    public void draw(SpriteBatch batch, BitmapFont font, MapCamera camera, int z) {
        int span = camera.tileSpanPx();
        int viewportHeight = camera.viewportHeightPx();
        int minX = camera.visibleTileMinX();
        int maxX = camera.visibleTileMaxX();
        int minY = camera.visibleTileMinY();
        int maxY = camera.visibleTileMaxY();

        font.getData().setScale(camera.zoom());
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            int cell = actor.cell();
            if (PackedPos.z(cell) != z) {
                continue;
            }
            int tx = PackedPos.x(cell);
            int ty = PackedPos.y(cell);
            if (tx < minX || tx > maxX || ty < minY || ty > maxY) {
                continue;
            }
            ActorTypeStats stats = actor.stats();
            glyphBuf[0] = stats.glyph();
            setTint(stats.tint());
            font.setColor(tint);
            layout.setText(font, new String(glyphBuf));

            int screenXLeft = camera.tileToScreenX(tx);
            int screenYTopDown = camera.tileToScreenY(ty);
            float tileBottomYUp = viewportHeight - screenYTopDown - span;
            float drawX = screenXLeft + (span - layout.width) / 2f;
            // BitmapFont draws downward from y (y = top of the glyph), so add height to
            // land the glyph centered within the tile's vertical span.
            float drawY = tileBottomYUp + (span + layout.height) / 2f;
            font.draw(batch, layout, drawX, drawY);
        }
        font.getData().setScale(1f);
        font.setColor(Color.WHITE);
    }

    private void setTint(int rgb) {
        float r = ((rgb >> 16) & 0xFF) / 255f;
        float g = ((rgb >> 8) & 0xFF) / 255f;
        float b = (rgb & 0xFF) / 255f;
        tint.set(r, g, b, 1f);
    }
}
