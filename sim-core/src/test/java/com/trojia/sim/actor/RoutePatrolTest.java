package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The REAL waypoint patrol (law &amp; order pass, Pass 13): a Watch whose anchor sits on a baked
 * {@link PatrolRouteTable} route walks that route's ordered waypoints and loops forever; an
 * A*-unreachable waypoint is skipped via the route-failure cache instead of freezing the beat;
 * a Watch on no route keeps the legacy square beat. All single-z.
 */
final class RoutePatrolTest {

    private static final int Z = 11;

    private static int cell(int x, int y) {
        return PackedPos.pack(x, y, Z);
    }

    /** Ctx double with a synthetic route table (and an optional wall set). */
    private static final class RouteContext extends NoOpActorContext {
        private PatrolRouteTable routes = PatrolRouteTable.EMPTY;
        private final List<Integer> walls = new ArrayList<>();

        RouteContext(ActorRegistry registry) {
            super(registry);
        }

        @Override
        public PatrolRouteTable patrolRoutes() {
            return routes;
        }

        @Override
        public boolean isWalkable(int c) {
            return !walls.contains(c);
        }
    }

    private static Actor spawnBoundWatch(ActorRegistry registry, ActorContext ctx, int at) {
        Actor watch = registry.spawn(MilitiaWatch.TYPE,
                ActorTestFixtures.stats(MilitiaWatch.TYPE), at);
        watch.setJobOrdinal((short) ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
        return watch;
    }

    private static Job patrolJob(ActorContext ctx) {
        return ctx.jobs().get(ctx.jobs().ordinalOf(Job.Watch.Patrol.ID));
    }

    @Test
    void followsTheWaypointsInOrderAndLoops() {
        ActorRegistry registry = new ActorRegistry();
        RouteContext ctx = new RouteContext(registry);
        int w1 = cell(10, 10);
        int w2 = cell(14, 10);
        int w3 = cell(14, 14);
        ctx.routes = new PatrolRouteTable(new int[][] {{w1, w2, w3}});
        Actor watch = spawnBoundWatch(registry, ctx, w1); // anchor == spawn == w1: route-bound
        Job patrol = patrolJob(ctx);

        // Record each waypoint ARRIVAL in visit order: standing on wp when goalProgress still
        // points at it is exactly the arrival tick (the next pursue advances the index).
        List<Integer> arrivals = new ArrayList<>();
        for (int i = 0; i < 60; i++) {
            int index = Math.floorMod(watch.goalProgress(), 3);
            int waypoint = ctx.patrolRoutes().waypoint(0, index);
            if (watch.cell() == waypoint) {
                arrivals.add(waypoint);
            }
            patrol.pursue(watch, ctx);
        }

        assertTrue(arrivals.size() >= 4, "several legs must have completed, saw " + arrivals);
        assertEquals(List.of(w1, w2, w3, w1), arrivals.subList(0, 4),
                "waypoints visited IN ORDER, wrapping back to the first (the loop)");
    }

    @Test
    void anUnreachableWaypointIsSkippedInsteadOfFreezingTheBeat() {
        ActorRegistry registry = new ActorRegistry();
        RouteContext ctx = new RouteContext(registry);
        int w1 = cell(10, 10);
        int w2 = cell(14, 10); // will be walled off entirely
        int w3 = cell(10, 14);
        ctx.routes = new PatrolRouteTable(new int[][] {{w1, w2, w3}});
        ctx.walls.add(w2); // an unwalkable target: every A* search to it fails
        Actor watch = spawnBoundWatch(registry, ctx, w1);
        Job patrol = patrolJob(ctx);

        patrol.pursue(watch, ctx); // at w1: arrival, advance to leg w2
        assertEquals(1, Math.floorMod(watch.goalProgress(), 3));
        patrol.pursue(watch, ctx); // leg w2: search fails -> route-failure cache -> skip
        assertEquals(2, Math.floorMod(watch.goalProgress(), 3),
                "the unreachable waypoint is skipped via the route-failure cache");

        // ...and the beat carries on: the watch actually reaches w3.
        for (int i = 0; i < 30 && watch.cell() != w3; i++) {
            patrol.pursue(watch, ctx);
        }
        assertEquals(w3, watch.cell(), "the loop continues past the dead leg");
    }

    @Test
    void aWatchWhoseAnchorIsOnNoRouteKeepsTheLegacySquareBeat() {
        ActorRegistry registry = new ActorRegistry();
        RouteContext ctx = new RouteContext(registry);
        int w1 = cell(40, 40);
        ctx.routes = new PatrolRouteTable(new int[][] {{cell(10, 10), cell(14, 10)}});
        Actor stationed = spawnBoundWatch(registry, ctx, cell(80, 80)); // anchor off-route
        Job patrol = patrolJob(ctx);

        for (int i = 0; i < 40; i++) {
            patrol.pursue(stationed, ctx);
        }
        // The square beat's corners are anchor +- BEAT_RADIUS: the stationed guard stays
        // leashed to its post's neighbourhood instead of marching off to the distant route.
        assertTrue(ActorGeometry.chebyshev(stationed.cell(), cell(80, 80)) <= 6,
                "stationed guards keep the legacy post beat");
        assertTrue(ActorGeometry.chebyshev(stationed.cell(), w1) > 6,
                "…and never adopt someone else's route");
    }
}
