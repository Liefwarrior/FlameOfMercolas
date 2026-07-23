package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.PatrolRouteTable;
import com.trojia.sim.actor.ZLinkTable;
import com.trojia.sim.actor.ZReachability;
import com.trojia.sim.actor.ZRouter;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.Walkability;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * S4 "the climb", bake acceptance: the docks' extracted {@link ZLinkTable} is real (the
 * authored stair pairs and the y96/y116 ramp rows), every patrol waypoint — including the
 * new cross-z Saltgate Rise route's — stands on walkable ground, {@link ZRouter} can plan
 * every band crossing of the Rise beat, and the K21 watch anchors bind to the appended
 * route. One bake, no soak.
 */
class DocksSaltgateRouteTest {

    private static FixtureWorldLoader.Loaded loaded;
    private static DocksPopulation population;
    private static ZLinkTable links;

    @BeforeAll
    static void bake() {
        loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        links = ZLinkTable.extract(loaded.world());
    }

    @Test
    void theExtractedConnectorTableIsRealAndSpansEveryInhabitedBandCrossing() {
        assertTrue(links.linkCount() >= 40,
                "the district authors dozens of stair pairs + two full ramp rows, found "
                        + links.linkCount());
        // Every crossing of the inhabited bands (z:+10 strand up to z:+14 roofs) has links.
        for (int mapZ = 10; mapZ <= 13; mapZ++) {
            int worldZ = com.trojia.sim.world.Coords.CHUNK_SIZE_Z + mapZ;
            assertTrue(links.anyLinkAtZ(worldZ),
                    "no connector crosses world z=" + worldZ + " (map z:+" + mapZ + ")");
        }
    }

    @Test
    void everyPatrolWaypointOfEveryRouteStandsOnWalkableGround() {
        PatrolRouteTable routes = PatrolRouteTable.of(DocksPopulation.patrolRoutes());
        TileCursor cursor = loaded.world().cursor();
        for (int r = 0; r < routes.routeCount(); r++) {
            for (int w = 0; w < routes.waypointCount(r); w++) {
                int cell = routes.waypoint(r, w);
                assertTrue(Walkability.isWalkable(cursor.moveTo(cell)),
                        "route " + r + " waypoint " + w + " at (" + PackedPos.x(cell) + ","
                                + PackedPos.y(cell) + "," + PackedPos.z(cell)
                                + ") is not walkable");
            }
        }
        assertEquals(4, routes.routeCount(), "the three S2 beats + the Saltgate Rise");
        assertEquals(3, routes.waypointCount(DocksPopulation.SALTGATE_ROUTE_INDEX));
    }

    @Test
    void zRouterPlansEveryBandCrossingOfTheSaltgateBeat() {
        PatrolRouteTable routes = PatrolRouteTable.of(DocksPopulation.patrolRoutes());
        int r = DocksPopulation.SALTGATE_ROUTE_INDEX;
        int count = routes.waypointCount(r);
        for (int w = 0; w < count; w++) {
            int from = routes.waypoint(r, w);
            int to = routes.waypoint(r, (w + 1) % count); // incl. the foot->head wrap climb
            if (PackedPos.z(from) == PackedPos.z(to)) {
                continue;
            }
            int hop = ZRouter.nextHop(from, to, links);
            assertNotEquals(Actor.NONE, hop, "no connector plan from waypoint " + w);
            // The chosen connector's near endpoint must itself be flood-reachable from the
            // waypoint (the honest-limitation guard: a planned-at connector across water
            // would strand the beat).
            ZReachability audit = ZReachability.flood(loaded.world(), links, from);
            assertTrue(audit.reachable(hop),
                    "the planned hop off waypoint " + w + " is not reachable from it");
            assertTrue(audit.reachable(to),
                    "waypoint " + ((w + 1) % count) + " is not reachable from waypoint " + w);
        }
    }

    @Test
    void theKn21WatchAnchorsBindToTheAppendedSaltgateRoute() {
        PatrolRouteTable routes = PatrolRouteTable.of(DocksPopulation.patrolRoutes());
        var registry = population.registry();
        int bound = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (!actor.typeId().key().equals("militia_watch")) {
                continue;
            }
            if (routes.routeContaining(actor.anchorCell())
                    == DocksPopulation.SALTGATE_ROUTE_INDEX) {
                bound++;
                assertEquals(com.trojia.sim.world.Coords.CHUNK_SIZE_Z + 13,
                        PackedPos.z(actor.anchorCell()),
                        "the Rise beat binds through the z:+13 head anchor");
            }
        }
        assertEquals(2, bound, "the K21 sergeant + the RISE_TOP beat watch walk the Rise");
        // The three single-z routes keep their original bindings (appended-last rule).
        int legacyBound = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            int route = routes.routeContaining(actor.anchorCell());
            if (route >= 0 && route < DocksPopulation.SALTGATE_ROUTE_INDEX
                    && actor.typeId().key().equals("militia_watch")) {
                legacyBound++;
            }
        }
        assertEquals(4, legacyBound, "the four S2 patrollers keep their beats");
    }
}
