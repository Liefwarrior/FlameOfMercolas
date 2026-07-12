package com.trojia.sim.world.change;

import com.trojia.sim.world.Coords;

import java.util.Arrays;

/**
 * THE shared frontier utility (ARCHITECTURE.md §1.1 #15): a packed-int FIFO
 * with per-chunk membership bitsets for O(1) dedupe. Insertion order is the
 * deterministic processing order; every field system's frontier (thermal,
 * fluids, light seeds) is an instance of this.
 *
 * <p>Allocation-free after construction; membership bitsets are allocated
 * lazily per touched chunk. Not thread-safe (single-threaded v0; parallel v1
 * stages per-chunk frontiers and merges at phase barriers).
 *
 * <p><b>Allocation discipline:</b> steady-state {@link #add}/{@link #poll} is
 * allocation-free. The only allocations are the amortized power-of-two ring
 * doubling (capacity is a retained high-water mark) and one lazy 1 KB bitset
 * per touched chunk, freed on {@link #dropChunk}/{@link #clear} so frozen
 * chunks reclaim their memory. {@link #dropChunk} compacts the ring in place
 * — no allocation, survivor order preserved.
 *
 * <p><b>Determinism:</b> state is a pure function of the operation sequence;
 * {@link #poll} order is first-{@link #add} order, independent of chunk
 * layout, growth timing or drop history.
 */
public final class ActiveSet {

    /** 8192 tiles / 64 bits: words in one chunk's membership bitset. */
    private static final int WORDS_PER_CHUNK = Coords.TILES_PER_CHUNK / 64;
    /** Initial ring capacity; must be a power of two. */
    private static final int INITIAL_CAPACITY = 1024;

    private final Coords coords;
    /** Indexed by chunkIndex; {@code null} until the chunk is first touched. */
    private final long[][] membership;
    /** Power-of-two ring buffer of queued packed positions. */
    private int[] ring = new int[INITIAL_CAPACITY];
    /** Physical index of the oldest queued cell. */
    private int head;
    private int size;

    /** A frontier sized for {@code coords}' world. */
    public ActiveSet(Coords coords) {
        if (coords == null) {
            throw new IllegalArgumentException("coords must be non-null");
        }
        this.coords = coords;
        this.membership = new long[coords.chunkCount()][];
    }

    /**
     * Adds {@code packedPos} unless already present; returns whether it was
     * newly added. Duplicate-tolerant intake: feeding every change-log entry
     * through this is the sanctioned dedupe.
     */
    public boolean add(int packedPos) {
        int chunkIndex = coords.chunkIndex(packedPos);
        long[] bits = membership[chunkIndex];
        if (bits == null) {
            bits = new long[WORDS_PER_CHUNK];
            membership[chunkIndex] = bits;
        }
        int localIdx = coords.localIdx(packedPos);
        int word = localIdx >>> 6;
        long bit = 1L << localIdx;
        if ((bits[word] & bit) != 0) {
            return false;
        }
        bits[word] |= bit;
        if (size == ring.length) {
            grow();
        }
        ring[(head + size) & (ring.length - 1)] = packedPos;
        size++;
        return true;
    }

    /** Whether {@code packedPos} is currently in the set. */
    public boolean contains(int packedPos) {
        long[] bits = membership[coords.chunkIndex(packedPos)];
        if (bits == null) {
            return false;
        }
        int localIdx = coords.localIdx(packedPos);
        return (bits[localIdx >>> 6] & (1L << localIdx)) != 0;
    }

    /** Whether the frontier is empty. */
    public boolean isEmpty() {
        return size == 0;
    }

    /** Number of queued cells. */
    public int size() {
        return size;
    }

    /**
     * Removes and returns the oldest cell (FIFO). Undefined if empty (hot
     * path: callers must check {@link #isEmpty()}).
     */
    public int poll() {
        int packedPos = ring[head];
        head = (head + 1) & (ring.length - 1);
        size--;
        int localIdx = coords.localIdx(packedPos);
        membership[coords.chunkIndex(packedPos)][localIdx >>> 6] &= ~(1L << localIdx);
        return packedPos;
    }

    /**
     * Drops every queued cell of {@code chunkIndex} (on ChunkFrozen). Queued
     * order of the survivors is preserved; the chunk's bitset is released.
     */
    public void dropChunk(int chunkIndex) {
        if (membership[chunkIndex] == null) {
            return;
        }
        membership[chunkIndex] = null;
        int mask = ring.length - 1;
        int kept = 0;
        for (int i = 0; i < size; i++) {
            int packedPos = ring[(head + i) & mask];
            if (coords.chunkIndex(packedPos) != chunkIndex) {
                ring[(head + kept) & mask] = packedPos;
                kept++;
            }
        }
        size = kept;
    }

    /** Empties the frontier and releases all membership bitsets. */
    public void clear() {
        Arrays.fill(membership, null);
        head = 0;
        size = 0;
    }

    /** Doubles the ring, straightening the queue so {@code head} returns to 0. */
    private void grow() {
        int[] doubled = new int[ring.length * 2];
        int mask = ring.length - 1;
        for (int i = 0; i < size; i++) {
            doubled[i] = ring[(head + i) & mask];
        }
        ring = doubled;
        head = 0;
    }
}
