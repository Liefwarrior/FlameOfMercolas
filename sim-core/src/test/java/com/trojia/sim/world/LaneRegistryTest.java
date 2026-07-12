package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Registration, lookup and sealing contract of the instance-owned lane registry. */
final class LaneRegistryTest {

    @Test
    void registrationMintsDenseIndicesInOrder() {
        LaneRegistry registry = new LaneRegistry();
        LaneId first = registry.register("alpha", 2);
        LaneId second = registry.register("beta", 1);
        assertEquals(0, first.index());
        assertEquals("alpha", first.name());
        assertEquals(2, first.bytesPerTile());
        assertEquals(1, second.index());
        assertEquals(2, registry.count());
        assertEquals(first, registry.byIndex(0));
        assertEquals(second, registry.byIndex(1));
        assertEquals(first, registry.byName("alpha"));
        assertEquals(second, registry.byName("beta"));
        assertTrue(registry.contains("alpha"));
        assertFalse(registry.contains("gamma"));
    }

    @Test
    void duplicateNameIsRejected() {
        LaneRegistry registry = new LaneRegistry();
        registry.register("alpha", 2);
        assertThrows(IllegalArgumentException.class, () -> registry.register("alpha", 1));
    }

    @Test
    void invalidWidthAndNameAreRejected() {
        LaneRegistry registry = new LaneRegistry();
        assertThrows(IllegalArgumentException.class, () -> registry.register("alpha", 3));
        assertThrows(IllegalArgumentException.class, () -> registry.register("alpha", 0));
        assertThrows(IllegalArgumentException.class, () -> registry.register("", 1));
        assertThrows(IllegalArgumentException.class, () -> registry.register(null, 1));
        assertEquals(0, registry.count());
    }

    @Test
    void missingLookupsThrow() {
        LaneRegistry registry = new LaneRegistry();
        registry.register("alpha", 2);
        assertThrows(IllegalArgumentException.class, () -> registry.byName("beta"));
        assertThrows(IllegalArgumentException.class, () -> registry.byIndex(1));
        assertThrows(IllegalArgumentException.class, () -> registry.byIndex(-1));
    }

    @Test
    void sealingForbidsRegistrationButNotLookups() {
        LaneRegistry registry = new LaneRegistry();
        LaneId lane = registry.register("alpha", 2);
        registry.seal();
        assertThrows(IllegalStateException.class, () -> registry.register("beta", 1));
        assertEquals(lane, registry.byName("alpha"));
        assertEquals(1, registry.count());
    }

    @Test
    void doubleSealThrows() {
        LaneRegistry registry = new LaneRegistry();
        registry.seal();
        assertThrows(IllegalStateException.class, registry::seal);
    }
}
