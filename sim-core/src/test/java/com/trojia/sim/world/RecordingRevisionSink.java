package com.trojia.sim.world;

import java.util.ArrayList;
import java.util.List;

/**
 * Recording {@link RevisionSink} fake: journals every changedBits mark as
 * {@code chunkIndex:localIdx} and every commit tick. Lets the writer tests
 * verify revision traffic without the real {@code world.change.ChunkRevisions}
 * (built in parallel).
 */
final class RecordingRevisionSink implements RevisionSink {

    final List<String> marks = new ArrayList<>();
    final List<Long> commits = new ArrayList<>();
    /** Optional shared journal for cross-sink ordering assertions. */
    List<String> journal;

    @Override
    public void mark(int chunkIndex, int localIdx) {
        marks.add(chunkIndex + ":" + localIdx);
    }

    @Override
    public void commit(long tick) {
        commits.add(tick);
        if (journal != null) {
            journal.add("revisions.commit@" + tick);
        }
    }
}
