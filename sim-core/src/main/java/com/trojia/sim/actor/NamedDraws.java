package com.trojia.sim.actor;

import com.trojia.sim.random.RandomSource;

/**
 * The named-draw addressing function for the actor package (ACTORS-SPEC.md
 * §2.2): {@code spatialKey = actorId} always; {@code drawIndex} is a per-actor
 * per-tick counter reset to 0 each tick and shared across every stream (the
 * §2.2 "one counter per actor" pinned attribution rule) — the caller supplies
 * it (typically via a small counter array indexed by actorId, reset once per
 * tick by the driving system).
 *
 * <p>Pure function of {@code (worldSeed, tick, stream, actorId, drawIndex)} —
 * identical to {@link com.trojia.sim.random.CounterRandomSource}'s mixing
 * chain but keyed by a stream-specific salt instead of a per-system one, so
 * many independently named streams can share the single persisted
 * {@code worldSeed} exactly as ARCHITECTURE.md §1.1 #16 requires.
 */
public final class NamedDraws {

    private NamedDraws() {
    }

    public static long draw(ActorRngStream stream, long worldSeed, long tick,
            int actorId, int drawIndex) {
        long h = RandomSource.mix64(worldSeed + RandomSource.TICK_STRIDE * tick);
        h = RandomSource.mix64(h ^ stream.salt());
        h = RandomSource.mix64(h + actorId);
        return RandomSource.mix64(h + drawIndex);
    }

    /**
     * Picks an index {@code 0..weights.length-1} from a draw, weighted by
     * {@code weights} (positive ints). Deterministic given the same draw
     * value; used for household-size and similar weighted-choice bakes
     * (ACTORS-SPEC.md §11.4).
     */
    public static int weightedPick(long draw, int[] weights) {
        int total = 0;
        for (int w : weights) {
            total += w;
        }
        if (total <= 0) {
            throw new IllegalArgumentException("weights must sum to a positive total");
        }
        // Unsigned remainder: the draw is a full 64-bit hash, never negative-biased.
        long slot = Long.remainderUnsigned(draw, total);
        int cumulative = 0;
        for (int i = 0; i < weights.length; i++) {
            cumulative += weights[i];
            if (slot < cumulative) {
                return i;
            }
        }
        return weights.length - 1;
    }
}
