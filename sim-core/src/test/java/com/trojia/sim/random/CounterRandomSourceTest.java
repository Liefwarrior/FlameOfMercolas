package com.trojia.sim.random;

import org.junit.jupiter.api.Test;

import java.util.HashSet;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Determinism and statistical-sanity tests for {@link CounterRandomSource}.
 * The derivation chain of ARCHITECTURE.md §1.1 #16 is re-derived independently
 * here — if the implementation and this test ever disagree, the pinned spec
 * has been broken and every golden master is invalid.
 */
final class CounterRandomSourceTest {

    /** The §1.1 #16 chain, written out longhand as the reference. */
    private static long reference(long worldSeed, long tick, long systemSalt,
                                  long spatialKey, int drawIndex) {
        long h = RandomSource.mix64(worldSeed + RandomSource.TICK_STRIDE * tick);
        h = RandomSource.mix64(h ^ systemSalt);
        h = RandomSource.mix64(h + spatialKey);
        return RandomSource.mix64(h + drawIndex);
    }

    @Test
    void drawMatchesThePinnedDerivationChain() {
        long[][] tuples = {
                // worldSeed, tick, salt, spatialKey, drawIndex
                {0L, 0L, 0L, 0L, 0L},
                {42L, 1L, 0x123456789ABCDEFL, 0x3FFFFFFFL, 0L},
                {-7L, 1_000_000L, -1L, -123456789L, 17L},
                {Long.MAX_VALUE, 3L, Long.MIN_VALUE, 0x2AAAAAAAL, 255L},
        };
        for (long[] t : tuples) {
            CounterRandomSource rng = CounterRandomSource.of(t[0], t[2]);
            rng.beginTick(t[1]);
            assertEquals(reference(t[0], t[1], t[2], t[3], (int) t[4]),
                    rng.draw(t[3], (int) t[4]),
                    "tuple seed=" + t[0] + " tick=" + t[1] + " salt=" + t[2]);
        }
    }

    @Test
    void singleArgDrawIsDrawIndexZero() {
        CounterRandomSource rng = CounterRandomSource.of(99L, 7L);
        rng.beginTick(12L);
        assertEquals(rng.draw(0x1234L, 0), rng.draw(0x1234L));
    }

    @Test
    void drawsAreCallOrderIndependent() {
        CounterRandomSource forward = CounterRandomSource.of(5L, 6L);
        forward.beginTick(9L);
        long a1 = forward.draw(100L, 0);
        long b1 = forward.draw(200L, 3);

        CounterRandomSource backward = CounterRandomSource.of(5L, 6L);
        backward.beginTick(9L);
        long b2 = backward.draw(200L, 3);
        long a2 = backward.draw(100L, 0);

        assertEquals(a1, a2);
        assertEquals(b1, b2);
    }

    @Test
    void freshInstanceReproducesEveryDrawFromTheSeedAlone() {
        // The save/load property by construction: worldSeed is the only state.
        CounterRandomSource live = CounterRandomSource.of(2026L, 0xF1EL);
        live.beginTick(500L);
        long[] draws = new long[16];
        for (int i = 0; i < draws.length; i++) {
            draws[i] = live.draw(i * 31L, i);
        }

        CounterRandomSource loaded = CounterRandomSource.of(2026L, 0xF1EL);
        loaded.beginTick(500L);
        for (int i = 0; i < draws.length; i++) {
            assertEquals(draws[i], loaded.draw(i * 31L, i));
        }
    }

    @Test
    void rebindingBackToAnEarlierTickReproducesItsDraws() {
        CounterRandomSource rng = CounterRandomSource.of(11L, 13L);
        rng.beginTick(3L);
        long atTick3 = rng.draw(777L, 2);
        rng.beginTick(4L);
        long atTick4 = rng.draw(777L, 2);
        rng.beginTick(3L);

        assertEquals(atTick3, rng.draw(777L, 2));
        assertNotEquals(atTick3, atTick4);
    }

    @Test
    void tickSaltKeyAndIndexAllDecorrelate() {
        CounterRandomSource base = CounterRandomSource.of(1L, 2L);
        base.beginTick(10L);
        long reference = base.draw(50L, 0);

        CounterRandomSource otherSalt = CounterRandomSource.of(1L, 3L);
        otherSalt.beginTick(10L);
        assertNotEquals(reference, otherSalt.draw(50L, 0));

        CounterRandomSource otherSeed = CounterRandomSource.of(2L, 2L);
        otherSeed.beginTick(10L);
        assertNotEquals(reference, otherSeed.draw(50L, 0));

        base.beginTick(11L);
        assertNotEquals(reference, base.draw(50L, 0));
        base.beginTick(10L);
        assertNotEquals(reference, base.draw(51L, 0));
        assertNotEquals(reference, base.draw(50L, 1));
    }

    @Test
    void everyOutputBitIsBalancedOverSequentialSpatialKeys() {
        // Sequential keys are the worst realistic input (PackedPos scans).
        int n = 200_000;
        CounterRandomSource rng = CounterRandomSource.of(0xABCDEFL, 0x5EEDL);
        rng.beginTick(1L);
        int[] ones = new int[64];
        for (int i = 0; i < n; i++) {
            long draw = rng.draw(i, 0);
            for (int bit = 0; bit < 64; bit++) {
                ones[bit] += (int) ((draw >>> bit) & 1L);
            }
        }
        // 1% tolerance is ~9 sigma at n=200k: fails only on real bias.
        int lo = (int) (n * 0.49);
        int hi = (int) (n * 0.51);
        for (int bit = 0; bit < 64; bit++) {
            assertTrue(ones[bit] >= lo && ones[bit] <= hi,
                    "bit " + bit + " is biased: " + ones[bit] + "/" + n);
        }
    }

    @Test
    void adjacentSpatialKeysAvalancheToRoughlyHalfTheBits() {
        int pairs = 100_000;
        CounterRandomSource rng = CounterRandomSource.of(31337L, 0x5EEDL);
        rng.beginTick(1L);
        long totalFlips = 0;
        long previous = rng.draw(0, 0);
        for (int i = 1; i <= pairs; i++) {
            long current = rng.draw(i, 0);
            totalFlips += Long.bitCount(previous ^ current);
            previous = current;
        }
        double mean = (double) totalFlips / pairs;
        assertTrue(mean > 31.0 && mean < 33.0,
                "avalanche between adjacent keys should average ~32 flipped bits: " + mean);
    }

    @Test
    void noCollisionsInAModestSample() {
        int n = 200_000;
        CounterRandomSource rng = CounterRandomSource.of(404L, 0x5EEDL);
        rng.beginTick(1L);
        Set<Long> seen = new HashSet<>(n * 2);
        for (int i = 0; i < n; i++) {
            assertTrue(seen.add(rng.draw(i, 0)), "collision at key " + i);
        }
    }
}
