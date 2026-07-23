package com.trojia.client.render;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.trojia.client.camera.MapCamera;
import com.trojia.client.hud.HudPanel;
import com.trojia.client.hud.icons.IconAtlas;
import com.trojia.client.inspect.NameplateText;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.world.PackedPos;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.function.IntSupplier;

/**
 * Hover nameplates (Sprint 1 item 2, the Morrowind hover-name — "everyone is somebody"):
 * a small DF-black plate above the tile under the cursor listing every soul standing
 * there ({@link NameplateText}'s PRESENTED-identity labels, in the same ascending-id
 * order {@link ActorRenderer}'s sprite stack cascades), plus a hold-{@code N} mode that
 * plates every actor on screen. Label CONTENT lives in the GL-free {@link NameplateText};
 * this class only measures, backs and draws it.
 *
 * <p><b>Standing tint (Sprint 2 item 2).</b> While an actor is PLAYED, each label tints by
 * how that soul regards the played actor's presented face —
 * {@link NameplateText#attitudeToward}'s token mapped to a subtle text color (kin/friend
 * gold, warm green, cold steel, hostile red). Outside Play mode every plate stays the
 * plain parchment.
 *
 * <p>Hover-only by default keeps crowds legible (the sprint plan's clutter mitigation);
 * show-all is deliberately a held key, not a toggle, so it can never be left on by
 * accident.
 */
public final class NameplateRenderer {

    /** Nameplate text: warm parchment-ish white, distinct from the sheet's cooler panel. */
    private static final Color PLATE_COLOR = new Color(0.95f, 0.92f, 0.80f, 1f);
    /** Gap between the tile's top edge and the plate's backing block, px. */
    private static final float PLATE_LIFT_PX = 3f;

    // ---- the standing tints (subtle: text color only, the plate stays DF-black) ----
    private static final Color TINT_KIN = new Color(1f, 0.84f, 0.40f, 1f);
    private static final Color TINT_WARM = new Color(0.60f, 0.88f, 0.60f, 1f);
    private static final Color TINT_COLD = new Color(0.58f, 0.68f, 0.82f, 1f);
    private static final Color TINT_HOSTILE = new Color(0.95f, 0.38f, 0.32f, 1f);

    private final ActorRegistry registry;
    private final JobRegistry jobs;
    private final IdentityRegistry identity;
    private final FactionStandings standings;
    private final RelationshipRegistry relationships;
    /** Live "who is played" read for the standing tint; {@code Actor.NONE} = no tint. */
    private final IntSupplier playedActorId;
    private final GlyphLayout layout = new GlyphLayout();
    /** Show-all per-frame cell suppression (cleared each draw; a stacked cell renders ONE
     *  cascading plate) — reused across frames so steady-state allocates nothing. */
    private final Set<Integer> platedCells = new HashSet<>();

    public NameplateRenderer(ActorRegistry registry, JobRegistry jobs,
            IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, IntSupplier playedActorId) {
        this.registry = registry;
        this.jobs = jobs;
        this.identity = identity;
        this.standings = standings;
        this.relationships = relationships;
        this.playedActorId = playedActorId;
    }

    /**
     * Draws this frame's nameplates on z-level {@code z}: the plate for the hovered tile
     * (cursor at top-down window px {@code (mouseX, mouseY)}, the same screen space
     * {@code InspectorInput} picks in), or — while {@code showAll} (the held {@code N}
     * key) — one plate per occupied on-screen tile. {@code font} is left white at scale 1.
     */
    public void draw(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera,
            int z, int mouseX, int mouseY, boolean showAll) {
        font.getData().setScale(1f);
        if (showAll) {
            drawAll(batch, font, icons, camera, z);
        } else {
            drawHover(batch, font, icons, camera, z, mouseX, mouseY);
        }
        font.setColor(Color.WHITE);
    }

    private void drawHover(SpriteBatch batch, BitmapFont font, IconAtlas icons,
            MapCamera camera, int z, int mouseX, int mouseY) {
        int tileX = camera.screenToTileX(mouseX);
        int tileY = camera.screenToTileY(mouseY);
        if (!camera.isInWorld(tileX, tileY)) {
            return;
        }
        List<NameplateText.Plate> plates = platesAt(tileX, tileY, z);
        if (!plates.isEmpty()) {
            drawPlate(batch, font, icons, camera, tileX, tileY, plates);
        }
    }

    private void drawAll(SpriteBatch batch, BitmapFont font, IconAtlas icons, MapCamera camera,
            int z) {
        platedCells.clear();
        for (int i = 0; i < registry.size(); i++) {
            int cell = registry.get(i).cell();
            if (PackedPos.z(cell) != z) {
                continue;
            }
            int tx = PackedPos.x(cell);
            int ty = PackedPos.y(cell);
            if (tx < camera.visibleTileMinX() || tx > camera.visibleTileMaxX()
                    || ty < camera.visibleTileMinY() || ty > camera.visibleTileMaxY()) {
                continue;
            }
            if (!platedCells.add(cell)) {
                continue; // this cell's whole stack already rendered on its first actor
            }
            drawPlate(batch, font, icons, camera, tx, ty, platesAt(tx, ty, z));
        }
    }

    private List<NameplateText.Plate> platesAt(int tileX, int tileY, int z) {
        return NameplateText.platesAt(tileX, tileY, z, registry, jobs, identity, standings,
                relationships, playedActorId.getAsInt());
    }

    /** One DF-black plate whose bottom edge floats just above the tile's top edge. */
    private void drawPlate(SpriteBatch batch, BitmapFont font, IconAtlas icons,
            MapCamera camera, int tileX, int tileY, List<NameplateText.Plate> plates) {
        float lineHeight = font.getLineHeight();
        float contentWidth = 0f;
        for (NameplateText.Plate plate : plates) {
            layout.setText(font, plate.label());
            contentWidth = Math.max(contentWidth, layout.width);
        }
        int screenXLeft = camera.tileToScreenX(tileX);
        int screenYTopDown = camera.tileToScreenY(tileY);
        float tileTopYUp = camera.viewportHeightPx() - screenYTopDown;

        float panelBottomY = tileTopYUp + PLATE_LIFT_PX;
        float panelHeight = plates.size() * lineHeight + 2 * HudPanel.PADDING;
        HudPanel.draw(batch, icons.whitePixel(), screenXLeft - HudPanel.PADDING, panelBottomY,
                contentWidth + 2 * HudPanel.PADDING, panelHeight);

        float y = panelBottomY + panelHeight - HudPanel.PADDING;
        for (NameplateText.Plate plate : plates) {
            font.setColor(tintFor(plate.attitude()));
            font.draw(batch, plate.label(), screenXLeft, y);
            y -= lineHeight;
        }
    }

    /** The subtle standing tint per attitude token; parchment when untinted/neutral. */
    static Color tintFor(String attitude) {
        if (attitude == null) {
            return PLATE_COLOR;
        }
        return switch (attitude) {
            case "kin", "friend" -> TINT_KIN;
            case "warm" -> TINT_WARM;
            case "cold" -> TINT_COLD;
            case "hostile" -> TINT_HOSTILE;
            default -> PLATE_COLOR;
        };
    }

    /** The no-viewer sentinel a caller without Play mode passes. */
    public static IntSupplier noViewer() {
        return () -> Actor.NONE;
    }
}
