package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.face.InspectorFaces;
import com.trojia.client.hud.HudPanel;
import com.trojia.client.hud.icons.IconAtlas;
import com.trojia.client.hud.icons.IconTextLine;
import com.trojia.client.inspect.EventLog;
import com.trojia.client.inspect.InspectorState;
import com.trojia.client.inspect.InspectorText;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.world.PackedPos;

import java.util.List;

/**
 * Draws the observer's inspector overlays over the world (M-inspector Behaviors 2 &amp; 3):
 * the selection panel (top-right), the population event feed (bottom-left), and a highlight
 * frame around the selected actor's tile. Text content comes from the GL-free
 * {@link InspectorText}/{@link EventLog}; this class only lays the strings out and draws
 * them (the {@code HudText} split, applied to the inspector).
 *
 * <p><b>No staleness.</b> Panel content is regenerated from live registry/side-table reads
 * every frame from the current selected {@code ActorId}; only the event feed retains
 * history (its whole purpose). Drawn above tiles/actors, below nothing — it is the topmost
 * UI. The caller owns {@code batch}'s begin/end and the y-up, bottom-left viewport
 * projection (the same contract {@link ActorRenderer#draw} assumes).
 */
public final class InspectorRenderer {

    private static final float MARGIN = 8f;
    private static final float PANEL_WIDTH = 430f;
    private static final int LOG_VISIBLE_LINES = 16;
    private static final Color PANEL_COLOR = new Color(0.86f, 0.90f, 0.98f, 1f);
    private static final Color LOG_COLOR = new Color(0.72f, 0.80f, 0.72f, 1f);
    private static final Color HIGHLIGHT_COLOR = new Color(1f, 0.86f, 0.20f, 1f);

    private final ActorRegistry registry;
    private final HomeRegistry homes;
    private final RelationshipRegistry relationships;
    private final JobRegistry jobs;
    private final ItemsLiteRegistry items;
    private final EventLog eventLog;
    private final InspectorFaces faces;
    private final GlyphLayout layout = new GlyphLayout();

    /**
     * @param faces the FaceGen portrait panel (unified art spec §4.8), or {@code null} to
     *              render the text-only panel (headless-ish callers, tests)
     */
    public InspectorRenderer(ActorRegistry registry, HomeRegistry homes,
            RelationshipRegistry relationships, JobRegistry jobs, ItemsLiteRegistry items,
            EventLog eventLog, InspectorFaces faces) {
        this.registry = registry;
        this.homes = homes;
        this.relationships = relationships;
        this.jobs = jobs;
        this.items = items;
        this.eventLog = eventLog;
        this.faces = faces;
    }

    /**
     * Draws the selection highlight, side panel and event feed for the current
     * {@code state} and viewed z-level {@code z}. {@code font} is left at scale 1 / white
     * afterward so the caller's other font uses are unaffected.
     */
    public void draw(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera,
            InspectorState state, int z) {
        drawSelectionHighlight(batch, font, camera, state, z);
        drawPanel(batch, font, icons, camera, state);
        drawEventLog(batch, font, icons, camera);
        font.getData().setScale(1f);
        font.setColor(Color.WHITE);
    }

