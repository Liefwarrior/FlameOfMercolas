package com.trojia.sim.time;

/**
 * The world's tick counter. Only {@code com.trojia.sim.core.Simulation} should
 * call {@link #advance()}; everything else treats the clock as read-only.
 *
 * <p>Grows in M2: run modes (paused/step/run/fast), speed control, scheduling.
 */
public final class TickClock {

    private long tick;

    /** The tick currently being (or last) simulated; 0 before the first tick. */
    public long currentTick() {
        return tick;
    }

    public void advance() {
        tick++;
    }
}
