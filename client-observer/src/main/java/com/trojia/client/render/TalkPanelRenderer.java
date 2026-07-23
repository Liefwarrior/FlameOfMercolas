package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.utils.Align;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.face.InspectorFaces;
import com.trojia.client.hud.HudPanel;
import com.trojia.client.hud.icons.IconAtlas;
import com.trojia.client.inspect.TalkState;
import com.trojia.client.inspect.TalkText;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;

/**
 * Draws the TALK surface (Sprint 2 item 1): one DF-black speech panel, centered low on the
 * screen — the speaker's FaceGen portrait and gold name header, its presented job with the
 * disposition tag, the selected bark, the latest skill-check result line (pickpocket
 * feedback while the exchange is up), and the key hints. Exchange CONTENT lives in the
 * GL-free {@link TalkText}; this class only measures, backs and draws the frozen
 * {@link TalkText.Exchange} held by {@link TalkState}.
 *
 * <p>The portrait resolves from the speaker's PRESENTED actor (the same rule the character
 * sheet's header follows) — a disguised soul speaks with its cover's face.
 */
public final class TalkPanelRenderer {

    private static final float PANEL_WIDTH = 520f;
    /** The panel's bottom edge floats this far above the viewport bottom (over the feed). */
    private static final float BOTTOM_MARGIN_PX = 96f;
    private static final float LINE_GAP_PX = 4f;

    private static final Color NAME_COLOR = new Color(1f, 0.86f, 0.20f, 1f);
    private static final Color META_COLOR = new Color(0.72f, 0.80f, 0.72f, 1f);
    private static final Color BARK_COLOR = new Color(0.95f, 0.92f, 0.80f, 1f);
    private static final Color CHECK_COLOR = new Color(1f, 0.86f, 0.20f, 1f);
    private static final Color HINT_COLOR = new Color(0.55f, 0.55f, 0.58f, 1f);

    private static final String HINTS = "(T greet again  ·  G pickpocket  ·  ESC close)";

    private final ActorRegistry registry;
    /** The FaceGen portrait panel, or {@code null} for text-only (headless-ish callers). */
    private final InspectorFaces faces;
    private final GlyphLayout layout = new GlyphLayout();

    public TalkPanelRenderer(ActorRegistry registry, InspectorFaces faces) {
        this.registry = registry;
        this.faces = faces;
    }

    /** Draws the open exchange; a no-op while {@code talk} is closed. */
    public void draw(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera,
            TalkState talk) {
        if (!talk.open()) {
            return;
        }
        TalkText.Exchange exchange = talk.exchange();
        Actor presented = registry.get(exchange.speakerPresentedId());
        String typeKey = presented.typeId().key();
        boolean showFace = faces != null && faces.hasFaceFor(typeKey);

        font.getData().setScale(1f);
        float lineHeight = font.getLineHeight();
        String meta = exchange.jobLine().isEmpty()
                ? exchange.contextLine()
                : exchange.jobLine() + "  " + exchange.contextLine();
        String bark = quoted(exchange.barkLine());
        layout.setText(font, bark, BARK_COLOR, PANEL_WIDTH, Align.left, true);
        float barkHeight = layout.height;
        String check = talk.checkLine();

        float contentHeight = lineHeight                                 // name header
                + (showFace ? InspectorFaces.PANEL_SHIFT_PX : 0f)        // portrait
                + lineHeight + LINE_GAP_PX                               // job + disposition
                + barkHeight + LINE_GAP_PX                               // the bark, wrapped
                + (check != null ? lineHeight + LINE_GAP_PX : 0f)        // check result
                + lineHeight;                                            // hints

        float x = (camera.viewportWidthPx() - PANEL_WIDTH) / 2f;
        float contentBottomY = BOTTOM_MARGIN_PX;
        float contentTopY = contentBottomY + contentHeight;
        HudPanel.draw(batch, icons.whitePixel(), x - HudPanel.PADDING,
                contentBottomY - HudPanel.PADDING, PANEL_WIDTH + 2 * HudPanel.PADDING,
                contentHeight + 2 * HudPanel.PADDING);

        float y = contentTopY;
        font.setColor(NAME_COLOR);
        font.draw(batch, exchange.nameLine(), x, y);
        y -= lineHeight;
        if (showFace) {
            drawPortrait(batch, icons, exchange.speakerPresentedId(), typeKey, x, y);
            y -= InspectorFaces.PANEL_SHIFT_PX;
        }
        font.setColor(META_COLOR);
        font.draw(batch, meta, x, y);
        y -= lineHeight + LINE_GAP_PX;
        font.setColor(BARK_COLOR);
        font.draw(batch, bark, x, y, PANEL_WIDTH, Align.left, true);
        y -= barkHeight + LINE_GAP_PX;
        if (check != null) {
            font.setColor(CHECK_COLOR);
            font.draw(batch, check, x, y);
            y -= lineHeight + LINE_GAP_PX;
        }
        font.setColor(HINT_COLOR);
        font.draw(batch, HINTS, x, y);
        font.setColor(Color.WHITE);
    }

    /** The dark interior the transparent face parts sit on (the sheet's own framing). */
    private static final Color PORTRAIT_INTERIOR_COLOR = new Color(0.02f, 0.02f, 0.03f, 1f);

    /** The gold-ringed portrait, centered in the panel column (the sheet's own framing). */
    private void drawPortrait(SpriteBatch batch, IconAtlas icons, int presentedId,
            String typeKey, float x, float contentTopY) {
        float centerX = x + PANEL_WIDTH / 2f;
        float borderSize = InspectorFaces.FACE_SIZE_PX + 2 * InspectorFaces.PORTRAIT_BORDER_PX;
        float borderX = centerX - InspectorFaces.FACE_SIZE_PX / 2f
                - InspectorFaces.PORTRAIT_BORDER_PX;
        float borderY = contentTopY - InspectorFaces.FACE_SIZE_PX
                - InspectorFaces.PORTRAIT_BORDER_PX;
        HudPanel.draw(batch, icons.whitePixel(), borderX, borderY, borderSize, borderSize,
                NAME_COLOR);
        HudPanel.draw(batch, icons.whitePixel(), centerX - InspectorFaces.FACE_SIZE_PX / 2f,
                contentTopY - InspectorFaces.FACE_SIZE_PX, InspectorFaces.FACE_SIZE_PX,
                InspectorFaces.FACE_SIZE_PX, PORTRAIT_INTERIOR_COLOR);
        faces.draw(batch, presentedId, typeKey, centerX, contentTopY);
    }

    /** Stage-direction rows ({@code (...)}) render bare; spoken rows render quoted. */
    static String quoted(String bark) {
        return bark.startsWith("(") ? bark : "\"" + bark + "\"";
    }
}
