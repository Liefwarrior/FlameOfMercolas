package com.trojia.sim.world.change;

import com.trojia.sim.world.LaneId;

/**
 * One consumer's private cursor over one lane's change log: allocation-free
 * pulls of packed {@code PackedPos} ints in append order. Entries are
 * duplicate-tolerant — a tile written twice appears twice; consumers dedupe
 * into their {@link ActiveSet} frontier.
 *
 * <p>Readers are minted only by {@link ChangeLogs#register} before sealing.
 * A reader that lags behind the compaction cap trips an assertion at
 * commitTick — logs never wrap silently.
 */
public interface ChangeLogReader {

    /** The lane this reader consumes. */
    LaneId lane();

    /** Whether an unconsumed entry is available. */
    boolean hasNext();

    /**
     * The next changed cell as a packed position, advancing the cursor.
     * Undefined if {@link #hasNext()} is false (hot path: no exception
     * guarantee — callers must check).
     */
    int next();
}
