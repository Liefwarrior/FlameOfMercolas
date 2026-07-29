package com.trojia.client.render;

import java.util.ArrayList;
import java.util.List;

/**
 * The LOOK of the building labels, as pure geometry (2026-07-28, Eli's art ruling, verbatim:
 * "NES style black box pop ups with signs (just like in zelda 2 or in early final fantasy
 * games)"). GL-free on purpose — the same quad list this class emits is what
 * {@link PlaceSignRenderer} hands to the batch and what the headless raster proof paints, so
 * a test can check the pixels and not merely the logic.
 *
 * <p><b>The box, honoured rather than reinterpreted.</b> A hard-edged filled BLACK rectangle
 * with a crisp light border: no rounded corners, no drop shadow, no translucency, no fade. It
 * snaps on and snaps off. The fill is pure {@code #000000} — the project's own art register
 * calls for true black voids (DECISIONS.md, art register row), so the box is the same black
 * the unbuilt world is, and the border is the only thing separating it from the night. The
 * border is a solid {@value #BORDER_PX}px ring drawn as four rects, which is exactly how a
 * 1988 cartridge would have done it: no gradient, no inner bevel, one ink colour.
 *
 * <p><b>The hanging sign</b> is the persistent half, so a player panning the ward can see a
 * place is named before any box appears: a bracket arm off the doorframe, a short hanger, and
 * a small plaque swinging under it — the same black-with-bone-border ink as the box, one tile
 * wide, so a shop sign and its pop-up read as the same object at two sizes. It is a MARK, not
 * text; forty of them are a legend, not a crowd. When a sign is the one the box is describing
 * it lights to the gold the inspector already uses for a highlight, which is how a reader
 * knows which door the words belong to without a speech tail (there were no tails on the NES).
 *
 * <p>Everything is measured in fractions of the tile span and floored at one pixel, so the
 * sign still resolves at the widest zoom ({@code MapCamera.MIN_ZOOM} = 16px per tile) instead
 * of collapsing to a smear.
 *
 * <p>Deterministic, allocation-bounded, headless. No colour here is ever multiplied into the
 * world — depth shading is applied by the caller.
 */
public final class PlaceSignArt {

    /** One flat rectangle of one colour: the only primitive either drawer needs. */
    public record Quad(float x, float y, float w, float h, float r, float g, float b) {
    }

    // ---- ink ---------------------------------------------------------------------------

    /** The void the box is cut from: true black, fully opaque, never translucent. */
    public static final float[] INK_BLACK = {0f, 0f, 0f};
    /** The border and the sign's plaque edge: bone white, one flat tone. */
    public static final float[] INK_BONE = {0.90f, 0.87f, 0.76f};
    /** The lit state of the sign the box is currently naming (the inspector's own gold). */
    public static final float[] INK_GOLD = {1f, 0.84f, 0.40f};

    // ---- the box -----------------------------------------------------------------------

    /** Border thickness of the pop-up, px. Two is crisp at 1x and still hard-edged. */
    public static final int BORDER_PX = 2;
    /** Space between the border and the text block, px, left/right. */
    public static final float PAD_X = 8f;
    /** Space between the border and the text block, px, top/bottom. */
    public static final float PAD_Y = 6f;
    /** Gap between the anchor tile's top edge and the box's bottom edge, px. */
    public static final float LIFT_PX = 10f;
    /** Keep-out margin from the viewport edge when the box is clamped on screen, px. */
    public static final float SCREEN_MARGIN_PX = 6f;

    // ---- the hanging sign (fractions of one tile span) ----------------------------------

    private static final float ARM_X0 = 0.10f;
    private static final float ARM_X1 = 0.52f;
    private static final float ARM_Y = 0.80f;
    private static final float ARM_WEIGHT = 0.06f;
    private static final float HANGER_X = 0.46f;
    private static final float HANGER_Y0 = 0.66f;
    private static final float PLAQUE_X0 = 0.16f;
    private static final float PLAQUE_X1 = 0.84f;
    private static final float PLAQUE_Y0 = 0.28f;
    private static final float PLAQUE_Y1 = 0.68f;
    private static final float PLAQUE_EDGE = 0.06f;

    private PlaceSignArt() {
    }

    /**
     * The pop-up's outer width for a two-line block: the wider line plus padding and both
     * borders.
     *
     * @param textWidthPx the widest of the two rendered lines
     */
    public static float boxWidth(float textWidthPx) {
        return textWidthPx + 2 * PAD_X + 2 * BORDER_PX;
    }

