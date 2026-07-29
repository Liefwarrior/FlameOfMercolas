package com.trojia.client.render;

/**
 * One named place of the ward, in world-tile coordinates: the door its sign hangs over, the
 * footprint it occupies, and the two lines a reader gets (2026-07-28, Eli: "we also need to
 * label buildings"). Authored as a {@code place_sign} marker in the district's own
 * {@code .tmx} (content/maps/README.md) and read by {@link com.trojia.client.boot.PlaceSignsLoader}.
 *
 * <p><b>Why the footprint travels with the sign.</b> A label that triggers on distance to the
 * DOOR is useless for the ward's big authored sites — the Ropewalk is a 64-tile shed, the
 * Weighhouse 16&times;17 — so the pop-up measures distance to this rect instead, which is zero
 * anywhere inside the building. The door cell is only where the hanging marker is DRAWN.
 *
 * <p>Immutable presentation data: nothing here is sim state, nothing feeds the world hasher.
 * A place is a way of looking at the ward, not a fact in it.
 *
 * @param place  the sign's words — the place's name
 * @param what   the second line: what the place IS, in the gazetteer's own words
 * @param doorX  world tile x of the door cell the sign hangs over
 * @param doorY  world tile y of that cell
 * @param z      the world z-level the place stands on
 * @param x0     west edge of the footprint rect, world tiles (inclusive)
 * @param y0     north edge of the footprint rect, world tiles (inclusive)
 * @param x1     east edge of the footprint rect, world tiles (inclusive)
 * @param y1     south edge of the footprint rect, world tiles (inclusive)
 */
public record PlaceSign(String place, String what, int doorX, int doorY, int z,
        int x0, int y0, int x1, int y1) {

    /**
     * Chebyshev-style rectilinear distance in tiles from {@code (tileX, tileY)} to this
     * place's footprint: {@code 0} anywhere inside it, otherwise how far outside the rect
     * the point lies (axis gaps summed — the same measure the map generator's own sign
     * self-check and {@code MarkerContractPass} use, so authoring and rendering agree).
     */
    public int distanceTo(int tileX, int tileY) {
        int dx = Math.max(Math.max(x0 - tileX, tileX - x1), 0);
        int dy = Math.max(Math.max(y0 - tileY, tileY - y1), 0);
        return dx + dy;
    }
}
