package com.trojia.sim.engine;

/**
 * Wall-clock timings of one completed tick. DIAGNOSTICS ONLY: never an input
 * to simulation logic — timings are non-deterministic by nature and must not
 * influence any state-affecting decision (budgets that shape behavior, like
 * the light visit cap, are counted in deterministic units, not nanos).
 *
 * @param tick       the measured tick
 * @param totalNanos wall-clock nanos for the whole tick
 * @param phaseNanos wall-clock nanos per phase, indexed by {@link TickPhase#ordinal()}
 */
public record TickProfile(long tick, long totalNanos, long[] phaseNanos) {

    public TickProfile {
        phaseNanos = phaseNanos.clone(); // never share the caller's mutable array
    }

    /** An all-zero profile (before the first tick). */
    public static TickProfile empty() {
        return new TickProfile(0L, 0L, new long[TickPhase.values().length]);
    }

    /** Nanos spent in {@code phase}. */
    public long nanosIn(TickPhase phase) {
        return phaseNanos[phase.ordinal()];
    }
}
