package com.trojia.sim.actor;

import java.util.Arrays;

/**
 * A deterministic open-addressing primitive map {@code packedCell -> occupantCount}
 * backing the actor-actor occupancy cap ({@link Actor#MAX_OCCUPANTS_PER_CELL}). One of
 * these lives in {@link ActorsSystem}, rebuilt each tick from every actor's current cell
 * and kept live as actors move (see {@link Actor.OccupancyQuery}).
 *
 * <p><b>Why not a dense array?</b> Packed cell positions ({@link com.trojia.sim.world.PackedPos})
 * span ~2<sup>30</sup>, so a dense {@code int[]} over the whole packed space is unthinkable —
 * only a few hundred cells are ever occupied. This is a linear-probing hash table over
 * <em>primitive</em> arrays, so it carries zero {@code java.util} collection state (the sim-core
 * determinism/ArchUnit contract bans {@code HashMap}/{@code HashSet} state fields) while staying
 * O(1) amortized.
 *
 * <p><b>Determinism.</b> Every operation is a pure function of the current table state: a fixed
 * multiplicative hash, deterministic linear probing, and backward-shift deletion (no tombstones,
 * so the table never depends on historical delete order). Iteration order is never observed —
 * only point {@link #count(int)} reads feed simulation decisions.
 *
 * <p><b>Layout.</b> Two parallel primitive arrays: {@code keys} (the packed cell, or
 * {@link #EMPTY} = {@code -1} for a free slot — every real packed cell is {@code >= 0}, so
 * {@code -1} is an unambiguous sentinel) and {@code counts} (a {@code short}: a single cell never
 * holds more than {@link Actor#MAX_OCCUPANTS_PER_CELL} live occupants during simulation, and even
 * the transient rebuild total per distinct cell is bounded by the actor count). Capacity is always
 * a power of two so the probe index is a mask-AND; it grows (doubling + rehash) if the live
 * distinct-cell count would exceed a 0.75 load factor.
 */
public final class OccupancyIndex {

    /** Free-slot sentinel: no real {@link com.trojia.sim.world.PackedPos} packs to a negative int. */
    private static final int EMPTY = -1;

    /** Fibonacci-hashing multiplier (2<sup>32</sup> / golden ratio, odd) — spreads packed cells well. */
    private static final int HASH_MULTIPLIER = 0x9E3779B1;

    /** Grow when {@code distinctCells * 4 >= capacity * 3} (load factor 0.75). */
    private static final int LOAD_NUM = 3;
    private static final int LOAD_DEN = 4;

    private int[] keys;
    private short[] counts;
    private int mask;
    /** Number of slots currently holding a live (count &gt; 0) cell. */
    private int distinctCells;

    /**
     * Sizes the table for roughly {@code expectedActors} occupied cells: capacity is the next
     * power of two at or above {@code 4 * expectedActors} (min 16), so the initial load factor
     * stays well under 0.75 for the common case and no rehash is needed at steady state.
     */
    public OccupancyIndex(int expectedActors) {
        int cap = capacityFor(expectedActors);
        this.keys = new int[cap];
        this.counts = new short[cap];
        this.mask = cap - 1;
        Arrays.fill(keys, EMPTY);
    }

    private static int capacityFor(int expectedActors) {
        int target = Math.max(16, expectedActors * 4);
        int cap = Integer.highestOneBit(target);
        if (cap < target) {
            cap <<= 1;
        }
        return cap;
    }

    /** Empties the table (all slots free, every count zero) — the per-tick rebuild's first step. */
    public void clear() {
        Arrays.fill(keys, EMPTY);
        Arrays.fill(counts, (short) 0);
        distinctCells = 0;
    }

    /** The number of actors currently recorded on {@code cell} (0 if the cell is absent). */
    public int count(int cell) {
        int i = slotOf(cell);
        return keys[i] == cell ? counts[i] : 0;
    }

    /** Records one more occupant on {@code cell} (inserting the cell if new). */
    public void add(int cell) {
        int i = slotOf(cell);
        if (keys[i] == cell) {
            counts[i]++;
            return;
        }
        keys[i] = cell;
        counts[i] = 1;
        distinctCells++;
        if (distinctCells * LOAD_DEN >= keys.length * LOAD_NUM) {
            grow();
        }
    }

    /**
     * Removes one occupant from {@code cell}; when the last one leaves, the slot is freed via
     * backward-shift deletion so the probe chains stay intact. A no-op if {@code cell} is absent
     * (defensive — the caller always pairs {@code remove(from)} with a prior {@code add(from)}).
     */
    public void remove(int cell) {
        int i = slotOf(cell);
        if (keys[i] != cell) {
            return;
        }
        counts[i]--;
        if (counts[i] > 0) {
            return;
        }
        deleteSlot(i);
        distinctCells--;
    }

    /**
     * Finds the slot holding {@code cell}, or — if absent — the first free slot on its probe
     * chain (the insertion point). Linear probing with wraparound; guaranteed to terminate
     * because the load factor is capped below 1.
     */
    private int slotOf(int cell) {
        int i = (cell * HASH_MULTIPLIER) & mask;
        while (keys[i] != EMPTY && keys[i] != cell) {
            i = (i + 1) & mask;
        }
        return i;
    }

    /** The home (unprobed) slot a cell hashes to — needed by backward-shift deletion. */
    private int home(int cell) {
        return (cell * HASH_MULTIPLIER) & mask;
    }

    /**
     * Backward-shift deletion (Knuth 6.4 algorithm R): closes the hole at {@code hole} by
     * pulling forward any following element whose home slot lies outside the open interval
     * {@code (hole, j]}, keeping every probe chain contiguous with no tombstones.
     */
    private void deleteSlot(int hole) {
        keys[hole] = EMPTY;
        counts[hole] = 0;
        int j = hole;
        while (true) {
            j = (j + 1) & mask;
            int k = keys[j];
            if (k == EMPTY) {
                return;
            }
            int h = home(k);
            // Keep element j where it is when its home is cyclically within (hole, j].
            boolean withinGap = hole <= j ? (hole < h && h <= j) : (hole < h || h <= j);
            if (withinGap) {
                continue;
            }
            keys[hole] = keys[j];
            counts[hole] = counts[j];
            keys[j] = EMPTY;
            counts[j] = 0;
            hole = j;
        }
    }

    private void grow() {
        int[] oldKeys = keys;
        short[] oldCounts = counts;
        int newCap = keys.length << 1;
        keys = new int[newCap];
        counts = new short[newCap];
        mask = newCap - 1;
        Arrays.fill(keys, EMPTY);
        distinctCells = 0;
        for (int s = 0; s < oldKeys.length; s++) {
            if (oldKeys[s] != EMPTY && oldCounts[s] > 0) {
                int i = slotOf(oldKeys[s]);
                keys[i] = oldKeys[s];
                counts[i] = oldCounts[s];
                distinctCells++;
            }
        }
    }

    /** The current backing-table capacity (a power of two) — for tests/introspection only. */
    int capacity() {
        return keys.length;
    }

    /** The number of distinct occupied cells — for tests/introspection only. */
    int distinctCells() {
        return distinctCells;
    }
}
