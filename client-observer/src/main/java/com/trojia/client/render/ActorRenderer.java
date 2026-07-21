package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.sprite.SpriteIndex;
import com.trojia.client.sprite.SpriteRef;
import com.trojia.client.sprite.SpriteSheet;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.world.PackedPos;

import java.util.HashMap;
import java.util.Map;

/**
 * Draws the actors resident on one z-level over the tiles {@link WorldRenderer} already
 * laid down, as <b>real sprites</b> from the tag-queryable {@link SpriteIndex}
 * (DECISIONS.md Art register pillar 3, Eli 2026-07-13 fourth revision; unified art spec
 * §3). Each actor draws {@code index.forActor(typeId, actorId)} — a pure, stateless pick,
 * so an actor keeps its sprite for life — through the {@link SpriteSheet}'s region,
 * <b>untinted by material colour</b> (batch color white by day): sprites are pre-colored,
 * and a multiply tint would fight the baked colors (§3.3). The one exception is the scene's
 * {@link AmbientLight} — the day/night pass multiplies each actor by the same lit ambient
 * as the tile it stands on (ambient plus any lamp pool at its cell), so actors sit in the
 * scene's light instead of glowing full-bright at midnight. At {@link AmbientLight#NEUTRAL}
 * that multiply is the identity and the historical untinted behavior is preserved exactly.
 *
 * <p>The raws {@code glyph}/{@code tint} stay in {@code ActorTypeStats} as data for text
 * surfaces (event log, HUD tokens) — this renderer no longer consumes them, and the old
 * BitmapFont glyph path is deleted outright rather than kept as a fallback: the index is
 * validated at load (every {@code actorQueries} entry resolves non-empty, boot fails
 * otherwise), so a type with no sprite here is a programming error that should throw, not
 * silently degrade to a glyph.
 *
 * <p><b>No staleness</b> (matching {@link WorldRenderer}'s tile read): positions are read
 * live from the {@link ActorRegistry} every frame — nothing is cached — so an actor walking
 * home under {@code RETURN_HOME} visibly moves tile-by-tile as the world ticks forward.
 * Only actors on the requested z-level and inside the camera's visible tile box are drawn
 * (the box is already clamped to world bounds).
 *
 * <p>Draws above tiles (and above the water overlay — spec §3.3 z-order), below UI. The
 * caller owns {@code batch}'s begin/end and the y-up, bottom-left projection sized to the
 * viewport (the libGDX default) — the same contract {@link WorldRenderer#draw} assumes.
 */
public final class ActorRenderer {

    private final ActorRegistry registry;
    private final SpriteIndex index;
    private final SpriteSheet sheet;
    private final LampGlowMap lamps;

    /** Convenience constructor with no lamp map (actors still get the ambient cycle). */
    public ActorRenderer(ActorRegistry registry, SpriteIndex index, SpriteSheet sheet) {
        this(registry, index, sheet, LampGlowMap.EMPTY);
    }

    public ActorRenderer(ActorRegistry registry, SpriteIndex index, SpriteSheet sheet,
            LampGlowMap lamps) {
        this.registry = registry;
        this.index = index;
        this.sheet = sheet;
        this.lamps = lamps;
    }

    /** Screen-px cascade per co-located actor beyond the first, so a Keeper standing on
     * its Animal's tile (routine per ACTORS-SPEC — every Keeper always has one) doesn't
     * vanish under it; capped so a handful of stacked actors stays inside the tile. */
    private static final int STACK_OFFSET_PX = 3;
    private static final int STACK_OFFSET_MAX = 3;

    /**
     * Draws every visible actor at {@link AmbientLight#NEUTRAL} — the exact historical
     * untinted look, kept for headless proofs and any caller with no clock.
     */
    public void draw(SpriteBatch batch, MapCamera camera, int z) {
        draw(batch, camera, z, AmbientLight.NEUTRAL);
    }

    /**
     * Draws every actor on z-level {@code z} within {@code camera}'s viewport, as its
     * per-life sprite filling its tile quad (16px art scaled to the camera's tile span,
     * pixel-snapped to the tile grid; nearest filtering keeps it crisp), multiplied by the
     * lit ambient of the actor's own cell (see the class javadoc). The batch color is
     * restored to white afterwards — the convention every renderer here restores to.
     */
    public void draw(SpriteBatch batch, MapCamera camera, int z, AmbientLight ambient) {
        int span = camera.tileSpanPx();
        int viewportHeight = camera.viewportHeightPx();
        int minX = camera.visibleTileMinX();
        int maxX = camera.visibleTileMaxX();
        int minY = camera.visibleTileMinY();
        int maxY = camera.visibleTileMaxY();
        float lampF = ambient.lampFactor();
        boolean neutral = ambient.isNeutral();

        // Ascending-id draw order is already deterministic; this only tracks how many
        // actors this frame have landed on a given cell so each later one past the first
        // nudges into view instead of fully overdrawing its tile-mate.
        Map<Integer, Integer> stackCountByCell = new HashMap<>();
        batch.setColor(Color.WHITE);
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
            SpriteRef ref = index.forActor(actor.typeId().key(), actor.id());
            TextureRegion region = sheet.region(ref);

            if (!neutral) {
                // Same lit-ambient lerp as WorldRenderer's tiles: ambient lifted toward
                // the cell's lamp-glow colour by strength * lampFactor.
                float r = ambient.r();
                float g = ambient.g();
                float b = ambient.b();
                if (lampF > 0f) {
                    int glow = lamps.glow(tx, ty, z);
                    if (glow != 0) {
                        float s = ((glow >>> 24) & 0xFF) / 255f * lampF;
                        r += (((glow >> 16) & 0xFF) / 255f - r) * s;
                        g += (((glow >> 8) & 0xFF) / 255f - g) * s;
                        b += ((glow & 0xFF) / 255f - b) * s;
                    }
                }
                batch.setColor(r, g, b, 1f);
            }

            int stackIndex = Math.min(stackCountByCell.merge(cell, 1, Integer::sum) - 1,
                    STACK_OFFSET_MAX);
            int screenXLeft = camera.tileToScreenX(tx) + stackIndex * STACK_OFFSET_PX;
            int screenYTopDown = camera.tileToScreenY(ty) - stackIndex * STACK_OFFSET_PX;
            float tileBottomYUp = viewportHeight - screenYTopDown - span;
            // Actor sprites are 1x1 cells today; a future multi-cell entry keeps its cell
            // aspect, anchored to the actor's tile.
            batch.draw(region, screenXLeft, tileBottomYUp,
                    span * ref.cellsW(), span * ref.cellsH());
        }
        // Restore so downstream draws in the same batch (HUD, inspector) are untinted.
        batch.setColor(Color.WHITE);
    }
}
