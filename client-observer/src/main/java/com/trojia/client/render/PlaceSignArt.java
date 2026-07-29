package com.trojia.client.render;

import com.trojia.client.atlas.Glyph8x8Font;

import java.util.ArrayList;
import java.util.List;

/**
 * The LOOK of the place labels, as pure geometry (2026-07-28, Eli's art ruling, verbatim:
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
 * <p><b>The letters are BLOCKY, and they are the same letters the proof paints</b> (S8 round 2,
 * Eli: the ruling asked for Zelda II / early Final Fantasy text and the box was shipping
 * libGDX's anti-aliased default while every proof PNG was painted with the repo's 8&times;8
 * font — "the images that proved the look are not what the game draws"). Text is now emitted
 * HERE, as one quad per horizontal run of ink in the repo's built-in {@link Glyph8x8Font},
 * scaled {@value #TEXT_SCALE}&times; with no filtering: hard square pixels, no anti-aliasing,
 * no hinting, nothing to smear at any zoom. The renderer and the raster call the same method,
 * so the proof and the frame cannot drift apart again.
 *
 * <p><b>Two marks, because a way is not a building.</b> {@link #hangingSign} is a shop's
 * plaque: a bracket arm off the doorframe, a short hanger and a small plaque swinging under
 * it. {@link #wayPost} is a street's fingerpost: a post planted in the kerb with a name-board
 * across its head. Same ink and same black-and-bone construction — one tile wide, so a mark
 * and its pop-up read as the same object at two sizes — but distinct silhouettes, so a player
 * can tell "somewhere you go IN" from "somewhere you go ALONG" without reading a word. Both
 * are MARKS, not text; sixty of them are a legend, not a crowd. The one the box is currently
 * describing lights to {@link #INK_LIT}, which is how a reader knows which mark the words
 * belong to without a speech tail (there were no tails on the NES).
 *
 * <p>Everything is measured in fractions of the tile span and floored at one pixel, so the
 * mark still resolves at the widest zoom ({@code MapCamera.MIN_ZOOM} = 16px per tile) instead
 * of collapsing to a smear.
 *
 * <p>Deterministic, allocation-bounded, headless. No colour here is ever multiplied into the
 * world — depth shading and the day/night ambient are applied by the caller.
 */
public final class PlaceSignArt {

    /** One flat rectangle of one colour: the only primitive either drawer needs. */
    public record Quad(float x, float y, float w, float h, float r, float g, float b) {
    }

    /** An axis-aligned keep-out box in batch space (bottom-left origin, y-up). */
    public record Rect(float x, float y, float w, float h) {

        /** Area of the intersection with another rect; {@code 0} when they do not touch. */
        public float overlapArea(float ox, float oy, float ow, float oh) {
            float dx = Math.min(x + w, ox + ow) - Math.max(x, ox);
            float dy = Math.min(y + h, oy + oh) - Math.max(y, oy);
            return dx <= 0f || dy <= 0f ? 0f : dx * dy;
        }
    }

    // ---- ink ---------------------------------------------------------------------------

    /** The void the box is cut from: true black, fully opaque, never translucent. */
    public static final float[] INK_BLACK = {0f, 0f, 0f};
    /** The border and the mark's edge: bone white, one flat tone. */
    public static final float[] INK_BONE = {0.90f, 0.87f, 0.76f};
    /**
     * The lit state of the mark the box is currently naming: a cold signal cyan.
     *
     * <p>Deliberately NOT the inspector's gold (S8 round 2 finding): that gold is byte-for-byte
     * {@code NameplateRenderer}'s KIN tint, so "the door these words belong to" and "this actor
     * is your kin" were the same ink in one frame. This tone matches none of the four
     * nameplate attitude tints, so a lit mark can never be misread as a social signal.
     */
    public static final float[] INK_LIT = {0.42f, 0.94f, 1f};

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

    // ---- the blocky text ---------------------------------------------------------------

    /** Whole-number magnification of the 8&times;8 font: square pixels, never resampled. */
    public static final int TEXT_SCALE = 2;
    /** One glyph cell's drawn width, px. */
    public static final int GLYPH_W = Glyph8x8Font.WIDTH * TEXT_SCALE;
    /** One glyph cell's drawn height, px. */
    public static final int GLYPH_H = Glyph8x8Font.HEIGHT * TEXT_SCALE;
    /** Blank rows between the pop-up's two lines, px. */
    public static final float LINE_GAP_PX = 4f;

