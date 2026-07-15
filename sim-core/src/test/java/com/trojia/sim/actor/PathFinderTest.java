package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.HashSet;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link PathFinder}'s bounded, deterministic 8-directional A* (the design's
 * "real, bounded, deterministic pathfinding" bar): routes around an obstacle
 * wider than the old greedy wall-slide's one-cell retry could ever escape,
 * gives up fast and deterministically on a genuinely unreachable target, and
 * never cuts a solid diagonal wall corner.
 */
final class PathFinderTest {

    private static final int Z = 3;

    /** A query that reports every cell walkable except an explicit blocked set. */
    private static Actor.WalkabilityQuery blocking(int... blockedCells) {
        Set<Integer> blocked = new HashSet<>();
        for (int c : blockedCells) {
            blocked.add(c);
        }
        return cell -> !blocked.contains(cell);
    }

    // ======================================================================
    // Correctness: routes around an obstacle the old wall-slide could not
    // (a 10-wide wall segment — far beyond the wall-slide's single-cell
    // orthogonal retry, reproducing the diagnosis's "3-wide interior table
    // spanning the only corridor" and "hovel door on the wrong wall" class).
    // ======================================================================

    @Test
    void routesAroundAWideObstacleTheOldWallSlideCouldNotEscape() {
        // A horizontal wall at y=10 spans x in [0,9] solid, with the only gap at x=10.
        int[] wall = new int[10];
        for (int x = 0; x < 10; x++) {
            wall[x] = PackedPos.pack(x, 10, Z);
        }
        Actor.WalkabilityQuery walk = blocking(wall);
        int start = PackedPos.pack(5, 5, Z);
        int target = PackedPos.pack(5, 15, Z);

        int[] route = PathFinder.findRoute(start, target, walk, PathFinder.DEFAULT_MAX_NODES);

        assertNotNull(route, "a route around the wide wall must exist");
        assertTrue(route.length > 0);
        assertEquals(target, route[route.length - 1], "the route must end exactly at the target");
        for (int cell : route) {
            assertTrue(walk.isWalkable(cell), "every waypoint must be walkable, cell " + describe(cell));
        }
        // A direct Chebyshev walk would be 10 steps; the detour around a 10-wide wall must be longer.
        assertTrue(route.length > 10, "the detour must be longer than the blocked direct line, was "
                + route.length);
    }

    // ======================================================================
    // Determinism: identical inputs -> byte-identical route, every time.
    // ======================================================================

    @Test
    void sameStartAndTargetAlwaysProduceTheSameRoute() {
        int[] wall = new int[10];
        for (int x = 0; x < 10; x++) {
            wall[x] = PackedPos.pack(x, 10, Z);
        }
        Actor.WalkabilityQuery walk = blocking(wall);
        int start = PackedPos.pack(5, 5, Z);
        int target = PackedPos.pack(5, 15, Z);

        int[] first = PathFinder.findRoute(start, target, walk, PathFinder.DEFAULT_MAX_NODES);
        for (int attempt = 0; attempt < 10; attempt++) {
            int[] again = PathFinder.findRoute(start, target, walk, PathFinder.DEFAULT_MAX_NODES);
            assertArrayEquals(first, again, "every independent search must reproduce the identical route");
        }
    }

    // ======================================================================
    // Bounded failure: a genuinely unreachable target fails fast, never hangs.
    // ======================================================================

