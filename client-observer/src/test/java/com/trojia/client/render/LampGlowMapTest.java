package com.trojia.client.render;

import com.trojia.client.render.LampGlowMap.Lamp;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for {@link LampGlowMap} — the precomputed per-cell lamp influence behind
 * the Dusk/Night warm pools. Contract: a warm peak at the lamp, smooth monotone falloff to
 * zero at the rim (radius graded ~4..5.5 tiles by authored luminance), fire sources warmer
 * than lanterns, saturating overlap, and zero everywhere no lamp reaches (including other
 * z-levels and out-of-bounds probes).
 */
class LampGlowMapTest {

    private static int strengthByte(int glow) {
        return (glow >>> 24) & 0xFF;
    }

    @Test
    void peakSitsOnTheLampCellAndFallsOffMonotonically() {
        LampGlowMap map = new LampGlowMap(List.of(new Lamp(20, 20, 5, 18, false)), 64, 64);
        int prev = Integer.MAX_VALUE;
        for (int d = 0; d <= 6; d++) {
            int s = strengthByte(map.glow(20 + d, 20, 5));
            assertTrue(s <= prev, "strength must not rise with distance (d=" + d + ")");
            prev = s;
        }
        int peak = strengthByte(map.glow(20, 20, 5));
        assertEquals(Math.round(LampGlowMap.peak(18) * 255f), peak,
                "the lamp's own cell carries the full graded peak");
        assertTrue(peak > strengthByte(map.glow(22, 20, 5)),
                "two tiles out is visibly dimmer than the core");
    }

    @Test
    void radiusIsGradedByLuminanceInsideTheFourToFivishWindow() {
        assertEquals(4.0f, LampGlowMap.radius(8), 1e-6, "shrine candles: smallest pool");
        assertEquals(5.5f, LampGlowMap.radius(26), 1e-6, "mast lamp: the largest pool");
        assertTrue(LampGlowMap.radius(0) >= LampGlowMap.RADIUS_MIN
                        && LampGlowMap.radius(31) <= LampGlowMap.RADIUS_MAX,
                "radius stays clamped for any legal 0..31 luminance");
    }

    @Test
    void glowEndsAtTheRim() {
        LampGlowMap map = new LampGlowMap(List.of(new Lamp(20, 20, 5, 26, false)), 64, 64);
        // radius(26) = 5.5: distance 6 is past the rim on the axis.
        assertEquals(0, map.glow(26, 20, 5), "past the rim there is no influence at all");
        assertTrue(strengthByte(map.glow(25, 20, 5)) > 0, "just inside the rim still glows");
    }

    @Test
    void fireGlowsWarmerThanLantern() {
        LampGlowMap lantern = new LampGlowMap(List.of(new Lamp(5, 5, 0, 18, false)), 16, 16);
        LampGlowMap fire = new LampGlowMap(List.of(new Lamp(5, 5, 0, 18, true)), 16, 16);
        int lanternGlow = lantern.glow(5, 5, 0);
        int fireGlow = fire.glow(5, 5, 0);
        assertEquals((lanternGlow >> 16) & 0xFF, (fireGlow >> 16) & 0xFF,
                "both warm families carry full red");
        assertTrue(((fireGlow >> 8) & 0xFF) < ((lanternGlow >> 8) & 0xFF),
                "fire pulls green lower: deeper ember orange");
        assertTrue((fireGlow & 0xFF) < (lanternGlow & 0xFF),
                "fire pulls blue lower too");
    }

    @Test
    void overlappingLampsSaturateInsteadOfBlowingOut() {
        List<Lamp> cluster = List.of(
                new Lamp(10, 10, 0, 26, false),
                new Lamp(11, 10, 0, 26, false),
                new Lamp(10, 11, 0, 26, true));
        LampGlowMap overlap = new LampGlowMap(cluster, 32, 32);
        LampGlowMap single = new LampGlowMap(cluster.subList(0, 1), 32, 32);
        int overlapS = strengthByte(overlap.glow(10, 10, 0));
        int singleS = strengthByte(single.glow(10, 10, 0));
        assertTrue(overlapS >= singleS, "more lamps never darken a cell");
        assertTrue(overlapS <= 255, "strength saturates at full");
        int glow = overlap.glow(10, 10, 0);
        assertTrue(((glow >> 16) & 0xFF) <= 255 && ((glow >> 8) & 0xFF) <= 255
                && (glow & 0xFF) <= 255, "colour bytes stay in range under overlap");
    }

    @Test
    void otherZLevelsAndOutOfBoundsProbesAreDark() {
        LampGlowMap map = new LampGlowMap(List.of(new Lamp(3, 3, 7, 20, false)), 16, 16);
        assertEquals(0, map.glow(3, 3, 6), "a lamp lights only its own z-level");
        assertEquals(0, map.glow(3, 3, 8));
        assertEquals(0, map.glow(-1, 3, 7), "out-of-bounds probes are safe and dark");
        assertEquals(0, map.glow(3, 99, 7));
    }

    @Test
    void lampAtTheFootprintEdgeClipsInsteadOfThrowing() {
        LampGlowMap map = new LampGlowMap(List.of(new Lamp(0, 0, 0, 26, false)), 8, 8);
        assertTrue(strengthByte(map.glow(0, 0, 0)) > 0, "the on-map part still glows");
        LampGlowMap outside = new LampGlowMap(List.of(new Lamp(500, 500, 0, 26, false)), 8, 8);
        assertEquals(0, outside.glow(7, 7, 0), "a fully off-map lamp contributes nothing");
    }

    @Test
    void emptyMapIsTheIdentityEverywhere() {
        assertEquals(0, LampGlowMap.EMPTY.glow(0, 0, 0));
        assertEquals(0, LampGlowMap.EMPTY.glow(123, 45, 19));
    }

    @Test
    void illegalLuminanceIsRejectedAtConstruction() {
        assertThrows(IllegalArgumentException.class, () -> new Lamp(0, 0, 0, 32, false));
        assertThrows(IllegalArgumentException.class, () -> new Lamp(0, 0, 0, -1, false));
    }
}
