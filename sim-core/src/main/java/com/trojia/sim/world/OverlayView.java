package com.trojia.sim.world;

/**
 * Read access to one sparse 16-bit overlay of one chunk, cells in ascending
 * {@code localIdx} order — the canonical overlay iteration order for hashing
 * and saves (ARCHITECTURE.md §1.1 #9). Obtained via
 * {@link ChunkView#overlay(OverlayId)}; borrowed like the lane arrays: valid
 * only until the chunk is written or frozen, never retained across ticks.
 *
 * <p>A stored value of 0 is distinct from an absent cell (see
 * {@link ChunkWriter#setOverlay}); absence is queried via {@link #contains}
 * or the {@code absentValue} parameter of {@link #get}.
 */
public interface OverlayView {

    /** Number of stored cells. */
    int size();

    /** The localIdx of cell {@code i} (0..size-1); strictly ascending in {@code i}. */
    int localIdxAt(int i);

    /** The unsigned 16-bit value of cell {@code i} (0..size-1). */
    int valueAt(int i);

    /** The stored value at {@code localIdx}, or {@code absentValue} when the cell is absent. */
    int get(int localIdx, int absentValue);

    /** Whether a cell is stored at {@code localIdx} (0 is a valid stored value). */
    boolean contains(int localIdx);
}
