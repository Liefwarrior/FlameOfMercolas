package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.camera.MapCamera;
import com.trojia.sim.actor.FishingSpots;
import com.trojia.sim.actor.FishingZoneTable;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.List;
import java.util.function.IntSupplier;

/**
 * The GL half of the fishing-spot overlay (Sprint 6): draws {@link FishingSpotOverlay}'s
 * planned markers as size-classed ripple rings over the water, between
 * {@code WorldRenderer}'s tiles/water and {@code ActorRenderer}'s actors (z-order
 * terrain &rarr; water &rarr; SPOTS &rarr; actors — a fisher stands IN the ripple, the
 * ripple sits ON the water).
 *
 * <p>Markers are drawn from the shared 1x1 white pixel (the {@code HudPanel} technique —
 * no new texture, no art-pack dependency) as an inset ring of four thin rects per water
 * cell: the three size classes read apart by color and weight — a thin pale foam ring
 * (small / inshore), a brighter teal ring (medium / pier-side), and a heavy gold-green
 * ring with a center glint (large / deep water). Below-plane markers multiply by the
 * shared {@link DepthVision#shade} curve, so a spot two bands down recedes exactly like
 * the terrain and the dockhand beside it.
 *
 * <p>Presentation-only, no staleness: the plan is recomputed from the live registry every
 * frame; nothing here is read back by the sim. The caller owns {@code batch} begin/end
 * (the {@code WorldRenderer.draw} contract); batch color is restored to white.
 */
public final class FishingSpotRenderer {

    /** Ring inset from the tile edge, as a fraction of the tile span. */
    private static final float RING_INSET = 0.18f;
    /** Ring line weight per size class (fraction of span, floored at 1px). */
    private static final float[] RING_WEIGHT = {0.06f, 0.09f, 0.14f};
    /** Ring color per size class (small foam / medium teal / large gold-green). */
    private static final Color[] RING_COLORS = {
            new Color(0.78f, 0.88f, 0.92f, 0.85f), // small: pale foam
            new Color(0.30f, 0.80f, 0.85f, 0.90f), // medium: teal
            new Color(0.95f, 0.85f, 0.35f, 0.95f), // large: gold glint
    };
    /** The large class's center glint size, as a fraction of the tile span. */
    private static final float GLINT_SIZE = 0.20f;

    private final FishingSpots spots;
    private final DepthSight depthSight;
    private final SkillTrackRegistry tracks;
    private final long worldSeed;
    /** Live "who is played" read — {@code Actor.NONE} = omniscient observer view. */
    private final IntSupplier viewerId;

    public FishingSpotRenderer(FishingSpots spots, DepthSight depthSight,
            SkillTrackRegistry tracks, long worldSeed, IntSupplier viewerId) {
        this.spots = spots;
        this.depthSight = depthSight;
        this.tracks = tracks;
        this.worldSeed = worldSeed;
        this.viewerId = viewerId;
    }

    /**
     * Draws every planned marker for the camera's current view of z-level {@code z}.
     * {@code whitePixel} is the shared 1x1 region ({@code IconAtlas.whitePixel()}).
     */
    public void draw(SpriteBatch batch, MapCamera camera, int z, TextureRegion whitePixel) {
        List<FishingSpotOverlay.Marker> markers = FishingSpotOverlay.plan(spots, z,
                camera.visibleTileMinX(), camera.visibleTileMaxX(),
                camera.visibleTileMinY(), camera.visibleTileMaxY(),
                depthSight, viewerId.getAsInt(), worldSeed, tracks);
        if (markers.isEmpty()) {
            return;
        }
        int span = camera.tileSpanPx();
        int viewportHeight = camera.viewportHeightPx();
        for (FishingSpotOverlay.Marker marker : markers) {
            float inset = span * RING_INSET;
            float weight = Math.max(1f, span * RING_WEIGHT[marker.sizeClass()]);
            Color base = RING_COLORS[marker.sizeClass()];
            float r = base.r;
            float g = base.g;
            float b = base.b;
            if (marker.depth() > 0) {
                DepthVision.Shade shade = DepthVision.shade(marker.depth());
                r *= shade.r();
                g *= shade.g();
                b *= shade.b();
            }
            batch.setColor(r, g, b, base.a);

            float left = camera.tileToScreenX(marker.tileX()) + inset;
            float topDown = camera.tileToScreenY(marker.tileY()) + inset;
            float size = span - 2 * inset;
            // MapCamera y grows downward; the batch projection is y-up (the WorldRenderer
            // per-tile flip).
            float bottom = viewportHeight - topDown - size;

            // The ring: four thin rects (top, bottom, left, right).
            batch.draw(whitePixel, left, bottom + size - weight, size, weight);
            batch.draw(whitePixel, left, bottom, size, weight);
            batch.draw(whitePixel, left, bottom + weight, weight, size - 2 * weight);
            batch.draw(whitePixel, left + size - weight, bottom + weight,
                    weight, size - 2 * weight);
            if (marker.sizeClass() == FishingZoneTable.LARGE) {
                float glint = Math.max(2f, span * GLINT_SIZE);
                batch.draw(whitePixel, left + (size - glint) / 2f,
                        bottom + (size - glint) / 2f, glint, glint);
            }
        }
        batch.setColor(Color.WHITE);
    }
}
