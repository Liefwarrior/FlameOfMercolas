package com.trojia.sim.progression;

/**
 * The pure integer arithmetic of PROGRESSION-SPEC.md &sect;1 and &sect;3:
 * grains, thresholds, satiation factors, and the saturating-add overflow
 * guard. Every operation here is exact &mdash; no division ever occurs on the
 * award path, by construction of the grain/aptitude/satiation constants
 * (&sect;1).
 */
public final class ProgressionMath {

    private ProgressionMath() {
    }

    /** 1 cp = 20 grains (&sect;1). */
    public static final int GRAINS_PER_CP = 20;

    /** Highest legal skill level (inclusive), per &sect;1. */
    public static final int MAX_LEVEL = 100;

    /** Satiation tier ceiling: tiers are 0..4, tier 4+ clamps (&sect;3.3). */
    public static final int MAX_SATIATION_TIER = 4;

    /** Idle ticks required for one tier of satiation decay (&sect;3.3, placeholder). */
    public static final long SATIATION_DECAY_TICKS = 3000L;

    /**
     * satFactor by tier: {tier0=100%, tier1=80%, tier2=60%, tier3=40%,
     * tier4+=25%} expressed as grains-per-cp (of the 20 grains/cp baseline) so
     * {@link #awardGrains(int, int)} is a pure multiply, never a division
     * (&sect;1, &sect;3.3). Index is the clamped tier 0..4.
     */
    private static final int[] SAT_FACTOR_GRAINS_PER_CP = {20, 16, 12, 8, 5};

    /**
     * Returns the satiation multiplier (grains-per-cp) for a tier, clamping
     * any tier {@code >= MAX_SATIATION_TIER} to the floor factor (25%,
     * &sect;3.3: "Floor is 25%, never 0").
     *
     * @param tier the satiation tier; negative values clamp to 0
     * @return the grains-per-cp factor, one of {20, 16, 12, 8, 5}
     */
    public static int satFactorGrainsPerCp(int tier) {
        int clamped = Math.max(0, Math.min(tier, SAT_FACTOR_GRAINS_PER_CP.length - 1));
        return SAT_FACTOR_GRAINS_PER_CP[clamped];
    }

    /**
     * Returns the grains awarded for a qualifying use: {@code baseCp *
     * satFactor(tier)}, computed with no division or rounding (&sect;1's
     * {@code awardGrains = baseCp x satFactor} exactly).
     *
     * @param baseCp the qualifying-use base award in cp (&sect;3.1 table)
     * @param tier   the satiation tier in effect for this award
     * @return the grains to add to progress; {@code >= 0} for {@code baseCp >= 0}
     * @throws IllegalArgumentException if {@code baseCp} is negative
     */
    public static int awardGrains(int baseCp, int tier) {
        if (baseCp < 0) {
            throw new IllegalArgumentException("baseCp must be non-negative: " + baseCp);
        }
        return baseCp * satFactorGrainsPerCp(tier);
    }

    /**
     * Returns the grains needed to advance from {@code level} to
     * {@code level + 1}: {@code (level + 1) * 100 * aptNum} (&sect;1).
     *
     * @param level the current level, {@code 0..100}
     * @param apt   the skill's aptitude tier
     * @return the threshold in grains; always {@code > 0}
     * @throws IllegalArgumentException if {@code level} is out of {@code 0..100}
     */
    public static int thresholdGrains(int level, AptitudeTier apt) {
        if (level < 0 || level > MAX_LEVEL) {
            throw new IllegalArgumentException("level out of range 0..100: " + level);
        }
        return (level + 1) * 100 * apt.aptNum();
    }

    /**
     * Saturating add at {@link Integer#MAX_VALUE} (&sect;1's mandatory
     * overflow guard: "the saturation guard is mandatory, see tests").
     * Both arguments are non-negative in every legal call on the award path.
     *
     * @param a first non-negative addend
     * @param b second non-negative addend
     * @return {@code a + b}, clamped to {@code Integer.MAX_VALUE}
     */
    public static int saturatingAdd(int a, int b) {
        long sum = (long) a + (long) b;
        return sum > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) sum;
    }
}
