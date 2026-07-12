package com.trojia.sim.world;

/**
 * The six axis-aligned neighbor directions. Enum declaration order is the
 * canonical iteration order for every deterministic fan-out (fluids wake
 * laterals then above, light spreads, etc.); never iterate neighbors in any
 * other order in a side-effectful path.
 */
public enum Dir {

    /** -x. */ WEST(-1, 0, 0),
    /** +x. */ EAST(1, 0, 0),
    /** -y. */ NORTH(0, -1, 0),
    /** +y. */ SOUTH(0, 1, 0),
    /** -z. */ DOWN(0, 0, -1),
    /** +z. */ UP(0, 0, 1);

    private final int dx;
    private final int dy;
    private final int dz;

    Dir(int dx, int dy, int dz) {
        this.dx = dx;
        this.dy = dy;
        this.dz = dz;
    }

    /** x delta of one step in this direction (-1, 0 or 1). */
    public int dx() {
        return dx;
    }

    /** y delta of one step in this direction (-1, 0 or 1). */
    public int dy() {
        return dy;
    }

    /** z delta of one step in this direction (-1, 0 or 1). */
    public int dz() {
        return dz;
    }

    /** The opposite direction (WEST↔EAST, NORTH↔SOUTH, DOWN↔UP). */
    public Dir opposite() {
        return switch (this) {
            case WEST -> EAST;
            case EAST -> WEST;
            case NORTH -> SOUTH;
            case SOUTH -> NORTH;
            case DOWN -> UP;
            case UP -> DOWN;
        };
    }
}
