package com.trojia.sim.progression;

/**
 * The evasion formula COMBAT-SPEC.md &sect;8 depends on, owned here per
 * PROGRESSION-SPEC.md &sect;8: {@code evasion = max(0, AGI - Σ armorBulk)}.
 * Floor-at-zero: heavy armor whose summed bulk meets or exceeds AGI never
 * produces negative evasion.
 */
public final class Evasion {

    private Evasion() {
    }

    /**
     * Computes evasion from AGI and total equipped armor bulk.
     *
     * @param agi            the entity's current effective Agility
     * @param totalArmorBulk the sum of {@code armorBulk} over every equipped
     *                       piece (PROGRESSION-SPEC.md &sect;8; per-material
     *                       armorBulk values are COMBAT-SPEC's concern)
     * @return {@code max(0, agi - totalArmorBulk)}
     */
    public static int evasion(int agi, int totalArmorBulk) {
        int raw = agi - totalArmorBulk;
        return Math.max(0, raw);
    }
}
