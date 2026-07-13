package com.trojia.sim.progression;

/**
 * A skill's fixed aptitude rational (PROGRESSION-SPEC.md &sect;2, &sect;3.1):
 * how fast a skill levels per grain of progress. Encoded as {@code aptNum},
 * the integer numerator of the threshold formula
 * {@code thresholdGrains(L) = (L + 1) * 100 * aptNum} &mdash; exact in grains,
 * no division ever occurs on the award path (&sect;1).
 *
 * <p>Ratios per &sect;1: Favored &times;3/4 (aptNum 15), Trained &times;1
 * (aptNum 20), Neglected &times;5/4 (aptNum 25), The Flame &times;4
 * (aptNum 80, PROGRESSION-SPEC.md &sect;7 &mdash; the demigod curve's
 * expensive spine).</p>
 */
public enum AptitudeTier {

    /** Fastest-levelling tier (&times;3/4 threshold). */
    FAVORED(15),
    /** Baseline tier (&times;1 threshold). */
    TRAINED(20),
    /** Slowest ordinary tier (&times;5/4 threshold). */
    NEGLECTED(25),
    /** The Flame's unique tier (&times;4 threshold, PROGRESSION-SPEC.md &sect;7). */
    FLAME(80);

    private final int aptNum;

    AptitudeTier(int aptNum) {
        this.aptNum = aptNum;
    }

    /**
     * Returns the threshold numerator: {@code thresholdGrains(L) = (L + 1) *
     * 100 * aptNum}.
     */
    public int aptNum() {
        return aptNum;
    }
}
