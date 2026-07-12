package com.trojia.sim.world.change;

import com.trojia.sim.world.Coords;

import java.util.Arrays;

/**
 * Per-chunk revision counters + per-tick changedBits: the observer's diff key
 * (ARCHITECTURE.md §3). Revisions are bumped for dirty chunks at commitTick in
 * ascending chunkIndex order; {@link #changedBits} is valid ONLY when the
 * observer's seen revision lags by exactly 1 — any larger delta means "remesh
 * the whole chunk".
 *
 * <p>Read side is between-ticks only (observer render thread reads after
 * commitTick, before the next beginTick). Marking is ChunkWriter-only.
 *
 * <p><b>Allocation discipline:</b> {@link #mark} is allocation-free in steady
 * state — each chunk owns at most two lazily allocated 1 KB bitsets (the
 * accumulating tick and the last published tick) that {@link #commit} swaps
 * and recycles forever; a chunk's first-ever dirty tick allocates them, never
 * again. The dirty-chunk list is a growable int array (retained high-water
 * capacity) sorted in place at commit.
 *
 * <p><b>Determinism:</b> revisions and published bits are a pure function of
 * the mark/commit sequence; the commit bump order is ascending chunkIndex
 * regardless of mark arrival order.
 */
public final class ChunkRevisions {

    /** 8192 tiles / 64 bits: words in one chunk's changedBits set. */
    private static final int WORDS_PER_CHUNK = Coords.TILES_PER_CHUNK / 64;

    /** Committed revision per chunk. */
    private final long[] revisions;
    /** Bits accumulating during the current tick; {@code null} until first needed. */
    private final long[][] pending;
    /** Bits of each chunk's last committed dirty tick; {@code null} until first commit. */
    private final long[][] published;
    /** Whether the chunk is already on this tick's dirty list. */
    private final boolean[] dirty;
    /** Chunk indices marked this tick, arrival order until sorted at commit. */
    private int[] dirtyChunks = new int[64];
    private int dirtyCount;
    /** All-zero borrow returned for chunks that never committed a dirty tick. */
    private final long[] emptyBits = new long[WORDS_PER_CHUNK];
    private long lastCommittedTick = Long.MIN_VALUE;

    /** Revision storage for {@code chunkCount} chunks, all starting at 0. */
    public ChunkRevisions(int chunkCount) {
        if (chunkCount <= 0) {
            throw new IllegalArgumentException("chunkCount must be positive: " + chunkCount);
        }
        this.revisions = new long[chunkCount];
        this.pending = new long[chunkCount][];
        this.published = new long[chunkCount][];
        this.dirty = new boolean[chunkCount];
    }

    /** The committed revision of {@code chunkIndex} (bumped only at commitTick). */
    public long revision(int chunkIndex) {
        return revisions[chunkIndex];
    }

    /**
     * The changed-tile bitset of {@code chunkIndex}'s LAST committed tick:
     * 8192 bits indexed by localIdx, as a borrowed 128-long array. Valid only
     * for a revision delta of exactly 1; contents are undefined otherwise.
     * Borrowed — callers must not mutate or retain past the next commit.
     */
    public long[] changedBits(int chunkIndex) {
        long[] bits = published[chunkIndex];
        return bits != null ? bits : emptyBits;
    }

    /** ChunkWriter-only: records a tile write for this tick's changedBits. */
    public void mark(int chunkIndex, int localIdx) {
        if (!dirty[chunkIndex]) {
            dirty[chunkIndex] = true;
            if (dirtyCount == dirtyChunks.length) {
                dirtyChunks = Arrays.copyOf(dirtyChunks, dirtyCount * 2);
            }
            dirtyChunks[dirtyCount++] = chunkIndex;
            if (pending[chunkIndex] == null) {
                pending[chunkIndex] = new long[WORDS_PER_CHUNK];
            }
        }
        pending[chunkIndex][localIdx >>> 6] |= 1L << localIdx;
    }

    /**
     * World-commit-only: bumps revisions of all chunks marked this tick, in
     * ascending chunkIndex order, publishes their changedBits and resets the
     * per-tick marks. Ticks must be strictly increasing; a repeated or
     * rewound tick throws {@code IllegalStateException}.
     */
    public void commit(long tick) {
        if (tick <= lastCommittedTick) {
            throw new IllegalStateException("commit tick must increase: " + tick
                    + " after " + lastCommittedTick);
        }
        lastCommittedTick = tick;
        Arrays.sort(dirtyChunks, 0, dirtyCount);
        for (int i = 0; i < dirtyCount; i++) {
            int chunkIndex = dirtyChunks[i];
            revisions[chunkIndex]++;
            long[] recycled = published[chunkIndex];
            published[chunkIndex] = pending[chunkIndex];
            if (recycled != null) {
                Arrays.fill(recycled, 0L);
            }
            pending[chunkIndex] = recycled;
            dirty[chunkIndex] = false;
        }
        dirtyCount = 0;
    }
}
