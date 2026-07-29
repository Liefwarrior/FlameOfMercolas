package com.trojia.client.fpv;

/**
 * The frame's immutable pinhole projection: where the eye is, which way it looks, and the
 * arithmetic that turns a world point into a screen point. Pure math, no libGDX, no world —
 * the same "plain state object" discipline {@link com.trojia.client.camera.MapCamera} keeps
 * for the top-down view, and for the same reason: it has to be exercisable with nothing but
 * numbers.
 *
 * <p><b>Coordinate conventions.</b> World x/y are tile units with y increasing SOUTH (the
 * grid's own convention). World height increases upward, in tile-width units
 * ({@link BandGeometry}). Yaw is radians, {@code 0} looking EAST (+x) and increasing toward
 * SOUTH (+y) — so yaw runs E &rarr; S &rarr; W &rarr; N, matching a compass turning clockwise
 * over a y-down map. Forward is {@code (cos yaw, sin yaw)}; the eye's right-hand side is
 * {@code (-sin yaw, cos yaw)}, which puts SOUTH on your right when you face EAST.
 *
 * <p>Screen coordinates are y-<b>up</b> from the bottom-left, sized to the viewport — the
 * libGDX default projection every other renderer in this package assumes, so a planned quad
 * can be handed straight to the same {@code SpriteBatch}.
 *
 * <p><b>There is no pitch.</b> Looking up and down is a vertical <em>shear</em> of the
 * horizon line ({@code horizonY}), Doom's trick, not a rotation of the view frustum. That is a
 * deliberate cost: it means every wall face stays an axis-aligned vertical strip in world
 * space, so a face projects to a plain quad and the whole scene is a batch of quads that a
 * headless test can read. Real pitch would buy a slightly less distorted look upward and cost
 * the entire verifiability story. The shear is clamped ({@link #MAX_SHEAR_FRACTION}) so the
 * horizon never leaves the frame; within that range you can crane up at a three-storey
 * warehouse from the pavement in front of it, which is the case the clamp is sized for.
 */
public final class EyeProjection {

    /**
     * Nothing closer than this (in view-forward distance) is projected: the arithmetic
     * divides by depth, and a vertex at or behind the eye has no screen position at all.
     *
     * <p>Nothing is <em>culled</em> for being close, though — every surface is clipped
     * against this plane properly, which matters because mid-stride the eye sits exactly on a
     * cell boundary and faces beginning at distance zero are routine. See
     * {@code FirstPersonPlanner}'s clipper.
     */
    public static final float NEAR_PLANE = 0.25f;

    /** How far the horizon may shear from centre, as a fraction of viewport height. */
    public static final float MAX_SHEAR_FRACTION = 0.45f;

    /** Default horizontal field of view, degrees. Wide enough to read a street, narrow
     * enough that the edges of the frame do not smear (Daggerfall sat around here). */
    public static final float DEFAULT_FOV_DEGREES = 72f;

    private final float eyeX;
    private final float eyeY;
    private final float eyeHeight;
    private final float yaw;
    private final float cos;
    private final float sin;
    private final int viewportW;
    private final int viewportH;
    private final float focalPx;
    private final float horizonY;
    private final float tanHalfFov;

    private EyeProjection(float eyeX, float eyeY, float eyeHeight, float yaw,
            int viewportW, int viewportH, float fovDegrees, float shearPx) {
        this.eyeX = eyeX;
        this.eyeY = eyeY;
        this.eyeHeight = eyeHeight;
        this.yaw = yaw;
        this.cos = (float) Math.cos(yaw);
        this.sin = (float) Math.sin(yaw);
        this.viewportW = viewportW;
        this.viewportH = viewportH;
        this.tanHalfFov = (float) Math.tan(Math.toRadians(fovDegrees) / 2.0);
        this.focalPx = (viewportW / 2f) / tanHalfFov;
        float maxShear = MAX_SHEAR_FRACTION * viewportH;
        // Looking UP moves the horizon DOWN the screen, so a positive shear subtracts.
        this.horizonY = viewportH / 2f - Math.max(-maxShear, Math.min(maxShear, shearPx));
    }

