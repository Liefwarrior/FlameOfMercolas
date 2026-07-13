package com.trojia.client.time;

/**
 * Observer time-control settings (ARCHITECTURE section 3, client-observer entry:
 * {@code SimulationDriver} is a fixed-timestep loop whose catch-up clamp scales with
 * speed, capped at whole ticks fitting a 12 ms frame budget). This enum is pure state —
 * GL-free, driver-free — carrying only each setting's real-time multiplier over the
 * canonical tick rate of {@value #BASE_TICK_MILLIS} ms/tick (ARCHITECTURE section 3,
 * {@code TickClock}).
 *
 * <p>Driver contract per setting:
 * <ul>
 *   <li>{@link #PAUSED} — never ticks; brush edits queue as {@code SimCommand}s and
 *       land on the next executed tick.</li>
 *   <li>{@link #STEP} — never ticks on its own; each explicit step request (the
 *       time-bar step button / hotkey) runs exactly one tick, so
 *       {@code run K+N == save@K, load, run N} style comparisons can be driven by
 *       hand.</li>
 *   <li>{@link #RUN} — real time: one tick per {@value #BASE_TICK_MILLIS} ms.</li>
 *   <li>{@link #FAST} — fast-forward at {@code 4x} (v0 default, batch-veto anytime).
 *       The multiplier is a <em>target</em> rate: each frame the driver executes at
 *       most the whole ticks that fit {@value #FRAME_TICK_BUDGET_MILLIS} ms and drops
 *       the remaining backlog rather than spiraling (clamp scales with speed).</li>
 * </ul>
 *
 * <p>Determinism note: speed settings change only <em>when</em> ticks execute, never
 * what they compute — a world advanced N ticks is bit-identical at any speed.
 */
public enum SpeedSetting {

    /** Simulation halted; no ticks execute. */
    PAUSED(0),

    /** Manual stepping: one tick per explicit step request, otherwise halted. */
    STEP(0),

    /** Real time: 1&times; the canonical {@code TickClock} rate (10 ticks/s). */
    RUN(1),

    /** Fast-forward: 4&times; real time (40 ticks/s target), frame-budget clamped. */
    FAST(4);

    /** Canonical tick period, ms (ARCHITECTURE section 3, {@code TickClock}). */
    public static final int BASE_TICK_MILLIS = 100;

    /**
     * Per-frame tick execution budget, ms (ARCHITECTURE section 3,
     * {@code SimulationDriver}: "MAX = whole ticks in 12 ms"). The driver stops
     * catch-up ticking once a frame's tick work exceeds this and discards the rest of
     * the backlog.
     */
    public static final int FRAME_TICK_BUDGET_MILLIS = 12;

    private final int multiplier;

    SpeedSetting(int multiplier) {
        this.multiplier = multiplier;
    }

    /**
     * Real-time multiplier over the canonical tick rate; {@code 0} for the settings
     * that never tick automatically ({@link #PAUSED}, {@link #STEP}).
     */
    public int multiplier() {
        return multiplier;
    }

    /** Whether the driver accumulates time and executes ticks without user input. */
    public boolean autoAdvances() {
        return multiplier > 0;
    }

    /** Whether explicit step requests execute a tick (true only for {@link #STEP}). */
    public boolean manualStep() {
        return this == STEP;
    }

    /**
     * Target wall-clock period between ticks at this setting.
     *
     * @return {@code BASE_TICK_MILLIS / multiplier} (RUN: 100, FAST: 25)
     * @throws IllegalStateException for {@link #PAUSED} and {@link #STEP}, which have
     *                               no period — guard with {@link #autoAdvances()}
     */
    public int tickPeriodMillis() {
        if (!autoAdvances()) {
            throw new IllegalStateException(name() + " has no tick period");
        }
        return BASE_TICK_MILLIS / multiplier;
    }

    /** Target tick rate: {@code 0} for non-advancing settings, else {@code 10 * multiplier}. */
    public int ticksPerSecond() {
        return autoAdvances() ? 1000 / tickPeriodMillis() : 0;
    }
}
