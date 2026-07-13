package com.trojia.client.atlas;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link AtlasRegionTable}: the row-major ascending-ASCII layout
 * rule of TILE-ART-SPEC section 3, its input-order independence, and its validation.
 */
class AtlasRegionTableTest {

    @Test
    void laysOutRowMajorInAscendingAsciiOrder() {
        // 32x32 sheet, 16 px cells: 2 columns. Names given out of order.
        AtlasRegionTable table =
                new AtlasRegionTable(32, 16, List.of("c", "a", "d", "b"));
        assertEquals(new AtlasCellRect(0, 0, 16, 16), table.cellRect("a"));
        assertEquals(new AtlasCellRect(16, 0, 16, 16), table.cellRect("b"));
        assertEquals(new AtlasCellRect(0, 16, 16, 16), table.cellRect("c"));
        assertEquals(new AtlasCellRect(16, 16, 16, 16), table.cellRect("d"));
    }

    @Test
    void orderIsAsciiByteOrderNotAlphabetical() {
        // '.' (0x2E) < 'B' (0x42) < '_' (0x5F) < 'a' (0x61): uppercase before
        // lowercase, punctuation first.
        AtlasRegionTable table =
                new AtlasRegionTable(32, 16, List.of("a", "B", "_", "."));
        assertEquals(new AtlasCellRect(0, 0, 16, 16), table.cellRect("."));
        assertEquals(new AtlasCellRect(16, 0, 16, 16), table.cellRect("B"));
        assertEquals(new AtlasCellRect(0, 16, 16, 16), table.cellRect("_"));
        assertEquals(new AtlasCellRect(16, 16, 16, 16), table.cellRect("a"));
    }

    @Test
    void layoutIsAPureFunctionOfTheNameSet() {
        List<String> names = List.of("granite", "granite.floor", "chromatis.a1",
                "missing", "water", "trudgeon_wood@getilia_soak");
        AtlasRegionTable sorted = new AtlasRegionTable(128, 16, names);
        AtlasRegionTable reversed = new AtlasRegionTable(128, 16, names.reversed());
        assertEquals(sorted.regionNames(), reversed.regionNames());
        for (String name : names) {
            assertEquals(sorted.cellRect(name), reversed.cellRect(name), name);
        }
    }

    @Test
    void duplicatesCollapse() {
        AtlasRegionTable table =
                new AtlasRegionTable(32, 16, List.of("a", "a", "b", "a"));
        assertEquals(2, table.size());
        assertEquals(new AtlasCellRect(16, 0, 16, 16), table.cellRect("b"));
    }

    @Test
    void regionNamesIterateInSortedOrder() {
        AtlasRegionTable table =
                new AtlasRegionTable(64, 16, List.of("zeta", "alpha", "mid"));
        assertEquals(List.of("alpha", "mid", "zeta"), List.copyOf(table.regionNames()));
    }

    @Test
    void geometryQueries() {
        AtlasRegionTable table = new AtlasRegionTable(128, 16, List.of("a"));
        assertEquals(128, table.atlasSizePx());
        assertEquals(16, table.cellPx());
        assertEquals(8, table.columns());
        assertTrue(table.contains("a"));
        assertFalse(table.contains("b"));
        assertFalse(table.contains(null));
    }

    @Test
    void rejectsOverflowingTheSheet() {
        // 32x32 with 16 px cells holds 4.
        assertThrows(IllegalArgumentException.class, () -> new AtlasRegionTable(
                32, 16, List.of("a", "b", "c", "d", "e")));
    }

    @Test
    void rejectsInvalidGeometry() {
        assertThrows(IllegalArgumentException.class,
                () -> new AtlasRegionTable(0, 16, List.of("a")));
        assertThrows(IllegalArgumentException.class,
                () -> new AtlasRegionTable(100, 16, List.of("a"))); // not a multiple
        assertThrows(IllegalArgumentException.class,
                () -> new AtlasRegionTable(128, 0, List.of("a")));
        assertThrows(IllegalArgumentException.class,
                () -> new AtlasRegionTable(128, 16, null));
    }

    @Test
    void rejectsNonPrintableAsciiNames() {
        assertThrows(IllegalArgumentException.class,
                () -> new AtlasRegionTable(128, 16, List.of("")));
        assertThrows(IllegalArgumentException.class,
                () -> new AtlasRegionTable(128, 16, List.of("has space")));
        assertThrows(IllegalArgumentException.class,
                () -> new AtlasRegionTable(128, 16, List.of("café")));
    }

    @Test
    void unknownRegionLookupThrows() {
        AtlasRegionTable table = new AtlasRegionTable(32, 16, List.of("a"));
        assertThrows(IllegalArgumentException.class, () -> table.cellRect("b"));
        assertThrows(IllegalArgumentException.class, () -> table.cellRect(null));
    }
}
