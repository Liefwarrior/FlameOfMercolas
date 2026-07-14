package com.trojia.client.face;

import com.trojia.client.sprite.SpriteIndex;
import com.trojia.client.sprite.SpriteRef;
import org.junit.jupiter.api.Test;

import java.awt.image.BufferedImage;
import java.util.List;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Raster-level contracts of the shipped face-part sheet (FACES-SPEC §7 T4/T5, re-targeted
 * per the unified art spec §4.9): every composition rasterizes to exactly 48&times;48, and
 * every opaque pixel — sheet-wide, not just composed — is one of the 24 MERCOLAS-24 colors
 * (spec §1.2). The face pack has no off-palette exception (the {@code missing} checker
 * lives in the tile pack only).
 */
class FaceRasterTest {

    /** MERCOLAS-24, RGB (unified art spec §1.2, hex-pinned). */
    private static final Set<Integer> MERCOLAS_24 = Set.of(
            0x0D0B10, 0x2B2A31, 0x3F3E47, 0x57565F, 0x75747C, 0xC9C2B0, 0xE4DCC6,
            0x16211A, 0x26382C, 0x3C523E, 0x5A6E4C, 0x221B14, 0x382C1F, 0x533F2B,
            0x70573A, 0x8E7452, 0x491722, 0x7A1F26, 0xA72C2A, 0x18242F, 0x2B4257,
            0x46708A, 0x83A7B4, 0xB98F42);

    @Test
    void everyOpaqueSheetPixel_isOnPalette_andPartsStayInTheirCells() {
        SpriteIndex index = ShippedFaceContent.index();
        FaceRaster raster = new FaceRaster(
                ShippedFaceContent.dir().resolve("face-parts.png"), index);
        // Palette census over full compositions (which also exercises every layer path).
        FaceGen gen = ShippedFaceContent.gen();
        List<String> archetypes = ShippedFaceContent.archetypes().archetypeIds()
                .stream().sorted().toList();
        for (long actorId = 0; actorId < 200; actorId++) {
            String archetype = archetypes.get((int) (actorId % archetypes.size()));
            BufferedImage face = raster.compose(gen.compose(0x5EEDF00DL, actorId, archetype));
            assertEquals(FaceComposition.CANVAS_PX, face.getWidth());
            assertEquals(FaceComposition.CANVAS_PX, face.getHeight());
            assertOnPalette(face, "actor " + actorId + " " + archetype);
        }
        // Both named portraits pass the same census.
        assertOnPalette(raster.compose(gen.composeNamed("devin")), "devin");
        assertOnPalette(raster.compose(gen.composeNamed("john")), "john");
    }

    private static void assertOnPalette(BufferedImage img, String what) {
        for (int y = 0; y < img.getHeight(); y++) {
            for (int x = 0; x < img.getWidth(); x++) {
                int argb = img.getRGB(x, y);
                if ((argb >>> 24) == 0) {
                    continue;
                }
                assertEquals(0xFF, argb >>> 24, what + ": partial alpha at " + x + "," + y);
                assertTrue(MERCOLAS_24.contains(argb & 0xFFFFFF),
                        what + ": off-palette pixel #" + Integer.toHexString(argb & 0xFFFFFF)
                                + " at " + x + "," + y);
            }
        }
    }

    @Test
    void everyPart_hasAtLeastOneOpaquePixel_insideItsCells() {
        SpriteIndex index = ShippedFaceContent.index();
        FaceRaster raster = new FaceRaster(
                ShippedFaceContent.dir().resolve("face-parts.png"), index);
        for (SpriteRef part : index.all()) {
            FaceComposition single = new FaceComposition(
                    List.of(new FaceComposition.PlacedPart(part, 0, 0)));
            BufferedImage img = raster.compose(single);
            boolean any = false;
            for (int y = 0; y < img.getHeight() && !any; y++) {
                for (int x = 0; x < img.getWidth() && !any; x++) {
                    any = (img.getRGB(x, y) >>> 24) != 0;
                }
            }
            assertTrue(any, part.id() + ": fully transparent part");
        }
    }
}
