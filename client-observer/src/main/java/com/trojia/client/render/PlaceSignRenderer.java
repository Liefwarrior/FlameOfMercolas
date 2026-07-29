package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.camera.MapCamera;

import java.util.List;

/**
 * The GL half of the building labels (2026-07-28, Eli: "We also need to label buildings!"):
 * draws {@link PlaceSignOverlay}'s planned hanging signs over the ward's doors, and its one
 * pop-up box over the tile the viewer is attending to.
 *
 * <p><b>Two passes, two places in the frame.</b> The hanging signs are WORLD furniture — they
 * belong over the doorframe, so they draw in the scene pass just after the actors (a figure in
 * the doorway is under its own shop's sign, never over it) and take the shared
 * {@link DepthVision#shade} treatment when the place reads from a band below the view plane,
 * exactly as {@code FishingSpotRenderer} and the depth actors do. The box is HUD — it draws in
 * the HUD pass, over everything, undimmed and unfaded, because words that recede are words you
 * cannot read.
 *
 * <p>Every rectangle comes from the GL-free {@link PlaceSignArt}, stretched from the shared
 * 1&times;1 white pixel ({@code HudPanel}'s technique — no new texture, no art-pack
 * dependency). The headless raster proof paints the same quad lists, so what a test checks and
 * what the GPU shows cannot drift apart.
 *
 * <p>Presentation-only, no staleness: the plan is recomputed from the loaded places every
 * frame; nothing here is read back by the sim, and nothing here writes world state. The caller
 * owns {@code batch} begin/end; batch and font colour are restored.
 */
public final class PlaceSignRenderer {

    /** The place name: the pop-up's bright line. */
    private static final Color TEXT_PLACE = new Color(0.96f, 0.94f, 0.84f, 1f);
    /** What the place IS: the pop-up's quieter second line. */
    private static final Color TEXT_WHAT = new Color(0.66f, 0.64f, 0.56f, 1f);

    private final List<PlaceSign> signs;
    private final DepthSight depthSight;
    private final GlyphLayout layout = new GlyphLayout();

    /** This frame's plan, computed in {@link #drawSigns} and consumed by {@link #drawBox}. */
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
     * Scene pass: plans this frame and draws every visible door's hanging sign.
     *
     * @param attentionX     the attention tile x — the cursor's tile in observer mode, the
     *                       played actor's own tile in Play mode
     * @param attentionY     the attention tile y
     * @param attentionLive  {@code false} when there is no attention point (cursor off the
     *                       world): signs still hang, nothing is named
     * @param playMode       {@code true} to use the tight doorway reach instead of the free
     *                       camera's generous one
     * @param whitePixel     the shared 1&times;1 region ({@code IconAtlas.whitePixel()})
     */
    public void drawSigns(SpriteBatch batch, MapCamera camera, int z, TextureRegion whitePixel,
            int attentionX, int attentionY, boolean attentionLive, boolean playMode) {
        this.plan = PlaceSignOverlay.plan(signs, z,
                camera.visibleTileMinX(), camera.visibleTileMaxX(),
                camera.visibleTileMinY(), camera.visibleTileMaxY(), depthSight,
                attentionX, attentionY, attentionLive,
                playMode ? PlaceSignOverlay.PLAY_APPROACH_TILES
                        : PlaceSignOverlay.OBSERVER_APPROACH_TILES);
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
            for (PlaceSignArt.Quad quad : PlaceSignArt.hangingSign(left, bottom, span,
                    marker.named(), shaded(PlaceSignArt.INK_BONE, marker.depth()))) {
                batch.setColor(quad.r(), quad.g(), quad.b(), 1f);
                batch.draw(whitePixel, quad.x(), quad.y(), quad.w(), quad.h());
            }
        }
        batch.setColor(Color.WHITE);
    }

    /**
     * HUD pass: the frame's one pop-up, if any — a hard-edged black box with a crisp border,
     * lifted just above the attention tile and clamped fully on screen. Call after
     * {@link #drawSigns} in the same frame; it consumes that pass's plan.
     */
    public void drawBox(SpriteBatch batch, BitmapFont font, MapCamera camera,
            TextureRegion whitePixel) {
        PlaceSignOverlay.Box box = plan.box();
        if (box == null) {
            return;   // snaps off: there is no fade, and nothing is drawn at all
        }
        float lineHeight = font.getLineHeight();
        layout.setText(font, box.place());
        float widest = layout.width;
        layout.setText(font, box.what());
        widest = Math.max(widest, layout.width);

        float w = PlaceSignArt.boxWidth(widest);
        float h = PlaceSignArt.boxHeight(lineHeight);
        int span = camera.tileSpanPx();
        float anchorLeft = camera.tileToScreenX(box.anchorTileX());
        float anchorBottom = camera.viewportHeightPx()
                - camera.tileToScreenY(box.anchorTileY()) - span;
        float[] at = PlaceSignArt.clampToViewport(
                anchorLeft + span / 2f - w / 2f, anchorBottom + span + PlaceSignArt.LIFT_PX,
                w, h, camera.viewportWidthPx(), camera.viewportHeightPx());

        // The border recedes with the place it names (a label read through two z-bands of air
        // is a label on something far below); the TEXT never does.
        float[] border = shaded(PlaceSignArt.INK_BONE, box.depth());
        for (PlaceSignArt.Quad quad : PlaceSignArt.box(at[0], at[1], w, h, border)) {
            batch.setColor(quad.r(), quad.g(), quad.b(), 1f);
            batch.draw(whitePixel, quad.x(), quad.y(), quad.w(), quad.h());
        }
        batch.setColor(Color.WHITE);

        float textX = PlaceSignArt.textLeftX(at[0]);
        float lineTop = PlaceSignArt.firstLineTopY(at[1], h);
        font.setColor(TEXT_PLACE);
        font.draw(batch, box.place(), textX, lineTop);
        font.setColor(TEXT_WHAT);
        font.draw(batch, box.what(), textX, lineTop - lineHeight);
        font.setColor(Color.WHITE);
    }

    /** The shared depth treatment, or the ink untouched on the viewed plane. */
    private static float[] shaded(float[] ink, int depth) {
        if (depth <= 0) {
            return ink;
        }
        DepthVision.Shade shade = DepthVision.shade(depth);
        return new float[] {ink[0] * shade.r(), ink[1] * shade.g(), ink[2] * shade.b()};
    }
}
