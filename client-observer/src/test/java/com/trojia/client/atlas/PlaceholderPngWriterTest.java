package com.trojia.client.atlas;

import org.junit.jupiter.api.Test;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.IOException;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * Headless tests for {@link PlaceholderPngWriter}: valid decodable PNG output,
 * byte-determinism, and pixel round-trip fidelity (verified through the JDK's own
 * ImageIO decoder, an independent implementation).
 */
class PlaceholderPngWriterTest {

    private static final int[] SAMPLE = {
            0xFFFF0000, 0xFF00FF00, 0xFF0000FF,
            0x00000000, 0xFF9FB8D8, 0xFF000000,
    };

    @Test
    void outputStartsWithThePngSignature() {
        byte[] png = PlaceholderPngWriter.encodeArgb(SAMPLE, 3, 2);
        byte[] signature = {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        for (int i = 0; i < signature.length; i++) {
            assertEquals(signature[i], png[i], "signature byte " + i);
        }
    }

    @Test
    void encodingIsByteDeterministic() {
        assertArrayEquals(PlaceholderPngWriter.encodeArgb(SAMPLE, 3, 2),
                PlaceholderPngWriter.encodeArgb(SAMPLE, 3, 2));
    }

    @Test
    void roundTripsPixelsThroughAnIndependentDecoder() throws IOException {
        byte[] png = PlaceholderPngWriter.encodeArgb(SAMPLE, 3, 2);
        BufferedImage image = ImageIO.read(new ByteArrayInputStream(png));
        assertNotNull(image, "ImageIO must recognize the stream as a PNG");
        assertEquals(3, image.getWidth());
        assertEquals(2, image.getHeight());
        for (int y = 0; y < 2; y++) {
            for (int x = 0; x < 3; x++) {
                int expected = SAMPLE[y * 3 + x];
                int actual = image.getRGB(x, y);
                if ((expected >>> 24) == 0) {
                    assertEquals(0, actual >>> 24, "(" + x + "," + y + ") alpha");
                } else {
                    assertEquals(expected, actual, "(" + x + "," + y + ")");
                }
            }
        }
    }

    @Test
    void roundTripsAFullPlaceholderSheet() throws IOException {
        PlaceholderSheetRaster raster =
                PlaceholderAtlasFactory.buildRaster(ShippedArtMapping.json());
        byte[] png = PlaceholderPngWriter.encodeArgb(
                raster.pixelsArgb(), raster.atlasSizePx(), raster.atlasSizePx());
        BufferedImage image = ImageIO.read(new ByteArrayInputStream(png));
        assertNotNull(image);
        assertEquals(128, image.getWidth());
        assertEquals(128, image.getHeight());
        // Spot-check an opaque texel: the missing checker's magenta corner.
        AtlasCellRect missing = raster.regionTable().cellRect("missing");
        assertEquals(0xFFFF00FF, image.getRGB(missing.x(), missing.y()));
    }

    @Test
    void rejectsInvalidArguments() {
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderPngWriter.encodeArgb(null, 1, 1));
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderPngWriter.encodeArgb(new int[1], 0, 1));
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderPngWriter.encodeArgb(new int[1], 1, 0));
        assertThrows(IllegalArgumentException.class,
                () -> PlaceholderPngWriter.encodeArgb(new int[5], 3, 2));
    }
}
