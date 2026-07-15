package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link Actor#stepAlongRoute(int, boolean, Actor.WalkabilityQuery)}: the route-following mover
 * built on top of {@link PathFinder}. Covers the cache lifecycle {@code stepAlongRoute} itself
 * owns — cache hit (steady-state, no re-search), replan on target change, invalidation on a
 * stale (post-teleport) adjacency, and the bounded retry-cooldown after a failed search — plus a
 * genuine reproduction of the diagnosis's traced stuck cases that the greedy
 * {@link Actor#stepToward(int, boolean, Actor.WalkabilityQuery)} could never escape.
 */
final class ActorStepAlongRouteTest {

    private static final int Z = 7;

    private static Actor serfAt(ActorRegistry registry, int x, int y) {
        ActorTypeStats stats = ActorTestFixtures.statsWithSpeedAndLeash(Serf.TYPE, true, 1, 200);
        return registry.spawn(Serf.TYPE, stats, PackedPos.pack(x, y, Z));
    }

    /** Counts every {@code isWalkable} call so tests can prove a cache hit does not re-search. */
    private static final class CountingQuery implements Actor.WalkabilityQuery {
        private final Actor.WalkabilityQuery delegate;
        private int calls;

        CountingQuery(Actor.WalkabilityQuery delegate) {
            this.delegate = delegate;
        }

        @Override
        public boolean isWalkable(int cell) {
            calls++;
            return delegate.isWalkable(cell);
        }
    }

    private static Actor.WalkabilityQuery wallGapAtRow(int wallY, int gapX, int wallWidth) {
        return cell -> {
            int x = PackedPos.x(cell);
            int y = PackedPos.y(cell);
            return y != wallY || x == gapX || x < 0 || x >= wallWidth;
        };
    }

    // ======================================================================
    // Cache hit: the second (and later) call for the same unchanged target
    // must not re-run the search — only the one-hop step cost is paid.
    // ======================================================================

    @Test
    void aCacheHitDoesNotReRunTheSearch() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 5, 5);
        int target = PackedPos.pack(5, 15, Z);
        CountingQuery walk = new CountingQuery(wallGapAtRow(10, 10, 10));

        actor.stepAlongRoute(target, false, walk); // first call: triggers the search + first hop
        int callsAfterFirst = walk.calls;
        assertTrue(callsAfterFirst > 5, "the first call must have run a real search");

        walk.calls = 0;
        actor.stepAlongRoute(target, false, walk); // steady-state: cached route, one more hop only
        assertTrue(walk.calls <= 5,
                "a cache hit must cost only the single next hop's walkability checks, was " + walk.calls);
    }

    // ======================================================================
    // Replan on target change: switching targets mid-route must not keep
    // walking toward the old (now-irrelevant) cached waypoint.
    // ======================================================================

    @Test
    void changingTheTargetForcesAReplanRatherThanContinuingTheOldRoute() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0);
        Actor.WalkabilityQuery openGround = cell -> true;
        int targetA = PackedPos.pack(20, 0, Z);
        int targetB = PackedPos.pack(0, 20, Z);

        for (int i = 0; i < 5; i++) {
            actor.stepAlongRoute(targetA, false, openGround);
        }
        assertTrue(PackedPos.x(actor.cell()) > 0, "must have made progress toward targetA");
        assertEquals(0, PackedPos.y(actor.cell()), "must not have drifted off the x-axis toward targetA");

        for (int i = 0; i < 40 && actor.cell() != targetB; i++) {
            actor.stepAlongRoute(targetB, false, openGround);
        }
        assertEquals(targetB, actor.cell(), "must have reached the new target, not stalled on the old route");
    }

    // ======================================================================
    // Invalidate on stale adjacency: a teleport (Play mode, save/load) that
    // breaks the cached waypoint's adjacency must force a replan, not a
    // silent walk toward a now-meaningless stale cell.
    // ======================================================================

    @Test
    void aTeleportThatBreaksCachedAdjacencyForcesAReplan() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0);
        Actor.WalkabilityQuery openGround = cell -> true;
        int target = PackedPos.pack(10, 10, Z);

        actor.stepAlongRoute(target, false, openGround); // establishes the cache, takes one hop

        // Simulate an out-of-band teleport (Play mode input / save-load) far from the cached
        // route's next waypoint, breaking the chebyshev(...) == 1 adjacency guard.
        actor.setCell(PackedPos.pack(30, 30, Z));

        for (int i = 0; i < 60 && actor.cell() != target; i++) {
            actor.stepAlongRoute(target, false, openGround);
        }
        assertEquals(target, actor.cell(), "must replan from the teleported position and still arrive");
    }

    // ======================================================================
    // Retry cooldown: a failed search is not retried every tick.
    // ======================================================================

    @Test
    void aFailedSearchCoolsDownRatherThanRetryingEveryTick() {
        ActorRegistry registry = new ActorRegistry();
        Actor actor = serfAt(registry, 0, 0);
        int tx = 50;
        int ty = 50;
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
        java.util.Set<Integer> blocked = new java.util.HashSet<>();
        for (int cell : ring) {
            blocked.add(cell);
        }
        CountingQuery walk = new CountingQuery(cell -> !blocked.contains(cell));
        int target = PackedPos.pack(tx, ty, Z);

        actor.stepAlongRoute(target, false, walk); // first call: search runs and fails (unreachable)
        int startCell = actor.cell();
        assertTrue(walk.calls > 0, "the first call must have attempted a real search");

        walk.calls = 0;
        for (int attempt = 0; attempt < 5; attempt++) {
            actor.stepAlongRoute(target, false, walk);
        }
        assertEquals(0, walk.calls, "while cooling down, no further search may run at all");
        assertEquals(startCell, actor.cell(), "a failed, cooling-down target must be a deterministic no-op");
    }
}
