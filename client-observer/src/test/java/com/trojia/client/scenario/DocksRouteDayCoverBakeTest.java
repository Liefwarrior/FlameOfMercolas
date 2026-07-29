package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.PatrolRouteTable;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * EVERY BAKED PATROL ROUTE KEEPS A DAY-SHIFT WALKER.
 *
 * <p>This test exists because the night roster took one. Round 2 moved the Tarwalk-west hand
 * onto {@code watch.nightwatch} — and that soul was route 1's ONLY walker, so from the moment
 * the roster landed the quay and berth apron had no daytime Watch on it at all. Nothing went
 * red. The soak report has no per-route line; the bake test checked that anchors BIND to routes,
 * which they still did; the throughput proof watches route 3 and nothing else. A ward-wide
 * coverage median moved a little and was read as noise.
 *
 * <p>That is the same shape of blind spot {@link DocksSaltgateThroughputTest} was written to
 * close on the climb, reopened on the flat. The rule it pins is deliberately narrow and
 * absolute: <b>a route may be thinned by the roster, never emptied.</b> Rostering one of route
 * 0's two hands is a decision; rostering route 1's only hand is a hole, and the difference is
 * mechanical, so a test can tell them apart.
 *
 * <p>Pure bake — no soak, no ticks. It reads the spawned population's anchors and bound jobs,
 * which is exactly where the mistake was made.
 */
class DocksRouteDayCoverBakeTest {

    private static DocksPopulation population;
    private static PatrolRouteTable routes;
    private static int dayOrdinal;
    private static int nightOrdinal;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        routes = PatrolRouteTable.of(DocksPopulation.patrolRoutes());
        dayOrdinal = population.jobs().ordinalOf(Job.Watch.Patrol.ID);
        nightOrdinal = population.jobs().ordinalOf(Job.Watch.NightWatch.ID);
    }

    /** Ids of the Watch bound to {@code route} (anchor sits on one of its waypoints). */
    private static List<Integer> boundTo(int route, int jobOrdinal) {
        ActorRegistry registry = population.registry();
        List<Integer> ids = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (!a.typeId().key().equals("militia_watch")) {
                continue;
            }
            if (routes.routeContaining(a.anchorCell()) == route
                    && (jobOrdinal < 0 || a.jobOrdinal() == jobOrdinal)) {
                ids.add(i);
            }
        }
        return ids;
    }

    @Test
    void everyRouteThatHasAWalkerAtAllKeepsOneOnTheDayShift() {
        int routeCount = DocksPopulation.patrolRoutes().size();
        for (int r = 0; r < routeCount; r++) {
            List<Integer> all = boundTo(r, -1);
            if (all.isEmpty()) {
                continue; // an unwalked authored route is a different (bake-authoring) question
            }
            List<Integer> day = boundTo(r, dayOrdinal);
            assertTrue(!day.isEmpty(),
                    "route " + r + " has walkers " + all + " but NONE of them is on the day"
                            + " shift -- the night roster emptied it. A route may be thinned"
                            + " by the roster, never emptied: the ward would have that beat"
                            + " unpatrolled through every daylight hour and no report line"
                            + " would say so. Night-rostered on this route: "
                            + boundTo(r, nightOrdinal));
        }
    }

    @Test
    void theQuayRouteSpecificallyStillHasItsDayWalker() {
        // The exact regression, named. Route 1 is the quay/berth apron; it carries one walker
        // and one only, so it is the route with no slack at all -- rostering its hand is the
        // single edit that can empty a beat in this bake, and round 2 made it.
        List<Integer> day = boundTo(1, dayOrdinal);
        assertEquals(1, day.size(),
                "route 1 (the quay/berth apron) is a ONE-man beat; its day walker is " + day
                        + ". Round 2 rostered that man to nights and the apron went dark from"
                        + " dawn to dusk.");
    }

    @Test
    void theRosterOnlyEverThinsADoubledBeatOrAStationedPost() {
        // The constructive half: state, per rostered soul, what it cost the day. Either the
        // soul was not route-bound at all (a stationed post -- the roof deck, a shop door), or
        // its route keeps at least one other day walker.
        ActorRegistry registry = population.registry();
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (a.jobOrdinal() != nightOrdinal) {
                continue;
            }
            int route = routes.routeContaining(a.anchorCell());
            if (route < 0) {
                continue; // stationed post: no route to empty
            }
            List<Integer> day = boundTo(route, dayOrdinal);
            assertTrue(day.size() >= 1,
                    "night-roster actor#" + i + " (anchor " + xyz(a.anchorCell())
                            + ") walks route " + route + ", which is left with " + day.size()
                            + " day walkers");
        }
    }

    private static String xyz(int cell) {
        return "(" + PackedPos.x(cell) + "," + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")";
    }
}
