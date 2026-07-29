package com.trojia.client.render;

/**
 * One named place of the ward, in world-tile coordinates: the cell its mark stands on, the
 * footprint it occupies, and the two lines a reader gets (2026-07-28, Eli: "we also need to
 * label buildings"). Authored as a {@code place_sign} marker in the district's own
 * {@code .tmx} (content/maps/README.md) and read by {@link com.trojia.client.boot.PlaceSignsLoader}.
 *
 * <p><b>Why the footprint travels with the sign.</b> A label that triggers on distance to the
 * DOOR is useless for the ward's big authored sites — the Ropewalk is a 64-tile shed, the
 * Weighhouse 16&times;17 — so the pop-up measures distance to this rect instead, which is zero
 * anywhere inside the building. The door cell is only where the hanging marker is DRAWN.
 *
 * <p><b>Two kinds of place, because a way is not a building.</b> A {@link Kind#DOOR} place is
 * something you go IN and its mark is the plaque hanging over its doorway. A {@link Kind#WAY}
 * place — the Tarwalk, the Long Quay, the fishbone pier — is something you go ALONG; it has no
 * door, so its mark is a kerb fingerpost instead, and a long way carries several of them, one
 * per authored segment. Ways and buildings overlap on purpose (a shop's door cell IS a street
 * cell); {@link PlaceSignOverlay} owns the rule that decides which one a reader is being told
 * about.
 *
 * <p>Immutable presentation data: nothing here is sim state, nothing feeds the world hasher.
 * A place is a way of looking at the ward, not a fact in it.
 *
 * @param place  the sign's words — the place's name
 * @param what   the second line: what the place IS, in the gazetteer's own words
 * @param kind   whether this place is entered ({@link Kind#DOOR}) or travelled
 *               ({@link Kind#WAY})
 * @param doorX  world tile x of the cell the mark stands on (a building's door cell, a way's
 *               kerb post)
 * @param doorY  world tile y of that cell
 * @param z      the world z-level the place stands on
 * @param x0     west edge of the footprint rect, world tiles (inclusive)
 * @param y0     north edge of the footprint rect, world tiles (inclusive)
 * @param x1     east edge of the footprint rect, world tiles (inclusive)
 * @param y1     south edge of the footprint rect, world tiles (inclusive)
 */
public record PlaceSign(String place, String what, Kind kind, int doorX, int doorY, int z,
        int x0, int y0, int x1, int y1) {

    /** What sort of place this is — see the class note. */
    public enum Kind {
        /** A building: entered through a door, marked by a hanging plaque. */
        DOOR,
        /** A street, lane, quay or pier: travelled along, marked by a kerb fingerpost. */
        WAY;

        /**
         * The authored {@code kind} property, defaulting to {@link #DOOR} for anything
         * unrecognised — the building plaque was the family's only member before the ways
         * were authored, so an older map with no {@code kind} still reads correctly.
         */
        public static Kind of(String authored) {
            return "way".equalsIgnoreCase(authored) ? WAY : DOOR;
        }
    }

    /**
     * Chebyshev (king-move) distance in tiles from {@code (tileX, tileY)} to this place's
     * footprint: {@code 0} anywhere inside it, otherwise how many steps outside the rect the
     * point lies, counting a diagonal as one step.
     *
     * <p>Chebyshev on purpose, and it is the sim's own measure: work reach in {@code sim-core}
     * is chebyshev, so the cell a worker can act on from a doorway and the cell that reads the
     * doorway's sign are the same set. (The generator's authoring self-check and
     * {@code MarkerContractPass} bound the sign-to-footprint gap with the STRICTER rectilinear
     * sum — an authoring tolerance, deliberately tighter than what the reader gets.)
     */
    public int distanceTo(int tileX, int tileY) {
        int dx = Math.max(Math.max(x0 - tileX, tileX - x1), 0);
        int dy = Math.max(Math.max(y0 - tileY, tileY - y1), 0);
        return Math.max(dx, dy);
    }

    /** Chebyshev distance in tiles from {@code (tileX, tileY)} to this place's mark cell. */
    public int distanceToMark(int tileX, int tileY) {
        return Math.max(Math.abs(doorX - tileX), Math.abs(doorY - tileY));
    }

    /** {@code true} when {@code (tileX, tileY)} is exactly the cell this place's mark stands on. */
    public boolean isMarkCell(int tileX, int tileY) {
        return tileX == doorX && tileY == doorY;
    }

    /**
     * Footprint area in tiles — the overlay's specificity tie-break: when two places are the
     * same distance away, the SMALLER one is the more specific answer (Wormwood Pier rather
     * than Pier Row, the Eel-Pots rather than the whole Tarwalk).
     */
    public int area() {
        return (x1 - x0 + 1) * (y1 - y0 + 1);
    }
}
