package com.trojia.client.hud.icons;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;

import java.util.List;

/**
 * Draws a GL-free {@link HudToken} line: text runs via {@code font.draw}, icon tokens as
 * inline glyph quads sized to the font's current line height, left-to-right, no wrapping
 * (every caller's line fits comfortably on one HUD/inspector row). The batch's tint is reset
 * to opaque white before each icon quad, since {@code BitmapFont} bakes its own color into
 * glyph vertices rather than reading the batch's — so a caller's panel/log font color (which
 * only tints the text runs) never bleeds onto the icon's own art.
 */
public final class IconTextLine {

    private static final float ICON_GAP_PX = 3f;

    /** Brightness multiplier for {@link HudToken.Text#dim() dim} text runs (alpha untouched). */
    private static final float DIM_FACTOR = 0.55f;

    private IconTextLine() {
    }

    /**
     * Draws {@code tokens} left-to-right starting at {@code (x, y)}, using the same
     * top-of-line {@code y} convention as {@code BitmapFont.draw(batch, text, x, y)}.
     *
     * @param font  supplies both the text glyphs and the line height icons are scaled to;
     *              its current color tints the text runs only
     * @param icons the icon glyph lookup for any {@link HudToken.Icon} tokens
     * @return the total width drawn, in pixels
     */
    public static float draw(SpriteBatch batch, BitmapFont font, IconAtlas icons, float x, float y,
            List<HudToken> tokens) {
        GlyphLayout layout = new GlyphLayout();
        float lineHeight = font.getLineHeight();
        float cursorX = x;
        for (HudToken token : tokens) {
            if (token instanceof HudToken.Text text) {
                if (text.dim()) {
                    // GlyphLayout bakes the font's color at setText time, so scale the font
                    // color down for this run and restore it after — the caller's color keeps
                    // tinting every non-dim run on the line.
                    Color bright = new Color(font.getColor());
                    font.setColor(bright.r * DIM_FACTOR, bright.g * DIM_FACTOR,
                            bright.b * DIM_FACTOR, bright.a);
                    layout.setText(font, text.value());
                    font.draw(batch, layout, cursorX, y);
                    font.setColor(bright);
                } else {
                    layout.setText(font, text.value());
                    font.draw(batch, layout, cursorX, y);
                }
                cursorX += layout.width;
            } else if (token instanceof HudToken.Icon iconToken) {
                TextureRegion region = icons.region(iconToken.key());
                batch.setColor(Color.WHITE);
                batch.draw(region, cursorX, y - lineHeight, lineHeight, lineHeight);
                cursorX += lineHeight + ICON_GAP_PX;
            }
        }
        return cursorX - x;
    }

    /**
     * Computes the width {@link #draw} would occupy for {@code tokens}, without a batch or an
     * {@link IconAtlas} — icon width is always the font's line height regardless of which icon,
     * so no texture lookup is needed. Lets a caller size a background panel to its content
     * before drawing either.
     */
    public static float measure(BitmapFont font, List<HudToken> tokens) {
        GlyphLayout layout = new GlyphLayout();
        float lineHeight = font.getLineHeight();
        float width = 0f;
        for (HudToken token : tokens) {
            if (token instanceof HudToken.Text text) {
                layout.setText(font, text.value());
                width += layout.width;
            } else if (token instanceof HudToken.Icon) {
                width += lineHeight + ICON_GAP_PX;
            }
        }
        return width;
    }
}
