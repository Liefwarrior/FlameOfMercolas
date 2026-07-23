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
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.world.PackedPos;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Hover nameplates (Sprint 1 item 2, the Morrowind hover-name — "everyone is somebody"):
 * a small DF-black plate above the tile under the cursor listing every soul standing
 * there ({@link NameplateText}'s PRESENTED-identity labels, in the same ascending-id
 * order {@link ActorRenderer}'s sprite stack cascades), plus a hold-{@code N} mode that
 * plates every actor on screen. Label CONTENT lives in the GL-free {@link NameplateText};
 * this class only measures, backs and draws it.
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

    private final ActorRegistry registry;
    private final JobRegistry jobs;
    private final IdentityRegistry identity;
    private final GlyphLayout layout = new GlyphLayout();
    /** Show-all per-frame cell suppression (cleared each draw; a stacked cell renders ONE
     *  cascading plate) — reused across frames so steady-state allocates nothing. */
    private final Set<Integer> platedCells = new HashSet<>();

    public NameplateRenderer(ActorRegistry registry, JobRegistry jobs,
            IdentityRegistry identity) {
        this.registry = registry;
        this.jobs = jobs;
        this.identity = identity;
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
        List<String> labels = NameplateText.labelsAt(tileX, tileY, z, registry, jobs, identity);
        if (!labels.isEmpty()) {
            drawPlate(batch, font, icons, camera, tileX, tileY, labels);
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
            drawPlate(batch, font, icons, camera, tx, ty,
                    NameplateText.labelsAt(tx, ty, z, registry, jobs, identity));
        }
    }

    /** One DF-black plate whose bottom edge floats just above the tile's top edge. */
    private void drawPlate(SpriteBatch batch, BitmapFont font, IconAtlas icons,
            MapCamera camera, int tileX, int tileY, List<String> labels) {
        float lineHeight = font.getLineHeight();
        float contentWidth = 0f;
        for (String label : labels) {
            layout.setText(font, label);
            contentWidth = Math.max(contentWidth, layout.width);
        }
        int screenXLeft = camera.tileToScreenX(tileX);
        int screenYTopDown = camera.tileToScreenY(tileY);
        float tileTopYUp = camera.viewportHeightPx() - screenYTopDown;

        float panelBottomY = tileTopYUp + PLATE_LIFT_PX;
        float panelHeight = labels.size() * lineHeight + 2 * HudPanel.PADDING;
        HudPanel.draw(batch, icons.whitePixel(), screenXLeft - HudPanel.PADDING, panelBottomY,
                contentWidth + 2 * HudPanel.PADDING, panelHeight);

        font.setColor(PLATE_COLOR);
        float y = panelBottomY + panelHeight - HudPanel.PADDING;
        for (String label : labels) {
            font.draw(batch, label, screenXLeft, y);
            y -= lineHeight;
        }
    }
}
