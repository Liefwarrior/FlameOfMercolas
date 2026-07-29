package com.trojia.client.fpv;

/**
 * Which face of a tile cell a planned quad shows.
 *
 * <p>A cell is an axis-aligned unit box one tile wide and one {@link BandGeometry#BAND_HEIGHT}
 * tall, so it has six faces and nothing else — there are no sloped surfaces anywhere in this
 * view, because there is no sloped geometry anywhere in the tile data. {@code RAMP} and
 * {@code STAIR} are walkable <em>forms</em> occupying a whole cell, with the actual connection
 * living in {@code ZLinkTable}, not in any authored incline; drawing a synthesised slope would
 * put the picture at odds with where the sim says the climber is. They draw as a floor slab
 * like any other, and the climb reads through the eye's eased rise between bands instead.
 */
public enum Face {

    /** The upward-facing horizontal surface: a floor slab's top, or a wall cube's roof. */
    TOP,

    /** The downward-facing horizontal surface: the underside of the slab above you — which
     * is what a ceiling <em>is</em> in this world, since there is no CEILING form. */
    BOTTOM,

    /** The {@code -x} side. */
    WEST,

    /** The {@code +x} side. */
    EAST,

    /** The {@code -y} side. */
    NORTH,

    /** The {@code +y} side. */
    SOUTH,

    /** Not a face of a cell at all: a flat sprite turned square to the camera. */
    BILLBOARD;

    /** Whether this face is one of the four vertical sides. */
    public boolean isSide() {
        return this == WEST || this == EAST || this == NORTH || this == SOUTH;
    }
}
