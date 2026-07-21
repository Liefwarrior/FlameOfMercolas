package com.trojia.client.render;

import com.trojia.client.hud.DayPhase;
import com.trojia.sim.actor.DailyRhythm;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link AmbientLight}'s day/night ambient curve — the pure
 * (tick) &rarr; (multiply colour, lamp gate) function behind the lighting cycle. The
 * contract under test: the whole Day phase is the exact identity (the daytime game is
 * pixel-unchanged), midnight is a deep cool darkening that still reads (~35-50%
 * luminance, blue-forward), the curve is continuous everywhere including the midnight
 * wrap (no pop at any phase boundary), and the light always agrees with
 * {@link DayPhase}'s clock tag.
 */
class AmbientLightTest {

    /** Rec. 709 luma — the "how dark does it read" yardstick for the night target. */
    private static double luminance(AmbientLight light) {
        return 0.2126 * light.r() + 0.7152 * light.g() + 0.0722 * light.b();
    }

    @Test
    void noonIsTheExactIdentity() {
        AmbientLight noon = AmbientLight.at(6_000); // DailyRhythm's noon anchor
        assertEquals(1f, noon.r());
        assertEquals(1f, noon.g());
        assertEquals(1f, noon.b());
        assertEquals(0f, noon.lampFactor());
        assertTrue(noon.isNeutral(), "noon must be the multiply identity, bit-exact");
    }

    @Test
    void theWholeDayPhaseIsTheExactIdentity() {
        // Not just noon: every tick the HUD tags "Day" must render pixel-identical to the
        // pre-cycle look — the curve holds (1,1,1)/0 across the full span, no drift.
        for (long t = DayPhase.DAY_START; t < DayPhase.DUSK_START; t += 250) {
            AmbientLight light = AmbientLight.at(t);
            assertTrue(light.isNeutral(), "tick-of-day " + t + " must be identity, got "
                    + light);
        }
    }

    @Test
    void midnightDimsToTheCoolNightTarget() {
        AmbientLight midnight = AmbientLight.at(18_000); // DailyRhythm's midnight anchor
        assertEquals(AmbientLight.NIGHT_R, midnight.r(), "midnight sits on the night hold");
        assertEquals(AmbientLight.NIGHT_G, midnight.g());
        assertEquals(AmbientLight.NIGHT_B, midnight.b());
        double luma = luminance(midnight);
        assertTrue(luma >= 0.35 && luma <= 0.50,
                "night must darken but still read the city: luminance " + luma);
        assertTrue(midnight.b() > midnight.r(),
                "night carries a cool blue cast (b > r), got " + midnight);
        assertEquals(1f, midnight.lampFactor(), "lamps are fully lit at midnight");
    }

    @Test
    void duskIsAnAmberFadeWhileNightIsCool() {
        AmbientLight amber = AmbientLight.at(AmbientLight.DUSK_AMBER_T);
        assertTrue(amber.r() > amber.b(), "dusk is warm (r > b), got " + amber);
        assertTrue(amber.r() >= 0.99f, "dusk keeps red near full for the amber cast");
        assertTrue(luminance(amber) > luminance(AmbientLight.at(18_000)),
                "dusk is brighter than full night");
    }

    @Test
    void dawnRisesWarmBeforeSettlingToNeutral() {
        AmbientLight warmRise = AmbientLight.at(AmbientLight.DAWN_WARM_T);
        assertTrue(warmRise.r() > warmRise.b(), "the dawn rise is warm (r > b)");
        assertTrue(warmRise.r() > AmbientLight.NIGHT_R, "dawn is brighter than night");
        assertTrue(AmbientLight.at(DayPhase.DAY_START).isNeutral(),
                "by DayPhase.DAY_START the light is exactly neutral");
    }

    @Test
    void curveIsContinuousAcrossTheWholeDayIncludingTheWrap() {
        // No pop anywhere: over any 10-tick step (about a rendered frame at speed), each
        // channel and the lamp gate move by well under a visible amount. Covers every
        // phase boundary and one full wrap into the next day.
        final long step = 10;
        final float bound = 0.02f;
        AmbientLight prev = AmbientLight.at(0);
        for (long t = step; t <= DailyRhythm.DAY + 2_000; t += step) {
            AmbientLight cur = AmbientLight.at(t);
            assertTrue(Math.abs(cur.r() - prev.r()) < bound, "r pops at tick " + t);
            assertTrue(Math.abs(cur.g() - prev.g()) < bound, "g pops at tick " + t);
            assertTrue(Math.abs(cur.b() - prev.b()) < bound, "b pops at tick " + t);
            assertTrue(Math.abs(cur.lampFactor() - prev.lampFactor()) < bound,
                    "lampFactor pops at tick " + t);
            prev = cur;
        }
    }

    @Test
    void lightAgreesWithTheClockTag() {
        for (long t = 0; t < DailyRhythm.DAY; t += 100) {
            AmbientLight light = AmbientLight.at(t);
            switch (DayPhase.of(t)) {
                case DAY -> assertTrue(light.isNeutral(),
                        "Day-tagged tick " + t + " must be identity");
                case NIGHT -> assertTrue(luminance(light) < 0.75 && light.lampFactor() > 0.5f,
                        "Night-tagged tick " + t + " must be visibly dark with lamps lit");
                case DAWN, DUSK -> {
                    // Transitional phases: only require legality, the boundary ticks
                    // themselves may still sit at a neighbour phase's endpoint value.
                }
            }
            assertTrue(light.r() >= 0f && light.r() <= 1f
                    && light.g() >= 0f && light.g() <= 1f
                    && light.b() >= 0f && light.b() <= 1f
                    && light.lampFactor() >= 0f && light.lampFactor() <= 1f,
                    "channels outside [0,1] at tick " + t + ": " + light);
        }
    }

    @Test
    void absoluteTicksWrapLikeTheHudClock() {
        assertEquals(AmbientLight.at(6_000), AmbientLight.at(DailyRhythm.DAY * 3 + 6_000),
                "the cycle keys on tick-of-day, same as DayPhase.of");
    }
}