    private void drawPanel(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera,
            InspectorState state) {
        font.getData().setScale(1f);
        font.setColor(PANEL_COLOR);
        float lineHeight = font.getLineHeight();
        float x = camera.viewportWidthPx() - PANEL_WIDTH;
        float topY = camera.viewportHeightPx() - MARGIN;

        // Tally content height before drawing anything, so the DF-style black backing block
        // can be sized to the actual content (follow badge + face portrait shift + text lines,
        // or just the no-selection hint) rather than a fixed guess.
        boolean showFollowBadge = state.followActive();
        boolean hasSelection = state.hasSelection();
        // Play mode (PLAY-MODE-SPEC.md §5.3): resolve the portrait/type from the PRESENTED
        // actor when disguised, so the panel visibly becomes the impersonated actor's face —
        // the same Bledhreft/Senator-Harris canon example, made observable end to end.
        Actor selectedActor = hasSelection ? registry.get(state.selectedActorId()) : null;
        Actor presentedActor = hasSelection && selectedActor.identity().isDisguised()
                ? registry.get(selectedActor.identity().presentedId())
                : selectedActor;
        String typeKey = hasSelection ? presentedActor.typeId().key() : null;
        boolean showFace = hasSelection && faces != null && faces.hasFaceFor(typeKey);
        List<String> lines = hasSelection
                ? InspectorText.describe(state.selectedActorId(), registry, homes, relationships,
                        jobs, items)
                : null;

        float contentHeight = 0f;
        if (showFollowBadge) {
            contentHeight += lineHeight;
        }
        if (!hasSelection) {
            contentHeight += lineHeight; // the selection-hint line
        } else {
            if (showFace) {
                contentHeight += InspectorFaces.PANEL_SHIFT_PX;
            }
            contentHeight += lines.size() * lineHeight;
        }

        float panelWidth = PANEL_WIDTH + 2 * HudPanel.PADDING;
        float panelHeight = contentHeight + 2 * HudPanel.PADDING;
        float panelX = x - HudPanel.PADDING;
        float panelBottomY = topY - contentHeight - HudPanel.PADDING;
        HudPanel.draw(batch, icons.whitePixel(), panelX, panelBottomY, panelWidth, panelHeight);

        float y = topY;
        if (showFollowBadge) {
            IconTextLine.draw(batch, font, icons, x, y, InspectorText.followBadgeTokens());
            y -= lineHeight;
        }
        if (!hasSelection) {
            IconTextLine.draw(batch, font, icons, x, y, InspectorText.selectionHintTokens());
            return;
        }
        // FaceGen portrait at the top of the panel (unified art spec §4.8): 48x48 at x2,
        // centered above the name line; the text block shifts down under it. Beast types
        // have no archetype mapping and keep the text-only panel.
        if (showFace) {
            faces.draw(batch, presentedActor.id(), typeKey, x + PANEL_WIDTH / 2f, y);
            y -= InspectorFaces.PANEL_SHIFT_PX;
        }
        for (String line : lines) {
            font.draw(batch, line, x, y);
            y -= lineHeight;
        }
    }

    private void drawEventLog(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera) {
        List<EventLog.Entry> entries = eventLog.recentNewestFirst(LOG_VISIBLE_LINES);
        font.getData().setScale(1f);
        font.setColor(LOG_COLOR);
        float lineHeight = font.getLineHeight();
        float topY = MARGIN + (entries.size() + 1) * lineHeight;

        String header = "EVENTS (newest first)  ·  " + eventLog.size() + " held";
        layout.setText(font, header);
        float contentWidth = layout.width;
        for (EventLog.Entry entry : entries) {
            layout.setText(font, "t" + entry.tick() + "  " + entry.text());
            contentWidth = Math.max(contentWidth, layout.width);
        }

        float panelWidth = contentWidth + 2 * HudPanel.PADDING;
        float panelHeight = (entries.size() + 1) * lineHeight + 2 * HudPanel.PADDING;
        float panelX = MARGIN - HudPanel.PADDING;
        float panelBottomY = MARGIN - HudPanel.PADDING;
        HudPanel.draw(batch, icons.whitePixel(), panelX, panelBottomY, panelWidth, panelHeight);

        font.draw(batch, header, MARGIN, topY);
        float y = topY - lineHeight;
        for (EventLog.Entry entry : entries) {
            font.draw(batch, "t" + entry.tick() + "  " + entry.text(), MARGIN, y);
            y -= lineHeight;
        }
    }

    /** A yellow bracket frame around the selected actor's tile, when it is on-screen on z. */
    private void drawSelectionHighlight(SpriteBatch batch, BitmapFont font, MapCamera camera,
            InspectorState state, int z) {
        if (!state.hasSelection()) {
            return;
        }
        Actor actor = registry.get(state.selectedActorId());
        int cell = actor.cell();
        if (PackedPos.z(cell) != z) {
            return;
        }
        int tx = PackedPos.x(cell);
        int ty = PackedPos.y(cell);
        if (tx < camera.visibleTileMinX() || tx > camera.visibleTileMaxX()
                || ty < camera.visibleTileMinY() || ty > camera.visibleTileMaxY()) {
            return;
        }
        int span = camera.tileSpanPx();
        int screenXLeft = camera.tileToScreenX(tx);
        int screenYTopDown = camera.tileToScreenY(ty);
        float tileBottomYUp = camera.viewportHeightPx() - screenYTopDown - span;

        font.getData().setScale(camera.zoom());
        font.setColor(HIGHLIGHT_COLOR);
        drawBracket(batch, font, "[", screenXLeft, tileBottomYUp, span, true);
        drawBracket(batch, font, "]", screenXLeft, tileBottomYUp, span, false);
    }

    private void drawBracket(SpriteBatch batch, BitmapFont font, String bracket, int screenXLeft,
            float tileBottomYUp, int span, boolean leftEdge) {
        layout.setText(font, bracket);
        float drawX = leftEdge ? screenXLeft : screenXLeft + span - layout.width;
        float drawY = tileBottomYUp + (span + layout.height) / 2f;
        font.draw(batch, layout, drawX, drawY);
    }
}
