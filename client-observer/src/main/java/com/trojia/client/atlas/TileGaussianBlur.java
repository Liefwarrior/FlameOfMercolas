package com.trojia.client.atlas;

import com.badlogic.gdx.graphics.Pixmap;

import java.util.LinkedHashSet;
import java.util.Set;

/**
 * A CPU-side separable gaussian blur used at atlas load to precompute the depth-of-field
 * "look-down through empty air" levels (the air-depth renderer, Eli 2026-07-15): an EMPTY
 * AIR cell at the camera's z shows the nearest drawn tile beneath it, blurred more the
 * further down that tile lives. Doing the blur once per pack at load — never per frame,
 * never in a shader — keeps the render loop a plain {@code batch.draw} of a pre-blurred
 * texture.
 *
 * <p><b>Per-tile isolation with edge clamp.</b> The sheet is a packed grid of independent
 * {@code tilePx}&times;{@code tilePx} cells with zero inter-cell spacing, so a naive
 * whole-sheet blur would bleed each tile's colour into its neighbours across the grid
 * lines. Every method here blurs <em>one cell's pixels in isolation</em>: the convolution
 * samples only within the cell and clamps at the cell edges (the sample coordinate is
 * pinned to {@code 0..w-1} / {@code 0..h-1}), so no colour crosses a cell boundary and a
 * flat cell stays exactly flat. {@link #blurSheet} applies this cell-by-cell over just the
 * cells a {@link SheetAtlasSpec} actually references.
 *
 * <p><b>GL-free core.</b> {@link #blurCellRgba} and {@link #gaussianKernel1D} are pure
 * integer/float math over {@code RGBA8888} pixel arrays (the layout
 * {@link Pixmap#getPixel} returns) with no libGDX graphics dependency, so they unit-test
 * headless. Only {@link #blurSheet} touches {@link Pixmap} and so needs the native image
 * codec (already required at boot to load the sheet).
 */
public final class TileGaussianBlur {

    private TileGaussianBlur() {
    }

    /**
     * Builds a normalized 1-D gaussian kernel for a standard deviation.
     *
     * <p>Radius is {@code ceil(3*sigma)} (the ~99.7% mass window) but at least 1, so the
     * kernel always has a real neighbourhood; weights are {@code exp(-x^2 / (2 sigma^2))}
     * normalized to sum 1. A non-positive sigma is rejected — callers use the sharp source
     * texture for level 0 rather than a zero-sigma "blur".
     *
     * @param sigma gaussian standard deviation, {@code > 0}
     * @return the kernel, length {@code 2*radius + 1}, centre at index {@code radius}
     * @throws IllegalArgumentException if {@code sigma <= 0}
     */
    public static float[] gaussianKernel1D(float sigma) {
        if (sigma <= 0f) {
            throw new IllegalArgumentException("sigma must be positive: " + sigma);
        }
        int radius = Math.max(1, (int) Math.ceil(3.0 * sigma));
        float[] kernel = new float[2 * radius + 1];
        float twoSigmaSq = 2f * sigma * sigma;
        float sum = 0f;
        for (int i = -radius; i <= radius; i++) {
            float w = (float) Math.exp(-(i * i) / twoSigmaSq);
            kernel[i + radius] = w;
            sum += w;
        }
        for (int i = 0; i < kernel.length; i++) {
            kernel[i] /= sum;
        }
        return kernel;
    }

