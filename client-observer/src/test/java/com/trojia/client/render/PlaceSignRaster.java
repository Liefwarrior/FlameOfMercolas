package com.trojia.client.render;

import com.trojia.client.atlas.Glyph8x8Font;

import java.util.List;

/**
 * Headless GL-free compositor for the building labels (the {@code FaceRaster} pattern, applied
 * to a quad list instead of a sprite sheet): paints {@link PlaceSignArt}'s quads onto an ARGB
 * canvas in exactly the order and with exactly the colours the {@code SpriteBatch} would, and
 * stamps text with the repo's built-in {@link Glyph8x8Font}.
 *
 * <p>This is the seam that makes the LOOK testable, not just the logic: the GL renderer and this
 * raster consume the same {@link PlaceSignArt} output, so a pixel assertion here is an assertion
 * about the shipped box. The only deliberate divergence is the FONT — the live HUD draws with
 * libGDX's default {@code BitmapFont}, which needs a GL context; the 8&times;8 built-in stands in
 * for it here, which proves the box's geometry and ink but not the exact glyph shapes.
 *
 * <p>Canvas origin is top-left (PNG convention); quads arrive bottom-left/y-up (batch
 * convention), so every paint flips through the canvas height — the same flip
 * {@code PlaceSignRenderer} performs against the viewport.
 */
final class PlaceSignRaster {

    /** The world behind the label: a mid-grey field, so a black box is unmistakably a hole. */
    static final int BACKDROP = 0xFF3A4048;

    private final int[] pixels;
    private final int width;
    private final int height;

    PlaceSignRaster(int width, int height) {
        this.width = width;
        this.height = height;
        this.pixels = new int[width * height];
        java.util.Arrays.fill(pixels, BACKDROP);
    }

    int width() {
        return width;
    }

    int height() {
        return height;
    }

    int[] pixelsArgb() {
        return pixels.clone();
    }

    /** ARGB at a canvas pixel (top-left origin). */
    int at(int x, int y) {
        return pixels[y * width + x];
    }

    /** Sets a canvas pixel directly (top-left origin) — the backdrop painter's own seam. */
    void setAt(int x, int y, int argb) {
        pixels[y * width + x] = argb;
    }

    /** ARGB at a batch-space pixel (bottom-left origin, y-up). */
    int atBatch(int x, float batchY) {
        return at(x, height - 1 - Math.round(batchY));
    }

    /** Paints one quad list in order, opaque, no blending — exactly what the batch does. */
    void paint(List<PlaceSignArt.Quad> quads) {
        for (PlaceSignArt.Quad quad : quads) {
            int argb = 0xFF000000
                    | (channel(quad.r()) << 16) | (channel(quad.g()) << 8) | channel(quad.b());
            int x0 = Math.round(quad.x());
            int y0 = Math.round(quad.y());
            int x1 = Math.round(quad.x() + quad.w());
            int y1 = Math.round(quad.y() + quad.h());
            for (int by = y0; by < y1; by++) {
                int cy = height - 1 - by;   // flip to canvas space
                if (cy < 0 || cy >= height) {
                    continue;
                }
                for (int bx = x0; bx < x1; bx++) {
                    if (bx >= 0 && bx < width) {
                        pixels[cy * width + bx] = argb;
                    }
                }
            }
        }
    }

    /**
     * Stamps a line of 8&times;8 glyphs with its TOP-left at batch-space {@code (x, topY)} —
     * the same anchoring {@code BitmapFont.draw} uses, so the raster's text sits where the
     * renderer's would.
     */
    void text(String line, float x, float topY, int argb) {
        for (int i = 0; i < line.length(); i++) {
            char glyph = line.charAt(i);
            for (int gy = 0; gy < Glyph8x8Font.HEIGHT; gy++) {
                for (int gx = 0; gx < Glyph8x8Font.WIDTH; gx++) {
                    if (!Glyph8x8Font.isInk(glyph, gx, gy)) {
                        continue;
                    }
                    int cx = Math.round(x) + i * Glyph8x8Font.WIDTH + gx;
                    int cy = height - 1 - Math.round(topY) + gy;
                    if (cx >= 0 && cx < width && cy >= 0 && cy < height) {
                        pixels[cy * width + cx] = argb;
                    }
                }
            }
        }
    }

    /** The pixel width a line of the built-in font occupies. */
    static float textWidth(String line) {
        return line.length() * (float) Glyph8x8Font.WIDTH;
    }

    private static int channel(float value) {
        int scaled = Math.round(value * 255f);
        return Math.max(0, Math.min(255, scaled));
    }
}
