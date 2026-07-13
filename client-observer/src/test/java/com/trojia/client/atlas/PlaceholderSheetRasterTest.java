package com.trojia.client.atlas;

import com.badlogic.gdx.utils.JsonReader;
import com.badlogic.gdx.utils.JsonValue;
import com.trojia.client.art.JsonTileArtResolver;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link PlaceholderSheetRaster} built through
 * {@link PlaceholderAtlasFactory#buildRaster} (the GL-free front half): layout
 * determinism (byte-identical {@code int[]} across runs), one distinct cell per
 * referenced region, the chromatis bucket ramp against the shipped mapping, the
 * block/floor/fluid/missing cell styles of TILE-ART-SPEC section 6, and the
 * byte-deterministic {@code .atlas} text.
 */
class PlaceholderSheetRasterTest {

    private static final String JSON = ShippedArtMapping.json();

    private final PlaceholderSheetRaster raster = PlaceholderAtlasFactory.buildRaster(JSON);

    private static int opaque(int rgb888) {
        return 0xFF000000 | rgb888;
    }

    /** Pixel at an offset inside a named cell. */
    private int cellPixel(String region, int dx, int dy) {
        AtlasCellRect rect = raster.regionTable().cellRect(region);
        return raster.pixelArgb(rect.x() + dx, rect.y() + dy);
    }

    @Nested
    class Determinism {

        @Test
        void pixelBufferIsByteIdenticalAcrossRuns() {
            PlaceholderSheetRaster again = PlaceholderAtlasFactory.buildRaster(JSON);
            assertArrayEquals(raster.pixelsArgb(), again.pixelsArgb());
            assertEquals(raster.atlasText("placeholder.png"),
                    again.atlasText("placeholder.png"));
        }

        @Test
        void regionNameInputOrderNeverMatters() {
            PlaceholderGenSpec spec = PlaceholderGenSpec.parse(JSON);
            JsonTileArtResolver resolver = JsonTileArtResolver.parse(JSON);
            List<String> names = new ArrayList<>(resolver.referencedRegionNames());
            names.sort(null);
            PlaceholderSheetRaster sorted = new PlaceholderSheetRaster(
                    spec, resolver.missingRegionName(), names);
            PlaceholderSheetRaster reversed = new PlaceholderSheetRaster(
                    spec, resolver.missingRegionName(), names.reversed());
            assertArrayEquals(sorted.pixelsArgb(), reversed.pixelsArgb());
        }
    }

    @Nested
    class Layout {

        @Test
        void everyReferencedRegionGetsADistinctCell() {
            Set<String> referenced =
                    JsonTileArtResolver.parse(JSON).referencedRegionNames();
            assertEquals(38, referenced.size(),
                    "TILE-ART-SPEC section 6.2: v0 inventory is 38 regions");
            Set<AtlasCellRect> rects = new HashSet<>();
            for (String name : referenced) {
                assertTrue(raster.regionTable().contains(name), name);
                rects.add(raster.regionTable().cellRect(name));
            }
            assertEquals(referenced.size(), rects.size(), "cells must not collide");
            assertEquals(referenced.size(), raster.regionTable().size(),
                    "no extra cells beyond the referenced set");
        }

        @Test
        void geometryMatchesThePlaceholderGenBlock() {
            assertEquals(128, raster.atlasSizePx());
            assertEquals(16, raster.cellPx());
            assertEquals(8, raster.regionTable().columns());
        }

        @Test
        void unusedCellsStayFullyTransparent() {
            // 38 regions on a 64-cell sheet: cell index 38 (row 4, column 6) onward
            // is unused, as is the bottom-right corner.
            assertEquals(0x00000000, raster.pixelArgb(6 * 16, 4 * 16));
            assertEquals(0x00000000, raster.pixelArgb(127, 127));
        }
    }

    @Nested
    class ChromatisRamp {

        @Test
        void bucketFillsMatchTheMappingBucketColors() {
            // The fill ramp read straight from the shipped placeholderGen block.
            JsonValue chromatis = new JsonReader().parse(JSON)
                    .get("placeholderGen").get("materials").get("chromatis");
            JsonValue stops = chromatis.get("bucketColors");
            assertEquals(3, stops.size);
            String[] regions = {"chromatis", "chromatis.a1", "chromatis.a2"};
            for (int bucket = 0; bucket < 3; bucket++) {
                int listed = PlaceholderColorMath.parseRgb(stops.getString(bucket));
                assertEquals(opaque(listed), cellPixel(regions[bucket], 2, 2),
                        regions[bucket] + " fill must be bucketColors[" + bucket + "]");
            }
        }

        @Test
        void floorVariantsScaleTheSameRamp() {
            int silver = PlaceholderColorMath.parseRgb("#9FB8D8");
            int expected = PlaceholderColorMath.scaleRgbQ8(silver, 184);
            assertEquals(opaque(expected), cellPixel("chromatis.floor", 2, 2));
        }

        @Test
        void glyphIsBlackCPerTheMapping() {
            // 'C' row 0 is 0x38: in-cell columns 6..8 of row 4 are glyph texels.
            assertEquals(opaque(0x000000), cellPixel("chromatis", 6, 4));
        }
    }

    @Nested
    class CellStyles {

        @Test
        void blockCellsAreFillPlusOutlinePlusGlyph() {
            int fill = 0xA9328F; // granite, TILE-ART-SPEC section 6.2
            assertEquals(opaque(fill), cellPixel("granite", 2, 2), "fill");
            int outline = PlaceholderColorMath.scaleRgbQ8(fill, 128);
            assertEquals(opaque(outline), cellPixel("granite", 0, 0), "outline corner");
            assertEquals(opaque(outline), cellPixel("granite", 15, 8), "outline edge");
            // 'G' row 0 is 0x38: glyph texel at in-cell (6, 4), white per the table.
            assertEquals(opaque(0xFFFFFF), cellPixel("granite", 6, 4), "glyph");
        }

        @Test
        void floorCellsScaleFillAndOutlineAndBlendTheGlyph() {
            int fill = 0xA9328F;
            int floorFill = PlaceholderColorMath.scaleRgbQ8(fill, 184);
            int floorOutline = PlaceholderColorMath.scaleRgbQ8(
                    PlaceholderColorMath.scaleRgbQ8(fill, 128), 184);
            int floorGlyph = PlaceholderColorMath.blendRgbQ8(0xFFFFFF, floorFill, 128);
            assertEquals(opaque(floorFill), cellPixel("granite.floor", 2, 2), "fill");
            assertEquals(opaque(floorOutline), cellPixel("granite.floor", 0, 0), "outline");
            assertEquals(opaque(floorGlyph), cellPixel("granite.floor", 6, 4), "glyph");
        }

        @Test
        void fluidCellsHaveNoOutlineAndFullAlpha() {
            int fill = 0x3F6FB5; // water override
            assertEquals(opaque(fill), cellPixel("water", 0, 0),
                    "border texel must be plain fill (no outline)");
            assertEquals(opaque(fill), cellPixel("water", 2, 2));
            // '~' row 3 is 0x34: glyph texel at in-cell (6, 7), white.
            assertEquals(opaque(0xFFFFFF), cellPixel("water", 6, 7), "glyph");
        }

        @Test
        void missingCellIsTheEightPixelChecker() {
            assertEquals(opaque(0xFF00FF), cellPixel("missing", 0, 0));
            assertEquals(opaque(0xFF00FF), cellPixel("missing", 7, 7));
            assertEquals(opaque(0x000000), cellPixel("missing", 8, 0));
            assertEquals(opaque(0x000000), cellPixel("missing", 0, 8));
            assertEquals(opaque(0xFF00FF), cellPixel("missing", 8, 8));
        }

        @Test
        void derivedMaterialCellsUseTheirOwnListedColor() {
            assertEquals(opaque(0x805B2D),
                    cellPixel("trudgeon_wood@getilia_soak", 2, 2));
            assertEquals(opaque(0xA6433A), cellPixel("trudgeon_wood", 2, 2));
        }

        @Test
        void glowstoneCanonOverrideWins() {
            assertEquals(opaque(0xB22D2D), cellPixel("glowstone", 2, 2));
        }
    }

    @Nested
    class MintFallback {

        @Test
        void unlistedIdsFallToTheFnvHslMint() {
            PlaceholderGenSpec spec = PlaceholderGenSpec.parse(JSON);
            PlaceholderSheetRaster minted = new PlaceholderSheetRaster(
                    spec, "missing", List.of("missing", "mystery_ore"));
            int fill = PlaceholderColorMath.mintFillRgb("mystery_ore");
            int glyphRgb = PlaceholderColorMath.mintGlyphRgb(fill);
            AtlasCellRect rect = minted.regionTable().cellRect("mystery_ore");
            assertEquals(opaque(fill), minted.pixelArgb(rect.x() + 2, rect.y() + 2));
            assertEquals(opaque(PlaceholderColorMath.scaleRgbQ8(fill, 128)),
                    minted.pixelArgb(rect.x(), rect.y()), "outline");
            // Glyph is the id's first char 'm'; row 2 is 0x68 = 01101000:
            // in-cell columns 5, 6, 8 of row 6 are glyph texels.
            assertEquals(opaque(glyphRgb),
                    minted.pixelArgb(rect.x() + 5, rect.y() + 6), "glyph");
        }
    }

    @Nested
    class AtlasText {

        @Test
        void standardLibgdxFormatNearestFiltered() {
            String text = raster.atlasText("placeholder.png");
            assertTrue(text.startsWith("placeholder.png\n"
                    + "size: 128, 128\n"
                    + "format: RGBA8888\n"
                    + "filter: Nearest, Nearest\n"
                    + "repeat: none\n"), text.substring(0, 100));
            assertTrue(text.endsWith("\n"));
        }

        @Test
        void regionsAppearInAscendingAsciiOrderWithLayoutCoordinates() {
            String text = raster.atlasText("placeholder.png");
            int ash = text.indexOf("\nash\n");
            int chromatis = text.indexOf("\nchromatis\n");
            int chromatisA1 = text.indexOf("\nchromatis.a1\n");
            int missing = text.indexOf("\nmissing\n");
            int water = text.indexOf("\nwater\n");
            assertTrue(ash > 0 && ash < chromatis && chromatis < chromatisA1
                    && chromatisA1 < missing && missing < water, "sorted order");
            AtlasCellRect rect = raster.regionTable().cellRect("ash");
            assertTrue(text.contains("\nash\n  rotate: false\n  xy: "
                            + rect.x() + ", " + rect.y() + "\n  size: 16, 16\n"),
                    "ash entry must carry its layout rect");
        }

        @Test
        void rejectsBlankPngName() {
            assertThrows(IllegalArgumentException.class, () -> raster.atlasText(" "));
        }
    }

    @Nested
    class Arguments {

        @Test
        void cellOfComposesTheGrammar() {
            assertEquals(raster.regionTable().cellRect("chromatis.floor.a1"),
                    raster.cellOf("chromatis", "floor", 1));
            assertEquals(raster.regionTable().cellRect("granite"),
                    raster.cellOf("granite", "block", 0));
        }

        @Test
        void pixelLookupRejectsOutOfBoundsCoordinates() {
            assertThrows(IllegalArgumentException.class, () -> raster.pixelArgb(-1, 0));
            assertThrows(IllegalArgumentException.class, () -> raster.pixelArgb(0, 128));
        }

        @Test
        void constructorRejectsNulls() {
            PlaceholderGenSpec spec = PlaceholderGenSpec.parse(JSON);
            assertThrows(IllegalArgumentException.class,
                    () -> new PlaceholderSheetRaster(null, "missing", List.of("a")));
            assertThrows(IllegalArgumentException.class,
                    () -> new PlaceholderSheetRaster(spec, " ", List.of("a")));
        }
    }
}
