package com.trojia.sim.random;

/**
 * The canonical {@link RandomSource} implementation: stateless-by-construction
 * counter-based derivation per ARCHITECTURE.md §1.1 #16. One instance is owned
 * per registered system; the engine rebinds it to the current tick at the top
 * of every tick via {@link #beginTick(long)}.
 *
 * <p>{@link #draw(long, int)} is allocation-free and branch-free. The only
 * state is the per-tick prefix hash {@code (worldSeed, tick, systemSalt)};
 * nothing here is ever serialized — the world seed alone reproduces every draw.
 */
public final class CounterRandomSource implements RandomSource {

    private final long worldSeed;
    private final long systemSalt;

    /** mix64(mix64(worldSeed + TICK_STRIDE * tick) ^ systemSalt) for the bound tick. */
    private long tickPrefix;

    private CounterRandomSource(long worldSeed, long systemSalt) {
        this.worldSeed = worldSeed;
        this.systemSalt = systemSalt;
        beginTick(0L);
    }

    /** Creates a source bound to one system's salt; initially at tick 0. */
    public static CounterRandomSource of(long worldSeed, long systemSalt) {
        return new CounterRandomSource(worldSeed, systemSalt);
    }

    /**
     * Rebinds this source to {@code tick}. Engine-only: called once per tick
     * before the owning system runs; systems never see a partially advanced source.
     */
    public void beginTick(long tick) {
        long h = RandomSource.mix64(worldSeed + TICK_STRIDE * tick);
        this.tickPrefix = RandomSource.mix64(h ^ systemSalt);
    }

    @Override
    public long draw(long spatialKey, int drawIndex) {
        long h = RandomSource.mix64(tickPrefix + spatialKey);
        return RandomSource.mix64(h + drawIndex);
    }
}