    /** The pop-up's outer height for a two-line block. */
    public static float boxHeight(float lineHeightPx) {
        return 2 * lineHeightPx + 2 * PAD_Y + 2 * BORDER_PX;
    }

    /**
     * Clamps a desired box position fully inside a viewport, leaving
     * {@value #SCREEN_MARGIN_PX}px of margin — the box never hangs half off the edge when
     * the named place sits at the screen border. Bottom-left origin, y-up.
     *
     * @return the clamped {@code {x, y}}
     */
    public static float[] clampToViewport(float x, float y, float w, float h,
            float viewportW, float viewportH) {
        float maxX = viewportW - w - SCREEN_MARGIN_PX;
        float maxY = viewportH - h - SCREEN_MARGIN_PX;
        float cx = maxX < SCREEN_MARGIN_PX ? SCREEN_MARGIN_PX
                : Math.min(Math.max(x, SCREEN_MARGIN_PX), maxX);
        float cy = maxY < SCREEN_MARGIN_PX ? SCREEN_MARGIN_PX
                : Math.min(Math.max(y, SCREEN_MARGIN_PX), maxY);
        return new float[] {cx, cy};
    }

    /**
     * The pop-up as five quads: the black field, then the four border rails. Drawn in that
     * order so the border is never eaten by the fill. Bottom-left origin, y-up.
     *
     * @param borderInk the border colour (already depth-shaded by the caller, if it must be)
     */
    public static List<Quad> box(float x, float y, float w, float h, float[] borderInk) {
        List<Quad> quads = new ArrayList<>(5);
        quads.add(quad(x, y, w, h, INK_BLACK));
        quads.add(quad(x, y + h - BORDER_PX, w, BORDER_PX, borderInk));          // top
        quads.add(quad(x, y, w, BORDER_PX, borderInk));                          // bottom
        quads.add(quad(x, y + BORDER_PX, BORDER_PX, h - 2 * BORDER_PX, borderInk));   // left
        quads.add(quad(x + w - BORDER_PX, y + BORDER_PX, BORDER_PX, h - 2 * BORDER_PX,
                borderInk));                                                     // right
        return quads;
    }

    /**
     * Top y (y-up) of the pop-up's first text line — the y {@code BitmapFont.draw} takes,
     * matching {@code InspectorRenderer}'s own top-down line walk.
     */
    public static float firstLineTopY(float boxY, float boxH) {
        return boxY + boxH - BORDER_PX - PAD_Y;
    }

    /** Left x of the pop-up's text block. */
    public static float textLeftX(float boxX) {
        return boxX + BORDER_PX + PAD_X;
    }

    /**
     * One hanging shop sign over a door cell, as quads: bracket arm, hanger, plaque edge,
     * plaque field. Bottom-left origin, y-up, {@code (left, bottom)} being the door tile's
     * own screen corner.
     *
     * @param named {@code true} when this is the sign the frame's box is describing — the
     *              plaque edge lights gold and the arm goes with it
     * @param ink   the base ink for the unlit state (depth-shaded bone, from the caller)
     */
    public static List<Quad> hangingSign(float left, float bottom, float span, boolean named,
            float[] ink) {
        float[] edge = named ? INK_GOLD : ink;
        float weight = Math.max(1f, span * ARM_WEIGHT);
        float plaqueEdge = Math.max(1f, span * PLAQUE_EDGE);
        float px0 = left + span * PLAQUE_X0;
        float py0 = bottom + span * PLAQUE_Y0;
        float pw = span * (PLAQUE_X1 - PLAQUE_X0);
        float ph = span * (PLAQUE_Y1 - PLAQUE_Y0);

        List<Quad> quads = new ArrayList<>(6);
        // The bracket arm out of the doorframe, and the short hanger it swings from.
        quads.add(quad(left + span * ARM_X0, bottom + span * ARM_Y,
                span * (ARM_X1 - ARM_X0), weight, edge));
        quads.add(quad(left + span * HANGER_X, bottom + span * HANGER_Y0,
                weight, span * (ARM_Y - HANGER_Y0), edge));
        // The plaque: bone edge, black field — the pop-up in miniature.
        quads.add(quad(px0, py0, pw, ph, edge));
        quads.add(quad(px0 + plaqueEdge, py0 + plaqueEdge,
                pw - 2 * plaqueEdge, ph - 2 * plaqueEdge, INK_BLACK));
        return quads;
    }

    private static Quad quad(float x, float y, float w, float h, float[] ink) {
        return new Quad(x, y, w, h, ink[0], ink[1], ink[2]);
    }
}
