package com.trojia.client.atlas;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link PlaceholderAtlasDump}: the optional debug dump writes
 * both files, byte-identically on every run (TILE-ART-SPEC section 3).
 */
class PlaceholderAtlasDumpTest {

    @Test
    void writesPngAndAtlasTextByteDeterministically(@TempDir Path tempDir) throws IOException {
        PlaceholderSheetRaster raster =
                PlaceholderAtlasFactory.buildRaster(ShippedArtMapping.json());

        Path first = PlaceholderAtlasDump.dump(raster, tempDir.resolve("one"));
        Path second = PlaceholderAtlasDump.dump(
                PlaceholderAtlasFactory.buildRaster(ShippedArtMapping.json()),
                tempDir.resolve("two"));

        byte[] png = Files.readAllBytes(first.resolve(PlaceholderAtlasDump.PNG_FILE_NAME));
        assertArrayEquals(png,
                Files.readAllBytes(second.resolve(PlaceholderAtlasDump.PNG_FILE_NAME)),
                "png must be byte-identical across runs");
        assertArrayEquals(png, PlaceholderPngWriter.encodeArgb(
                raster.pixelsArgb(), raster.atlasSizePx(), raster.atlasSizePx()));

        String atlas = Files.readString(
                first.resolve(PlaceholderAtlasDump.ATLAS_FILE_NAME), StandardCharsets.UTF_8);
        assertEquals(raster.atlasText(PlaceholderAtlasDump.PNG_FILE_NAME), atlas);
        assertEquals(atlas, Files.readString(
                second.resolve(PlaceholderAtlasDump.ATLAS_FILE_NAME), StandardCharsets.UTF_8));
        assertTrue(atlas.startsWith(PlaceholderAtlasDump.PNG_FILE_NAME + "\n"),
                "atlas text must reference the dumped page image");
    }

    @Test
    void overwritesPreviousDumps(@TempDir Path tempDir) throws IOException {
        PlaceholderSheetRaster raster =
                PlaceholderAtlasFactory.buildRaster(ShippedArtMapping.json());
        PlaceholderAtlasDump.dump(raster, tempDir);
        PlaceholderAtlasDump.dump(raster, tempDir); // must not throw
        assertTrue(Files.isRegularFile(tempDir.resolve(PlaceholderAtlasDump.PNG_FILE_NAME)));
    }

    @Test
    void rejectsNullArguments(@TempDir Path tempDir) {
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderAtlasDump.dump(null, tempDir));
        assertThrows(IllegalArgumentException.class, () -> PlaceholderAtlasDump.dump(
                PlaceholderAtlasFactory.buildRaster(ShippedArtMapping.json()), null));
    }
}
