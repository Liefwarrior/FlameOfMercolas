package com.trojia.client.face;

import com.trojia.client.face.FaceComposition.PlacedPart;
import com.trojia.client.sprite.SpriteIndex;
import com.trojia.client.sprite.SpriteRef;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Path;

/**
 * Headless raster compositor for tests (unified art spec §4.8 golden-test seam): decodes
 * the face-part sheet with ImageIO and alpha-overs a {@link FaceComposition}'s parts in
 * list order onto a 48&times;48 ARGB canvas — exactly the layering the GL batch performs,
 * minus the GPU. Alpha is binary in this pack (parts are opaque-or-transparent), so
 * "alpha-over" is replace-when-opaque.
 */
final class FaceRaster {

    private final BufferedImage sheet;
    private final int tilePx;

    FaceRaster(Path sheetPng, SpriteIndex index) {
        try {
            this.sheet = ImageIO.read(sheetPng.toFile());
        } catch (IOException e) {
            throw new UncheckedIOException("cannot decode " + sheetPng, e);
        }
        this.tilePx = index.tilePx();
    }

    /** Composes to a 48&times;48 TYPE_INT_ARGB image; transparent where no part painted. */
    BufferedImage compose(FaceComposition composition) {
        BufferedImage out = new BufferedImage(FaceComposition.CANVAS_PX,
                FaceComposition.CANVAS_PX, BufferedImage.TYPE_INT_ARGB);
        for (PlacedPart placed : composition.parts()) {
            SpriteRef part = placed.part();
            int srcX = part.col() * tilePx;
            int srcY = part.row() * tilePx;
            int wPx = part.cellsW() * tilePx;
            int hPx = part.cellsH() * tilePx;
            for (int y = 0; y < hPx; y++) {
                for (int x = 0; x < wPx; x++) {
                    int argb = sheet.getRGB(srcX + x, srcY + y);
                    if ((argb >>> 24) == 0) {
                        continue;
                    }
                    int dx = placed.x() + x;
                    int dy = placed.y() + y;
                    if (dx >= 0 && dx < out.getWidth() && dy >= 0 && dy < out.getHeight()) {
                        out.setRGB(dx, dy, argb);
                    }
                }
            }
        }
        return out;
    }
}
