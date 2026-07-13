package com.trojia.client.art;

import java.util.Arrays;

/**
 * The z-peek dimming curve of TILE-ART-SPEC section 5.2: a Q8 brightness factor per
 * peek depth {@code d}, where {@code d} is the number of z-levels below the camera
 * level that a tile shows through OPEN cells ({@code ZPeekResolver} computes {@code d};
 * this table only prices it).
 *
 * <p>The curve ships in {@code art-mapping.json} ({@code zPeekDimQ8}) so a pack swap
 * can re-mood the world without code. The v0 placeholder curve is
 * {@code [256, 168, 112, 76]} (maxPeekDepth 3 — BLESSING-QUEUE.md default 14; below
 * that depth nothing is drawn and the cell renders as flat {@code voidColor}). This
 * class accepts any curve satisfying the schema shape rules of TILE-ART-SPEC
 * section 7.1: length {@code maxPeekDepth + 1} (at least 1), first entry exactly
 * {@code 256}, every entry in {@code 0..256}, monotone non-increasing.
 *
 * <p>Render pipeline (TILE-ART-SPEC section 5): dimming multiplies <em>after</em> the
 * light tint — {@code rgb_out = (((rgb * lightTintQ8[L]) >> 8) * zPeekDimQ8[d]) >> 8}
 * per channel. {@link #shadeRgb(int, int)} implements the second multiply for tests and
 * CPU-side previews; the GL renderer applies it as a batch color multiply.
 *
 * <p>Immutable, allocation-free after construction, deterministic (pure integer math).
 */
public final class ZPeekDimTable {

    /** Q8 identity factor (256 = 1.0). */
    public static final int UNIT_Q8 = 256;

    private final int[] dimQ8;

    private ZPeekDimTable(int[] validatedCopy) {
        this.dimQ8 = validatedCopy;
    }

    /**
     * Builds a table from a raw Q8 curve, validating the TILE-ART-SPEC section 7.1
     * shape rules.
     *
     * @param curveQ8 the {@code zPeekDimQ8} array; defensively copied
     * @return the immutable table
     * @throws IllegalArgumentException if {@code curveQ8} is null or empty, its first
     *                                  entry is not exactly 256, any entry is outside
     *                                  0..256, or the sequence increases anywhere
     */
    public static ZPeekDimTable fromQ8(int[] curveQ8) {
        if (curveQ8 == null || curveQ8.length == 0) {
            throw new IllegalArgumentException("zPeekDimQ8: null or empty");
        }
        if (curveQ8[0] != UNIT_Q8) {
            throw new IllegalArgumentException(
                    "zPeekDimQ8[0] = " + curveQ8[0] + " (must be exactly " + UNIT_Q8
                            + " — depth 0 is the camera's own level, undimmed)");
        }
        int prev = UNIT_Q8;
        for (int i = 0; i < curveQ8.length; i++) {
            int v = curveQ8[i];
            if (v < 0 || v > UNIT_Q8) {
                throw new IllegalArgumentException(
                        "zPeekDimQ8[" + i + "] = " + v + " outside 0.." + UNIT_Q8);
            }
            if (v > prev) {
                throw new IllegalArgumentException(
                        "zPeekDimQ8[" + i + "] = " + v + " increases (previous " + prev
                                + "); curve must be monotone non-increasing");
            }
            prev = v;
        }
        return new ZPeekDimTable(Arrays.copyOf(curveQ8, curveQ8.length));
    }

    /**
     * The deepest drawable peek depth. Cells that would peek deeper render as flat
     * {@code voidColor} (TILE-ART-SPEC section 5.2); 3 in the v0 placeholder mapping.
     *
     * @return the largest legal argument to {@link #dimQ8(int)}; at least 0
     */
    public int maxPeekDepth() {
        return dimQ8.length - 1;
    }

    /**
     * The Q8 dim factor for a peek depth.
     *
     * @param peekDepth z-levels below the camera level, 0..{@link #maxPeekDepth()}
     * @return the factor, 0..256
     * @throws IllegalArgumentException if {@code peekDepth} is outside
     *                                  0..{@link #maxPeekDepth()}
     */
    public int dimQ8(int peekDepth) {
        if (peekDepth < 0 || peekDepth >= dimQ8.length) {
            throw new IllegalArgumentException(
                    "peekDepth " + peekDepth + " outside 0.." + maxPeekDepth());
        }
        return dimQ8[peekDepth];
    }

    /**
     * Applies the dim factor to a packed {@code 0xRRGGBB} color, per channel:
     * {@code c' = (c * zPeekDimQ8[d]) >> 8}.
     *
     * @param rgb888    packed color, high byte ignored
     * @param peekDepth z-levels below the camera level, 0..{@link #maxPeekDepth()}
     * @return the dimmed packed color
     * @throws IllegalArgumentException if {@code peekDepth} is outside
     *                                  0..{@link #maxPeekDepth()}
     */
    public int shadeRgb(int rgb888, int peekDepth) {
        int f = dimQ8(peekDepth);
        int r = (((rgb888 >> 16) & 0xFF) * f) >> 8;
        int g = (((rgb888 >> 8) & 0xFF) * f) >> 8;
        int b = ((rgb888 & 0xFF) * f) >> 8;
        return (r << 16) | (g << 8) | b;
    }
}
