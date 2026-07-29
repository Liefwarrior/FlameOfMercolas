package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.camera.MapCamera;

import java.util.List;

/**
 * The GL half of the place labels (2026-07-28, Eli: "We also need to label buildings!"):
 * draws {@link PlaceSignOverlay}'s planned marks over the ward's doors and kerbs, and its one
 * pop-up box near the tile the viewer is attending to.
 *
 * <p><b>Two passes, two places in the frame.</b> The marks are WORLD furniture — a plaque
 * belongs over its doorframe and a fingerpost stands in its kerb, so they draw in the scene
 * pass just after the actors (a figure in the doorway is under its own shop's sign, never over
 * it) and take the shared {@link DepthVision#shade} treatment when the place reads from a band
 * below the view plane, exactly as {@code FishingSpotRenderer} and the depth actors do. They
 * also take the frame's day/night {@link AmbientLight}, like every other scene draw — before
 * this pass the ward's plaques burned at full noon brightness at midnight. The box is HUD: it
 * draws in the HUD pass, over the world, undimmed and unfaded, because words that recede are
 * words you cannot read.
 *
 * <p><b>It stays off the UI.</b> The box is given the frame's keep-out zones — the hover
 * nameplate it belongs to, the status/pulse/purse block, an open inspector sheet — and
 * {@link PlaceSignArt#placeAvoiding} moves it rather than covering them.
 *
 * <p>Every rectangle, INCLUDING every letter, comes from the GL-free {@link PlaceSignArt},
 * stretched from the shared 1&times;1 white pixel ({@code HudPanel}'s technique — no new
 * texture, no art-pack dependency, and no font object). The headless raster proof paints the
 * same quad lists from the same calls, so what a test checks and what the GPU shows cannot
 * drift apart.
 *
 * <p>Presentation-only, no staleness: the plan is recomputed from the loaded places every
 * frame; nothing here is read back by the sim, and nothing here writes world state. The caller
 * owns {@code batch} begin/end; batch colour is restored.
 */
public final class PlaceSignRenderer {

    /** The place name: the pop-up's bright line. */
    private static final float[] TEXT_PLACE = {0.96f, 0.94f, 0.84f};
    /** What the place IS: the pop-up's quieter second line. */
    private static final float[] TEXT_WHAT = {0.66f, 0.64f, 0.56f};

    private final List<PlaceSign> signs;
    private final DepthSight depthSight;

    /** This frame's plan, computed in {@link #drawMarks} and consumed by {@link #drawBox}. */
    private PlaceSignOverlay.Plan plan = PlaceSignOverlay.Plan.NOTHING;

    public PlaceSignRenderer(List<PlaceSign> signs, DepthSight depthSight) {
        this.signs = List.copyOf(signs);
        this.depthSight = depthSight;
    }

    /** True when the ward has no authored places at all — the caller can skip both passes. */
    public boolean isEmpty() {
        return signs.isEmpty();
    }

    /**
     * Scene pass: plans this frame and draws every visible place's mark.
     *
     * @param attentionX     the attention tile x — the cursor's tile in observer mode, the
     *                       played actor's own tile in Play mode
     * @param attentionY     the attention tile y
     * @param attentionLive  {@code false} when there is no attention point (cursor off the
     *                       world): marks still stand, nothing is named
     * @param playMode       {@code true} for the tight doorway reach instead of the free
     *                       camera's generous one
     * @param ambient        the frame's day/night light, the same one the world draws under
     * @param whitePixel     the shared 1&times;1 region ({@code IconAtlas.whitePixel()})
     */
    public void drawMarks(SpriteBatch batch, MapCamera camera, int z, TextureRegion whitePixel,
            int attentionX, int attentionY, boolean attentionLive, boolean playMode,
            AmbientLight ambient) {
        this.plan = PlaceSignOverlay.plan(signs, z,
                camera.visibleTileMinX(), camera.visibleTileMaxX(),
                camera.visibleTileMinY(), camera.visibleTileMaxY(), depthSight,
                attentionX, attentionY, attentionLive, playMode);
        if (plan.markers().isEmpty()) {
            return;
        }
        int span = camera.tileSpanPx();
        int viewportHeight = camera.viewportHeightPx();
        for (PlaceSignOverlay.Marker marker : plan.markers()) {
            float left = camera.tileToScreenX(marker.tileX());
            // MapCamera y grows downward; the batch projection is y-up (WorldRenderer's own
            // per-tile flip).
            float bottom = viewportHeight - camera.tileToScreenY(marker.tileY()) - span;
            for (PlaceSignArt.Quad quad : PlaceSignArt.mark(marker.way(), left, bottom, span,
                    marker.named(), lit(PlaceSignArt.INK_BONE, marker.depth(), ambient))) {
                batch.setColor(quad.r(), quad.g(), quad.b(), 1f);
                batch.draw(whitePixel, quad.x(), quad.y(), quad.w(), quad.h());
            }
        }
        batch.setColor(Color.WHITE);
    }

