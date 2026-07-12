package com.trojia.sim.engine;

import com.trojia.sim.world.World;

import java.io.IOException;
import java.nio.file.Path;

/**
 * One running simulation (ARCHITECTURE.md §3). Owns the tick clock, the phase
 * loop, the event bus, the input gate and per-system RNG binding; created only
 * through {@link Simulations}. Multiple engines coexist in one JVM with zero
 * shared mutable state.
 *
 * <p>Single-threaded in v0: all methods are called from the owning thread;
 * {@link #submit} is the only intake for external input and is legal between
 * ticks (clients queue on their side and flush between frames).
 */
public interface SimulationEngine {

    /** The tick last completed; 0 before the first tick. */
    long currentTick();

    /**
     * Read access to the world this engine ticks (renderers and harnesses
     * hash/diff through it between ticks), or {@code null} on a world-less
     * bootstrap engine. Never the {@code TickableWorld} face — driving the
     * world's lifecycle stays engine-only.
     */
    World world();

    /** Advances exactly one tick through the full phase pipeline. */
    void tick();

    /**
     * Advances exactly {@code ticks} ticks.
     *
     * @param ticks number of ticks, non-negative
     */
    void step(int ticks);

    /**
     * Queues an external command; drained by the InputGate at the next
     * TICK_BEGIN in arrival order (arrival order is logged, so replays are
     * exact).
     */
    void submit(SimCommand command);

    /**
     * Saves the complete simulation to a TROJSAV at {@code file}. Legal only
     * between ticks (TICK_END boundary); atomic tmp + rename. The saved run
     * satisfies {@code run K+N ≡ save@K, load, run N}.
     */
    void save(Path file) throws IOException;

    /** Diagnostics of the last completed tick; {@code TickProfile.empty()} before it. */
    TickProfile inspect();
}
