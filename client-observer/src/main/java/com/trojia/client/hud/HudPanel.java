package com.trojia.client.hud;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;

/**
 * Draws a solid near-black rectangle behind a block of HUD/inspector text — the
 * Dwarf-Fortress-style "every readout sits in its own bordered block" look Eli asked for,
 * instead of text floating bare over the world view. GL-cheap: one quad per panel, stretching
 * {@link com.trojia.client.hud.icons.IconAtlas#whitePixel()} to the target rectangle, so no new
 * texture needs loading at any call site.
 *
 * <p>Callers draw the panel first, then their existing (unmoved) text/icon calls on top — this
 * is a pure background insertion, never a text-layout change.
 */
public final class HudPanel {

    /** Near-black, fully opaque — reads as a distinct panel against any world tile color. */
    public static final Color BACKGROUND = new Color(0.04f, 0.04f, 0.05f, 1f);

    /** Padding, in px, a caller should reserve between panel edge and its content on every side. */
    public static final float PADDING = 6f;

    private HudPanel() {
    }

    /**
     * Fills a {@code w}&times;{@code h} rectangle whose bottom-left corner is {@code (x, y)}
     * (the same y-up, bottom-left-origin convention {@link com.trojia.client.hud.icons.IconTextLine}
     * and {@code SpriteBatch} already use) with {@link #BACKGROUND}.
     *
     * @param whitePixel a 1&times;1 opaque white region, e.g. {@code IconAtlas.whitePixel()}
     */
    public static void draw(SpriteBatch batch, TextureRegion whitePixel, float x, float y, float w,
            float h) {
        batch.setColor(BACKGROUND);
        batch.draw(whitePixel, x, y, w, h);
        batch.setColor(Color.WHITE); // matches IconTextLine's existing white-reset convention
    }
}
