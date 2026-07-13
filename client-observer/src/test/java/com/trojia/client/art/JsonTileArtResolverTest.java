package com.trojia.client.art;

import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link JsonTileArtResolver}: the shipped
 * {@code content/art/placeholder/art-mapping.json} must load and resolve per
 * TILE-ART-SPEC sections 2/3/7, and schema violations must aggregate into one
 * {@link ArtMappingException}. No GL context — libGDX {@code JsonReader} is pure.
 */
class JsonTileArtResolverTest {

    /** Locates the real mapping by walking up from the test working directory. */
    private static Path shippedMappingPath() {
        Path dir = Paths.get(System.getProperty("user.dir")).toAbsolutePath();
        for (int i = 0; i < 5 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve(Paths.get("content", "art", "placeholder", "art-mapping.json"));
            if (Files.isRegularFile(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException(
                "art-mapping.json not found above " + System.getProperty("user.dir"));
    }

    private static JsonTileArtResolver shipped() {
        try {
            return JsonTileArtResolver.parse(
                    Files.readString(shippedMappingPath(), StandardCharsets.UTF_8));
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }

    @Nested
    class ShippedMapping {

        private final JsonTileArtResolver resolver = shipped();

        @Test
        void resolvesDefaultFormAndBucketZeroToBareId() {
            assertEquals("granite", resolver.regionName("granite", "block", 0));
            assertEquals("oak", resolver.regionName("oak", "block", 0));
        }

        @Test
        void resolvesFloorForm() {
            assertEquals("granite.floor", resolver.regionName("granite", "floor", 0));
        }

        @Test
        void formTokenIsCaseInsensitiveSoTileFormNamesWork() {
            assertEquals("granite", resolver.regionName("granite", "BLOCK", 0));
            assertEquals("granite.floor", resolver.regionName("granite", "FLOOR", 0));
        }

        @Test
        void chromatisAppearanceBucketsFollowTheGrammar() {
            assertEquals("chromatis", resolver.regionName("chromatis", "block", 0));
            assertEquals("chromatis.a1", resolver.regionName("chromatis", "block", 1));
            assertEquals("chromatis.a2", resolver.regionName("chromatis", "block", 2));
            assertEquals("chromatis.floor.a1", resolver.regionName("chromatis", "floor", 1));
        }

        @Test
        void bucketsClampHighNeverWrap() {
            // chromatis defines 3 regions: bucket 3 (and anything above) saturates at a2.
            assertEquals("chromatis.a2", resolver.regionName("chromatis", "block", 3));
            assertEquals("chromatis.a2", resolver.regionName("chromatis", "block", 99));
            // lightstone defines a single region: any bucket stays on it.
            assertEquals("lightstone", resolver.regionName("lightstone", "block", 2));
        }

        @Test
        void unknownFormFallsBackToBlockKeepingTheBucket() {
            assertEquals("chromatis.a1", resolver.regionName("chromatis", "ramp", 1));
        }

        @Test
        void unknownMaterialResolvesToMissing() {
            assertEquals("missing", resolver.regionName("uether_crystal", "block", 0));
            assertEquals("missing", resolver.missingRegionName());
        }

        @Test
        void treatmentDerivedIdResolvesVerbatim() {
            assertEquals("trudgeon_wood@getilia_soak.floor",
                    resolver.regionName("trudgeon_wood@getilia_soak", "floor", 0));
        }

        @Test
        void minLightIsGlowstonesCosmeticClampOnly() {
            assertEquals(8, resolver.minLight("glowstone"));
            assertEquals(0, resolver.minLight("granite"));
            assertEquals(0, resolver.minLight("not_a_material"));
        }

        @Test
        void chromatisExposesTheCanonHeatGlowTint() {
            // BLESSING-QUEUE ruling 5: #E8842A is the discharge/saturation overlay.
            assertEquals(0xE8842A, resolver.heatGlowTintRgb("chromatis"));
            assertEquals(TileArtResolver.NO_TINT, resolver.heatGlowTintRgb("granite"));
            assertEquals(TileArtResolver.NO_TINT, resolver.heatGlowTintRgb("not_a_material"));
        }

        @Test
        void headerFieldsMatchTheSpec() {
            assertEquals("art/placeholder/placeholder.atlas", resolver.atlasPath());
            assertEquals(16, resolver.tilePx());
            assertEquals(0x0E0E12, resolver.voidColorRgb());
        }

        @Test
        void lightTintTableLoadsTheSpecCurve() {
            LightTintTable tint = resolver.lightTintTable();
            assertEquals(36, tint.tintQ8(0));
            assertEquals(87, tint.tintQ8(15));
            assertEquals(256, tint.tintQ8(31));
        }

        @Test
        void zPeekDimTableLoadsTheBlessedDefaults() {
            ZPeekDimTable dim = resolver.zPeekDimTable();
            assertEquals(3, dim.maxPeekDepth());
            assertEquals(256, dim.dimQ8(0));
            assertEquals(168, dim.dimQ8(1));
            assertEquals(112, dim.dimQ8(2));
            assertEquals(76, dim.dimQ8(3));
        }

        @Test
        void waterOverlayLoads() {
            assertEquals("water", resolver.fluidRegion("water"));
            assertEquals(0, resolver.fluidDepthAlphaQ8("water", 0));
            assertEquals(96, resolver.fluidDepthAlphaQ8("water", 1));
            assertEquals(240, resolver.fluidDepthAlphaQ8("water", 7));
        }

        @Test
        void unknownFluidRendersNothing() {
            assertEquals("missing", resolver.fluidRegion("magma"));
            assertEquals(0, resolver.fluidDepthAlphaQ8("magma", 7));
        }

        @Test
        void referencedRegionNamesCoverTheFullPlaceholderInventory() {
            // TILE-ART-SPEC section 6.2: 16 materials x {block, floor} + 2 extra
            // chromatis buckets x 2 forms + water + missing = 38 regions.
            assertEquals(38, resolver.referencedRegionNames().size());
            assertTrue(resolver.referencedRegionNames().contains("missing"));
            assertTrue(resolver.referencedRegionNames().contains("chromatis.floor.a2"));
            assertTrue(resolver.referencedRegionNames().contains("water"));
        }

        @Test
        void argumentContractsAreEnforced() {
            assertThrows(IllegalArgumentException.class,
                    () -> resolver.regionName(null, "block", 0));
            assertThrows(IllegalArgumentException.class,
                    () -> resolver.regionName("granite", " ", 0));
            assertThrows(IllegalArgumentException.class,
                    () -> resolver.regionName("granite", "block", -1));
            assertThrows(IllegalArgumentException.class, () -> resolver.minLight(""));
            assertThrows(IllegalArgumentException.class, () -> resolver.heatGlowTintRgb(null));
            assertThrows(IllegalArgumentException.class,
                    () -> resolver.fluidDepthAlphaQ8("water", 8));
            assertThrows(IllegalArgumentException.class,
                    () -> resolver.fluidDepthAlphaQ8("water", -1));
        }
    }

    @Nested
    class Validation {

        /** Minimal valid document the failure cases mutate away from. */
        private String valid() {
            StringBuilder tint = new StringBuilder();
            for (int level = 0; level < 32; level++) {
                if (level > 0) {
                    tint.append(',');
                }
                tint.append(36 + 220 * level * level / 961);
            }
            return """
                    {
                      "schemaVersion": 1,
                      "atlas": "art/placeholder/placeholder.atlas",
                      "tilePx": 16,
                      "missingRegion": "missing",
                      "voidColor": "#0E0E12",
                      "lightTintQ8": [%s],
                      "zPeekDimQ8": [256, 168, 112, 76],
                      "materials": {
                        "granite": { "forms": { "block": { "byAppearance": ["granite"] } } }
                      }
                    }
                    """.formatted(tint);
        }

        @Test
        void minimalDocumentParses() {
            JsonTileArtResolver resolver = JsonTileArtResolver.parse(valid());
            assertEquals("granite", resolver.regionName("granite", "block", 0));
        }

        @Test
        void malformedJsonIsWrapped() {
            assertThrows(ArtMappingException.class, () -> JsonTileArtResolver.parse("{ nope ["));
        }

        @Test
        void wrongSchemaVersionFails() {
            String json = valid().replace("\"schemaVersion\": 1", "\"schemaVersion\": 2");
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> JsonTileArtResolver.parse(json));
            assertTrue(e.getMessage().contains("schemaVersion"));
        }

        @Test
        void wrongTilePxFails() {
            String json = valid().replace("\"tilePx\": 16", "\"tilePx\": 8");
            assertThrows(ArtMappingException.class, () -> JsonTileArtResolver.parse(json));
        }

        @Test
        void errorsAggregateOnePerLine() {
            String json = valid()
                    .replace("\"schemaVersion\": 1", "\"schemaVersion\": 2")
                    .replace("\"tilePx\": 16", "\"tilePx\": 8")
                    .replace("\"missingRegion\": \"missing\",", "");
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> JsonTileArtResolver.parse(json));
            assertTrue(e.getMessage().contains("schemaVersion"));
            assertTrue(e.getMessage().contains("tilePx"));
            assertTrue(e.getMessage().contains("missingRegion"));
            assertTrue(e.getMessage().lines().count() >= 3);
        }

        @Test
        void byAppearanceLongerThanFourBucketsFails() {
            String json = valid().replace("[\"granite\"]",
                    "[\"a\", \"b\", \"c\", \"d\", \"e\"]");
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> JsonTileArtResolver.parse(json));
            assertTrue(e.getMessage().contains("byAppearance"));
        }

        @Test
        void emptyByAppearanceFails() {
            String json = valid().replace("[\"granite\"]", "[]");
            assertThrows(ArtMappingException.class, () -> JsonTileArtResolver.parse(json));
        }

        @Test
        void blankRegionNameFails() {
            String json = valid().replace("[\"granite\"]", "[\"  \"]");
            assertThrows(ArtMappingException.class, () -> JsonTileArtResolver.parse(json));
        }

        @Test
        void materialWithoutFormsFails() {
            String json = valid().replace(
                    "{ \"forms\": { \"block\": { \"byAppearance\": [\"granite\"] } } }",
                    "{ \"minLight\": 3 }");
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> JsonTileArtResolver.parse(json));
            assertTrue(e.getMessage().contains("forms"));
        }

        @Test
        void minLightOutOfRangeFails() {
            String json = valid().replace(
                    "\"forms\": { \"block\": { \"byAppearance\": [\"granite\"] } }",
                    "\"minLight\": 32, \"forms\": { \"block\": { \"byAppearance\": [\"granite\"] } }");
            assertThrows(ArtMappingException.class, () -> JsonTileArtResolver.parse(json));
        }

        @Test
        void badHeatGlowTintFormatFails() {
            String json = valid().replace(
                    "\"forms\": { \"block\": { \"byAppearance\": [\"granite\"] } }",
                    "\"heatGlowTint\": \"E8842A\", \"forms\": { \"block\": { \"byAppearance\": [\"granite\"] } }");
            assertThrows(ArtMappingException.class, () -> JsonTileArtResolver.parse(json));
        }

        @Test
        void lightTintCurveShapeIsEnforced() {
            String json = valid().replaceFirst("\"lightTintQ8\": \\[[^\\]]*\\]",
                    "\"lightTintQ8\": [0, 256]");
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> JsonTileArtResolver.parse(json));
            assertTrue(e.getMessage().contains("lightTintQ8"));
        }

        @Test
        void zPeekCurveMustStartAtUnit() {
            String json = valid().replace("[256, 168, 112, 76]", "[255, 168, 112, 76]");
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> JsonTileArtResolver.parse(json));
            assertTrue(e.getMessage().contains("zPeekDimQ8"));
        }

        @Test
        void fluidDepthAlphaShapeIsEnforced() {
            String json = valid().replace(
                    "\"materials\": {",
                    "\"fluids\": { \"water\": { \"region\": \"water\", \"depthAlphaQ8\": [10, 96, 120, 144, 168, 192, 216, 240] } },\n\"materials\": {");
            ArtMappingException e = assertThrows(ArtMappingException.class,
                    () -> JsonTileArtResolver.parse(json));
            assertTrue(e.getMessage().contains("depthAlphaQ8"));
        }

        @Test
        void unknownFieldsAndPlaceholderGenAreIgnored() {
            String json = valid().replace(
                    "\"materials\": {",
                    "\"placeholderGen\": { \"anything\": [1, 2, 3] },\n\"futureField\": true,\n\"materials\": {");
            JsonTileArtResolver resolver = JsonTileArtResolver.parse(json);
            assertEquals("granite", resolver.regionName("granite", "block", 0));
        }
    }
}
