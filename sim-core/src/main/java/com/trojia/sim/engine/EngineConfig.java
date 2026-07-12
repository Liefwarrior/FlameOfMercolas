package com.trojia.sim.engine;

/**
 * Immutable per-engine configuration. The world seed is the ONLY persisted
 * RNG state in the entire simulation (ARCHITECTURE.md §1.1 #16) — every
 * random draw derives from it, the tick, a system salt, a spatial key and a
 * draw index.
 *
 * @param worldSeed the world seed all counter-based RNG derives from
 */
public record EngineConfig(long worldSeed) {
}
