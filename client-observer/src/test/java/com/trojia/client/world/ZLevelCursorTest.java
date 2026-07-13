package com.trojia.client.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** {@link ZLevelCursor} clamp contract (M1 Behavior 3, DoD7) — pure, headless. */
class ZLevelCursorTest {

    @Test
    void startsAtTheGivenInitialZ() {
        assertEquals(5, new ZLevelCursor(0, 10, 5).z());
    }

    @Test
    void initialZOutsideRangeIsClampedAtConstruction() {
        assertEquals(10, new ZLevelCursor(0, 10, 999).z());
        assertEquals(0, new ZLevelCursor(0, 10, -999).z());
    }

    @Test
    void upIncrementsAndSaturatesAtMaxZ() {
        ZLevelCursor cursor = new ZLevelCursor(0, 2, 1);
        assertEquals(2, cursor.up());
        assertEquals(2, cursor.up(), "up() past maxZ must be a no-op, not wrap or crash");
    }

    @Test
    void downDecrementsAndSaturatesAtMinZ() {
        ZLevelCursor cursor = new ZLevelCursor(0, 2, 1);
        assertEquals(0, cursor.down());
        assertEquals(0, cursor.down(), "down() past minZ must be a no-op, not wrap or crash");
    }

    @Test
    void toJumpsDirectlyAndClampsIntoRange() {
        ZLevelCursor cursor = new ZLevelCursor(0, 10, 5);
        assertEquals(8, cursor.to(8), "to() jumps straight to an in-range level");
        assertEquals(10, cursor.to(999), "to() saturates at maxZ");
        assertEquals(0, cursor.to(-999), "to() saturates at minZ");
    }

    @Test
    void constructorRejectsMinGreaterThanMax() {
        assertThrows(IllegalArgumentException.class, () -> new ZLevelCursor(5, 4, 4));
    }
}
