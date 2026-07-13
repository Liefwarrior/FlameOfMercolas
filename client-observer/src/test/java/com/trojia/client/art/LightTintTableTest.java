package com.trojia.client.art;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** {@link LightTintTable} shape rules (TILE-ART-SPEC sections 5.1 / 7.1) and Q8 math. */
class LightTintTableTest {

    /** The v0 placeholder curve: {@code 36 + floor(220 * L^2 / 961)}. */
    private static int[] specCurve() {
        int[] curve = new int[LightTintTable.LEVELS];
        for (int level = 0; level < curve.length; level++) {
            curve[level] = 36 + 220 * level * level / 961;
        }
        return curve;
    }

    @Test
    void specCurveLoadsAndEndsAtUnit() {
        LightTintTable table = LightTintTable.fromQ8(specCurve());
        assertEquals(36, table.tintQ8(0));
        assertEquals(38, table.tintQ8(3));
        assertEquals(87, table.tintQ8(15));
        assertEquals(256, table.tintQ8(31));
    }

    @Test
    void minLightClampBrightensDarkTiles() {
        LightTintTable table = LightTintTable.fromQ8(specCurve());
        // glowstone (minLight 8) at ambient light 2 renders as if at 8...
        assertEquals(table.tintQ8(8), table.tintQ8(2, 8));
        // ...but the clamp never darkens a brighter tile.
        assertEquals(table.tintQ8(20), table.tintQ8(20, 8));
    }

    @Test
    void shadeRgbAtFullLightIsIdentity() {
        assertEquals(0xFFFFFF, LightTintTable.fromQ8(specCurve()).shadeRgb(0xFFFFFF, 31));
    }

    @Test
    void shadeRgbMultipliesPerChannel() {
        // 0x80 * 36 >> 8 = 18 = 0x12; 0xFF * 36 >> 8 = 35 = 0x23.
        assertEquals(0x120023, LightTintTable.fromQ8(specCurve()).shadeRgb(0x8000FF, 0));
    }

    @Test
    void levelOutsideRangeThrows() {
        LightTintTable table = LightTintTable.fromQ8(specCurve());
        assertThrows(IllegalArgumentException.class, () -> table.tintQ8(32));
        assertThrows(IllegalArgumentException.class, () -> table.tintQ8(-1));
        assertThrows(IllegalArgumentException.class, () -> table.tintQ8(0, 32));
    }

    @Test
    void rejectsWrongLength() {
        assertThrows(IllegalArgumentException.class,
                () -> LightTintTable.fromQ8(new int[] {0, 256}));
        assertThrows(IllegalArgumentException.class, () -> LightTintTable.fromQ8(null));
    }

    @Test
    void rejectsDecreasingSequence() {
        int[] curve = specCurve();
        curve[10] = curve[9] - 1;
        assertThrows(IllegalArgumentException.class, () -> LightTintTable.fromQ8(curve));
    }

    @Test
    void rejectsLastEntryNotUnit() {
        int[] curve = specCurve();
        curve[31] = 255;
        assertThrows(IllegalArgumentException.class, () -> LightTintTable.fromQ8(curve));
    }

    @Test
    void rejectsOutOfRangeEntries() {
        int[] curve = specCurve();
        curve[0] = -1;
        assertThrows(IllegalArgumentException.class, () -> LightTintTable.fromQ8(curve));
    }
}
