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
import com.trojia.client.hud.icons.IconTextLine;
import com.trojia.client.inspect.CharacterSheetText;
import com.trojia.client.inspect.EventLog;
import com.trojia.client.inspect.InspectorState;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.world.PackedPos;

import java.util.List;

/**
 * Draws the observer's inspector overlays over the world: the CHARACTER SHEET (top-right —
 * Sprint 1 "Click a person, meet a person": name + epithet header over the FaceGen
 * portrait, the bio line, then IDENTITY / NEEDS / SKILLS / TIES as separate DF panel
 * blocks), the population event feed (bottom-left), and a highlight frame around the
 * selected actor's tile. Text content comes from the GL-free
 * {@link CharacterSheetText}/{@link EventLog}; this class only lays the strings out and
 * draws them (the {@code HudText} split, applied to the sheet).
 *
 * <p><b>No staleness.</b> Sheet content is regenerated from live registry/side-table reads
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

    /** Vertical breathing room between two of the sheet's DF panel blocks, px. */
    private static final float SECTION_GAP_PX = 6f;
    /** Gap between the wrapped bio's last line and the header block's bottom edge, px. */
    private static final float BIO_GAP_PX = 4f;

    // Zelda-II-style segmented need bars (2026-07-15 stat-box redesign, design doc §3): 10
    // discrete square segments per need, each worth a fixed 1000 of the 0..10000 range —
    // "one segment = fixed point slice," mirroring the NES life meter's own square segments.
    private static final int NEED_SEGMENTS = 10;
    private static final int NEED_SEGMENT_VALUE = 10000 / NEED_SEGMENTS;
    private static final float NEED_SEGMENT_SIZE_PX = 14f;
    private static final float NEED_SEGMENT_GAP_PX = 2f;
    private static final float NEED_BAR_WIDTH_PX =
            NEED_SEGMENTS * NEED_SEGMENT_SIZE_PX + (NEED_SEGMENTS - 1) * NEED_SEGMENT_GAP_PX;
    private static final float NEED_LABEL_WIDTH_PX = 70f;
    private static final float NEED_VALUE_GAP_PX = 8f;
    private static final float NEED_ROW_GAP_PX = 4f;
    private static final float NEED_EMPTY_OUTLINE_PX = 1f;
    private static final Color NEED_EMPTY_OUTLINE_COLOR = new Color(0.35f, 0.35f, 0.38f, 1f);
    /** The dark interior the portrait sits on, so the gold border reads as a ring, not a fill
     * (FaceGen parts draw on transparency and have no background of their own). */
    private static final Color PORTRAIT_INTERIOR_COLOR = new Color(0.02f, 0.02f, 0.03f, 1f);
    /** One saturated hue per need, indexed by {@link Need#ordinal()}; COIN reuses the panel's
     *  existing gold highlight instead of adding a new constant. */
    private static final Color[] NEED_COLORS = {
            new Color(0.90f, 0.45f, 0.15f, 1f), // HUNGER: orange
            new Color(0.35f, 0.55f, 0.95f, 1f), // REST: blue
            HIGHLIGHT_COLOR,                    // COIN: gold
            new Color(0.35f, 0.80f, 0.40f, 1f), // SAFETY: green
            new Color(0.65f, 0.40f, 0.85f, 1f), // DUTY: purple
    };

    private final ActorRegistry registry;
    private final HomeRegistry homes;
    private final RelationshipRegistry relationships;
    private final JobRegistry jobs;
    private final ItemsLiteRegistry items;
    private final EventLog eventLog;
    private final InspectorFaces faces;
    /** The bake-side name table (S1 NameForge); {@link IdentityRegistry#EMPTY} degrades the
     *  sheet to the pre-names "#id" style, never fails. */
    private final IdentityRegistry identity;
    /** The Sim team's per-actor skill table; {@code UNWIRED} renders "(unschooled)". */
    private final SkillTrackRegistry skillTracks;
    /** The Sim team's standing ledger; {@code UNWIRED} renders the "(no ledgers)" line. */
    private final FactionStandings standings;
    private final GlyphLayout layout = new GlyphLayout();

    /**
     * @param faces the FaceGen portrait panel (unified art spec §4.8), or {@code null} to
     *              render the text-only panel (headless-ish callers, tests)
     */
    public InspectorRenderer(ActorRegistry registry, HomeRegistry homes,
            RelationshipRegistry relationships, JobRegistry jobs, ItemsLiteRegistry items,
            EventLog eventLog, InspectorFaces faces, IdentityRegistry identity,
            SkillTrackRegistry skillTracks, FactionStandings standings) {
        this.registry = registry;
        this.homes = homes;
        this.relationships = relationships;
        this.jobs = jobs;
        this.items = items;
        this.eventLog = eventLog;
        this.faces = faces;
        this.identity = identity;
        this.skillTracks = skillTracks;
        this.standings = standings;
    }

    /**
     * Draws the selection highlight, character sheet and event feed for the current
     * {@code state} and viewed z-level {@code z}. {@code font} is left at scale 1 / white
     * afterward so the caller's other font uses are unaffected.
     */
    public void draw(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera,
            InspectorState state, int z) {
        drawSelectionHighlight(batch, font, camera, state, z);
        drawSheet(batch, font, icons, camera, state);
        drawEventLog(batch, font, icons, camera);
        font.getData().setScale(1f);
        font.setColor(Color.WHITE);
    }

    private void drawSheet(SpriteBatch batch, BitmapFont font, IconAtlas icons,
            MapCamera camera, InspectorState state) {
        font.getData().setScale(1f);
        float lineHeight = font.getLineHeight();
        float x = camera.viewportWidthPx() - PANEL_WIDTH;
        float topY = camera.viewportHeightPx() - MARGIN;

        boolean showFollowBadge = state.followActive();
        if (!state.hasSelection()) {
            float contentHeight = lineHeight + (showFollowBadge ? lineHeight : 0f);
            drawBlockBackground(batch, icons, x, topY, contentHeight);
            float y = topY;
            if (showFollowBadge) {
                IconTextLine.draw(batch, font, icons, x, y,
                        CharacterSheetText.followBadgeTokens());
                y -= lineHeight;
            }
            IconTextLine.draw(batch, font, icons, x, y,
                    CharacterSheetText.selectionHintTokens());
            return;
        }

        int selectedId = state.selectedActorId();
        Actor selectedActor = registry.get(selectedId);
        // Play mode (PLAY-MODE-SPEC.md §5.3): the header band — portrait, name, bio — is
        // the PRESENTED identity, so the sheet visibly becomes the impersonated soul (the
        // same Bledhreft/Senator-Harris canon example, made observable end to end). The
        // IDENTITY section below stays the omniscient truth.
        Actor presentedActor = selectedActor.identity().isDisguised()
                ? registry.get(selectedActor.identity().presentedId())
                : selectedActor;
        String typeKey = presentedActor.typeId().key();
        boolean showFace = faces != null && faces.hasFaceFor(typeKey);

        String name = CharacterSheetText.nameLine(selectedId, registry, identity);
        String bio = CharacterSheetText.bioLine(selectedId, registry, identity);
        CharacterSheetText.Section identitySection =
                CharacterSheetText.identitySection(selectedId, registry, homes, jobs, items);
        CharacterSheetText.Section skillsSection =
                CharacterSheetText.skillsSection(selectedId, skillTracks);
        CharacterSheetText.Section standingsSection =
                CharacterSheetText.standingsSection(selectedId, registry, standings);
        CharacterSheetText.Section tiesSection = CharacterSheetText.tiesSection(selectedId,
                registry, relationships, jobs, identity);

        // ---- header block: [follow badge] + name + portrait + wrapped bio --------------
        float bioHeight = 0f;
        if (!bio.isBlank()) {
            layout.setText(font, bio, PANEL_COLOR, PANEL_WIDTH, Align.left, true);
            bioHeight = layout.height + BIO_GAP_PX;
        }
        float headerContent = (showFollowBadge ? lineHeight : 0f) + lineHeight
                + (showFace ? InspectorFaces.PANEL_SHIFT_PX : 0f) + bioHeight;
        drawBlockBackground(batch, icons, x, topY, headerContent);
        float y = topY;
        if (showFollowBadge) {
            IconTextLine.draw(batch, font, icons, x, y, CharacterSheetText.followBadgeTokens());
            y -= lineHeight;
        }
        font.setColor(HIGHLIGHT_COLOR);
        font.draw(batch, name, x, y);
        y -= lineHeight;
        if (showFace) {
            drawPortrait(batch, icons, presentedActor, typeKey, x, y);
            y -= InspectorFaces.PANEL_SHIFT_PX;
        }
        if (!bio.isBlank()) {
            font.setColor(PANEL_COLOR);
            font.draw(batch, bio, x, y, PANEL_WIDTH, Align.left, true);
            y -= bioHeight;
        }
        float nextTop = nextBlockTop(topY, headerContent);

        // ---- the five sheet sections, each its own DF block -----------------------------
        nextTop = drawSection(batch, font, icons, x, nextTop, identitySection);
        nextTop = drawNeedsSection(batch, font, icons, x, nextTop, selectedActor);
        nextTop = drawSection(batch, font, icons, x, nextTop, skillsSection);
        nextTop = drawSection(batch, font, icons, x, nextTop, standingsSection);
        drawSection(batch, font, icons, x, nextTop, tiesSection);
    }

    /**
     * FaceGen portrait (unified art spec §4.8): 48x48 at x4, ringed with a gold
     * Zelda-II-style border frame, centered in the panel column; {@code contentTopY} is the
     * y the portrait's top edge hangs from. Beast types have no archetype mapping and the
     * caller skips this entirely (text-only sheet).
     */
    private void drawPortrait(SpriteBatch batch, IconAtlas icons, Actor presentedActor,
            String typeKey, float x, float contentTopY) {
        float centerX = x + PANEL_WIDTH / 2f;
        float borderSize = InspectorFaces.FACE_SIZE_PX + 2 * InspectorFaces.PORTRAIT_BORDER_PX;
        float borderX = centerX - InspectorFaces.FACE_SIZE_PX / 2f
                - InspectorFaces.PORTRAIT_BORDER_PX;
        float borderY = contentTopY - InspectorFaces.FACE_SIZE_PX
                - InspectorFaces.PORTRAIT_BORDER_PX;
        HudPanel.draw(batch, icons.whitePixel(), borderX, borderY, borderSize, borderSize,
                HIGHLIGHT_COLOR);
        // Dark interior inset by the border width, so the gold reads as a ring around the
        // portrait rather than a solid backdrop the transparent face parts sit on top of.
        float interiorX = centerX - InspectorFaces.FACE_SIZE_PX / 2f;
        float interiorY = contentTopY - InspectorFaces.FACE_SIZE_PX;
        HudPanel.draw(batch, icons.whitePixel(), interiorX, interiorY,
                InspectorFaces.FACE_SIZE_PX, InspectorFaces.FACE_SIZE_PX,
                PORTRAIT_INTERIOR_COLOR);
        faces.draw(batch, presentedActor.id(), typeKey, centerX, contentTopY);
    }

    /**
     * One titled sheet section as its own DF block: gold title line, then the section's
     * lines. Returns the content-top y for the block below it.
     */
    private float drawSection(SpriteBatch batch, BitmapFont font, IconAtlas icons, float x,
            float contentTopY, CharacterSheetText.Section section) {
        float lineHeight = font.getLineHeight();
        float contentHeight = (1 + section.lines().size()) * lineHeight;
        drawBlockBackground(batch, icons, x, contentTopY, contentHeight);
        float y = contentTopY;
        font.setColor(HIGHLIGHT_COLOR);
        font.draw(batch, section.title(), x, y);
        y -= lineHeight;
        font.setColor(PANEL_COLOR);
        for (String line : section.lines()) {
            font.draw(batch, line, x, y);
            y -= lineHeight;
        }
        return nextBlockTop(contentTopY, contentHeight);
    }

    /** The NEEDS section block: gold title, then the Zelda-II segmented bar grid. */
    private float drawNeedsSection(SpriteBatch batch, BitmapFont font, IconAtlas icons,
            float x, float contentTopY, Actor selectedActor) {
        float lineHeight = font.getLineHeight();
        float contentHeight = lineHeight + needsGridHeight(font);
        drawBlockBackground(batch, icons, x, contentTopY, contentHeight);
        font.setColor(HIGHLIGHT_COLOR);
        font.draw(batch, "NEEDS", x, contentTopY);
        // Bars read from the true selected actor's live needs — same actor the IDENTITY
        // section describes (a disguise changes the header, never the body's needs).
        drawNeedsBarGrid(batch, font, icons, x, contentTopY - lineHeight, selectedActor);
        return nextBlockTop(contentTopY, contentHeight);
    }

    /** The near-black DF backing block for one sheet section's content. */
    private void drawBlockBackground(SpriteBatch batch, IconAtlas icons, float x,
            float contentTopY, float contentHeight) {
        HudPanel.draw(batch, icons.whitePixel(), x - HudPanel.PADDING,
                contentTopY - contentHeight - HudPanel.PADDING,
                PANEL_WIDTH + 2 * HudPanel.PADDING, contentHeight + 2 * HudPanel.PADDING);
    }

    /** Where the next block's CONTENT starts, below a block of {@code contentHeight}. */
    private static float nextBlockTop(float contentTopY, float contentHeight) {
        return contentTopY - contentHeight - 2 * HudPanel.PADDING - SECTION_GAP_PX;
    }

    /** Row height for one need's bar: tall enough for both the 14px segments and the font. */
    private static float needRowHeight(BitmapFont font) {
        return Math.max(font.getLineHeight(), NEED_SEGMENT_SIZE_PX) + NEED_ROW_GAP_PX;
    }

    /** Total height of the five-row need bar grid, for the section's content-height tally. */
    private static float needsGridHeight(BitmapFont font) {
        return Need.COUNT * needRowHeight(font);
    }

    /**
     * Draws the five-row Zelda-II-style stat box: LABEL &rarr; 10-segment bar (each segment
     * worth 1000 of the 0..10000 need range, filled left-to-right, empty slots outlined so
     * they still read as slots) &rarr; exact numeric value, one row per {@link Need}, top
     * edge at {@code topY}.
     */
    private void drawNeedsBarGrid(SpriteBatch batch, BitmapFont font, IconAtlas icons, float x,
            float topY, Actor actor) {
        short[] needs = actor.needsSnapshot();
        float rowHeight = needRowHeight(font);
        float barLeft = x + NEED_LABEL_WIDTH_PX;
        float y = topY;
        for (int i = 0; i < Need.COUNT; i++) {
            short value = needs[i];
            int filled = Math.max(0, Math.min(NEED_SEGMENTS, value / NEED_SEGMENT_VALUE));
            float segBottomY = y - NEED_SEGMENT_SIZE_PX;

            font.setColor(PANEL_COLOR);
            font.draw(batch, CharacterSheetText.NEED_LABELS[i], x, y);

            for (int s = 0; s < NEED_SEGMENTS; s++) {
                float segX = barLeft + s * (NEED_SEGMENT_SIZE_PX + NEED_SEGMENT_GAP_PX);
                if (s < filled) {
                    HudPanel.draw(batch, icons.whitePixel(), segX, segBottomY, NEED_SEGMENT_SIZE_PX,
                            NEED_SEGMENT_SIZE_PX, NEED_COLORS[i]);
                } else {
                    HudPanel.draw(batch, icons.whitePixel(), segX - NEED_EMPTY_OUTLINE_PX,
                            segBottomY - NEED_EMPTY_OUTLINE_PX,
                            NEED_SEGMENT_SIZE_PX + 2 * NEED_EMPTY_OUTLINE_PX,
                            NEED_SEGMENT_SIZE_PX + 2 * NEED_EMPTY_OUTLINE_PX, NEED_EMPTY_OUTLINE_COLOR);
                    HudPanel.draw(batch, icons.whitePixel(), segX, segBottomY, NEED_SEGMENT_SIZE_PX,
                            NEED_SEGMENT_SIZE_PX, HudPanel.BACKGROUND);
                }
            }

            font.setColor(PANEL_COLOR);
            font.draw(batch, String.format("%5d", value), barLeft + NEED_BAR_WIDTH_PX + NEED_VALUE_GAP_PX, y);

            y -= rowHeight;
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

        font.setColor(LOG_COLOR);
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
