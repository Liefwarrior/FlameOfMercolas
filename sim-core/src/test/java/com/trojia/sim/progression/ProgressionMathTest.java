package com.trojia.sim.progression;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Pure arithmetic tests for &sect;1/&sect;3's grain, threshold, and
 * saturating-add rules (spec &sect;10 tests 1, 2, 5).
 */
final class ProgressionMathTest {

    /** Test 1: awardGrains_exactFactors_noDivision. */
    @Test
    void awardGrainsExactFactorsForEverySatiationTier() {
        int[] expectedFactors = {20, 16, 12, 8, 5};
        int[] baseCps = {100, 120, 150, 200, 250, 300};
        for (int tier = 0; tier < expectedFactors.length; tier++) {
            for (int baseCp : baseCps) {
                assertEquals(baseCp * expectedFactors[tier],
                        ProgressionMath.awardGrains(baseCp, tier),
                        "tier " + tier + " baseCp " + baseCp);
            }
        }
        // tier 4 and beyond all clamp to the 25% floor.
        assertEquals(100 * 5, ProgressionMath.awardGrains(100, 4));
        assertEquals(100 * 5, ProgressionMath.awardGrains(100, 99));
    }

    /** Test 2: threshold_matchesFormula_allAptitudes. */
    @Test
    void thresholdMatchesFormulaForEveryAptitude() {
        for (AptitudeTier apt : AptitudeTier.values()) {
            for (int level = 0; level <= 100; level++) {
                assertEquals((level + 1) * 100 * apt.aptNum(),
                        ProgressionMath.thresholdGrains(level, apt),
                        apt + " level " + level);
            }
        }
    }

    @Test
    void aptNumMatchesSpecRationals() {
        assertEquals(15, AptitudeTier.FAVORED.aptNum());
        assertEquals(20, AptitudeTier.TRAINED.aptNum());
        assertEquals(25, AptitudeTier.NEGLECTED.aptNum());
        assertEquals(80, AptitudeTier.FLAME.aptNum());
    }

    /** Test 5: progress_saturatingAdd_neverWraps. */
    @Test
    void saturatingAddNeverWraps() {
        assertEquals(Integer.MAX_VALUE,
                ProgressionMath.saturatingAdd(Integer.MAX_VALUE, 1));
        assertEquals(Integer.MAX_VALUE,
                ProgressionMath.saturatingAdd(Integer.MAX_VALUE, Integer.MAX_VALUE));
        assertEquals(Integer.MAX_VALUE,
                ProgressionMath.saturatingAdd(Integer.MAX_VALUE - 1, 2));
        // ordinary (non-saturating) case still adds exactly.
        assertEquals(5_000, ProgressionMath.saturatingAdd(2_000, 3_000));
        assertEquals(0, ProgressionMath.saturatingAdd(0, 0));
    }

    /** spec section 9.1's worked example: level 5 -> 6..29 threshold ladder (Trained). */
    @Test
    void grindWeekWorkedExampleThresholds() {
        // cumulative cost from level 5 up to level 29 = 2000 * sum(6..29) = 2000*420 = 840,000
        int sum = 0;
        for (int level = 5; level < 29; level++) {
            sum += ProgressionMath.thresholdGrains(level, AptitudeTier.TRAINED);
        }
        assertEquals(840_000, sum);
        // to level 30 costs 2000*450 = 900,000 (one more threshold beyond 29's cumulative cost)
        sum += ProgressionMath.thresholdGrains(29, AptitudeTier.TRAINED);
        assertEquals(900_000, sum);
    }
}
