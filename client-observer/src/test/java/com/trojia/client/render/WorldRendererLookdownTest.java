package com.trojia.client.render;

import org.junit.jupiter.api.Test;

import java.util.function.IntPredicate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for the GL-free brain of the air-depth "look-down" pass (Eli 2026-07-15):
 * {@link WorldRenderer#findLookdownZ} (the z-column probe that finds the nearest drawn cell
 * below empty air, respecting {@link WorldRenderer#MAX_LOOKDOWN} and the world floor) and
 * {@link WorldRenderer#depthDim} (the per-depth brightness). Both are pure functions, so they
 * exercise with a synthetic column and no world or GL — the GL half (blur level + batch draw)
 * runs in the observer boot.
 */
class WorldRendererLookdownTest {

    /** A synthetic z-stack: {@code drawable[z]} is whether the cell at that z draws something. */
    private static IntPredicate column(boolean... drawable) {
        return z -> z >= 0 && z < drawable.length && drawable[z];
    }

    @Test
    void findsTheNearestDrawnCellBelowAndItsDepth() {
        // Camera at z=10; cells drawn at z=7 and z=3. The nearer one (z=7) wins, depth 3.
        IntPredicate col = z -> z == 7 || z == 3;
        int found = WorldRenderer.findLookdownZ(10, WorldRenderer.MAX_LOOKDOWN, col);
        assertEquals(7, found);
        assertEquals(3, 10 - found, "depth d = z - z'");
    }

    @Test
    void immediatelyBelowIsDepthOne() {
        int found = WorldRenderer.findLookdownZ(5, WorldRenderer.MAX_LOOKDOWN, z -> z == 4);
        assertEquals(4, found);
        assertEquals(1, 5 - found);
    }

    @Test
    void allAirColumnReturnsNone() {
        assertEquals(WorldRenderer.LOOKDOWN_NONE,
                WorldRenderer.findLookdownZ(10, WorldRenderer.MAX_LOOKDOWN, z -> false));
    }

    @Test
    void respectsMaxLookdown() {
        // Only cell drawn is 9 levels down (z=1) — one past the 8-level cap — so nothing shows.
        int found = WorldRenderer.findLookdownZ(10, WorldRenderer.MAX_LOOKDOWN, z -> z == 1);
        assertEquals(WorldRenderer.LOOKDOWN_NONE, found);
    }

    @Test
    void findsCellExactlyAtTheMaxLookdownBoundary() {
        // Drawn cell exactly MAX_LOOKDOWN below (z=2, depth 8) is the deepest still reachable.
        int found = WorldRenderer.findLookdownZ(10, WorldRenderer.MAX_LOOKDOWN, z -> z == 2);
        assertEquals(2, found);
        assertEquals(WorldRenderer.MAX_LOOKDOWN, 10 - found);
    }

    @Test
    void neverProbesBelowTheWorldFloor() {
        // Camera at z=3, drawn cell at floor z=0: found, depth 3, and no negative z is probed.
        int[] minProbed = {Integer.MAX_VALUE};
        IntPredicate tracking = z -> {
            minProbed[0] = Math.min(minProbed[0], z);
            return z == 0;
        };
        int found = WorldRenderer.findLookdownZ(3, WorldRenderer.MAX_LOOKDOWN, tracking);
        assertEquals(0, found);
        assertTrue(minProbed[0] >= 0, "probed a negative z: " + minProbed[0]);
    }

    @Test
    void cameraAtFloorProbesNothing() {
        // z=0: the loop starts at z'=-1 which is already below the floor, so no probe runs.
        IntPredicate throwing = z -> {
            throw new AssertionError("probed z=" + z + " below a z=0 camera");
        };
        assertEquals(WorldRenderer.LOOKDOWN_NONE,
                WorldRenderer.findLookdownZ(0, WorldRenderer.MAX_LOOKDOWN, throwing));
    }

    @Test
    void skipsEmptyLevelsToReachTheFirstDrawn() {
        // Air at 9,8,7; solid at 6. From z=10 the probe should walk past the air to z=6.
        int found = WorldRenderer.findLookdownZ(10, WorldRenderer.MAX_LOOKDOWN,
                column(false, false, false, false, false, false, true, false, false, false));
        assertEquals(6, found);
        assertEquals(4, 10 - found);
    }

    @Test
    void depthDimIsMonotoneNonIncreasingAndFloored() {
        float prev = WorldRenderer.depthDim(1);
        assertTrue(prev < 1f, "depth 1 already dims below full brightness");
        for (int d = 2; d <= WorldRenderer.MAX_LOOKDOWN; d++) {
            float dim = WorldRenderer.depthDim(d);
            assertTrue(dim <= prev, "dim rose at depth " + d);
            assertTrue(dim >= 0.55f, "dim fell below the floor at depth " + d);
            prev = dim;
        }
    }
}
