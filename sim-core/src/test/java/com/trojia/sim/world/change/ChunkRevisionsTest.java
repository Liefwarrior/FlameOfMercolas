package com.trojia.sim.world.change;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * Contract tests for {@link ChunkRevisions}: revisions bump only at commit
 * and only for marked chunks, changedBits publish exactly the last committed
 * dirty tick (no leakage through the recycled double buffer), tick
 * monotonicity, and cross-instance determinism.
 */
final class ChunkRevisionsTest {

    private static final int CHUNKS = 27;
    private static final int WORDS = 128; // 8192 bits / 64

    @Test
    void revisionsBumpOnlyAtCommitAndOnlyForMarkedChunks() {
        ChunkRevisions revisions = new ChunkRevisions(CHUNKS);
        revisions.mark(3, 100);
        revisions.mark(3, 101); // idempotent within the tick: one bump
        revisions.mark(7, 0);

        assertEquals(0, revisions.revision(3)); // not yet committed
        revisions.commit(1);

        assertEquals(1, revisions.revision(3));
        assertEquals(1, revisions.revision(7));
        assertEquals(0, revisions.revision(0));

        revisions.commit(2); // clean tick: nothing bumps
        assertEquals(1, revisions.revision(3));
    }

    @Test
    void changedBitsPublishExactlyTheLastCommittedTick() {
        ChunkRevisions revisions = new ChunkRevisions(CHUNKS);

        revisions.mark(5, 5);
        revisions.mark(5, 700);
        revisions.commit(1);
        assertArrayEquals(bits(5, 700), revisions.changedBits(5));

        // Second dirty tick: only tick 2's tile — nothing may leak from tick 1.
        revisions.mark(5, 9);
        revisions.commit(2);
        assertArrayEquals(bits(9), revisions.changedBits(5));

        // Third dirty tick exercises the fully recycled double buffer.
        revisions.mark(5, 8191);
        revisions.commit(3);
        assertArrayEquals(bits(8191), revisions.changedBits(5));
        assertEquals(3, revisions.revision(5));
    }

    @Test
    void quietChunksKeepTheirLastPublishedBitsAndRevision() {
        ChunkRevisions revisions = new ChunkRevisions(CHUNKS);
        revisions.mark(4, 42);
        revisions.commit(1);

        revisions.mark(11, 7); // a different chunk is dirty on tick 2
        revisions.commit(2);

        // Chunk 4's revision delta since tick 1 is 0, its publication intact.
        assertEquals(1, revisions.revision(4));
        assertArrayEquals(bits(42), revisions.changedBits(4));
        assertEquals(1, revisions.revision(11));
        assertArrayEquals(bits(7), revisions.changedBits(11));
    }

    @Test
    void neverDirtyChunksReportEmptyChangedBits() {
        ChunkRevisions revisions = new ChunkRevisions(CHUNKS);
        assertEquals(0, revisions.revision(2));
        assertArrayEquals(new long[WORDS], revisions.changedBits(2));
    }

    @Test
    void commitTicksMustStrictlyIncrease() {
        ChunkRevisions revisions = new ChunkRevisions(CHUNKS);
        revisions.commit(1);
        assertThrows(IllegalStateException.class, () -> revisions.commit(1));
        assertThrows(IllegalStateException.class, () -> revisions.commit(0));
        revisions.commit(2);
    }

    @Test
    void constructorRejectsNonPositiveChunkCounts() {
        assertThrows(IllegalArgumentException.class, () -> new ChunkRevisions(0));
        assertThrows(IllegalArgumentException.class, () -> new ChunkRevisions(-1));
    }

    @Test
    void identicalMarkCommitSequencesYieldIdenticalState() {
        ChunkRevisions first = runScriptedSequence();
        ChunkRevisions second = runScriptedSequence();

        for (int chunk = 0; chunk < CHUNKS; chunk++) {
            assertEquals(first.revision(chunk), second.revision(chunk),
                    "revision of chunk " + chunk);
            assertArrayEquals(first.changedBits(chunk), second.changedBits(chunk),
                    "changedBits of chunk " + chunk);
        }
    }

    /** A fixed mark/commit script with out-of-order chunk arrivals. */
    private static ChunkRevisions runScriptedSequence() {
        ChunkRevisions revisions = new ChunkRevisions(CHUNKS);
        for (long tick = 1; tick <= 30; tick++) {
            for (int i = 0; i < 12; i++) {
                // Arrival order deliberately not ascending by chunk.
                int chunk = (int) ((tick * 11 + i * 5) % CHUNKS);
                int localIdx = (int) ((tick * 131 + i * 37) % 8192);
                revisions.mark(chunk, localIdx);
            }
            revisions.commit(tick);
        }
        return revisions;
    }

    /** A 128-long bitset with the given localIdx bits set. */
    private static long[] bits(int... localIndices) {
        long[] words = new long[WORDS];
        for (int localIdx : localIndices) {
            words[localIdx >>> 6] |= 1L << localIdx;
        }
        return words;
    }
}
