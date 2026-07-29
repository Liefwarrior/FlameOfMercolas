package com.trojia.client.fpv;

import com.trojia.sim.world.Dir;

/**
 * Pure conversions between a continuous view yaw and the things around it that are not
 * continuous: the sim's four-way {@code Dir}, the eight-way grid step the movement seam wants,
 * and the compass reading a player needs in order not to lose their bearings across the mode
 * switch.
 *
 * <p><b>Yaw convention</b> matches {@link EyeProjection}: radians, {@code 0} looking EAST,
 * increasing toward SOUTH (so it runs E, S, W, N — clockwise over a y-down map, which is
 * clockwise on a compass too). Compass degrees are the ordinary ones: {@code 0} at NORTH,
 * rising through EAST.
 */
public final class ViewFacing {

    private static final float HALF_PI = (float) (Math.PI / 2);

    /** The eight compass points, starting at NORTH and turning clockwise. */
    private static final String[] POINTS =
            {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

    private ViewFacing() {
    }

    /**
     * The view yaw that looks the way a sim {@link Dir} points. Only the four lateral
     * directions have a yaw; {@code UP}/{@code DOWN} fall back to EAST, since an actor whose
     * last committed step was a climb has no lateral facing to inherit.
     */
    public static float yawOf(Dir dir) {
        return switch (dir) {
            case EAST, UP, DOWN -> 0f;
            case SOUTH -> HALF_PI;
            case WEST -> (float) Math.PI;
            case NORTH -> 3 * HALF_PI;
        };
    }

    /** {@link #yawOf(Dir)} for a raw {@code Actor.facing()} ordinal, defensively clamped. */
    public static float yawOfFacing(int facingOrdinal) {
        Dir[] all = Dir.values();
        if (facingOrdinal < 0 || facingOrdinal >= all.length) {
            return 0f;
        }
        return yawOf(all[facingOrdinal]);
    }

    /** Compass degrees for a view yaw: {@code 0} north, {@code 90} east. */
    public static float compassDegrees(float yaw) {
        double deg = Math.toDegrees(FirstPersonCamera.normalize(yaw)) + 90.0;
        return (float) (deg % 360.0);
    }

    /** The eight-point compass label for a view yaw ("N", "NE", ...). */
    public static String compassPoint(float yaw) {
        float deg = compassDegrees(yaw);
        int index = Math.floorMod(Math.round(deg / 45f), 8);
        return POINTS[index];
    }

    /**
     * Turns a first-person movement intent into the world-axis {@code (dx, dy)} the sim's own
     * movement seam takes.
     *
     * <p>{@code forward} and {@code strafe} are each in {@code -1, 0, 1} — W/S and A/D. They
     * are rotated by the view yaw into world space and then snapped to the eight grid
     * directions, because that is the only vocabulary {@code Actor.tryStep} has. The snap
     * threshold is {@code cos 67.5}, i.e. each of the eight directions owns a 45-degree
     * wedge of the circle, so walking forward while facing north-north-east steps NORTH until
     * you have turned past halfway to north-east and then steps diagonally. No hysteresis:
     * the sim rejects an illegal step deterministically and the player simply turns.
     *
     * @return {@code {dx, dy}}, each in {@code -1, 0, 1}; {@code {0, 0}} for no intent
     */
    public static int[] stepDelta(float yaw, int forward, int strafe) {
        if (forward == 0 && strafe == 0) {
            return new int[] {0, 0};
        }
        float cos = (float) Math.cos(yaw);
        float sin = (float) Math.sin(yaw);
        // forward is (cos, sin); the eye's right-hand side is (-sin, cos).
        float vx = forward * cos - strafe * sin;
        float vy = forward * sin + strafe * cos;
        float len = (float) Math.sqrt(vx * vx + vy * vy);
        if (len <= 0f) {
            return new int[] {0, 0};
        }
        vx /= len;
        vy /= len;
        float threshold = (float) Math.cos(Math.toRadians(67.5));
        int dx = Math.abs(vx) >= threshold ? (int) Math.signum(vx) : 0;
        int dy = Math.abs(vy) >= threshold ? (int) Math.signum(vy) : 0;
        if (dx == 0 && dy == 0) {
            // Exactly on a wedge boundary: take the dominant axis rather than standing still.
            if (Math.abs(vx) >= Math.abs(vy)) {
                dx = (int) Math.signum(vx);
            } else {
                dy = (int) Math.signum(vy);
            }
        }
        return new int[] {dx, dy};
    }
}