    // ---- the marks (fractions of one tile span) ----------------------------------------

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

    private static final float POST_X = 0.46f;
    private static final float POST_Y0 = 0.08f;
    private static final float POST_Y1 = 0.78f;
    private static final float BOARD_X0 = 0.10f;
    private static final float BOARD_X1 = 0.90f;
    private static final float BOARD_Y0 = 0.58f;
    private static final float BOARD_Y1 = 0.86f;

    private PlaceSignArt() {
    }

    /**
     * The pop-up's outer width for a two-line block: the wider line plus padding and both
     * borders.
     *
     * @param textWidthPx the widest of the two rendered lines, from {@link #textWidth}
     */
    public static float boxWidth(float textWidthPx) {
        return textWidthPx + 2 * PAD_X + 2 * BORDER_PX;
    }

    /** The pop-up's outer height for its two blocky lines. */
    public static float boxHeight() {
        return 2 * GLYPH_H + LINE_GAP_PX + 2 * PAD_Y + 2 * BORDER_PX;
    }

    /** The drawn width of one blocky line, px — fixed pitch, so it is exact, not measured. */
    public static float textWidth(String line) {
        return line.length() * (float) GLYPH_W;
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
     * Where the pop-up actually goes, given what it must not cover (S8 round 2, Eli: "THE BOX
     * PAINTS OVER THE UI" — it was drawn last with no zone awareness, so it sat on top of the
     * hover nameplate every time, and on the HUD block and an open inspector sheet whenever
     * the named place was under them).
     *
     * <p>Six placements are tried in a fixed order — above the anchor tile (the NES default,
     * and where it stays whenever it can), then below, then right, then left, then the top
     * centre (the ward's HUD block sits top-LEFT and the inspector column top-RIGHT, so the
     * middle of the top edge is usually clear), then parked at the foot of the screen — and
     * each is clamped on screen before it is judged. The first that touches no keep-out rect
     * wins. If the screen is so crowded that every placement overlaps something, the one that
     * covers the LEAST is used: a box that must overlap still has to be readable, and picking
     * deterministically beats flickering between two bad choices frame to frame.
     *
     * @param anchorCentreX centre x of the attention tile
     * @param anchorTopY    top edge (y-up) of the attention tile
     * @param anchorBottomY bottom edge (y-up) of the attention tile
     * @param keepOut       the frame's forbidden zones, in batch space
     * @return the chosen {@code {x, y}}
     */
    public static float[] placeAvoiding(float anchorCentreX, float anchorTopY, float anchorBottomY,
            float w, float h, float viewportW, float viewportH, List<Rect> keepOut) {
        float[][] candidates = {
            {anchorCentreX - w / 2f, anchorTopY + LIFT_PX},
            {anchorCentreX - w / 2f, anchorBottomY - LIFT_PX - h},
            {anchorCentreX + LIFT_PX, anchorBottomY + (anchorTopY - anchorBottomY) / 2f - h / 2f},
            {anchorCentreX - LIFT_PX - w,
                anchorBottomY + (anchorTopY - anchorBottomY) / 2f - h / 2f},
            {viewportW / 2f - w / 2f, viewportH - h - SCREEN_MARGIN_PX},
            {viewportW / 2f - w / 2f, SCREEN_MARGIN_PX},
        };
        float[] best = null;
        float bestOverlap = Float.MAX_VALUE;
        for (float[] candidate : candidates) {
            float[] at = clampToViewport(candidate[0], candidate[1], w, h, viewportW, viewportH);
            float overlap = 0f;
            for (Rect zone : keepOut) {
                overlap += zone.overlapArea(at[0], at[1], w, h);
            }
            if (overlap <= 0f) {
                return at;
            }
            if (overlap < bestOverlap) {
                bestOverlap = overlap;
                best = at;
            }
        }
        return best;
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
     * Top y (y-up) of the pop-up's first text line — the top edge of the glyph box, matching
     * the anchoring {@link #textQuads} expects.
     */
    public static float firstLineTopY(float boxY, float boxH) {
        return boxY + boxH - BORDER_PX - PAD_Y;
    }

    /** Top y of the pop-up's second text line. */
    public static float secondLineTopY(float boxY, float boxH) {
        return firstLineTopY(boxY, boxH) - GLYPH_H - LINE_GAP_PX;
    }

    /** Left x of the pop-up's text block. */
    public static float textLeftX(float boxX) {
        return boxX + BORDER_PX + PAD_X;
    }

    /**
     * One line of blocky text as quads: every horizontal run of ink in the 8&times;8 font,
     * magnified {@value #TEXT_SCALE}&times;. {@code (x, topY)} is the TOP-left of the line's
     * glyph box, bottom-left origin / y-up like everything else here. Characters the font does
     * not cover fall back to its hollow box, so no string can crash a frame.
     *
     * <p>Run-merged rather than one quad per texel: a 36-character line is a few hundred
     * quads, not two thousand, and the pixels are identical either way.
     */
    public static List<Quad> textQuads(String line, float x, float topY, float[] ink) {
        List<Quad> quads = new ArrayList<>();
        for (int i = 0; i < line.length(); i++) {
            char glyph = line.charAt(i);
            float cellLeft = x + i * (float) GLYPH_W;
            for (int row = 0; row < Glyph8x8Font.HEIGHT; row++) {
                float rowTop = topY - row * (float) TEXT_SCALE;
                int runStart = -1;
                for (int col = 0; col <= Glyph8x8Font.WIDTH; col++) {
                    boolean ipk = col < Glyph8x8Font.WIDTH && Glyph8x8Font.isInk(glyph, col, row);
                    if (ipk && runStart < 0) {
                        runStart = col;
                    } else if (!ipk && runStart >= 0) {
                        quads.add(quad(cellLeft + runStart * (float) TEXT_SCALE,
                                rowTop - TEXT_SCALE, (col - runStart) * (float) TEXT_SCALE,
                                TEXT_SCALE, ink));
                        runStart = -1;
                    }
                }
            }
        }
        return quads;
    }

    /**
     * One hanging shop sign over a door cell, as quads: bracket arm, hanger, plaque edge,
     * plaque field. Bottom-left origin, y-up, {@code (left, bottom)} being the door tile's
     * own screen corner.
     *
     * @param named {@code true} when this is the mark the frame's box is describing — the
     *              plaque edge lights and the arm goes with it
     * @param ink   the base ink for the unlit state (depth-shaded, ambient-lit bone, from the
     *              caller)
     */
    public static List<Quad> hangingSign(float left, float bottom, float span, boolean named,
            float[] ink) {
        float[] edge = named ? INK_LIT : ink;
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

    /**
     * One street fingerpost planted in a kerb cell, as quads: the post itself, then the
     * name-board across its head (bone edge, black field — the pop-up in miniature again).
     * Same arguments and same ink discipline as {@link #hangingSign}; the silhouette is the
     * only difference, and it is the whole point — a post standing UP off the ground reads as
     * a way, a plaque hanging DOWN off a bracket reads as a door.
     */
    public static List<Quad> wayPost(float left, float bottom, float span, boolean named,
            float[] ink) {
        float[] edge = named ? INK_LIT : ink;
        float weight = Math.max(1f, span * ARM_WEIGHT);
        float boardEdge = Math.max(1f, span * PLAQUE_EDGE);
        float bx0 = left + span * BOARD_X0;
        float by0 = bottom + span * BOARD_Y0;
        float bw = span * (BOARD_X1 - BOARD_X0);
        float bh = span * (BOARD_Y1 - BOARD_Y0);

        List<Quad> quads = new ArrayList<>(4);
        quads.add(quad(left + span * POST_X, bottom + span * POST_Y0,
                weight, span * (POST_Y1 - POST_Y0), edge));
        quads.add(quad(bx0, by0, bw, bh, edge));
        quads.add(quad(bx0 + boardEdge, by0 + boardEdge,
                bw - 2 * boardEdge, bh - 2 * boardEdge, INK_BLACK));
        return quads;
    }

    /** The mark for a place: a way's fingerpost, or a building's hanging plaque. */
    public static List<Quad> mark(boolean way, float left, float bottom, float span,
            boolean named, float[] ink) {
        return way ? wayPost(left, bottom, span, named, ink)
                : hangingSign(left, bottom, span, named, ink);
    }

    private static Quad quad(float x, float y, float w, float h, float[] ink) {
        return new Quad(x, y, w, h, ink[0], ink[1], ink[2]);
    }
}
