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
    class ShippedKenneyPack {

        private final SheetAtlasSpec spec = SheetAtlasSpec.parse(ShippedArtMapping.kenneyJson());
        private final JsonTileArtResolver resolver =
                JsonTileArtResolver.parse(ShippedArtMapping.kenneyJson());

        @Test
        void gridDimensionsMatchTheKenneySheet() {
            assertEquals(49, spec.columns());
            assertEquals(22, spec.rows());
            assertEquals(16, spec.tilePx());
        }

        @Test
        void everyRegionTheResolverCanReturnResolvesInTheSheet() {
            // The boot-time check in ObserverApp (TILE-ART-SPEC section 7.2): no byAppearance
            // or fluid region name may point at a cell the sheet does not carry.
            assertDoesNotThrow(() -> spec.validateReferenced(resolver.referencedRegionNames()));
        }

        @Test
        void surfacesCarryRealVariety() {
            // The whole point of the re-scope: a large wall/floor is not one repeated sprite.
            // Counts reflect the colored-pack cell picks (art-mapping.json regionsProvenance);
            // several of the monochrome-era cells recolor to a different family in the colored
            // sheet (e.g. old wall_stone's (9,5) bakes blue, not beige) so the sets differ from
            // the superseded monochrome-tint mapping.
            assertEquals(3, spec.variantCount("wall_brick"));
            assertEquals(3, spec.variantCount("wall_stone"));
            assertEquals(4, spec.variantCount("wall_rubble"));
            assertEquals(2, spec.variantCount("wall_plank"));
            assertEquals(3, spec.variantCount("floor_tile"));
            assertEquals(4, spec.variantCount("floor_plank"));
            assertEquals(4, spec.variantCount("floor_earth"));
            assertEquals(4, spec.variantCount("wall_crystal"));
            assertEquals(3, spec.variantCount("wall_moss"));
            assertEquals(5, spec.variantCount("water"));
        }

        @Test
        void mostReferencedRegionsHaveMoreThanOneVariant() {
            long multi = resolver.referencedRegionNames().stream()
                    .filter(name -> spec.variantCount(name) > 1)
                    .count();
            // Only `missing` (the framed-X fallback) is deliberately single-cell.
            assertTrue(multi >= 10, "expected many multi-variant regions, got " + multi);
            assertEquals(1, spec.variantCount("missing"));
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
