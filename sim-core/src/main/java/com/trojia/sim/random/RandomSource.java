package com.trojia.sim.random;

/**
 * The one counter-based random source of the simulation (ARCHITECTURE.md §1.1
 * #16). Every random decision in sim-core derives from the pure chain
 *
 * <pre>
 *   h = mix64(worldSeed + TICK_STRIDE * tick);
 *   h = mix64(h ^ systemSalt);
 *   h = mix64(h + spatialKey);
 *   h = mix64(h + drawIndex);
 * </pre>
 *
 * <p>There are no stream positions and no hidden state: the only persisted RNG
 * state is {@code worldSeed}, so save/load round-trips are deterministic by
 * construction. The mixing function and {@link #TICK_STRIDE} are pinned before
 * the first golden-master bless and must never change afterwards.
 *
 * <p>An instance handed to a system (via {@code TickContext.rng()}) is already
 * bound to that system's salt and the current tick; the system supplies only
 * the spatial key and draw index.
 */
public interface RandomSource {

    /**
     * K1: the golden-ratio tick stride folded into the seed each tick. Part of
     * the pinned mixing spec — changing it invalidates every golden master.
     */
    long TICK_STRIDE = 0x9E3779B97F4A7C15L;

    /**
     * Allocation-free hot path: the {@code drawIndex}-th 64-bit draw for
     * {@code spatialKey} at the bound (tick, system). Pure: the same
     * (worldSeed, tick, systemSalt, spatialKey, drawIndex) tuple always yields
     * the same value, on every platform and regardless of call order.
     *
     * @param spatialKey canonical key of the deciding entity — a {@code PackedPos}
     *                   (zero-extended), a chunkIndex, a siteId, or any other
     *                   stable id; callers must document their key scheme
     * @param drawIndex  0-based index for multiple draws against one key per tick
     */
    long draw(long spatialKey, int drawIndex);

    /** Convenience for the common single-draw case: {@code draw(spatialKey, 0)}. */
    default long draw(long spatialKey) {
        return draw(spatialKey, 0);
    }

    /**
     * THE 64-bit finalizer (SplitMix64) used by every derivation step in
     * sim-core, including {@link com.trojia.sim.engine.SystemId} salts and
     * macro hazard rolls. Pure integer math; pinned constants.
     */
    static long mix64(long z) {
        z = (z ^ (z >>> 30)) * 0xBF58476D1CE4E5B9L;
        z = (z ^ (z >>> 27)) * 0x94D049BB133111EBL;
        return z ^ (z >>> 31);
    }
}
