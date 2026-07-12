package com.trojia.sim.world;

/**
 * The structural form of a tile's material (one material + one form per tile,
 * ARCHITECTURE.md §1.1 #17). Stored in the FORM lane as the ordinal byte;
 * ordinals are save-format-stable — append only, never reorder.
 */
public enum TileForm {

    /** Immutable nothing: the world border ring. Never paintable. */
    VOID,

    /** No material at this tile (air); MATERIAL lane is ignored. */
    OPEN,

    /** Material occupies only the walkable floor slab of the tile. */
    FLOOR,

    /** Material fills the tile solid (blocks movement; opacity per material). */
    WALL,

    /** Walkable slope connecting this z-level to the one above. */
    RAMP,

    /** Walkable stair connecting this z-level to the one above. */
    STAIR;

    private static final TileForm[] VALUES = values();

    /** Decodes a FORM-lane byte; inverse of {@link #ordinal()}. */
    public static TileForm ofOrdinal(int ordinal) {
        return VALUES[ordinal];
    }
}
