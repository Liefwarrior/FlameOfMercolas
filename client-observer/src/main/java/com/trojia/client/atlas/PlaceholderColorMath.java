package com.trojia.client.atlas;

import java.nio.charset.StandardCharsets;

/**
 * GL-free color math for the placeholder atlas (TILE-ART-SPEC section 6.1):
 * the FNV-1a-64 HSL color mint for material ids, the WCAG glyph-contrast rule,
 * {@code #RRGGBB} parsing, and the Q8 per-channel scale/blend used by the block/floor
 * cell variants.
 *
 * <p>The mint rule is normative:
 *
 * <pre>
 * h    = FNV-1a-64(utf8(id))
 * hue  = h mod 360                       (unsigned)
 * sat  = 45 + ((h &gt;&gt;&gt; 16) mod 25)        // 45..69 %
 * lit  = 34 + ((h &gt;&gt;&gt; 32) mod 18)        // 34..51 %
 * color = HSL(hue, sat, lit) &rarr; sRGB, rounded (Math.round per channel)
 * glyphColor = black if WCAG relative luminance &gt; 0.30 else white
 * </pre>
 *
 * This implementation reproduces the normative table of TILE-ART-SPEC section 6.2
 * exactly (asserted by test against {@code art-mapping.json}): the listed values are
 * the mint's own output for every entry marked {@code "colorSource": "hash"}.
 *
 * <p>{@code double} appears only in the HSL&rarr;sRGB conversion and the luminance
 * test; both are pure functions of integer inputs evaluated identically on every JVM
 * (strict IEEE-754, {@code Math.round}/{@code Math.pow} on the same inputs), so the
 * mint is deterministic. The sim-core no-float rule does not apply to the observer.
 *
 * <p>All colors are packed {@code 0xRRGGBB} ints unless a method says ARGB.
 */
public final class PlaceholderColorMath {

    /** FNV-1a 64-bit offset basis. */
    public static final long FNV_OFFSET_BASIS = 0xCBF29CE484222325L;

    /** FNV-1a 64-bit prime. */
    public static final long FNV_PRIME = 0x100000001B3L;

    /** Q8 identity factor (256 = 1.0), matching the mapping's Q8 convention. */
    public static final int UNIT_Q8 = 256;

    /**
     * WCAG relative-luminance threshold of the mint rule: above it the glyph is
     * black, otherwise white.
     */
    public static final double GLYPH_LUMINANCE_THRESHOLD = 0.30;

    private PlaceholderColorMath() {
    }

    /**
     * FNV-1a-64 over the UTF-8 bytes of {@code text}.
     *
     * @param text the input, non-null (empty allowed: returns the offset basis)
     * @return the 64-bit hash (treat as unsigned)
     * @throws IllegalArgumentException if {@code text} is null
     */
    public static long fnv1a64(String text) {
        if (text == null) {
            throw new IllegalArgumentException("text must be non-null");
        }
        long h = FNV_OFFSET_BASIS;
        for (byte b : text.getBytes(StandardCharsets.UTF_8)) {
            h ^= (b & 0xFFL);
            h *= FNV_PRIME;
        }
        return h;
    }

    /**
     * Mints the deterministic fill color for a material id per TILE-ART-SPEC
     * section 6.1 (see class javadoc for the exact rule).
     *
     * @param materialId canonical raws id, non-blank, hashed verbatim — derived ids
     *                   ({@code trudgeon_wood@getilia_soak}) get distinct colors free
     * @return packed {@code 0xRRGGBB}
     * @throws IllegalArgumentException if {@code materialId} is null or blank
     */
    public static int mintFillRgb(String materialId) {
        if (materialId == null || materialId.isBlank()) {
            throw new IllegalArgumentException("materialId must be non-blank");
        }
        long h = fnv1a64(materialId);
        int hue = (int) Long.remainderUnsigned(h, 360);
        int sat = 45 + (int) Long.remainderUnsigned(h >>> 16, 25);
        int lit = 34 + (int) Long.remainderUnsigned(h >>> 32, 18);
        return hslToRgb(hue, sat, lit);
    }

    /**
     * The mint's glyph color for a fill: black if
     * {@link #relativeLuminance(int)}{@code > }{@value #GLYPH_LUMINANCE_THRESHOLD},
     * else white.
     *
     * @param fillRgb packed {@code 0xRRGGBB}, high byte ignored
     * @return {@code 0x000000} or {@code 0xFFFFFF}
     */
    public static int mintGlyphRgb(int fillRgb) {
        return relativeLuminance(fillRgb) > GLYPH_LUMINANCE_THRESHOLD ? 0x000000 : 0xFFFFFF;
    }

