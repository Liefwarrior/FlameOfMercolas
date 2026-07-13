package com.trojia.client.camera;

/**
 * Pure-math observer camera state (ARCHITECTURE section 3, client-observer entry):
 * pan, integer zoom, world-bounds clamping, tile&harr;screen transforms, and an
 * integer-snapped rendering origin. <strong>No libGDX dependency</strong> — this is a
 * plain state object; the GL layer syncs its {@code OrthographicCamera} (or batch
 * transform) from {@link #renderOriginX()}/{@link #renderOriginY()} and
 * {@link #zoom()} each frame.
 *
 * <p>Coordinate spaces (all axes increase rightward/downward; tile row 0 is the top of
 * the map — the GL sync layer flips y if its projection is y-up):
 * <ul>
 *   <li><b>tile</b> — integer world-tile coordinates, {@code 0..worldWidthTiles-1}.</li>
 *   <li><b>world px</b> — tile space times {@link #tilePx()} (unscaled art pixels).
 *       The camera center is tracked here in Q8 fixed point (256 = one world pixel),
 *       so sub-pixel panning at high zoom is deterministic integer math — no floats
 *       anywhere in this class.</li>
 *   <li><b>screen px</b> — viewport pixels; one world px covers {@link #zoom()} screen
 *       px (TILE-ART-SPEC section 4: nearest filtering, integer zoom only).</li>
 * </ul>
 *
 * <p><b>Snapping contract</b> (TILE-ART-SPEC section 4): the rendering origin — the
 * screen position of world pixel (0,0) — is rounded to a whole screen pixel, so world
 * texels always land exactly on screen pixels and a 0-padding atlas cannot bleed. All
 * tile&harr;screen transforms are defined against the <em>snapped</em> origin, so
 * picking agrees with rendering exactly: {@code screenToTileX(tileToScreenX(t)) == t}
 * for every in-view tile.
 *
 * <p><b>Clamping contract:</b> after every mutation the center is clamped so the view
 * never shows past the world edge on an axis where the world is wider (in screen px)
 * than the viewport; where it is narrower, the world is centered on that axis and
 * panning it is a no-op.
 *
 * <p>Deterministic: same call sequence, same state — division is the JVM's
 * truncate-toward-zero, so pan deltas of equal magnitude move symmetrically in either
 * direction. Not thread-safe; the observer mutates it from the frame loop only.
 */
public final class MapCamera {

    /** Minimum zoom: 1 screen px per world px. */
    public static final int MIN_ZOOM = 1;

    /** Maximum zoom in v0 (integer zooms only, TILE-ART-SPEC section 4). */
    public static final int MAX_ZOOM = 8;

    private static final int Q8_SHIFT = 8;
    private static final long Q8_ONE = 1 << Q8_SHIFT;
    private static final long Q8_HALF = Q8_ONE / 2;

    private final int tilePx;
    private final int worldWidthTiles;
    private final int worldHeightTiles;

    private int viewportWidthPx;
    private int viewportHeightPx;
    private int zoom;
    /** Camera center in world px, Q8 fixed point; always clamped to world bounds. */
    private long centerXQ8;
    private long centerYQ8;

    /**
     * Creates a camera centered on the middle of the world at zoom 1.
     *
     * @param tilePx           tile edge in art pixels (16 in v0)
     * @param worldWidthTiles  world width in tiles
     * @param worldHeightTiles world height in tiles
     * @param viewportWidthPx  initial viewport width in screen px
     * @param viewportHeightPx initial viewport height in screen px
     * @throws IllegalArgumentException if any argument is not strictly positive
     */
    public MapCamera(int tilePx, int worldWidthTiles, int worldHeightTiles,
                     int viewportWidthPx, int viewportHeightPx) {
        requirePositive(tilePx, "tilePx");
        requirePositive(worldWidthTiles, "worldWidthTiles");
        requirePositive(worldHeightTiles, "worldHeightTiles");
        requirePositive(viewportWidthPx, "viewportWidthPx");
        requirePositive(viewportHeightPx, "viewportHeightPx");
        this.tilePx = tilePx;
        this.worldWidthTiles = worldWidthTiles;
        this.worldHeightTiles = worldHeightTiles;
        this.viewportWidthPx = viewportWidthPx;
        this.viewportHeightPx = viewportHeightPx;
        this.zoom = MIN_ZOOM;
        this.centerXQ8 = (worldWidthPx() << Q8_SHIFT) / 2;
        this.centerYQ8 = (worldHeightPx() << Q8_SHIFT) / 2;
        clampPosition();
    }

