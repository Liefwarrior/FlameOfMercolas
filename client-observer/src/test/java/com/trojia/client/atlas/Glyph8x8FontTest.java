package com.trojia.client.atlas;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Headless tests for the built-in 8x8 ASCII bitmap font. */
class Glyph8x8FontTest {

    @Test
    void spaceIsTheOnlyEmptyPrintableGlyph() {
        for (char c = Glyph8x8Font.FIRST; c <= Glyph8x8Font.LAST; c++) {
            int inked = 0;
            for (int row = 0; row < Glyph8x8Font.HEIGHT; row++) {
                inked |= Glyph8x8Font.rowBits(c, row);
            }
            if (c == ' ') {
                assertEquals(0, inked, "space must be blank");
            } else {
                assertTrue(inked != 0, "glyph '" + c + "' has no ink");
            }
        }
    }

    @Test
    void everyRowFitsInEightBits() {
        for (char c = Glyph8x8Font.FIRST; c <= Glyph8x8Font.LAST; c++) {
            for (int row = 0; row < Glyph8x8Font.HEIGHT; row++) {
                int bits = Glyph8x8Font.rowBits(c, row);
                assertTrue(bits >= 0 && bits <= 0xFF,
                        "glyph '" + c + "' row " + row + " = " + bits);
            }
        }
    }

    @Test
    void isInkMatchesRowBitsBitSevenIsLeftmost() {
        // 'T' row 0 is 0x7C = 01111100: columns 1..5 inked, 0/6/7 clear.
        assertEquals(0x7C, Glyph8x8Font.rowBits('T', 0));
        assertFalse(Glyph8x8Font.isInk('T', 0, 0));
        for (int x = 1; x <= 5; x++) {
            assertTrue(Glyph8x8Font.isInk('T', x, 0), "column " + x);
        }
        assertFalse(Glyph8x8Font.isInk('T', 6, 0));
        assertFalse(Glyph8x8Font.isInk('T', 7, 0));
    }

    @Test
    void outOfRangeCharactersFallBackToTheHollowBox() {
        for (int row = 0; row < Glyph8x8Font.HEIGHT; row++) {
            assertEquals(Glyph8x8Font.rowBits((char) 0x1F, row),
                    Glyph8x8Font.rowBits((char) 0x7F, row));
            assertEquals(Glyph8x8Font.rowBits('é', row),
                    Glyph8x8Font.rowBits((char) 0x7F, row));
        }
        // The box has ink (it must be visible, not blank).
        assertTrue(Glyph8x8Font.rowBits((char) 0x7F, 0) != 0);
    }

    @Test
    void rejectsOutOfRangeCoordinates() {
        assertThrows(IllegalArgumentException.class, () -> Glyph8x8Font.rowBits('A', -1));
        assertThrows(IllegalArgumentException.class, () -> Glyph8x8Font.rowBits('A', 8));
        assertThrows(IllegalArgumentException.class, () -> Glyph8x8Font.isInk('A', -1, 0));
        assertThrows(IllegalArgumentException.class, () -> Glyph8x8Font.isInk('A', 8, 0));
    }
}