    /**
     * Standard CSS HSL &rarr; sRGB conversion, each channel rounded with
     * {@link Math#round(double)}.
     *
     * @param hueDeg hue in degrees, 0..359
     * @param satPct saturation percent, 0..100
     * @param litPct lightness percent, 0..100
     * @return packed {@code 0xRRGGBB}
     * @throws IllegalArgumentException if any argument is outside its range
     */
    public static int hslToRgb(int hueDeg, int satPct, int litPct) {
        if (hueDeg < 0 || hueDeg >= 360) {
            throw new IllegalArgumentException("hueDeg " + hueDeg + " outside 0..359");
        }
        if (satPct < 0 || satPct > 100) {
            throw new IllegalArgumentException("satPct " + satPct + " outside 0..100");
        }
        if (litPct < 0 || litPct > 100) {
            throw new IllegalArgumentException("litPct " + litPct + " outside 0..100");
        }
        double s = satPct / 100.0;
        double l = litPct / 100.0;
        double c = (1.0 - Math.abs(2.0 * l - 1.0)) * s;
        double hp = hueDeg / 60.0;
        double x = c * (1.0 - Math.abs(hp % 2.0 - 1.0));
        double r1 = 0;
        double g1 = 0;
        double b1 = 0;
        switch ((int) hp) {
            case 0 -> {
                r1 = c;
                g1 = x;
            }
            case 1 -> {
                r1 = x;
                g1 = c;
            }
            case 2 -> {
                g1 = c;
                b1 = x;
            }
            case 3 -> {
                g1 = x;
                b1 = c;
            }
            case 4 -> {
                r1 = x;
                b1 = c;
            }
            default -> {
                r1 = c;
                b1 = x;
            }
        }
        double m = l - c / 2.0;
        int r = (int) Math.round((r1 + m) * 255.0);
        int g = (int) Math.round((g1 + m) * 255.0);
        int b = (int) Math.round((b1 + m) * 255.0);
        return (r << 16) | (g << 8) | b;
    }

    /**
     * WCAG 2.x relative luminance of an sRGB color: channels linearized with the
     * piecewise gamma ({@code c <= 0.03928 ? c/12.92 : ((c+0.055)/1.055)^2.4}),
     * weighted {@code 0.2126 R + 0.7152 G + 0.0722 B}.
     *
     * @param rgb888 packed {@code 0xRRGGBB}, high byte ignored
     * @return luminance in 0..1
     */
    public static double relativeLuminance(int rgb888) {
        double r = linearize(((rgb888 >> 16) & 0xFF) / 255.0);
        double g = linearize(((rgb888 >> 8) & 0xFF) / 255.0);
        double b = linearize((rgb888 & 0xFF) / 255.0);
        return 0.2126 * r + 0.7152 * g + 0.0722 * b;
    }

    private static double linearize(double c) {
        return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
    }

    /**
     * Parses a {@code #RRGGBB} color literal (the only color syntax the mapping file
     * uses).
     *
     * @param literal e.g. {@code "#9FB8D8"}; case-insensitive hex digits
     * @return packed {@code 0xRRGGBB}
     * @throws IllegalArgumentException if {@code literal} is not exactly {@code '#'}
     *                                  followed by six hex digits
     */
    public static int parseRgb(String literal) {
        if (literal != null && literal.length() == 7 && literal.charAt(0) == '#') {
            try {
                return Integer.parseInt(literal.substring(1), 16);
            } catch (NumberFormatException ignored) {
                // falls through to the error below
            }
        }
        throw new IllegalArgumentException("expected \"#RRGGBB\", found " + literal);
    }

    /**
     * Scales each channel by a Q8 factor: {@code c' = (c * q8) >> 8}. This is the
     * exact integer transform of the outline ({@code outlineScaleQ8}) and floor
     * ({@code floorScaleQ8}) rules of TILE-ART-SPEC section 6.
     *
     * @param rgb888 packed {@code 0xRRGGBB}, high byte ignored
     * @param q8     factor 0..256 (256 = identity)
     * @return the scaled packed color
     * @throws IllegalArgumentException if {@code q8} is outside 0..256
     */
    public static int scaleRgbQ8(int rgb888, int q8) {
        checkQ8(q8);
        int r = (((rgb888 >> 16) & 0xFF) * q8) >> 8;
        int g = (((rgb888 >> 8) & 0xFF) * q8) >> 8;
        int b = ((rgb888 & 0xFF) * q8) >> 8;
        return (r << 16) | (g << 8) | b;
    }

    /**
     * Blends {@code from} toward {@code to} by a Q8 amount, per channel:
     * {@code c' = (c_from * (256 - q8) + c_to * q8) >> 8}. Used for the floor glyph
     * ({@code floorGlyphBlendQ8 = 128}: glyph blended 50 percent toward the fill).
     *
     * @param fromRgb packed {@code 0xRRGGBB}, high byte ignored
     * @param toRgb   packed {@code 0xRRGGBB}, high byte ignored
     * @param q8      blend amount 0..256 (0 = {@code from}, 256 = {@code to})
     * @return the blended packed color
     * @throws IllegalArgumentException if {@code q8} is outside 0..256
     */
    public static int blendRgbQ8(int fromRgb, int toRgb, int q8) {
        checkQ8(q8);
        int inv = UNIT_Q8 - q8;
        int r = ((((fromRgb >> 16) & 0xFF) * inv) + (((toRgb >> 16) & 0xFF) * q8)) >> 8;
        int g = ((((fromRgb >> 8) & 0xFF) * inv) + (((toRgb >> 8) & 0xFF) * q8)) >> 8;
        int b = (((fromRgb & 0xFF) * inv) + ((toRgb & 0xFF) * q8)) >> 8;
        return (r << 16) | (g << 8) | b;
    }

    private static void checkQ8(int q8) {
        if (q8 < 0 || q8 > UNIT_Q8) {
            throw new IllegalArgumentException("q8 " + q8 + " outside 0.." + UNIT_Q8);
        }
    }
}
