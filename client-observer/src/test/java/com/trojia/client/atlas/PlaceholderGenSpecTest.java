package com.trojia.client.atlas;

import com.trojia.client.art.ArtMappingException;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link PlaceholderGenSpec}: parsing the shipped
 * {@code placeholderGen} block, the chromatis fill ramp with the clamp-high bucket
 * rule, and the aggregate boot-fails validation.
 */
class PlaceholderGenSpecTest {

    @Nested
    class ShippedMapping {

        private final PlaceholderGenSpec spec =
                PlaceholderGenSpec.parse(ShippedArtMapping.json());

        @Test
        void geometryAndStyleConstants() {
            assertEquals(128, spec.atlasSizePx());
            assertEquals(16, spec.cellPx());
            assertEquals(128, spec.outlineScaleQ8());
            assertEquals(184, spec.floorScaleQ8());
            assertEquals(128, spec.floorGlyphBlendQ8());
        }

        @Test
        void chromatisCarriesTheThreeBucketFillRamp() {
            // BLESSING-QUEUE ruling 5: silver -> pale gold -> gold.
            PlaceholderGenSpec.Entry chromatis = spec.entry("chromatis");
            assertEquals(3, chromatis.bucketCount());
            assertEquals(0x9FB8D8, chromatis.fillRgb(0));
            assertEquals(0xE3CE7A, chromatis.fillRgb(1));
            assertEquals(0xF5C542, chromatis.fillRgb(2));
            assertEquals('C', chromatis.glyph());
            assertEquals(0x000000, chromatis.glyphRgb());
            assertFalse(chromatis.fluid());
        }

        @Test
        void bucketsClampHighNeverWrap() {
            PlaceholderGenSpec.Entry chromatis = spec.entry("chromatis");
            assertEquals(chromatis.fillRgb(2), chromatis.fillRgb(3));
            assertEquals(chromatis.fillRgb(2), chromatis.fillRgb(9));
            assertThrows(IllegalArgumentException.class, () -> chromatis.fillRgb(-1));
        }

        @Test
        void singleColorEntriesHaveOneBucket() {
            PlaceholderGenSpec.Entry granite = spec.entry("granite");
            assertEquals(1, granite.bucketCount());
            assertEquals(0xA9328F, granite.fillRgb(0));
            assertEquals(0xA9328F, granite.fillRgb(2)); // clamp
            assertEquals('G', granite.glyph());
            assertEquals(0xFFFFFF, granite.glyphRgb());
        }

        @Test
        void canonOverridesAreCarriedVerbatim() {
            assertEquals(0xB22D2D, spec.entry("glowstone").fillRgb(0)); // eerie red
            assertEquals(0xA9D4E8, spec.entry("ice").fillRgb(0));
        }

        @Test
        void waterIsAFluidEntry() {
            PlaceholderGenSpec.Entry water = spec.entry("water");
            assertTrue(water.fluid());
            assertEquals(0x3F6FB5, water.fillRgb(0));
            assertEquals('~', water.glyph());
            assertEquals(0xFFFFFF, water.glyphRgb());
        }

        @Test
        void missingCheckerStyle() {
            assertEquals(0xFF00FF, spec.missingCheckerColorA());
            assertEquals(0x000000, spec.missingCheckerColorB());
            assertEquals(8, spec.missingCheckerPx());
        }

        @Test
        void listsAllSeventeenIdsSorted() {
            // 16 placeholderGen.materials + water.
            assertEquals(17, spec.entryIds().size());
            assertEquals("ash", spec.entryIds().first());
            assertEquals("water", spec.entryIds().last());
            assertTrue(spec.entryIds().contains("trudgeon_wood@getilia_soak"));
        }

        @Test
        void unlistedIdsReturnNullForTheMintFallback() {
            assertNull(spec.entry("mystery_ore"));
            assertThrows(IllegalArgumentException.class, () -> spec.entry(" "));
        }
    }

    @Nested
    class Validation {

        @Test
        void missingPlaceholderGenBlockFailsBoot() {
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> PlaceholderGenSpec.parse("{\"schemaVersion\": 1}"));
            assertTrue(e.getMessage().contains("placeholderGen"));
        }

        @Test
        void malformedJsonFailsBoot() {
            assertThrows(ArtMappingException.class,
                    () -> PlaceholderGenSpec.parse("not json {"));
        }

        @Test
        void aggregatesEveryDefectIntoOneException() {
            // Bad geometry, bad Q8, entry with both color styles, bad glyph,
            // missing 'missing': all reported at once, one per line.
            String bad = """
                    { "placeholderGen": {
                        "atlasSizePx": 100, "cellPx": 16,
                        "outlineScaleQ8": 999, "floorScaleQ8": 184,
                        "floorGlyphBlendQ8": 128,
                        "materials": {
                          "both": { "color": "#112233", "bucketColors": ["#445566"],
                                    "glyph": "b", "glyphColor": "#FFFFFF" },
                          "badglyph": { "color": "#112233", "glyph": "xx",
                                        "glyphColor": "#FFFFFF" },
                          "badcolor": { "color": "112233", "glyph": "c",
                                        "glyphColor": "#FFFFFF" }
                        }
                    }}""";
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> PlaceholderGenSpec.parse(bad));
            String message = e.getMessage();
            assertTrue(message.contains("atlasSizePx"), message);
            assertTrue(message.contains("outlineScaleQ8"), message);
            assertTrue(message.contains("materials.both"), message);
            assertTrue(message.contains("materials.badglyph"), message);
            assertTrue(message.contains("materials.badcolor"), message);
            assertTrue(message.contains("missing"), message);
            assertTrue(message.lines().count() >= 6, message);
        }

        @Test
        void cellTooSmallForTheGlyphBoxFailsBoot() {
            String bad = """
                    { "placeholderGen": {
                        "atlasSizePx": 32, "cellPx": 4,
                        "outlineScaleQ8": 128, "floorScaleQ8": 184,
                        "floorGlyphBlendQ8": 128,
                        "missing": { "checkerColors": ["#FF00FF", "#000000"],
                                     "checkerPx": 2 }
                    }}""";
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> PlaceholderGenSpec.parse(bad));
            assertTrue(e.getMessage().contains("cellPx"));
        }
    }
}
