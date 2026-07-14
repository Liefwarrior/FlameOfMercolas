package com.trojia.client.face;

import org.junit.jupiter.api.Test;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The golden face sheet (FACES-SPEC §7 T16, re-targeted): a fixed
 * {@code (worldSeed, actorId)} set across all six archetypes plus the two named
 * portraits, headless-rastered into one grid and compared pixel-exact against
 * {@code content/art/faces/golden-faces.png} (a committed review artifact — open it to
 * SEE what the generator ships). Bless a deliberate change with
 * {@code -Dfacegen.bless=true} (Gradle: {@code -Dfacegen.bless=true} via
 * {@code test.systemProperty}or {@code gradlew :client-observer:test -Dfacegen.bless=true}
 * with forwarding); never bless blind.
 */
class FaceGoldenTest {

    private static final long WORLD_SEED = 0x5EEDF00DL;
    private static final int COLS = 7;
    private static final int ROWS = 2;
    private static final int CELL = FaceComposition.CANVAS_PX;

    @Test
    void goldenFaceSheet_pixelExact() throws IOException {
        FaceGen gen = ShippedFaceContent.gen();
        FaceRaster raster = new FaceRaster(
                ShippedFaceContent.dir().resolve("face-parts.png"), ShippedFaceContent.index());
        List<String> archetypes = List.of("cultist", "guard", "laborer", "monk", "noble", "thug");

        BufferedImage sheet = new BufferedImage(COLS * CELL, ROWS * CELL,
                BufferedImage.TYPE_INT_ARGB);
        for (int i = 0; i < 12; i++) {
            long actorId = 1000 + i * 37L;   // the Python sampler's id walk
            BufferedImage face = raster.compose(
                    gen.compose(WORLD_SEED, actorId, archetypes.get(i % archetypes.size())));
            blit(face, sheet, (i % 6) * CELL, (i / 6) * CELL);
        }
        blit(raster.compose(gen.composeNamed("devin")), sheet, 6 * CELL, 0);
        blit(raster.compose(gen.composeNamed("john")), sheet, 6 * CELL, CELL);

        Path golden = ShippedFaceContent.dir().resolve("golden-faces.png");
        if (Boolean.getBoolean("facegen.bless")) {
            ImageIO.write(sheet, "png", golden.toFile());
            return;
        }
        assertTrue(Files.exists(golden),
                "no golden face sheet committed — run once with -Dfacegen.bless=true");
        BufferedImage expected = ImageIO.read(golden.toFile());
        assertEquals(expected.getWidth(), sheet.getWidth(), "golden width");
        assertEquals(expected.getHeight(), sheet.getHeight(), "golden height");
        for (int y = 0; y < sheet.getHeight(); y++) {
            for (int x = 0; x < sheet.getWidth(); x++) {
                int got = sheet.getRGB(x, y);
                int want = expected.getRGB(x, y);
                // Normalize fully-transparent pixels (color bits under alpha 0 are noise).
                if ((got >>> 24) == 0 && (want >>> 24) == 0) {
                    continue;
                }
                assertEquals(want, got, "pixel (" + x + "," + y + ") drifted from the"
                        + " blessed golden face sheet — if deliberate, rebless with"
                        + " -Dfacegen.bless=true and review the diff");
            }
        }
    }

    private static void blit(BufferedImage src, BufferedImage dst, int ox, int oy) {
        for (int y = 0; y < src.getHeight(); y++) {
            for (int x = 0; x < src.getWidth(); x++) {
                dst.setRGB(ox + x, oy + y, src.getRGB(x, y));
            }
        }
    }
}
