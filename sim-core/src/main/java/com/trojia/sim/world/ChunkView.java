package com.trojia.sim.world;

/**
 * Borrowed read access to one concrete chunk's dense lane arrays, indexed by
 * {@code localIdx = (z<<10)|(y<<5)|x} (8192 cells). The arrays are the live
 * backing storage: valid only until the chunk is frozen or the world is
 * mutated structurally, and NEVER to be written — all writes go through
 * {@link ChunkWriter}.
 *
 * <p>This is the bulk-read seam for kernels (thermal diffusion, relight,
 * hashing, codec) that would drown in per-tile getter calls.
 */
public interface ChunkView {

    /** The flat chunk index this view exposes. */
    int chunkIndex();

    /**
     * The backing array of a 2-byte lane ({@code bytesPerTile == 2}), length
     * 8192. Values are raw lane bits; treat as unsigned where the lane says so.
     */
    short[] shortLane(LaneId lane);

    /** The backing array of a 1-byte lane ({@code bytesPerTile == 1}), length 8192. */
    byte[] byteLane(LaneId lane);

    /**
     * Read access to one sparse overlay's cells in ascending localIdx order
     * (the canonical order hashing and the codec rely on). Every view served
     * by {@link World#chunk} supports this; the default throws so minimal
     * test fakes that carry no overlays stay compilable.
     */
    default OverlayView overlay(OverlayId overlay) {
        throw new UnsupportedOperationException("this view carries no overlays");
    }
}
