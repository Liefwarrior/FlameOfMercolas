package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.utils.Align;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.hud.HudPanel;
import com.trojia.client.hud.icons.IconAtlas;

import java.util.List;

/**
 * Draws the JOURNAL pane (Sprint 3 "The Vanished Clerk"): one DF near-black bordered panel,
 * centered high on the screen, holding the {@code JournalText} lines — quest title in the
 * talk panel's gold, section markers in hint gray, body prose in the bark cream — each line
 * wrapped to the panel width, with the {@code (J close)} hint at the foot. Journal CONTENT
 * lives in the GL-free {@code JournalText}; this class only measures, backs and draws it.
 */
public final class JournalRenderer {

    private static final float PANEL_WIDTH = 560f;
    /** The panel's top edge sits this far below the viewport top (under the status HUD). */
    private static final float TOP_MARGIN_PX = 64f;
    private static final float LINE_GAP_PX = 3f;

    private static final Color TITLE_COLOR = new Color(1f, 0.86f, 0.20f, 1f);
    private static final Color MARKER_COLOR = new Color(0.55f, 0.55f, 0.58f, 1f);
    private static final Color BODY_COLOR = new Color(0.95f, 0.92f, 0.80f, 1f);
    private static final Color STORY_COLOR = new Color(0.72f, 0.80f, 0.72f, 1f);
    private static final Color HINT_COLOR = MARKER_COLOR;

    private static final String HINT = "(J close)";

    private final GlyphLayout layout = new GlyphLayout();

    /** Draws {@code lines} as the open journal; a no-op while {@code open} is false. */
    public void draw(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera,
            boolean open, List<String> lines) {
        if (!open) {
            return;
        }
        font.getData().setScale(1f);
        float lineHeight = font.getLineHeight();

        float contentHeight = 0f;
        for (String line : lines) {
            contentHeight += wrappedHeight(font, line, lineHeight) + LINE_GAP_PX;
        }
        contentHeight += lineHeight; // the close hint

        float x = (camera.viewportWidthPx() - PANEL_WIDTH) / 2f;
        float topY = camera.viewportHeightPx() - TOP_MARGIN_PX;
        HudPanel.draw(batch, icons.whitePixel(), x - HudPanel.PADDING,
                topY - contentHeight - HudPanel.PADDING, PANEL_WIDTH + 2 * HudPanel.PADDING,
                contentHeight + 2 * HudPanel.PADDING);

        float y = topY;
        boolean titled = false;
        for (String line : lines) {
            font.setColor(colorOf(line, titled));
            if (!line.isEmpty() && !isMarker(line) && !titled) {
                titled = true; // the first non-empty line of a block is its gold title...
            } else if (line.isEmpty()) {
                titled = false; // ...and a blank separator starts the next quest's block
            }
            font.draw(batch, line, x, y, PANEL_WIDTH, Align.left, true);
            y -= wrappedHeight(font, line, lineHeight) + LINE_GAP_PX;
        }
        font.setColor(HINT_COLOR);
        font.draw(batch, HINT, x, y);
        font.setColor(Color.WHITE);
    }

    private float wrappedHeight(BitmapFont font, String line, float lineHeight) {
        if (line.isEmpty()) {
            return lineHeight;
        }
        layout.setText(font, line, Color.WHITE, PANEL_WIDTH, Align.left, true);
        return layout.height;
    }

    private Color colorOf(String line, boolean titled) {
        if (isMarker(line)) {
            return MARKER_COLOR;
        }
        if (!titled && !line.isEmpty()) {
            return TITLE_COLOR;
        }
        return line.startsWith("Day ") ? STORY_COLOR : BODY_COLOR;
    }

    private static boolean isMarker(String line) {
        return line.startsWith("-- ") && line.endsWith(" --");
    }
}
