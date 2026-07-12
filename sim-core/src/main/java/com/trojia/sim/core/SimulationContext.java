package com.trojia.sim.core;

/**
 * Per-tick facade handed to every {@link SimulationSystem}.
 *
 * <p>Grows with the engine (world access, tile writers, event queue, RNG streams);
 * systems must reach the world only through this context, never through globals.
 */
public interface SimulationContext {

    /** The tick currently being simulated (first tick is 1). */
    long currentTick();
}
