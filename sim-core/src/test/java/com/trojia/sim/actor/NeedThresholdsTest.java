package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Direct coverage of {@link NeedThresholds}, previously untested anywhere in
 * the suite despite its javadoc pinning it to the spec's tests A11/A12: the
 * saturating {@code [0, MAX]} clamp and the CRITICAL/LOW band boundaries.
 * Also confirms {@link Actor#applyNeedDelta} saturates at both ends starting
 * from a near-boundary value — the only real caller path, and the one whose
 * {@code short}-cast (needs[] is {@code short[]}) depends on the clamp
 * actually holding the range.
 */
final class NeedThresholdsTest {

    @Test
    void clampSaturatesBelowZero() {
        assertEquals(0, NeedThresholds.clamp(-1));
        assertEquals(0, NeedThresholds.clamp(Integer.MIN_VALUE));
    }

    @Test
    void clampIsIdentityInsideRange() {
        assertEquals(0, NeedThresholds.clamp(0));
        assertEquals(5_000, NeedThresholds.clamp(5_000));
        assertEquals(NeedThresholds.MAX, NeedThresholds.clamp(NeedThresholds.MAX));
    }

    @Test
    void clampSaturatesAboveMax() {
        assertEquals(NeedThresholds.MAX, NeedThresholds.clamp(NeedThresholds.MAX + 1));
        assertEquals(NeedThresholds.MAX, NeedThresholds.clamp(Integer.MAX_VALUE));
    }

    @Test
    void isCriticalBoundary() {
        assertTrue(NeedThresholds.isCritical(NeedThresholds.CRITICAL - 1));
        assertFalse(NeedThresholds.isCritical(NeedThresholds.CRITICAL));
    }

    @Test
    void isLowBoundary() {
        assertTrue(NeedThresholds.isLow(NeedThresholds.LOW - 1));
        assertFalse(NeedThresholds.isLow(NeedThresholds.LOW));
        // CRITICAL implies LOW.
        assertTrue(NeedThresholds.isLow(NeedThresholds.CRITICAL - 1));
    }

    @Test
    void applyNeedDeltaSaturatesAtTheTopStartingNearTheBoundary() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(0, 0, 1));
        // fixture COIN need starts at 8000 (below MAX); push it far past MAX.
        actor.applyNeedDelta(Need.COIN, 100_000);

        assertEquals(NeedThresholds.MAX, actor.need(Need.COIN),
                "a huge positive delta (e.g. a job/event award) must saturate at MAX, "
                        + "not overflow the short[] backing store");
    }

    @Test
    void applyNeedDeltaSaturatesAtTheBottomStartingNearTheBoundary() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(0, 0, 1));
        actor.applyNeedDelta(Need.COIN, -100_000);

        assertEquals(0, actor.need(Need.COIN),
                "a huge negative delta (e.g. a decay-multiplier bug) must saturate at 0, "
                        + "not wrap to a negative short");
    }
}
