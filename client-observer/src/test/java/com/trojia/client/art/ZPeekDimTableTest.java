package com.trojia.client.art;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** {@link ZPeekDimTable} shape rules (TILE-ART-SPEC sections 5.2 / 7.1) and Q8 math. */
class ZPeekDimTableTest {

    private static final int[] V0 = {256, 168, 112, 76};

    @Test
    void v0CurveLoadsWithMaxPeekDepthThree() {
        ZPeekDimTable table = ZPeekDimTable.fromQ8(V0);
        assertEquals(3, table.maxPeekDepth());
        assertEquals(256, table.dimQ8(0));
        assertEquals(168, table.dimQ8(1));
        assertEquals(112, table.dimQ8(2));
        assertEquals(76, table.dimQ8(3));
    }

    @Test
    void depthZeroIsIdentity() {
        assertEquals(0xABCDEF, ZPeekDimTable.fromQ8(V0).shadeRgb(0xABCDEF, 0));
    }

    @Test
    void shadeRgbMultipliesPerChannel() {
        // 0xFF * 168 >> 8 = 167; 0x80 * 168 >> 8 = 84; 0x00 stays 0.
        assertEquals((167 << 16) | (84 << 8), ZPeekDimTable.fromQ8(V0).shadeRgb(0xFF8000, 1));
    }

    @Test
    void inputIsDefensivelyCopied() {
        int[] curve = {256, 168, 112, 76};
        ZPeekDimTable table = ZPeekDimTable.fromQ8(curve);
        curve[1] = 0;
        assertEquals(168, table.dimQ8(1));
    }

    @Test
    void depthOutsideTableThrows() {
        ZPeekDimTable table = ZPeekDimTable.fromQ8(V0);
        assertThrows(IllegalArgumentException.class, () -> table.dimQ8(4));
        assertThrows(IllegalArgumentException.class, () -> table.dimQ8(-1));
    }

    @Test
    void rejectsNullOrEmpty() {
        assertThrows(IllegalArgumentException.class, () -> ZPeekDimTable.fromQ8(null));
        assertThrows(IllegalArgumentException.class, () -> ZPeekDimTable.fromQ8(new int[0]));
    }

    @Test
    void rejectsFirstEntryNotUnit() {
        assertThrows(IllegalArgumentException.class,
                () -> ZPeekDimTable.fromQ8(new int[] {255, 168, 112, 76}));
    }

    @Test
    void rejectsIncreasingSequence() {
        assertThrows(IllegalArgumentException.class,
                () -> ZPeekDimTable.fromQ8(new int[] {256, 100, 150, 76}));
    }

    @Test
    void rejectsOutOfRangeEntries() {
        assertThrows(IllegalArgumentException.class,
                () -> ZPeekDimTable.fromQ8(new int[] {256, -1}));
    }
}
