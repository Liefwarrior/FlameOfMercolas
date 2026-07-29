package com.trojia.client.fpv;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.trojia.client.hud.HudPanel;

import java.util.List;

/**
 * Draws the {@link CompassStrip}: a narrow panel at the top-centre of the frame with the
 * compass points sliding through it and a fixed tick at the middle marking where you are
 * looking.
 *
 * <p>Drawn identically in both modes and at the same screen position, on purpose — it is the
 * one element that is provably unchanged across the switch, so it carries orientation through
 * the cross-fade.
 */
public final class CompassRenderer {

    /** Strip width in px. */
    public static final float WIDTH = 300f;

    /** Strip height in px. */
    public static final float HEIGHT = 20f;

    /** Distance from the top edge of the viewport. */
    private static final float TOP_MARGIN = 6f;

    private static final Color PANEL = new Color(0.04f, 0.04f, 0.05f, 0.82f);
    private static final Color CARDINAL = new Color(1f, 0.94f, 0.78f, 1f);
    private static final Color INTERCARDINAL = new Color(0.62f, 0.60f, 0.56f, 1f);
    private static final Color TICK = new Color(1f, 0.85f, 0.35f, 1f);

    private final GlyphLayout layout = new GlyphLayout();

    /**
     * @param yaw          the view yaw both cameras share
     * @param viewportW    viewport width in px
     * @param viewportH    viewport height in px
     */
    public void draw(SpriteBatch batch, BitmapFont font, TextureRegion whitePixel, float yaw,
            int viewportW, int viewportH) {
        float left = (viewportW - WIDTH) / 2f;
        float bottom = viewportH - TOP_MARGIN - HEIGHT;
        HudPanel.draw(batch, whitePixel, left, bottom, WIDTH, HEIGHT, PANEL);

        List<CompassStrip.Mark> marks = CompassStrip.plan(yaw, WIDTH);
        float baseline = bottom + HEIGHT - 4f;
        for (CompassStrip.Mark mark : marks) {
            font.setColor(mark.cardinal() ? CARDINAL : INTERCARDINAL);
            layout.setText(font, mark.label());
            font.draw(batch, layout, left + mark.x() - layout.width / 2f, baseline);
        }
        font.setColor(Color.WHITE);
        // The fixed centre tick: the compass moves, this does not.
        HudPanel.draw(batch, whitePixel, left + WIDTH / 2f - 1f, bottom, 2f, 4f, TICK);
    }
}
