package com.trojia.client.art;

import java.util.Arrays;

/**
 * The light-level tint curve of TILE-ART-SPEC section 5.1: a Q8 brightness factor
 * per light level {@code L} in 0..31 (ARCHITECTURE section 1.1 #19 fixes the light
 * range at 0-31).
 *
 * <p>The curve ships in {@code art-mapping.json} ({@code lightTintQ8}) so a pack swap
 * can re-mood the world without code. The v0 placeholder curve is quadratic with a
 * legibility floor: {@code lightTintQ8[L] = 36 + floor(220 * L^2 / 961)}, which makes
 * {@code [31] = 256} exactly. This class does not hard-code that formula — it accepts
 * any curve satisfying the schema shape rules of TILE-ART-SPEC section 7.1:
 * length exactly {@value #LEVELS}, every entry in {@code 0..256}, monotone
 * non-decreasing, last entry {@code == 256}.
 *
 * <p>Render pipeline (TILE-ART-SPEC section 5): the tint multiplies <em>before</em>
 * z-peek dimming — {@code rgb_out = (((rgb * lightTintQ8[L]) >> 8) * zPeekDimQ8[d]) >> 8}
 * per channel. {@link #shadeRgb(int, int)} implements the first multiply for tests and
 * CPU-side previews; the GL renderer applies it as a batch color multiply.
 *
 * <p>Immutable, allocation-free after construction, deterministic (pure integer math).
 */
public final class LightTintTable {

    /** Number of light levels: 0..31 (ARCHITECTURE section 1.1 #19). */
    public static final int LEVELS = 32;

    /** Q8 identity factor (256 = 1.0). */
    public static final int UNIT_Q8 = 256;

    private final int[] tintQ8;

    private LightTintTable(int[] validatedCopy) {
        this.tintQ8 = validatedCopy;
    }

    /**
     * Builds a table from a raw Q8 curve, validating the TILE-ART-SPEC section 7.1
     * shape rules.
     *
     * @param curveQ8 the {@code lightTintQ8} array; defensively copied
     * @return the immutable table
     * @throws IllegalArgumentException if {@code curveQ8} is null, its length is not
     *                                  {@value #LEVELS}, any entry is outside 0..256,
     *                                  the sequence decreases anywhere, or the last
     *                                  entry is not exactly 256
     */
    public static LightTintTable fromQ8(int[] curveQ8) {
        if (curveQ8 == null) {
            throw new IllegalArgumentException("lightTintQ8: null");
        }
        if (curveQ8.length != LEVELS) {
            throw new IllegalArgumentException(
                    "lightTintQ8: length " + curveQ8.length + " (expected " + LEVELS + ")");
        }
        int prev = -1;
        for (int i = 0; i < curveQ8.length; i++) {
            int v = curveQ8[i];
            if (v < 0 || v > UNIT_Q8) {
                throw new IllegalArgumentException(
                        "lightTintQ8[" + i + "] = " + v + " outside 0.." + UNIT_Q8);
            }
            if (v < prev) {
                throw new IllegalArgumentException(
                        "lightTintQ8[" + i + "] = " + v + " decreases (previous " + prev
                                + "); curve must be monotone non-decreasing");
            }
            prev = v;
        }
        if (curveQ8[LEVELS - 1] != UNIT_Q8) {
            throw new IllegalArgumentException(
                    "lightTintQ8[" + (LEVELS - 1) + "] = " + curveQ8[LEVELS - 1]
                            + " (must be exactly " + UNIT_Q8 + ")");
        }
        return new LightTintTable(Arrays.copyOf(curveQ8, curveQ8.length));
    }

    /**
     * The Q8 tint factor for a light level.
     *
     * @param lightLevel effective brightness 0..31 from {@code LightQuery}
     * @return the factor, 0..256
     * @throws IllegalArgumentException if {@code lightLevel} is outside 0..31
     */
    public int tintQ8(int lightLevel) {
        checkLevel(lightLevel, "lightLevel");
        return tintQ8[lightLevel];
    }

    /**
     * The Q8 tint factor after the per-material cosmetic clamp of TILE-ART-SPEC
     * section 5.1: {@code L' = max(L, minLight)}. The clamp never feeds back into sim
     * light values — it only brightens this material's texels.
     *
     * @param lightLevel effective brightness 0..31
     * @param minLight   the material's {@code minLight} (0 = no clamp), 0..31
     * @return the factor for {@code max(lightLevel, minLight)}
     * @throws IllegalArgumentException if either argument is outside 0..31
     */
    public int tintQ8(int lightLevel, int minLight) {
        checkLevel(lightLevel, "lightLevel");
        checkLevel(minLight, "minLight");
        return tintQ8[Math.max(lightLevel, minLight)];
    }

    /**
     * Applies the tint to a packed {@code 0xRRGGBB} color, per channel:
     * {@code c' = (c * tintQ8[L]) >> 8}.
     *
     * @param rgb888     packed color, high byte ignored
     * @param lightLevel effective brightness 0..31
     * @return the shaded packed color
     * @throws IllegalArgumentException if {@code lightLevel} is outside 0..31
     */
    public int shadeRgb(int rgb888, int lightLevel) {
        int t = tintQ8(lightLevel);
        int r = (((rgb888 >> 16) & 0xFF) * t) >> 8;
        int g = (((rgb888 >> 8) & 0xFF) * t) >> 8;
        int b = ((rgb888 & 0xFF) * t) >> 8;
        return (r << 16) | (g << 8) | b;
    }

    private static void checkLevel(int level, String what) {
        if (level < 0 || level >= LEVELS) {
            throw new IllegalArgumentException(what + " " + level + " outside 0.." + (LEVELS - 1));
        }
    }
}
