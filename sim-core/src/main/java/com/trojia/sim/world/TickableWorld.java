package com.trojia.sim.world;

/**
 * The scheduler-only face of the world: the engine calls {@link #beginTick}
 * at TICK_BEGIN and {@link #commitTick} at TICK_END; no simulation system
 * ever sees this interface (systems get {@link World}).
 */
public interface TickableWorld extends World {

    /** Opens tick {@code tick}: resets per-tick write accounting, advances change-log epochs. */
    void beginTick(long tick);

    /**
     * Commits tick {@code tick}: bumps revisions of dirty chunks in ascending
     * chunkIndex order, publishes changedBits, compacts change logs (asserting
     * the reader-lag cap), and clears per-tick dirty state. Saves are legal
     * only after this returns and before the next {@link #beginTick}.
     */
    void commitTick(long tick);
}
