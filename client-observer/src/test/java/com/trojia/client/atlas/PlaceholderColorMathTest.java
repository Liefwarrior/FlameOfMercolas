package com.trojia.client.atlas;

import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.JsonValue;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link PlaceholderColorMath}: FNV-1a-64 reference vectors, the
 * normative HSL mint of TILE-ART-SPEC section 6.1/6.2 (asserted against the shipped
 * {@code art-mapping.json} — the generator must reproduce the listed table exactly),
 * the WCAG glyph-contrast rule, and the Q8 channel ops.
 */
class PlaceholderColorMathTest {

    @Nested
    class Fnv1a64 {

        @Test
        void emptyStringHashesToOffsetBasis() {
            assertEquals(PlaceholderColorMath.FNV_OFFSET_BASIS,
                    PlaceholderColorMath.fnv1a64(""));
        }

        @Test
        void referenceVectors() {
            // Published FNV-1a 64-bit test vectors.
            assertEquals(0xAF63DC4C8601EC8CL, PlaceholderColorMath.fnv1a64("a"));
            assertEquals(0x85944171F73967E8L, PlaceholderColorMath.fnv1a64("foobar"));
        }

        @Test
        void rejectsNull() {
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.fnv1a64(null));
        }
    }

    @Nested
    class Mint {

        @Test
        void reproducesTheNormativeTableOfSection62() {
            // TILE-ART-SPEC section 6.2, every "hash" row.
            assertEquals(0xA9328F, PlaceholderColorMath.mintFillRgb("granite"));
            assertEquals(0x29679E, PlaceholderColorMath.mintFillRgb("dirt"));
            assertEquals(0x24768F, PlaceholderColorMath.mintFillRgb("oak"));
            assertEquals(0xD62971, PlaceholderColorMath.mintFillRgb("thatch"));
            assertEquals(0xA6433A, PlaceholderColorMath.mintFillRgb("trudgeon_wood"));
            assertEquals(0x805B2D,
                    PlaceholderColorMath.mintFillRgb("trudgeon_wood@getilia_soak"));
            assertEquals(0x461E94, PlaceholderColorMath.mintFillRgb("steel"));
            assertEquals(0x335899, PlaceholderColorMath.mintFillRgb("brick"));
            assertEquals(0xCEAC27, PlaceholderColorMath.mintFillRgb("chromatis_melt"));
            assertEquals(0x2A6993, PlaceholderColorMath.mintFillRgb("phorys"));
            assertEquals(0x51D530, PlaceholderColorMath.mintFillRgb("lightstone"));
            assertEquals(0xA5B635, PlaceholderColorMath.mintFillRgb("lightstone_shards"));
            assertEquals(0xB6BD28, PlaceholderColorMath.mintFillRgb("ash"));
        }

        @Test
        void derivedIdsHashTheFullStringSoTheyDifferFromTheBase() {
            assertTrue(PlaceholderColorMath.mintFillRgb("trudgeon_wood")
                    != PlaceholderColorMath.mintFillRgb("trudgeon_wood@getilia_soak"));
        }

        @Test
        void everyHashSourcedMappingEntryMatchesTheMintExactly() {
            // TILE-ART-SPEC section 6.1: "the generator must reproduce this table
            // exactly (it is asserted by test against art-mapping.json)".
            JsonValue root = new JsonReader().parse(ShippedArtMapping.json());
            JsonValue gen = root.get("placeholderGen");
            int checked = 0;
            for (String section : new String[]{"materials", "fluids"}) {
                for (JsonValue entry = gen.get(section).child; entry != null;
                        entry = entry.next) {
                    if (!"hash".equals(entry.getString("colorSource", ""))) {
                        continue;
                    }
                    int listedFill = PlaceholderColorMath.parseRgb(entry.getString("color"));
                    int listedGlyph =
                            PlaceholderColorMath.parseRgb(entry.getString("glyphColor"));
                    assertEquals(listedFill,
                            PlaceholderColorMath.mintFillRgb(entry.name),
                            entry.name + ": fill drifted from the mint");
                    assertEquals(listedGlyph,
                            PlaceholderColorMath.mintGlyphRgb(listedFill),
                            entry.name + ": glyph contrast drifted from the mint");
                    checked++;
                }
            }
            assertEquals(13, checked, "expected 13 hash-sourced entries in the mapping");
        }

        @Test
        void rejectsBlankIds() {
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.mintFillRgb(null));
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.mintFillRgb("  "));
        }
    }

    @Nested
    class GlyphContrast {

        @Test
        void darkFillsGetWhiteGlyphs() {
            assertEquals(0xFFFFFF, PlaceholderColorMath.mintGlyphRgb(0xA9328F)); // granite
            assertEquals(0xFFFFFF, PlaceholderColorMath.mintGlyphRgb(0x000000));
        }

        @Test
        void brightFillsGetBlackGlyphs() {
            assertEquals(0x000000, PlaceholderColorMath.mintGlyphRgb(0x51D530)); // lightstone
            assertEquals(0x000000, PlaceholderColorMath.mintGlyphRgb(0xFFFFFF));
        }

        @Test
        void luminanceEndpoints() {
            assertEquals(0.0, PlaceholderColorMath.relativeLuminance(0x000000), 1e-12);
            assertEquals(1.0, PlaceholderColorMath.relativeLuminance(0xFFFFFF), 1e-9);
        }
    }

    @Nested
    class HslToRgb {

        @Test
        void primariesAndGreys() {
            assertEquals(0xFF0000, PlaceholderColorMath.hslToRgb(0, 100, 50));
            assertEquals(0x00FF00, PlaceholderColorMath.hslToRgb(120, 100, 50));
            assertEquals(0x0000FF, PlaceholderColorMath.hslToRgb(240, 100, 50));
            assertEquals(0x000000, PlaceholderColorMath.hslToRgb(0, 0, 0));
            assertEquals(0xFFFFFF, PlaceholderColorMath.hslToRgb(0, 0, 100));
            assertEquals(0x808080, PlaceholderColorMath.hslToRgb(180, 0, 50));
        }

        @Test
        void rejectsOutOfRangeArguments() {
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.hslToRgb(360, 50, 50));
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.hslToRgb(-1, 50, 50));
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.hslToRgb(0, 101, 50));
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.hslToRgb(0, 50, 101));
        }
    }

    @Nested
    class Q8Ops {

        @Test
        void scaleIdentityAndZero() {
            assertEquals(0xA9328F, PlaceholderColorMath.scaleRgbQ8(0xA9328F, 256));
            assertEquals(0x000000, PlaceholderColorMath.scaleRgbQ8(0xA9328F, 0));
        }

        @Test
        void scaleHalvesPerChannelWithFloor() {
            // (c * 128) >> 8 == c >> 1: 0xA9 -> 0x54, 0x32 -> 0x19, 0x8F -> 0x47.
            assertEquals(0x541947, PlaceholderColorMath.scaleRgbQ8(0xA9328F, 128));
        }

        @Test
        void blendEndpointsAndMidpoint() {
            assertEquals(0x102030, PlaceholderColorMath.blendRgbQ8(0x102030, 0xFFFFFF, 0));
            assertEquals(0xFFFFFF, PlaceholderColorMath.blendRgbQ8(0x102030, 0xFFFFFF, 256));
            // 50 percent: (0x00*128 + 0xFF*128) >> 8 = 0x7F per channel.
            assertEquals(0x7F7F7F, PlaceholderColorMath.blendRgbQ8(0x000000, 0xFFFFFF, 128));
        }

        @Test
        void rejectsOutOfRangeFactors() {
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.scaleRgbQ8(0xFFFFFF, 257));
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.blendRgbQ8(0, 0, -1));
        }
    }

    @Nested
    class ParseRgb {

        @Test
        void parsesHexLiterals() {
            assertEquals(0x9FB8D8, PlaceholderColorMath.parseRgb("#9FB8D8"));
            assertEquals(0x000000, PlaceholderColorMath.parseRgb("#000000"));
            assertEquals(0xFFFFFF, PlaceholderColorMath.parseRgb("#ffffff"));
        }

        @Test
        void rejectsMalformedLiterals() {
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.parseRgb(null));
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.parseRgb("9FB8D8"));
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.parseRgb("#9FB8"));
            assertThrows(IllegalArgumentException.class,
                    () -> PlaceholderColorMath.parseRgb("#GGGGGG"));
        }
    }
}
