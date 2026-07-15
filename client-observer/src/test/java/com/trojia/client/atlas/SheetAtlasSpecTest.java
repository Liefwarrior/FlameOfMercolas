package com.trojia.client.atlas;

import com.trojia.client.art.ArtMappingException;
import com.trojia.client.art.JsonTileArtResolver;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * GL-free tests for {@link SheetAtlasSpec}: the cosmetic-variant schema (TILE-ART-SPEC
 * section 12) — a region is one {@code [col,row]} pair or an array of them — and the shipped
 * Kenney pack ({@code content/art/kenney/art-mapping.json}) parsing, cross-checking against
 * {@link JsonTileArtResolver}'s referenced region set and asserting real per-surface variety.
 * No GL context; the JSON parse is pure.
 */
class SheetAtlasSpecTest {

    private static String doc(String regions) {
        return """
                {
                  "atlas": "sheet.png",
                  "tilePx": 16,
                  "sheet": { "columns": 49, "rows": 22 },
                  "regions": %s
                }
                """.formatted(regions);
    }

    private static String docWithPatterns(String regions, String patterns) {
        return """
                {
                  "atlas": "sheet.png",
                  "tilePx": 16,
                  "sheet": { "columns": 49, "rows": 22 },
                  "regions": %s,
                  "variantPatterns": %s
                }
                """.formatted(regions, patterns);
    }

    @Nested
    class VariantSchema {

        @Test
        void singlePairShorthandIsOneVariant() {
            SheetAtlasSpec spec = SheetAtlasSpec.parse(doc("{ \"wall\": [10, 17] }"));
            assertEquals(1, spec.variantCount("wall"));
            assertEquals(List.of(new AtlasCellRect(10 * 16, 17 * 16, 16, 16)),
                    spec.cellRects("wall"));
        }

        @Test
        void arrayOfPairsKeepsEveryVariantInOrder() {
            SheetAtlasSpec spec = SheetAtlasSpec.parse(
                    doc("{ \"wall_brick\": [[10, 17], [10, 18], [11, 18]] }"));
            assertEquals(3, spec.variantCount("wall_brick"));
            assertEquals(List.of(
                            new AtlasCellRect(10 * 16, 17 * 16, 16, 16),
                            new AtlasCellRect(10 * 16, 18 * 16, 16, 16),
                            new AtlasCellRect(11 * 16, 18 * 16, 16, 16)),
                    spec.cellRects("wall_brick"));
        }

        @Test
        void mixedSingleAndMultiRegionsCoexist() {
            SheetAtlasSpec spec = SheetAtlasSpec.parse(doc(
                    "{ \"solo\": [1, 2], \"multi\": [[3, 4], [5, 6]] }"));
            assertEquals(1, spec.variantCount("solo"));
            assertEquals(2, spec.variantCount("multi"));
        }

        @Test
        void unknownRegionHasNoVariantsAndCellRectsThrows() {
            SheetAtlasSpec spec = SheetAtlasSpec.parse(doc("{ \"wall\": [0, 0] }"));
            assertEquals(0, spec.variantCount("nope"));
            assertFalse(spec.contains("nope"));
            assertThrows(IllegalArgumentException.class, () -> spec.cellRects("nope"));
        }

        @Test
        void outOfBoundsVariantFailsAndNamesTheIndex() {
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> SheetAtlasSpec.parse(doc("{ \"w\": [[0, 0], [49, 0]] }")));
            assertTrue(e.getMessage().contains("regions.w[1]"), e.getMessage());
            assertTrue(e.getMessage().contains("col 49"), e.getMessage());
        }

        @Test
        void malformedVariantPairFails() {
            assertThrows(ArtMappingException.class,
                    () -> SheetAtlasSpec.parse(doc("{ \"w\": [[0, 0], [1]] }")));
        }

        @Test
        void emptyVariantListFails() {
            assertThrows(ArtMappingException.class,
                    () -> SheetAtlasSpec.parse(doc("{ \"w\": [] }")));
        }