    /**
     * Separable gaussian blur of one {@code w}&times;{@code h} cell of {@code RGBA8888}
     * pixels, in isolation with edge clamp. All four channels (including alpha) are blurred
     * so the depth haze softens both colour and the sprite's transparent silhouette. The
     * input is never mutated; a fresh array is returned.
     *
     * <p>Because the sample coordinate is clamped to the cell's own bounds, a uniform cell
     * is returned unchanged (weights sum to 1 over identical samples) — this is the property
     * that proves there is no cross-tile bleed.
     *
     * @param srcRgba row-major {@code RGBA8888} pixels, length {@code w*h}
     * @param w       cell width in pixels, {@code > 0}
     * @param h       cell height in pixels, {@code > 0}
     * @param sigma   gaussian standard deviation; {@code <= 0} returns a copy unchanged
     * @return a new blurred {@code RGBA8888} array, length {@code w*h}
     * @throws IllegalArgumentException if dimensions are non-positive or length mismatches
     */
    public static int[] blurCellRgba(int[] srcRgba, int w, int h, float sigma) {
        if (w <= 0 || h <= 0) {
            throw new IllegalArgumentException("cell " + w + "x" + h + " not positive");
        }
        if (srcRgba == null || srcRgba.length != w * h) {
            throw new IllegalArgumentException(
                    "srcRgba length " + (srcRgba == null ? -1 : srcRgba.length)
                            + " != " + w + "*" + h);
        }
        if (sigma <= 0f) {
            return srcRgba.clone();
        }
        float[] kernel = gaussianKernel1D(sigma);
        int radius = kernel.length / 2;
        // Horizontal pass: src -> tmp.
        int[] tmp = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float r = 0f, g = 0f, b = 0f, a = 0f;
                for (int t = -radius; t <= radius; t++) {
                    int sx = clamp(x + t, w);
                    int px = srcRgba[y * w + sx];
                    float wt = kernel[t + radius];
                    r += ((px >>> 24) & 0xFF) * wt;
                    g += ((px >>> 16) & 0xFF) * wt;
                    b += ((px >>> 8) & 0xFF) * wt;
                    a += (px & 0xFF) * wt;
                }
                tmp[y * w + x] = pack(r, g, b, a);
            }
        }
        // Vertical pass: tmp -> out.
        int[] out = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float r = 0f, g = 0f, b = 0f, a = 0f;
                for (int t = -radius; t <= radius; t++) {
                    int sy = clamp(y + t, h);
                    int px = tmp[sy * w + x];
                    float wt = kernel[t + radius];
                    r += ((px >>> 24) & 0xFF) * wt;
                    g += ((px >>> 16) & 0xFF) * wt;
                    b += ((px >>> 8) & 0xFF) * wt;
                    a += (px & 0xFF) * wt;
                }
                out[y * w + x] = pack(r, g, b, a);
            }
        }
        return out;
    }

    /**
     * Returns a new {@link Pixmap} that is {@code src} with each cell {@code spec}
     * references blurred in isolation at {@code sigma}. Cells the mapping never uses are
     * copied through untouched (the whole source is copied first, then referenced cells are
     * overwritten with their blurred selves), so the result stays a valid full-sheet image.
     * A referenced cell shared by several regions/variants is blurred once.
     *
     * <p>Caller owns disposal of the returned pixmap; {@code src} is left untouched.
     *
     * @param src   the loaded sheet pixmap (must be {@code RGBA8888})
     * @param spec  the layout whose referenced cells get blurred
     * @param sigma gaussian standard deviation, {@code > 0}
     * @return a new blurred pixmap the caller disposes
     */
    public static Pixmap blurSheet(Pixmap src, SheetAtlasSpec spec, float sigma) {
        Pixmap dst = new Pixmap(src.getWidth(), src.getHeight(), Pixmap.Format.RGBA8888);
        dst.setBlending(Pixmap.Blending.None);
        dst.drawPixmap(src, 0, 0);
        Set<AtlasCellRect> unique = new LinkedHashSet<>();
        for (String name : spec.regionNames()) {
            unique.addAll(spec.cellRects(name));
        }
        for (AtlasCellRect rect : unique) {
            blurCellInto(src, dst, rect, sigma);
        }
        return dst;
    }

    /** Reads one cell out of {@code src}, blurs it, writes it back into {@code dst}. */
    private static void blurCellInto(Pixmap src, Pixmap dst, AtlasCellRect rect, float sigma) {
        int w = rect.width();
        int h = rect.height();
        int[] cell = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                cell[y * w + x] = src.getPixel(rect.x() + x, rect.y() + y);
            }
        }
        int[] blurred = blurCellRgba(cell, w, h, sigma);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                dst.drawPixel(rect.x() + x, rect.y() + y, blurred[y * w + x]);
            }
        }
    }

    /** Clamps a sample coordinate to {@code 0..size-1} (edge-clamp isolation). */
    private static int clamp(int i, int size) {
        if (i < 0) {
            return 0;
        }
        return i >= size ? size - 1 : i;
    }

    /** Rounds/clamps four float channels back into a packed {@code RGBA8888} int. */
    private static int pack(float r, float g, float b, float a) {
        return (channel(r) << 24) | (channel(g) << 16) | (channel(b) << 8) | channel(a);
    }

    private static int channel(float v) {
        int i = Math.round(v);
        if (i < 0) {
            return 0;
        }
        return i > 255 ? 255 : i;
    }
}
