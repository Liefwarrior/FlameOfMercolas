package com.trojia.client.fpv;

import java.util.ArrayList;
import java.util.List;

/**
 * The compass ribbon, drawn at the top of the frame in <b>both</b> modes.
 *
 * <p>The third of the switch's three anchors (see {@link ViewModeState}): the tile under the
 * middle of the screen does not move, the wedge already showed you which way you would be
 * looking, and this does not blink out or jump during the cross-fade. Three things staying
 * nailed down is what keeps a mode change from being a relocation.
 *
 * <p>It is a strip, not a dial, because a strip degrades gracefully: at a glance it says which
 * way you face, and out of the corner of your eye its motion says which way you are turning,
 * which is the thing you actually need while walking a street.
 *
 * <p>Pure geometry — bearings in, screen x positions out. No GL, no fonts.
 */
public final class CompassStrip {

    /** How many compass degrees the ribbon shows across the whole strip. Wider than the view
     * fan, so the neighbouring points stay on screen and turning reads as sliding rather than
     * as labels popping in. */
    public static final float SPAN_DEGREES = 150f;

    private static final String[] POINTS =
            {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

    /**
     * One label on the ribbon.
     *
     * @param label   the compass point
     * @param x       screen x, px, measured across the strip's width
     * @param cardinal whether this is one of N/E/S/W (drawn brighter than the intercardinals)
     */
    public record Mark(String label, float x, boolean cardinal) {
    }

    private CompassStrip() {
    }

    /**
     * The labels visible on a strip of {@code widthPx}, centred on the view yaw.
     *
     * <p>Ascending screen x, so the list reads left to right; a point exactly under the centre
     * tick is the direction you are facing. Points outside the strip are dropped rather than
     * clamped — a clamped label would sit still while you turned, which is worse than absent.
     */
    public static List<Mark> plan(float yaw, float widthPx) {
        float bearing = ViewFacing.compassDegrees(yaw);
        float pxPerDegree = widthPx / SPAN_DEGREES;
        List<Mark> marks = new ArrayList<>(5);
        // Walk the eight points in bearing order so the output is ascending in x.
        for (int i = 0; i < POINTS.length; i++) {
            float pointBearing = i * 45f;
            float delta = wrapSigned(pointBearing - bearing);
            if (Math.abs(delta) > SPAN_DEGREES / 2f) {
                continue;
            }
            marks.add(new Mark(POINTS[i], widthPx / 2f + delta * pxPerDegree, i % 2 == 0));
        }
        marks.sort((a, b) -> Float.compare(a.x(), b.x()));
        return marks;
    }

    /** Signed difference in degrees, wrapped into {@code (-180, 180]}. */
    static float wrapSigned(float degrees) {
        float d = degrees % 360f;
        if (d > 180f) {
            d -= 360f;
        }
        if (d <= -180f) {
            d += 360f;
        }
        return d;
    }
}
