package com.trojia.sim.world;

import com.trojia.sim.world.change.ChunkRevisions;

/**
 * Production {@link RevisionSink}: straight delegation to the world's
 * {@link ChunkRevisions} instance (the one returned by {@link World#revisions()}).
 */
final class RevisionsAdapter implements RevisionSink {

    private final ChunkRevisions revisions;

    RevisionsAdapter(ChunkRevisions revisions) {
        this.revisions = revisions;
    }

    @Override
    public void mark(int chunkIndex, int localIdx) {
        revisions.mark(chunkIndex, localIdx);
    }

    @Override
    public void commit(long tick) {
        revisions.commit(tick);
    }
}