        @Test
        void allDefectsAggregateIntoOneException() {
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> SheetAtlasSpec.parse(doc("{ \"a\": [99, 0], \"b\": [0, 99] }")));
            assertTrue(e.getMessage().contains("regions.a"), e.getMessage());
            assertTrue(e.getMessage().contains("regions.b"), e.getMessage());
        }
    }

    @Nested
    class VariantPatterns {

        @Test
        void absentPatternDefaultsToHash() {
            SheetAtlasSpec spec = SheetAtlasSpec.parse(doc("{ \"w\": [[0, 0], [1, 0]] }"));
            assertEquals(VariantPattern.HASH, spec.variantPattern("w"));
        }

        @Test
        void periodicPatternIsParsed() {
            SheetAtlasSpec spec = SheetAtlasSpec.parse(docWithPatterns(
                    "{ \"pave\": [[0, 0], [2, 0]] }", "{ \"pave\": \"periodic\" }"));
            assertEquals(VariantPattern.PERIODIC, spec.variantPattern("pave"));
        }

        @Test
        void patternForUnknownRegionFails() {
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> SheetAtlasSpec.parse(docWithPatterns(
                            "{ \"w\": [0, 0] }", "{ \"nope\": \"periodic\" }")));
            assertTrue(e.getMessage().contains("variantPatterns.nope"), e.getMessage());
        }

        @Test
        void unknownPatternTokenFails() {
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> SheetAtlasSpec.parse(docWithPatterns(
                            "{ \"w\": [0, 0] }", "{ \"w\": \"stripey\" }")));
            assertTrue(e.getMessage().contains("variantPatterns.w"), e.getMessage());
            assertTrue(e.getMessage().contains("stripey"), e.getMessage());
        }
    }

    @Nested
    class ShippedKenneyPack {

        private final SheetAtlasSpec spec = SheetAtlasSpec.parse(ShippedArtMapping.kenneyJson());
        private final JsonTileArtResolver resolver =
                JsonTileArtResolver.parse(ShippedArtMapping.kenneyJson());

        @Test
        void gridDimensionsMatchTheKenneySheet() {
            // Sixth revision (DECISIONS.md art register): the dominant-surface atlas swapped
            // from the Kenney 1-Bit sheet (49x22, an icon atlas that tiled into ugly glyph
            // noise) to Kenney's Roguelike Modern City Pack (37x28, a seamless masonry terrain
            // set). Same SheetAtlasSpec grid mechanism, different sheet + cell coords.
            assertEquals(37, spec.columns());
            assertEquals(28, spec.rows());
            assertEquals(16, spec.tilePx());
        }

        @Test
        void everyRegionTheResolverCanReturnResolvesInTheSheet() {
            // The boot-time check in ObserverApp (TILE-ART-SPEC section 7.2): no byAppearance
            // or fluid region name may point at a cell the sheet does not carry.
            assertDoesNotThrow(() -> spec.validateReferenced(resolver.referencedRegionNames()));
        }

        @Test
        void smoothSurfacesRenderHomogeneously() {
            // Seventh revision (DECISIONS.md art register, Eli 2026-07-15 senior-level-design
            // homogeneity pass, art-mapping.json variantPatternsProvenance): Eli's biggest issue
            // was the AI-generated patchwork the multi-cell hash scatter produced on every
            // dominant SMOOTH surface. Each such region is now trimmed to ONE cell, so
            // variantCount<=1 => the renderer draws variant 0 => one clean repeated tile.
            for (String smooth : List.of("wall_brick", "wall_stone", "wall_masonry", "wall_plank",
                    "wall_hatch", "floor_tile", "floor_stone", "floor_plank",
                    "roof_thatch", "roof_tile", "roof_cloth",
                    "facade_granite", "facade_brick", "facade_reman")) {
                assertEquals(1, spec.variantCount(smooth),
                        smooth + " must be a single homogeneous cell");
            }
        }

        @Test
        void roughSurfacesAndWaterKeepTheScatter() {
            // Only the deliberately-rough materials (bare dirt/ash ground, rubble walls) and the
            // moving harbor water keep the multi-cell HASH scatter — the "less regular" zones
            // Eli explicitly reserved irregularity for.
            assertTrue(spec.variantCount("floor_earth") > 1);
            assertTrue(spec.variantCount("wall_rubble") > 1);
            assertTrue(spec.variantCount("water") > 1);
            for (String rough : List.of("floor_earth", "wall_rubble", "water")) {
                assertEquals(VariantPattern.HASH, spec.variantPattern(rough),
                        rough + " scatters via the position hash");
            }
        }

        @Test
        void theSidewalkIsAPeriodicTwoTonePaver() {
            // The one surface Eli calls out as needing to be "obvious and not irregularly
            // patterned": floor_pave is a dedicated 2-cell region tagged periodic, so the
            // renderer draws a fixed (x^y)&1 laid-paver weave a random hash cannot produce.
            assertTrue(spec.contains("floor_pave"));
            assertEquals(2, spec.variantCount("floor_pave"));
            assertEquals(VariantPattern.PERIODIC, spec.variantPattern("floor_pave"));
            // Granite floor (the civic-paving material — quay apron, bathhouse, guardhouse,
            // watch-post, well plaza) resolves onto it; smooth interior floors default to HASH
            // (harmless, since their single cell always draws variant 0).
            assertEquals(VariantPattern.HASH, spec.variantPattern("floor_tile"));
        }

        @Test
        void referencesABroadSwathOfTheSheet() {
            // "Really use the Kenney graphics": far more than the original six shared cells.
            int distinctCells = spec.regionNames().stream()
                    .flatMap(name -> spec.cellRects(name).stream())
                    .collect(java.util.stream.Collectors.toSet())
                    .size();
            assertTrue(distinctCells >= 30,
                    "expected a broad tile vocabulary, got " + distinctCells + " distinct cells");
        }
    }
}