    // ---------------------------------------------------------------- mutation

    /**
     * Resizes the viewport (window resize); the center is re-clamped.
     *
     * @throws IllegalArgumentException if either dimension is not strictly positive
     */
    public void setViewport(int widthPx, int heightPx) {
        requirePositive(widthPx, "widthPx");
        requirePositive(heightPx, "heightPx");
        this.viewportWidthPx = widthPx;
        this.viewportHeightPx = heightPx;
        clampPosition();
    }

    /**
     * Pans the view by a screen-pixel delta: positive {@code dxScreenPx} moves the
     * camera (and thus the visible window) rightward through the world. The world-px
     * displacement is {@code d / zoom}, kept in Q8, so a slow drag at 8&times; zoom
     * still accumulates. Clamped afterwards.
     */
    public void pan(int dxScreenPx, int dyScreenPx) {
        centerXQ8 += ((long) dxScreenPx << Q8_SHIFT) / zoom;
        centerYQ8 += ((long) dyScreenPx << Q8_SHIFT) / zoom;
        clampPosition();
    }

    /**
     * Centers the view on the center of a tile (z-scrub keeps x/y; inspector "jump to"
     * uses this). The tile need not be inside the world — the result is clamped.
     */
    public void centerOnTile(int tileX, int tileY) {
        centerXQ8 = ((long) tileX * tilePx << Q8_SHIFT) + ((long) tilePx << Q8_SHIFT) / 2;
        centerYQ8 = ((long) tileY * tilePx << Q8_SHIFT) + ((long) tilePx << Q8_SHIFT) / 2;
        clampPosition();
    }

