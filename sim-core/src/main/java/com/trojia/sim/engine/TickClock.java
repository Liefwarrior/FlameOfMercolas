package com.trojia.sim.engine;

/**
 * The engine-owned tick counter. One simulated tick is exactly
 * {@link #MILLIS_PER_TICK} milliseconds of world time; wall-clock pacing is a
 * client concern (the observer's fixed-timestep driver), never the engine's.
 *
 * <p>Only the engine advances the clock; systems read the current tick via
 * {@code TickContext.tick()}.
 */
public final class TickClock {

    /** Simulated milliseconds per tick: 100 ms (10 ticks per world second). */
    public static final int MILLIS_PER_TICK = 100;

    private long tick;

    /** A clock at tick 0 (no tick simulated yet). */
    public TickClock() {
    }

    /** The tick currently being (or last) simulated; 0 before the first tick. */
    public long currentTick() {
        return tick;
    }

    /** Simulated world time in milliseconds: {@code currentTick() * 100}. */
    public long simTimeMillis() {
        return tick * MILLIS_PER_TICK;
    }

    /** Engine-only: advances to the next tick. */
    public void advance() {
        tick++;
    }

    /** Engine-only (load path): jumps the clock to a saved tick. */
    public void resetTo(long tick) {
        if (tick < 0) {
            throw new IllegalArgumentException("tick must be >= 0: " + tick);
        }
        this.tick = tick;
    }
}
