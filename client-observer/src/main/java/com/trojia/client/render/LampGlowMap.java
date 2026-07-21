package com.trojia.client.render;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Precomputed per-cell lamplight influence — the warm radial pools the district's static
 * light-source markers (lamps, braziers) cast at Dusk/Night. Built once at load from the
 * fixture's marker list ({@code LampMarkersLoader}); per drawn tile the renderer pays one
 * array lookup ({@link #glow}). Presentation-only, pure function of the static marker
 * data — never read by sim-core or the {@code WorldHasher}.
 *
 * <p><b>Model.</b> Each lamp lights the cells of its own z-level within radius
 * {@code R = 4 + (luminance - 8) / 12} tiles (clamped {@value #RADIUS_MIN}..{@value
 * #RADIUS_MAX} — the map's authored luminance grading, 8 shrine candles to 26 mast lamp,
 * maps to pool sizes ~4..5.5) at peak strength {@code P = 0.55 + 0.45 * luminance / 26},
 * falling off smoothly as {@code w(d) = P * (1 - (d/R)^2)^2} (zero value <em>and</em> zero
 * slope at the rim — pools melt into the dark, no visible edge). Overlapping lamps combine
 * with a saturating union on strength ({@code 1 - prod(1 - w_i)}) and a weighted average on
 * colour.
 *
 * <p><b>Warmth.</b> Fire-type sources (braziers, cauldrons, ovens, torches, hearths,
 * candles) glow a deeper ember orange ({@link #FIRE_R}/{@code G}/{@code B}) than oil/lamp
 * lanterns ({@link #LANTERN_R}/{@code G}/{@code B}) — distinguishable when close, one warm
 * family from afar.
 *
 * <p>The renderer lifts a cell's ambient toward the glow colour by
 * {@code strength * AmbientLight.lampFactor()}: invisible by day (factor 0), warm pools on
 * cool dark streets at night.
 *
 * <p><b>Storage.</b> One packed-int plane per z-level that has any influence
 * ({@code (strength<<24) | (r<<16) | (g<<8) | b}, all bytes 0..255); {@link #glow} is a map
 * probe plus one array read, returning 0 (no glow) for every untouched cell/level.
 */
public final class LampGlowMap {

    /** The no-lamps map: {@link #glow} is 0 everywhere (identity for the renderer). */
    public static final LampGlowMap EMPTY = new LampGlowMap(List.of(), 1, 1);

    /** Lantern warm target — oil-lamp gold. */
    static final float LANTERN_R = 1.00f;
    static final float LANTERN_G = 0.80f;
    static final float LANTERN_B = 0.52f;

    /** Fire warm target — brazier/hearth ember, deeper orange than the lanterns. */
    static final float FIRE_R = 1.00f;
    static final float FIRE_G = 0.64f;
    static final float FIRE_B = 0.32f;

    /** Radius bounds, tiles (README maps/§7.2 luminance grading 8..26 maps inside these). */
    static final float RADIUS_MIN = 3.5f;
    static final float RADIUS_MAX = 5.5f;

    /** Luminance range of the marker contract (light levels are 5-bit, 0..31). */
    private static final int LUMINANCE_MAX = 31;

    /**
     * One static light source in world-tile coordinates.
     *
     * @param x         world tile x
     * @param y         world tile y
     * @param z         world z-level the lamp lights
     * @param luminance authored 0..31 marker luminance (grades radius and peak)
     * @param fire      true for ember-warm sources (braziers etc.), false for lanterns
     */
    public record Lamp(int x, int y, int z, int luminance, boolean fire) {
        public Lamp {
            if (luminance < 0 || luminance > LUMINANCE_MAX) {
                throw new IllegalArgumentException("luminance " + luminance + " outside 0..31");
            }
        }
    }

    private final int width;
    private final int height;
    private final Map<Integer, int[]> planesByZ;

    /**
     * Precomputes the influence planes for {@code lamps} over a {@code width x height}
     * world footprint. Lamps outside the footprint are clipped cell-wise (never thrown).
     *
     * @param lamps  the static light sources, world coordinates
     * @param width  world width, tiles
     * @param height world height, tiles
     */
    public LampGlowMap(List<Lamp> lamps, int width, int height) {
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("bad footprint " + width + "x" + height);
        }
        this.width = width;
        this.height = height;
        // Accumulate in float (strength-union + weighted colour), pack to int at the end.
        Map<Integer, float[]> acc = new HashMap<>();
        for (Lamp lamp : lamps) {
            accumulate(acc, lamp);
        }
        this.planesByZ = pack(acc);
    }

    /**
     * The packed glow at a cell: {@code (strength<<24)|(r<<16)|(g<<8)|b} with every byte
     * 0..255, or {@code 0} when no lamp reaches it (including any out-of-bounds cell).
     */
    public int glow(int x, int y, int z) {
        int[] plane = planesByZ.get(z);
        if (plane == null || x < 0 || y < 0 || x >= width || y >= height) {
            return 0;
        }
        return plane[y * width + x];
    }

    /** The radius, tiles, a lamp of {@code luminance} lights (package: tested + compositor). */
    static float radius(int luminance) {
        float r = 4f + (luminance - 8) / 12f;
        return Math.clamp(r, RADIUS_MIN, RADIUS_MAX);
    }

    /** The peak (at-the-lamp) strength for {@code luminance}, 0..1. */
    static float peak(int luminance) {
        return 0.55f + 0.45f * Math.min(1f, luminance / 26f);
    }

    /** The smooth falloff weight at Euclidean distance {@code d} for one lamp. */
    static float falloff(float d, float radius, float peak) {
        float q = 1f - (d * d) / (radius * radius);
        if (q <= 0f) {
            return 0f;
        }
        return peak * q * q;
    }

    private void accumulate(Map<Integer, float[]> acc, Lamp lamp) {
        float radius = radius(lamp.luminance());
        float peak = peak(lamp.luminance());
        float warmR = lamp.fire() ? FIRE_R : LANTERN_R;
        float warmG = lamp.fire() ? FIRE_G : LANTERN_G;
        float warmB = lamp.fire() ? FIRE_B : LANTERN_B;
        int reach = (int) Math.ceil(radius);
        int minX = Math.max(0, lamp.x() - reach);
        int maxX = Math.min(width - 1, lamp.x() + reach);
        int minY = Math.max(0, lamp.y() - reach);
        int maxY = Math.min(height - 1, lamp.y() + reach);
        if (minX > maxX || minY > maxY) {
            return; // fully outside the footprint
        }
        // Accumulator plane layout: per cell 5 floats — transparency prod(1-w), then
        // weight-sum and weighted r/g/b sums for the colour average.
        float[] plane = acc.computeIfAbsent(lamp.z(), z -> {
            float[] fresh = new float[width * height * 5];
            for (int i = 0; i < fresh.length; i += 5) {
                fresh[i] = 1f; // transparency starts at 1 (no light)
            }
            return fresh;
        });
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float dx = x - lamp.x();
                float dy = y - lamp.y();
                float w = falloff((float) Math.sqrt(dx * dx + dy * dy), radius, peak);
                if (w <= 0f) {
                    continue;
                }
                int i = (y * width + x) * 5;
                plane[i] *= (1f - w);
                plane[i + 1] += w;
                plane[i + 2] += w * warmR;
                plane[i + 3] += w * warmG;
                plane[i + 4] += w * warmB;
            }
        }
    }

    private Map<Integer, int[]> pack(Map<Integer, float[]> acc) {
        Map<Integer, int[]> out = new HashMap<>();
        for (Map.Entry<Integer, float[]> e : acc.entrySet()) {
            float[] plane = e.getValue();
            int[] packed = new int[width * height];
            for (int cell = 0; cell < packed.length; cell++) {
                int i = cell * 5;
                float weightSum = plane[i + 1];
                if (weightSum <= 0f) {
                    continue;
                }
                float strength = 1f - plane[i]; // saturating union of all lamps here
                int s = Math.round(strength * 255f);
                if (s <= 0) {
                    continue;
                }
                int r = Math.round(plane[i + 2] / weightSum * 255f);
                int g = Math.round(plane[i + 3] / weightSum * 255f);
                int b = Math.round(plane[i + 4] / weightSum * 255f);
                packed[cell] = (s << 24) | (r << 16) | (g << 8) | b;
            }
            out.put(e.getKey(), packed);
        }
        return out;
    }
}
