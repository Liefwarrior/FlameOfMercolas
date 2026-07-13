package com.trojia.sim.progression;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Behavior 3: {@code evasion = max(0, AGI - sum(armorBulk))}
 * (PROGRESSION-SPEC.md &sect;8, COMBAT-SPEC.md &sect;8 cross-reference).
 */
final class EvasionTest {

    @Test
    void evasionIsAgiMinusArmorBulk() {
        assertEquals(30, Evasion.evasion(35, 5));
        assertEquals(0, Evasion.evasion(10, 10), "exact cancel floors at zero, not negative");
    }

    /** DoD7: heavy armor exceeding AGI never produces negative evasion. */
    @Test
    void heavyArmorExceedingAgiFloorsAtZero() {
        assertEquals(0, Evasion.evasion(10, 25));
        assertEquals(0, Evasion.evasion(0, 100));
    }

    @Test
    void zeroArmorPassesAgiThrough() {
        assertEquals(42, Evasion.evasion(42, 0));
    }
}