    /**
     * Builds the frame's projection.
     *
     * @param eyeX        eye x in tile units (a cell centre is {@code tileX + 0.5})
     * @param eyeY        eye y in tile units
     * @param eyeHeight   eye height in world units ({@link BandGeometry})
     * @param yaw         view yaw in radians, 0 = EAST, increasing toward SOUTH
     * @param viewportW   viewport width in screen px
     * @param viewportH   viewport height in screen px
     * @param fovDegrees  horizontal field of view
     * @param shearPx     vertical horizon shear in screen px (positive looks up), clamped
     */
    public static EyeProjection of(float eyeX, float eyeY, float eyeHeight, float yaw,
            int viewportW, int viewportH, float fovDegrees, float shearPx) {
        if (viewportW <= 0 || viewportH <= 0) {
            throw new IllegalArgumentException("viewport must be positive: "
                    + viewportW + "x" + viewportH);
        }
        return new EyeProjection(eyeX, eyeY, eyeHeight, yaw, viewportW, viewportH, fovDegrees,
                shearPx);
    }

    // ------------------------------------------------------------------ view space

    /** Forward distance of a world x/y point from the eye — negative means behind you. */
    public float depthOf(float worldX, float worldY) {
        return (worldX - eyeX) * cos + (worldY - eyeY) * sin;
    }

    /** Sideways offset of a world x/y point, positive to the eye's RIGHT. */
    public float lateralOf(float worldX, float worldY) {
        return -(worldX - eyeX) * sin + (worldY - eyeY) * cos;
    }

    // ------------------------------------------------------------------ screen space

    /** Screen x (px, from the left edge) of a view-space (lateral, depth) pair. */
    public float screenX(float lateral, float depth) {
        return viewportW / 2f + lateral * focalPx / depth;
    }

    /** Screen y (px, y-up from the bottom edge) of a world height seen at {@code depth}. */
    public float screenY(float worldHeight, float depth) {
        return horizonY + (worldHeight - eyeHeight) * focalPx / depth;
    }

    /**
     * Half the on-screen width, in px, of something one tile wide standing at {@code depth}
     * square to the view — the billboard scale.
     */
    public float halfWidthPx(float widthWorld, float depth) {
        return 0.5f * widthWorld * focalPx / depth;
    }

    /**
     * Whether a cell of unit size centred at {@code (cx, cy)} can possibly touch the frame:
     * in front of the near plane and within the horizontal fan plus one cell of slack (so a
     * cell whose centre is just outside still contributes its near corner and its billboard).
     */
    public boolean mayBeVisible(float cx, float cy, float maxDepth) {
        float d = depthOf(cx, cy);
        if (d < NEAR_PLANE || d > maxDepth) {
            return false;
        }
        float lateral = lateralOf(cx, cy);
        return Math.abs(lateral) <= d * tanHalfFov + 1.25f;
    }

    /**
     * The nearest distance at which a point on a floor plane {@code floorHeight} below the eye
     * can appear on screen at all: everything closer projects below the bottom edge. Not used
     * for culling (the near clip handles that properly) — it is the honest answer to "how much
     * of the ground under my feet can I actually see", which is what sets how far away the
     * visible floor starts and therefore how much of the frame the street occupies.
     */
    public float nearestVisibleFloorDistance(float floorHeight) {
        float drop = eyeHeight - floorHeight;
        if (drop <= 0f || horizonY <= 0f) {
            return NEAR_PLANE;
        }
        return drop * focalPx / horizonY;
    }

    /**
     * The greatest world height that can appear on screen at all for something
     * {@code depth} forward of the eye: above this it is off the top edge. Used to stop each
     * column's upward band scan exactly where the frame stops, which is an <em>exact</em>
     * cutoff rather than a budget — a cell above this line contributes no pixels.
     */
    public float highestVisibleHeight(float depth) {
        return eyeHeight + (viewportH - horizonY) * depth / focalPx;
    }

    // ------------------------------------------------------------------ getters

    public float eyeX() {
        return eyeX;
    }

    public float eyeY() {
        return eyeY;
    }

    public float eyeHeight() {
        return eyeHeight;
    }

    public float yaw() {
        return yaw;
    }

    public float focalPx() {
        return focalPx;
    }

    public float horizonY() {
        return horizonY;
    }

    public int viewportWidthPx() {
        return viewportW;
    }

    public int viewportHeightPx() {
        return viewportH;
    }
}
