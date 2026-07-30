package com.trojia.client.fpv;

/**
 * The planner's read-only window onto the tile lanes, shaped exactly like
 * {@code TileCursor}: position first, then read. Position-then-read (rather than three
 * coordinate-taking getters) is not stylistic — it is what lets the world-backed
 * implementation be a single zero-allocation flyweight, and it keeps the planner honest that
 * every lane it reads came from the same cell.
 *
 * <p>Existing so the first-person planner can be exercised over a hand-written scene with no
 * world, no chunks and no GL — the same trick {@code DepthSight} plays for the look-down
 * pass. The world-backed implementation is {@link WorldCellSight}, and it owns <b>its own</b>
 * cursor: borrowing {@code WorldRenderer}'s or {@code DepthVision}'s would move their position
 * out from under them mid-frame.
 */
public interface CellSight {

    /**
     * Positions on a cell. Subsequent {@link #form()}/{@link #fluidBits()}/
     * {@link #materialId()} calls describe it.
     *
     * @return {@code false} if the coordinate is outside the world, in which case the lane
     *         getters must not be called and the caller treats the cell as solid nothing
     */
    boolean moveTo(int x, int y, int z);

    /** The current cell's {@code TileForm} ordinal. */
    int form();

    /** The current cell's raw FLUID-lane bits (depth 0-2, fluidId 3-5, SETTLED 6). */
    int fluidBits();

    /** The current cell's MATERIAL-lane id. */
    int materialId();
}
