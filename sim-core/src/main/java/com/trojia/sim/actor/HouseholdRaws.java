package com.trojia.sim.actor;

/**
 * The bake-time tuning knobs for household/employment formation
 * (ACTORS-SPEC.md §11.5): one shared file, {@code content/raws/actors/household.json}
 * — cross-cutting bake-time rules, not per-actor-type stat blocks.
 *
 * @param householdSizeWeights weights for household size 1..N (index 0 = size 1)
 * @param staffCountMin        inclusive min employer staff count
 * @param staffCountMax        inclusive max employer staff count
 * @param neighborFlavorMin    inclusive min NEIGHBOR flavor-edge count per home
 * @param neighborFlavorMax    inclusive max NEIGHBOR flavor-edge count per home
 * @param friendFlavorMin      inclusive min FRIEND flavor-edge count per actor
 * @param friendFlavorMax      inclusive max FRIEND flavor-edge count per actor
 */
public record HouseholdRaws(
        int[] householdSizeWeights,
        int staffCountMin, int staffCountMax,
        int neighborFlavorMin, int neighborFlavorMax,
        int friendFlavorMin, int friendFlavorMax) {

    public HouseholdRaws {
        if (householdSizeWeights.length == 0) {
            throw new IllegalArgumentException("householdSizeWeights must be non-empty");
        }
        householdSizeWeights = householdSizeWeights.clone();
        for (int w : householdSizeWeights) {
            if (w < 0) {
                throw new IllegalArgumentException("household size weights must be >= 0");
            }
        }
        requireRange("staffCount", staffCountMin, staffCountMax);
        requireRange("neighborFlavor", neighborFlavorMin, neighborFlavorMax);
        requireRange("friendFlavor", friendFlavorMin, friendFlavorMax);
    }

    @Override
    public int[] householdSizeWeights() {
        return householdSizeWeights.clone();
    }

    private static void requireRange(String name, int min, int max) {
        if (min < 0 || max < min) {
            throw new IllegalArgumentException(
                    "invalid " + name + " range [" + min + ", " + max + "]");
        }
    }
}
