package com.trojia.sim.world;

/**
 * The world's write-side seam onto the per-chunk revision tracker: exactly
 * the two calls {@link DenseChunkWriter} and {@link DenseWorld#commitTick}
 * make against {@code com.trojia.sim.world.change.ChunkRevisions}. Production
 * wiring is {@link RevisionsAdapter}; unit tests inject recording fakes via
 * the package-private {@link WorldBuilder#revisionSink} hook.
 */
interface RevisionSink {

    /** Records a tile write for this tick's changedBits (every accepted ChunkWriter write). */
    void mark(int chunkIndex, int localIdx);

    /** Revision bump + changedBits publish, forwarded from {@link TickableWorld#commitTick}. */
    void commit(long tick);
}