    @Test
    void aTargetFullyEnclosedByWallsIsUnreachableAndFailsFast() {
        // Target (20,20) is boxed in on all 8 sides by unwalkable cells — genuinely unreachable.
        int tx = 20;
        int ty = 20;
        int[] ring = new int[8];
        int i = 0;
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                ring[i++] = PackedPos.pack(tx + dx, ty + dy, Z);
            }
        }
        Actor.WalkabilityQuery walk = blocking(ring);
        int start = PackedPos.pack(0, 0, Z);
        int target = PackedPos.pack(tx, ty, Z);

        long startNanos = System.nanoTime();
        int[] route = PathFinder.findRoute(start, target, walk, PathFinder.DEFAULT_MAX_NODES);
        long elapsedMillis = (System.nanoTime() - startNanos) / 1_000_000;

        assertNull(route, "a fully enclosed target must be unreachable");
        assertTrue(elapsedMillis < 5_000, "a bounded search must fail fast, took " + elapsedMillis + "ms");
    }

    @Test
    void exhaustingTheNodeBudgetGivesUpDeterministicallyRatherThanHanging() {
        // Reachable in principle (a long winding corridor), but the node budget is set far
        // below what a full search would need — must return null promptly, not hang or scan
        // the whole window regardless of budget.
        int start = PackedPos.pack(0, 0, Z);
        int target = PackedPos.pack(60, 60, Z);
        Actor.WalkabilityQuery walk = cell -> true; // open ground, cheap to expand many nodes

        int[] route = PathFinder.findRoute(start, target, walk, 5);

        assertNull(route, "an exhausted node budget must give up deterministically, not succeed partially");
    }

    // ======================================================================
    // Corner-cutting prevention: a diagonal step is refused unless BOTH
    // flanking orthogonal cells are also walkable.
    // ======================================================================

    @Test
    void aDiagonalStepThroughASolidWallCornerIsNeverTaken() {
        // Two solid rectangular quadrants wall off every possible route between the start's
        // quadrant (x<=0,y<=0) and the target's quadrant (x>=1,y>=1) EXCEPT the single
        // diagonal pair (0,0)-(1,1) itself: any other adjacency between the two quadrants
        // would require passing through (1,0) or (0,1), both blocked here. If (and only if)
        // corner-cutting were incorrectly permitted, the target would be reachable in one
        // diagonal hop; with the rule correctly enforced, the two quadrants are provably
        // fully disconnected, so the target must be unreachable.
        Actor.WalkabilityQuery walk = cell -> {
            int x = PackedPos.x(cell);
            int y = PackedPos.y(cell);
            boolean flankF1 = x >= 1 && y <= 0; // bottom-right quadrant, includes (1,0)
            boolean flankF2 = x <= 0 && y >= 1; // top-left quadrant, includes (0,1)
            return !(flankF1 || flankF2);
        };
        int start = PackedPos.pack(0, 0, Z);
        int target = PackedPos.pack(1, 1, Z);

        int[] route = PathFinder.findRoute(start, target, walk, PathFinder.DEFAULT_MAX_NODES);

        assertNull(route, "cutting a solid diagonal wall corner must never be permitted");
    }

    // ======================================================================
    // Boundary/degenerate cases.
    // ======================================================================

    @Test
    void sameStartAndTargetIsAnEmptyRoute() {
        int cell = PackedPos.pack(5, 5, Z);
        int[] route = PathFinder.findRoute(cell, cell, cell2 -> true, PathFinder.DEFAULT_MAX_NODES);
        assertNotNull(route);
        assertEquals(0, route.length);
    }

    @Test
    void crossZTargetIsRejectedImmediately() {
        int start = PackedPos.pack(5, 5, 1);
        int target = PackedPos.pack(5, 5, 2);
        int[] route = PathFinder.findRoute(start, target, cell -> true, PathFinder.DEFAULT_MAX_NODES);
        assertNull(route, "actors move on one z-level only — a cross-z target is out of scope");
    }

    @Test
    void anUnwalkableTargetIsRejectedImmediately() {
        int start = PackedPos.pack(0, 0, Z);
        int target = PackedPos.pack(5, 5, Z);
        int[] route = PathFinder.findRoute(start, target, blocking(target), PathFinder.DEFAULT_MAX_NODES);
        assertNull(route, "an unwalkable target can never be a valid destination");
    }

    @Test
    void aSpanBeyondMaxDimIsOutOfScopeAndRejectedImmediately() {
        int start = PackedPos.pack(0, 0, Z);
        int target = PackedPos.pack(2000, 0, Z); // far beyond MAX_DIM's 384-tile safety valve
        int[] route = PathFinder.findRoute(start, target, cell -> true, PathFinder.DEFAULT_MAX_NODES);
        assertNull(route, "a span this large is out of scope by design, not a hang");
    }

    private static String describe(int cell) {
        return "(" + PackedPos.x(cell) + "," + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")";
    }
}
