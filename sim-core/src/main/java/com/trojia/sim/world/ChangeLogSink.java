package com.trojia.sim.world;

/**
 * The world's write-side seam onto the per-lane change logs: exactly the
 * three calls {@link DenseChunkWriter} and {@link DenseWorld#commitTick} make
 * against {@code com.trojia.sim.world.change.ChangeLogs}. Production wiring
 * is {@link ChangeLogsAdapter}; unit tests inject recording fakes via the
 * package-private {@link WorldBuilder#changeLogSink} hook so the world is
 * testable independently of the change-log implementation.
 */
interface ChangeLogSink {

    /** Whether {@code lane} has at least one registered reader (writer-side skip check). */
    boolean hasReaders(LaneId lane);

    /** Appends a changed cell to {@code lane}'s log; only called when {@link #hasReaders} is true. */
    void append(LaneId lane, int packedPos);

    /** TICK_END compaction, forwarded from {@link TickableWorld#commitTick}. */
    void compact(long tick);
}
