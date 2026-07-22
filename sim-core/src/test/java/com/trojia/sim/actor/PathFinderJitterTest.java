package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.Arrays;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The per-actor route-variety jitter (density revisit, "unique paths per person"): different
 * actor salts must yield visibly different — but still near-optimal — routes between the same
 * endpoints on open ground, while the same salt is twin-run reproducible and salt 0 stays the
 * legacy unjittered search.
 */
final class PathFinderJitterTest {

    private static final int Z = 11;
    private static final Actor.WalkabilityQuery OPEN = cell -> true;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    @Test
    void differentSaltsProduceDifferentRoutesOnOpenGround() {
        int start = cell(10, 10);
        int target = cell(40, 24);
        // On fully open ground many equal-cost near-optimal routes exist; the jitter must make
        // distinct actors pick distinct ones. A few salts might coincide by chance, so require
        // at least one differing pair across a handful of salts (deterministic, not flaky).
        int[] a = PathFinder.findRoute(start, target, OPEN, PathFinder.DEFAULT_MAX_NODES, 1);
        int[] b = PathFinder.findRoute(start, target, OPEN, PathFinder.DEFAULT_MAX_NODES, 2);
        int[] c = PathFinder.findRoute(start, target, OPEN, PathFinder.DEFAULT_MAX_NODES, 3);
        assertNotNull(a);
        assertNotNull(b);
        assertNotNull(c);
        boolean anyDiffer = !Arrays.equals(a, b) || !Arrays.equals(b, c) || !Arrays.equals(a, c);
        assertTrue(anyDiffer, "three salted actors on the same endpoints must not all share "
                + "one ruler-line route");
    }

    @Test
    void sameSaltIsTwinRunIdentical() {
        int start = cell(10, 10);
        int target = cell(40, 24);
        int[] first = PathFinder.findRoute(start, target, OPEN, PathFinder.DEFAULT_MAX_NODES, 17);
        int[] second = PathFinder.findRoute(start, target, OPEN, PathFinder.DEFAULT_MAX_NODES, 17);
        assertArrayEquals(first, second, "a route is a pure function of (salt, start, target, walk)");
    }

    @Test
    void jitteredRoutesStayNearOptimal() {
        int start = cell(10, 10);
        int target = cell(40, 24);
        int[] base = PathFinder.findRoute(start, target, OPEN, PathFinder.DEFAULT_MAX_NODES);
        for (int salt = 1; salt <= 8; salt++) {
            int[] jittered = PathFinder.findRoute(start, target, OPEN,
                    PathFinder.DEFAULT_MAX_NODES, salt);
            assertNotNull(jittered);
            // Jitter is 0..3 per entered cell vs a 10/14 base: hop count can grow only where a
            // detour is genuinely cheaper under the perturbed costs — bounded to a whisker.
            assertTrue(jittered.length <= base.length + 4,
                    "salt " + salt + " route ballooned: " + jittered.length + " vs " + base.length);
        }
    }

    @Test
    void saltZeroIsByteIdenticalToTheUnsaltedOverload() {
        int start = cell(5, 5);
        int target = cell(30, 12);
        assertArrayEquals(
                PathFinder.findRoute(start, target, OPEN, PathFinder.DEFAULT_MAX_NODES),
                PathFinder.findRoute(start, target, OPEN, PathFinder.DEFAULT_MAX_NODES, 0),
                "salt 0 is the legacy no-jitter sentinel");
    }
}
