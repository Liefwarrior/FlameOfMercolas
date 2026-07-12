package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Audit of the 30-bit PackedPos bit math: the packing formula is checked
 * literally against the spec, round-trips are exhausted along full coordinate
 * planes plus a large deterministic sample, and {@code step} is verified
 * against re-packing for every direction.
 */
final class PackedPosTest {

    @Test
    void constantsMatchTheSpec() {
        assertEquals(4095, PackedPos.X_MASK);
        assertEquals(4095, PackedPos.Y_MASK);
        assertEquals(63, PackedPos.Z_MASK);
    }

    @Test
    void packMatchesTheSpecFormulaAtExtremes() {
        int[] xs = {0, 1, 31, 32, 2047, 2048, 4094, 4095};
        int[] zs = {0, 1, 7, 8, 31, 32, 62, 63};
        for (int z : zs) {
            for (int y : xs) {
                for (int x : xs) {
                    assertEquals((z << 24) | (y << 12) | x, PackedPos.pack(x, y, z));
                }
            }
        }
    }

    @Test
    void exhaustiveXyPlaneRoundTrip() {
        for (int z : new int[] {0, 63}) {
            for (int y = 0; y <= 4095; y++) {
                for (int x = 0; x <= 4095; x++) {
                    int pos = PackedPos.pack(x, y, z);
                    if (PackedPos.x(pos) != x || PackedPos.y(pos) != y || PackedPos.z(pos) != z) {
                        assertEquals(x + "," + y + "," + z, PackedPos.x(pos) + ","
                                + PackedPos.y(pos) + "," + PackedPos.z(pos));
                    }
                }
            }
        }
    }

    @Test
    void exhaustiveXzAndYzPlaneRoundTrips() {
        for (int fixed : new int[] {0, 1, 2047, 4095}) {
            for (int z = 0; z <= 63; z++) {
                for (int v = 0; v <= 4095; v++) {
                    int posXz = PackedPos.pack(v, fixed, z);
                    assertEquals(v, PackedPos.x(posXz));
                    assertEquals(fixed, PackedPos.y(posXz));
                    assertEquals(z, PackedPos.z(posXz));
                    int posYz = PackedPos.pack(fixed, v, z);
                    assertEquals(fixed, PackedPos.x(posYz));
                    assertEquals(v, PackedPos.y(posYz));
                    assertEquals(z, PackedPos.z(posYz));
                }
            }
        }
    }

    @Test
    void largeDeterministicSampleRoundTrips() {
        long state = 0x5DEECE66DL;
        for (int i = 0; i < 1_000_000; i++) {
            state = state * 6364136223846793005L + 1442695040888963407L;
            int x = (int) (state >>> 16) & 4095;
            int y = (int) (state >>> 28) & 4095;
            int z = (int) (state >>> 40) & 63;
            int pos = PackedPos.pack(x, y, z);
            assertEquals(x, PackedPos.x(pos));
            assertEquals(y, PackedPos.y(pos));
            assertEquals(z, PackedPos.z(pos));
        }
    }

    @Test
    void stepMatchesRepackInEveryDirection() {
        long state = 0x2545F4914F6CDD1DL;
        for (int i = 0; i < 100_000; i++) {
            state = state * 6364136223846793005L + 1442695040888963407L;
            // Interior coordinates: one blind step stays in range on every axis.
            int x = 1 + ((int) (state >>> 16) & 2047);
            int y = 1 + ((int) (state >>> 28) & 2047);
            int z = 1 + ((int) (state >>> 40) & 31);
            int pos = PackedPos.pack(x, y, z);
            for (Dir dir : Dir.values()) {
                assertEquals(PackedPos.pack(x + dir.dx(), y + dir.dy(), z + dir.dz()),
                        PackedPos.step(pos, dir));
            }
        }
    }

    @Test
    void stepAndOppositeReturnToStart() {
        int pos = PackedPos.pack(100, 200, 30);
        for (Dir dir : Dir.values()) {
            assertEquals(pos, PackedPos.step(PackedPos.step(pos, dir), dir.opposite()));
        }
    }
}