    /**
     * HUD pass: the frame's one pop-up, if any — a hard-edged black box with a crisp border
     * and blocky letters, placed near the attention tile without covering {@code keepOut}.
     * Call after {@link #drawMarks} in the same frame; it consumes that pass's plan.
     *
     * @param keepOut the frame's UI zones the box must not occlude
     */
    public void drawBox(SpriteBatch batch, MapCamera camera, TextureRegion whitePixel,
            List<PlaceSignArt.Rect> keepOut) {
        PlaceSignOverlay.Box box = plan.box();
        if (box == null) {
            return;   // snaps off: there is no fade, and nothing is drawn at all
        }
        float w = PlaceSignArt.boxWidth(Math.max(PlaceSignArt.textWidth(box.place()),
                PlaceSignArt.textWidth(box.what())));
        float h = PlaceSignArt.boxHeight();
        int span = camera.tileSpanPx();
        float anchorLeft = camera.tileToScreenX(box.anchorTileX());
        float anchorBottom = camera.viewportHeightPx()
                - camera.tileToScreenY(box.anchorTileY()) - span;
        float[] at = PlaceSignArt.placeAvoiding(anchorLeft + span / 2f, anchorBottom + span,
                anchorBottom, w, h, camera.viewportWidthPx(), camera.viewportHeightPx(),
                keepOut);

        // The border recedes with the place it names (a label read through two z-bands of air
        // is a label on something far below); the TEXT never does.
        float[] border = lit(PlaceSignArt.INK_BONE, box.depth(), AmbientLight.NEUTRAL);
        paint(batch, whitePixel, PlaceSignArt.box(at[0], at[1], w, h, border));

        float textX = PlaceSignArt.textLeftX(at[0]);
        paint(batch, whitePixel, PlaceSignArt.textQuads(box.place(), textX,
                PlaceSignArt.firstLineTopY(at[1], h), TEXT_PLACE));
        paint(batch, whitePixel, PlaceSignArt.textQuads(box.what(), textX,
                PlaceSignArt.secondLineTopY(at[1], h), TEXT_WHAT));
        batch.setColor(Color.WHITE);
    }

    private static void paint(SpriteBatch batch, TextureRegion whitePixel,
            List<PlaceSignArt.Quad> quads) {
        for (PlaceSignArt.Quad quad : quads) {
            batch.setColor(quad.r(), quad.g(), quad.b(), 1f);
            batch.draw(whitePixel, quad.x(), quad.y(), quad.w(), quad.h());
        }
    }

    /**
     * The shared depth treatment and the frame's ambient, in that order — the same two
     * multiplications every other scene draw takes.
     */
    static float[] lit(float[] ink, int depth, AmbientLight ambient) {
        float r = ink[0];
        float g = ink[1];
        float b = ink[2];
        if (depth > 0) {
            DepthVision.Shade shade = DepthVision.shade(depth);
            r *= shade.r();
            g *= shade.g();
            b *= shade.b();
        }
        return new float[] {r * ambient.r(), g * ambient.g(), b * ambient.b()};
    }
}
