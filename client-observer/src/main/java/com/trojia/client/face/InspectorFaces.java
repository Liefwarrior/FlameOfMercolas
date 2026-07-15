package com.trojia.client.face;

import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.trojia.client.face.FaceComposition.PlacedPart;
import com.trojia.client.sprite.SpriteIndex;
import com.trojia.client.sprite.SpriteSheet;

import java.util.Objects;

/**
 * The GL half of the inspector face panel (unified art spec §4.8): draws a
 * {@link FaceGen} composition — an ordered bottom&rarr;top part list — from the shared
 * unified {@link SpriteSheet} (actors + face parts; NON-owning — {@code ObserverApp}
 * creates and disposes that sheet exactly once) at integer scale {@value #SCALE}
 * (48&times;48 &rarr;
 * {@value #FACE_SIZE_PX}&times;{@value #FACE_SIZE_PX} px, placeholder pending bless). No
 * offscreen compositing, no projection features: the batch draws each part quad in list
 * order, untinted (parts are pre-colored in MERCOLAS-24).
 *
 * <p>Beast types ({@code animal}, {@code feral}) have no archetype mapping and therefore
 * no face — {@link #hasFaceFor} is the caller's gate. Compositions are cached by actor id
 * ({@code worldSeed} is fixed for the app's life); the cache is an optimization, never a
 * source of truth, and never serialized (FACES-SPEC §5, retained).
 */
public final class InspectorFaces {

    /**
     * Integer on-screen scale for the inspector portrait (Zelda II stat-box redesign,
     * 2026-07-15: bumped from the original placeholder 2x to 4x per Eli's "faces should be
     * a lot bigger" directive). Must stay an integer — TILE-ART-SPEC.md §4 mandates
     * nearest-filtering/integer-only zoom for this 16px-grid pixel art.
     */
    public static final int SCALE = 4;

    /** On-screen portrait edge, px. */
    public static final int FACE_SIZE_PX = FaceComposition.CANVAS_PX * SCALE;

    /** Gold portrait border frame thickness, px (Zelda II "bordered box" convention). */
    public static final int PORTRAIT_BORDER_PX = 3;

    /** Gap between the portrait border and the text block beneath it, px. */
    private static final int PORTRAIT_GAP_PX = 10;

    /**
     * How far the inspector panel's text shifts down under the portrait: the portrait
     * itself, plus its border frame on both edges, plus a breathing gap.
     */
    public static final int PANEL_SHIFT_PX =
            FACE_SIZE_PX + 2 * PORTRAIT_BORDER_PX + PORTRAIT_GAP_PX;

    private final FaceGen gen;
    private final FaceArchetypes archetypes;
    private final SpriteSheet sheet;
    private final int tilePx;
    private final long worldSeed;

    private long cachedActorId = Long.MIN_VALUE;
    private FaceComposition cachedComposition;

    public InspectorFaces(FaceGen gen, FaceArchetypes archetypes, SpriteIndex index,
                          SpriteSheet sheet, long worldSeed) {
        this.gen = Objects.requireNonNull(gen, "gen");
        this.archetypes = Objects.requireNonNull(archetypes, "archetypes");
        this.sheet = Objects.requireNonNull(sheet, "sheet");
        this.tilePx = index.tilePx();
        this.worldSeed = worldSeed;
    }

    /** Whether this actor type gets a generated face (beasts do not — spec §4.6). */
    public boolean hasFaceFor(String actorTypeId) {
        return archetypes.archetypeForActorType(actorTypeId) != null;
    }

    /**
     * Draws the actor's portrait with its top edge at {@code topY} (y-up screen space),
     * horizontally centered on {@code centerX}. Caller owns {@code batch} begin/end;
     * batch color is left white. Call only when {@link #hasFaceFor} is true.
     */
    public void draw(SpriteBatch batch, int actorId, String actorTypeId,
                     float centerX, float topY) {
        String archetypeId = archetypes.archetypeForActorType(actorTypeId);
        if (archetypeId == null) {
            throw new IllegalArgumentException(
                    "actor type \"" + actorTypeId + "\" has no face archetype");
        }
        if (cachedActorId != actorId || cachedComposition == null) {
            cachedComposition = gen.compose(worldSeed, actorId, archetypeId);
            cachedActorId = actorId;
        }
        float left = centerX - FACE_SIZE_PX / 2;
        batch.setColor(Color.WHITE);   // parts are pre-colored; never multiply a tint
        for (PlacedPart placed : cachedComposition.parts()) {
            int wPx = placed.part().cellsW() * tilePx;
            int hPx = placed.part().cellsH() * tilePx;
            // Composition anchors are top-down canvas px; the batch is y-up.
            float x = left + placed.x() * SCALE;
            float y = topY - (placed.y() + hPx) * SCALE;
            batch.draw(sheet.region(placed.part()), x, y, wPx * SCALE, hPx * SCALE);
        }
    }
}
