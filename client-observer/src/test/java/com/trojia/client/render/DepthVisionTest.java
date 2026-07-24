package com.trojia.client.render;

import org.junit.jupiter.api.Test;

import java.util.function.IntPredicate;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Headless tests for the depth-vision brain (Sprint 4 EPIC — depth-visible actors):
 * {@link DepthVision#resolveThrough} (the occlusion-aware column resolution the actor
 * pass, picker fallback and hover plates all share), {@link DepthVision#shade} (the one
 * dim + cool-haze curve, asserted in lockstep with the terrain look-down's math), and
 * {@link DepthVision#blurLevelFor} (the pyramid-level policy). All pure functions —
 * synthetic columns, no world, no GL.
 */
class DepthVisionTest {

    /** A synthetic z-stack: {@code drawable[z]} is whether the cell at that z draws. */
    private static IntPredicate column(boolean... drawable) {
        return z -> z >= 0 && z < drawable.length && drawable[z];
    }

    // ------------------------------------------------------------- resolveThrough

    @Test
    void occludedColumnResolvesToNoneEvenWithADrawnCellBelow() {
        // The view-z cell itself draws (a floor under the camera) — no look-down happens
        // there, so nothing below is depth-visible no matter what the column holds.
        IntPredicate col = z -> z == 10 || z == 7;
        assertEquals(DepthSight.NONE, DepthVision.resolveThrough(10, col));
    }

    @Test
    void openColumnResolvesToTheNearestDrawnCell() {
        IntPredicate col = z -> z == 7 || z == 3;
        assertEquals(7, DepthVision.resolveThrough(10, col),
                "the nearer drawn cell wins — exactly what the terrain look-down draws");
    }

    @Test
    void immediatelyBelowIsVisible() {
        assertEquals(4, DepthVision.resolveThrough(5, z -> z == 4));
    }

    @Test
    void allAirColumnResolvesToNone() {
        assertEquals(DepthSight.NONE, DepthVision.resolveThrough(10, z -> false));
    }

    @Test
    void beyondMaxLookdownResolvesToNone() {
        // Drawn cell 9 levels down — one past the terrain pass's 8-level cap: invisible,
        // so an actor standing there must not render either.
        assertEquals(DepthSight.NONE, DepthVision.resolveThrough(10, z -> z == 1));
    }

    @Test
    void exactlyAtMaxLookdownResolves() {
        assertEquals(2, DepthVision.resolveThrough(10, z -> z == 2));
    }

    @Test
    void agreesWithTheTerrainLookdownOnEveryOpenColumn() {
        // The invariant the whole feature hangs on: on a non-occluded column the actor
        // pass's resolution IS the terrain pass's findLookdownZ — actor and floor land on
        // the same z', so the figure always stands on the ground the player sees.
        for (int floorZ = 0; floorZ < 10; floorZ++) {
            final int f = floorZ;
            IntPredicate col = z -> z == f;
            int viewZ = 10;
            assertEquals(
                    WorldRenderer.findLookdownZ(viewZ, WorldRenderer.MAX_LOOKDOWN, col),
                    DepthVision.resolveThrough(viewZ, col),
                    "floor at z=" + f);
        }
    }

    // ----------------------------------------------------------------- shade

    @Test
    void shadeBrightnessIsTheTerrainDepthDimCurve() {
        for (int d = 1; d <= DepthVision.MAX_LOOKDOWN; d++) {
            DepthVision.Shade s = DepthVision.shade(d);
            assertEquals(WorldRenderer.depthDim(d), s.b(), 0f,
                    "blue carries the pure depthDim (no cool pull) at depth " + d);
        }
    }

    @Test
    void shadeIsCoolBiased() {
        // The haze pulls red hardest, green half as much, blue not at all — so the triple
        // is monotone r <= g <= b at every depth (the "recessed, slightly cold" read).
        for (int d = 1; d <= DepthVision.MAX_LOOKDOWN; d++) {
            DepthVision.Shade s = DepthVision.shade(d);
            assertTrue(s.r() <= s.g(), "red exceeds green at depth " + d);
            assertTrue(s.g() <= s.b(), "green exceeds blue at depth " + d);
            assertTrue(s.r() > 0f, "shade went black at depth " + d);
        }
    }

    @Test
    void shadeIsMonotoneNonIncreasingWithDepth() {
        DepthVision.Shade prev = DepthVision.shade(1);
        assertTrue(prev.b() < 1f, "depth 1 already dims");
        for (int d = 2; d <= DepthVision.MAX_LOOKDOWN; d++) {
            DepthVision.Shade s = DepthVision.shade(d);
            assertTrue(s.r() <= prev.r() && s.g() <= prev.g() && s.b() <= prev.b(),
                    "shade brightened at depth " + d);
            prev = s;
        }
    }

    // ------------------------------------------------------------- blurLevelFor

    @Test
    void blurLevelMatchesTheTileAtlasPolicy() {
        int levels = 5; // the shipped SheetTileAtlas pyramid depth
        assertEquals(0, DepthVision.blurLevelFor(1, levels), "nearest depth stays sharp");
        assertEquals(1, DepthVision.blurLevelFor(2, levels));
        assertEquals(3, DepthVision.blurLevelFor(4, levels));
        assertEquals(4, DepthVision.blurLevelFor(8, levels), "clamps at the pyramid top");
    }

    @Test
    void sharpOnlyPyramidAlwaysPicksLevelZero() {
        for (int d = 1; d <= DepthVision.MAX_LOOKDOWN; d++) {
            assertEquals(0, DepthVision.blurLevelFor(d, 1));
        }
    }
}
