package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.hud.HudPanel;
import com.trojia.client.hud.icons.IconAtlas;
import com.trojia.client.inspect.ToastQueue;

import java.util.List;

/**
 * Draws the played actor's skill-up toasts (Sprint 1 item 3): each live
 * {@link ToastQueue.Toast} as a bottom-center DF-black plate, stacked oldest-on-top,
 * fading out over its last second ({@link ToastQueue.Toast#alpha()} — the queue owns the
 * timing, this class only draws it). Gold text — a level-up is a reward, it gets the
 * highlight color the sheet already uses for headers.
 */
public final class ToastRenderer {

    /** Gold — the same reward/highlight hue the sheet's headers use. */
    private static final Color TOAST_COLOR = new Color(1f, 0.86f, 0.20f, 1f);
    /** Bottom screen margin under the newest toast, px — clear of the event feed's rows
     *  only in spirit (the feed is bottom-LEFT; toasts are centered). */
    private static final float BOTTOM_MARGIN_PX = 56f;
    /** Vertical gap between two stacked toast plates, px. */
    private static final float STACK_GAP_PX = 4f;

    private final GlyphLayout layout = new GlyphLayout();
    /** Reused scratch colors so the render loop allocates nothing per frame. */
    private final Color backing = new Color();
    private final Color text = new Color();

    /** Draws every live toast; {@code font} is left white at scale 1 afterward. */
    public void draw(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera,
            ToastQueue toasts) {
        List<ToastQueue.Toast> visible = toasts.visible();
        if (visible.isEmpty()) {
            return;
        }
        font.getData().setScale(1f);
        float lineHeight = font.getLineHeight();
        float centerX = camera.viewportWidthPx() / 2f;
        float plateHeight = lineHeight + 2 * HudPanel.PADDING;
        // Stack grows upward from the bottom margin, oldest at the top — a fresh toast
        // always appears at the same lowest slot and pushes older ones up.
        int n = visible.size();
        for (int i = 0; i < n; i++) {
            ToastQueue.Toast toast = visible.get(i);
            float plateBottomY = BOTTOM_MARGIN_PX + (n - 1 - i) * (plateHeight + STACK_GAP_PX);
            layout.setText(font, toast.text());
            float alpha = toast.alpha();
            backing.set(HudPanel.BACKGROUND.r, HudPanel.BACKGROUND.g, HudPanel.BACKGROUND.b,
                    0.9f * alpha);
            HudPanel.draw(batch, icons.whitePixel(),
                    centerX - layout.width / 2f - HudPanel.PADDING, plateBottomY,
                    layout.width + 2 * HudPanel.PADDING, plateHeight, backing);
            text.set(TOAST_COLOR.r, TOAST_COLOR.g, TOAST_COLOR.b, alpha);
            font.setColor(text);
            font.draw(batch, toast.text(), centerX - layout.width / 2f,
                    plateBottomY + plateHeight - HudPanel.PADDING);
        }
        font.setColor(Color.WHITE);
    }
}
