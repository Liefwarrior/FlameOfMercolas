package com.trojia.client.atlas;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for the GL-free core of {@link TileGaussianBlur} — the per-tile,
 * edge-clamped separable gaussian used to precompute the air-depth "look-down" blur pyramid
 * (Eli 2026-07-15). These hit only the {@code int[] RGBA8888} math, never a {@link
 * com.badlogic.gdx.graphics.Pixmap} or GL, so they run with no display.
 *
 * <p>The two load-bearing properties: (a) a flat cell survives the blur byte-for-byte, which
 * proves the convolution clamps at the cell edge and never pulls in a neighbour's colour (no
 * cross-tile bleed); (b) a high-contrast cell's per-channel variance falls monotonically as
 * the blur sigma rises, which proves each deeper pyramid level really is softer.
 */
class TileGaussianBlurTest {

    private static final int W = 16;
    private static final int H = 16;

    /** RGBA8888 pack, matching Pixmap.getPixel's channel order. */
    private static int rgba(int r, int g, int b, int a) {
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    @Test
    void kernelIsNormalizedAndCentred() {
        float[] k = TileGaussianBlur.gaussianKernel1D(1.5f);
        float sum = 0f;
        for (float w : k) {
            sum += w;
        }
        assertEquals(1f, sum, 1e-5f, "weights must sum to 1");
        int mid = k.length / 2;
        assertTrue(k[mid] > k[mid - 1], "centre weight is the largest");
        assertTrue(k[mid - 1] > k[0], "weights fall off toward the edge");
    }

    @Test
    void kernelRejectsNonPositiveSigma() {
        assertThrows(IllegalArgumentException.class, () -> TileGaussianBlur.gaussianKernel1D(0f));
        assertThrows(IllegalArgumentException.class, () -> TileGaussianBlur.gaussianKernel1D(-1f));
    }

    @Test
    void flatCellSurvivesBlurUnchanged() {
        // A uniform cell must come out identical: with edge clamp every sample inside the
        // convolution equals the cell colour, so the weighted sum is that colour exactly.
        // This is the no-cross-tile-bleed guarantee — nothing outside the cell can leak in.
        int[] flat = new int[W * H];
        int color = rgba(0x2A, 0xBB, 0xCC, 0xFF);
        java.util.Arrays.fill(flat, color);
        for (float sigma : new float[] {0.6f, 1.1f, 1.7f, 2.4f, 4.0f}) {
            int[] out = TileGaussianBlur.blurCellRgba(flat, W, H, sigma);
            assertArrayEquals(flat, out, "flat cell changed at sigma " + sigma);
        }
    }

    @Test
    void nonPositiveSigmaReturnsAnUnchangedCopy() {
        int[] src = highContrastRedStep();
        int[] out = TileGaussianBlur.blurCellRgba(src, W, H, 0f);
        assertArrayEquals(src, out);
        assertTrue(out != src, "must be a copy, not the same array");
    }

    @Test
    void varianceFallsMonotonicallyWithSigma() {
        // A hard left/right step in the red channel is maximally high-frequency; each larger
        // sigma low-passes it harder, so the red-channel variance must strictly decrease. The
        // sharp (sigma 0) copy is the high-variance baseline the blur levels sit below.
        int[] step = highContrastRedStep();
        double prev = redVariance(step);
        for (float sigma : SheetTileAtlas.SIGMAS) {
            if (sigma <= 0f) {
                continue; // level 0 is the sharp source, not a blur
            }
            double v = redVariance(TileGaussianBlur.blurCellRgba(step, W, H, sigma));
            assertTrue(v < prev,
                    "variance did not decrease at sigma " + sigma + " (" + v + " !< " + prev + ")");
            prev = v;
        }
    }

    @Test
    void rejectsBadDimensionsAndLength() {
        assertThrows(IllegalArgumentException.class,
                () -> TileGaussianBlur.blurCellRgba(new int[4], 0, 2, 1f));
        assertThrows(IllegalArgumentException.class,
                () -> TileGaussianBlur.blurCellRgba(new int[3], 2, 2, 1f));
        assertThrows(IllegalArgumentException.class,
                () -> TileGaussianBlur.blurCellRgba(null, 2, 2, 1f));
    }

    // ------------------------------------------------------------------ helpers

    /** Left half red 0, right half red 255 (a hard vertical step); g/b 0, a 255. */
    private static int[] highContrastRedStep() {
        int[] cell = new int[W * H];
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int red = x < W / 2 ? 0 : 255;
                cell[y * W + x] = rgba(red, 0, 0, 255);
            }
        }
        return cell;
    }

    private static double redVariance(int[] cell) {
        double mean = 0;
        for (int px : cell) {
            mean += (px >>> 24) & 0xFF;
        }
        mean /= cell.length;
        double var = 0;
        for (int px : cell) {
            double d = ((px >>> 24) & 0xFF) - mean;
            var += d * d;
        }
        return var / cell.length;
    }
}