    /**
     * Sets the zoom, clamped to {@link #MIN_ZOOM}..{@link #MAX_ZOOM}, keeping the
     * camera center fixed (then re-clamping against bounds).
     *
     * @return the effective zoom after clamping
     */
    public int setZoom(int newZoom) {
        zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, newZoom));
        clampPosition();
        return zoom;
    }

    /** Increments zoom by one step; saturates at {@link #MAX_ZOOM}. */
    public int zoomIn() {
        return setZoom(zoom + 1);
    }

    /** Decrements zoom by one step; saturates at {@link #MIN_ZOOM}. */
    public int zoomOut() {
        return setZoom(zoom - 1);
    }

    /**
     * Sets the zoom while keeping the world point currently under the given screen
     * position stationary on screen (mouse-wheel zoom-to-cursor). The anchor is
     * computed from the unsnapped center, so repeated zooms through a point are exact
     * in Q8; the result is clamped, which may move the anchor when the view hits a
     * world edge.
     *
     * @return the effective zoom after clamping
     */
    public int zoomAt(int screenX, int screenY, int newZoom) {
        int target = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, newZoom));
        if (target == zoom) {
            return zoom;
        }
        long offX = (long) (screenX - viewportWidthPx / 2) << Q8_SHIFT;
        long offY = (long) (screenY - viewportHeightPx / 2) << Q8_SHIFT;
        long anchorXQ8 = centerXQ8 + offX / zoom;
        long anchorYQ8 = centerYQ8 + offY / zoom;
        zoom = target;
        centerXQ8 = anchorXQ8 - offX / zoom;
        centerYQ8 = anchorYQ8 - offY / zoom;
        clampPosition();
        return zoom;
    }

    // ---------------------------------------------------------------- transforms

    /**
     * Screen x of world pixel (0,0) — the integer-snapped rendering origin the GL
     * layer draws from. Rounded half-up from the Q8 center; usually negative.
     */
    public int renderOriginX() {
        return viewportWidthPx / 2 - (int) ((centerXQ8 * zoom + Q8_HALF) >> Q8_SHIFT);
    }

    /** Screen y of world pixel (0,0); see {@link #renderOriginX()}. */
    public int renderOriginY() {
        return viewportHeightPx / 2 - (int) ((centerYQ8 * zoom + Q8_HALF) >> Q8_SHIFT);
    }

    /** Edge of one drawn tile in screen px: {@code tilePx * zoom}. */
    public int tileSpanPx() {
        return tilePx * zoom;
    }

    /** Screen x of the left edge of a tile column (any {@code tileX}, even off-world). */
    public int tileToScreenX(int tileX) {
        return renderOriginX() + tileX * tileSpanPx();
    }

    /** Screen y of the top edge of a tile row (any {@code tileY}, even off-world). */
    public int tileToScreenY(int tileY) {
        return renderOriginY() + tileY * tileSpanPx();
    }

    /**
     * Tile column under a screen x (floor division — negative results mean "left of
     * the world"; callers bounds-check with {@link #isInWorld(int, int)}).
     */
    public int screenToTileX(int screenX) {
        return Math.floorDiv(screenX - renderOriginX(), tileSpanPx());
    }

    /** Tile row under a screen y; see {@link #screenToTileX(int)}. */
    public int screenToTileY(int screenY) {
        return Math.floorDiv(screenY - renderOriginY(), tileSpanPx());
    }

    /** Whether a tile coordinate lies inside the world bounds. */
    public boolean isInWorld(int tileX, int tileY) {
        return tileX >= 0 && tileX < worldWidthTiles && tileY >= 0 && tileY < worldHeightTiles;
    }

    // ---------------------------------------------------------------- culling

    /** First (leftmost) world tile column intersecting the viewport; &ge; 0. */
    public int visibleTileMinX() {
        return Math.max(0, screenToTileX(0));
    }

    /** Last (rightmost) world tile column intersecting the viewport; &le; width-1. */
    public int visibleTileMaxX() {
        return Math.min(worldWidthTiles - 1, screenToTileX(viewportWidthPx - 1));
    }

    /** First (topmost) world tile row intersecting the viewport; &ge; 0. */
    public int visibleTileMinY() {
        return Math.max(0, screenToTileY(0));
    }

    /** Last (bottom) world tile row intersecting the viewport; &le; height-1. */
    public int visibleTileMaxY() {
        return Math.min(worldHeightTiles - 1, screenToTileY(viewportHeightPx - 1));
    }

    // ---------------------------------------------------------------- getters

    public int tilePx() {
        return tilePx;
    }

    public int worldWidthTiles() {
        return worldWidthTiles;
    }

    public int worldHeightTiles() {
        return worldHeightTiles;
    }

    public int viewportWidthPx() {
        return viewportWidthPx;
    }

    public int viewportHeightPx() {
        return viewportHeightPx;
    }

    public int zoom() {
        return zoom;
    }

    /** Camera center x in world px, Q8 fixed point (256 = one world pixel). */
    public long centerXWorldPxQ8() {
        return centerXQ8;
    }

    /** Camera center y in world px, Q8 fixed point (256 = one world pixel). */
    public long centerYWorldPxQ8() {
        return centerYQ8;
    }

    // ---------------------------------------------------------------- internals

    private long worldWidthPx() {
        return (long) worldWidthTiles * tilePx;
    }

    private long worldHeightPx() {
        return (long) worldHeightTiles * tilePx;
    }

    private void clampPosition() {
        centerXQ8 = clampAxis(centerXQ8, worldWidthPx(), viewportWidthPx);
        centerYQ8 = clampAxis(centerYQ8, worldHeightPx(), viewportHeightPx);
    }

    private long clampAxis(long centerQ8, long worldPx, int viewportPx) {
        long worldQ8 = worldPx << Q8_SHIFT;
        long viewWorldQ8 = ((long) viewportPx << Q8_SHIFT) / zoom;
        if (viewWorldQ8 >= worldQ8) {
            return worldQ8 / 2; // world narrower than view: keep it centered
        }
        long half = viewWorldQ8 / 2;
        return Math.max(half, Math.min(centerQ8, worldQ8 - half));
    }

    private static void requirePositive(int value, String what) {
        if (value <= 0) {
            throw new IllegalArgumentException(what + " must be > 0 (was " + value + ")");
        }
    }
}
