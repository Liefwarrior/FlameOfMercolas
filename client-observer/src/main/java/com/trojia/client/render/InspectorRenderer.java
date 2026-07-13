package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.trojia.client.camera.MapCamera;
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
    private final GlyphLayout layout = new GlyphLayout();

    public InspectorRenderer(ActorRegistry registry, HomeRegistry homes,
            RelationshipRegistry relationships, JobRegistry jobs, ItemsLiteRegistry items,
            EventLog eventLog) {
        this.registry = registry;
        this.homes = homes;
        this.relationships = relationships;
        this.jobs = jobs;
        this.items = items;
        this.eventLog = eventLog;
    }

    /**
     * Draws the selection highlight, side panel and event feed for the current
     * {@code state} and viewed z-level {@code z}. {@code font} is left at scale 1 / white
     * afterward so the caller's other font uses are unaffected.
     */
    public void draw(SpriteBatch batch, BitmapFont font, MapCamera camera, InspectorState state, int z) {
        drawSelectionHighlight(batch, font, camera, state, z);
        drawPanel(batch, font, camera, state);
        drawEventLog(batch, font, camera);
        font.getData().setScale(1f);
        font.setColor(Color.WHITE);
    }

    private void drawPanel(SpriteBatch batch, BitmapFont font, MapCamera camera, InspectorState state) {
        List<String> lines = InspectorText.describe(state.selectedActorId(), registry, homes,
                relationships, jobs, items);
        font.getData().setScale(1f);
        font.setColor(PANEL_COLOR);
        float lineHeight = font.getLineHeight();
        float x = camera.viewportWidthPx() - PANEL_WIDTH;
        float y = camera.viewportHeightPx() - MARGIN;
        if (state.followActive()) {
            font.draw(batch, "[FOLLOW]  C to release", x, y);
            y -= lineHeight;
        }
        for (String line : lines) {
            font.draw(batch, line, x, y);
            y -= lineHeight;
        }
    }

    private void drawEventLog(SpriteBatch batch, BitmapFont font, MapCamera camera) {
        List<EventLog.Entry> entries = eventLog.recentNewestFirst(LOG_VISIBLE_LINES);
        font.getData().setScale(1f);
        font.setColor(LOG_COLOR);
        float lineHeight = font.getLineHeight();
        float topY = MARGIN + (entries.size() + 1) * lineHeight;
        font.draw(batch, "EVENTS (newest first)  ·  " + eventLog.size() + " held", MARGIN, topY);
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
