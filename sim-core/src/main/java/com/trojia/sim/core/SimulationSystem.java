package com.trojia.sim.core;

/**
 * One stage of the simulation pipeline (fluids, thermal, fire, light, ...).
 *
 * <p>Systems are invoked in the fixed order defined by {@link SimulationPipeline};
 * that order is part of the determinism contract and never varies at runtime.
 */
public interface SimulationSystem {

    /** Stable unique identifier; also names this system's RNG stream and dirty set. */
    String id();

    /** Advances this system by exactly one tick. Must be deterministic. */
    void tick(SimulationContext context);
}
