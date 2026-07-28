package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.sim.actor.Actor;
import java.util.ArrayList;
import java.util.List;
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
        // S7 slice 2 re-bless (deliberate, behavioural): this was 2. The second K21 watch
        // was bound to the Rise by being handed the sergeant's OWN anchor cell — the beat
        // loop's first element was PATROL_RISE_TOP, set one line after the sergeant's
        // setAnchorCell(PATROL_RISE_TOP) — so two guards walked one route and worked one
        // 12x11 hall (17,480 pair-ticks at 60,000 ticks, the second-worst pair in the ward).
        // Duplicating an anchor is not how you staff a route: selectRouteStart's id-stagger
        // is applied once at bake and never re-asserted (Watch.Patrol.isComplete() is
        // hardcoded false), so unequal leg times free-run the pair back into lockstep inside
        // one loop. The sergeant walks the Rise; the second watch beats the Rise-head street's
        // east end. If a second soul is ever wanted ON the Rise it needs its own route or a
        // reversed waypoint order, not a copied cell.
        assertEquals(1, bound, "the K21 sergeant walks the Rise");
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

    /**
     * S7 slice 2, the permanent guard against the class of bug that cost 17,480 pair-ticks:
     * no two Watch may be baked onto the SAME work anchor. Two guards sharing an anchor
     * share a beat — the same route waypoints, or the same blind radius-6 square — and the
     * sim has no restoring force to separate them again once their legs drift into phase.
     * Ascending scan, no unordered iteration.
     */
    @Test
    void noTwoWatchShareAWorkAnchor() {
        var registry = population.registry();
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (!a.typeId().key().equals("militia_watch")) {
                continue;
            }
            for (int j = i + 1; j < registry.size(); j++) {
                Actor b = registry.get(j);
                if (!b.typeId().key().equals("militia_watch")) {
                    continue;
                }
                assertNotEquals(a.anchorCell(), b.anchorCell(),
                        "watch #" + i + " and watch #" + j + " share anchor "
                                + PackedPos.x(a.anchorCell()) + "," + PackedPos.y(a.anchorCell())
                                + "," + PackedPos.z(a.anchorCell())
                                + " -- they will walk the same beat forever");
            }
        }
    }

    /**
     * S7: the K34 garrison pair must not be baked onto the SAME anchor, and their beat
     * corners must be clear of the PRISON_CELLS_K34 cages.
     *
     * <p>An earlier draft of this test demanded the two radius-6 squares not intersect at
     * all, which forced both beats out of the building. Three configurations were measured
     * at 60,000 ticks and that one was the WORST on the metric that is the actual bug --
     * corridor-pinch pair-ticks 2,449 (S6 anchors) vs 5,417 (both outside) vs 7,185 (one
     * inside, one outside) -- because the guardhouse's one door opens into a 1-TALL lane,
     * so any beat centred outside drags its guard through that gut on every leg. The pair's
     * enormous raw adjacency turned out to be SLEEP: their on-duty wedge run is 64 ticks
     * against a 3,000-tick bar. The invariant that survives measurement is this one.
     */
    @Test
    void theGuardhouseGarrisonKeepsDistinctAnchorsClearOfTheCages() {
        var registry = population.registry();
        TileCursor cursor = loaded.world().cursor();
        List<Integer> anchors = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (!a.typeId().key().equals("militia_watch")) {
                continue;
            }
            int home = population.homes().get(a.homeId()).homeCell();
            if (PackedPos.z(home) == PackedPos.z(worldGuardhouse())
                    && Math.abs(PackedPos.x(home) - PackedPos.x(worldGuardhouse())) <= 1
                    && Math.abs(PackedPos.y(home) - PackedPos.y(worldGuardhouse())) <= 1) {
                anchors.add(a.anchorCell());
            }
        }
        assertEquals(2, anchors.size(), "the K34 garrison is a pair quartered at the post");
        assertNotEquals(anchors.get(0), anchors.get(1),
                "the garrison pair must not share one beat");
        // Slice 4's contract, pinned at the bake: every corner the square beat can settle on
        // is ground two guards can pass each other on, not a 1x1 cage.
        for (int anchor : anchors) {
            for (int leg = 0; leg < 4; leg++) {
                int corner = squareBeatCorner(anchor, leg, cursor);
                assertTrue(!com.trojia.sim.actor.CorridorPinch.isPinched(corner,
                                c -> Walkability.isWalkable(cursor.moveTo(c))),
                        "garrison beat corner " + PackedPos.x(corner) + ","
                                + PackedPos.y(corner) + " (anchor " + PackedPos.x(anchor) + ","
                                + PackedPos.y(anchor) + " leg " + leg
                                + ") is 1 cell wide -- a cage or an alley stub");
            }
        }
    }

    /** Mirrors JobBehaviors.retargetPatrolCorner for BEAT_RADIUS=6, PATROL_RETRY_BUDGET=8. */
    private static int squareBeatCorner(int anchor, int leg, TileCursor cursor) {
        int[] dx = {1, 1, -1, -1};
        int[] dy = {1, -1, -1, 1};
        com.trojia.sim.actor.Actor.WalkabilityQuery walk =
                c -> Walkability.isWalkable(cursor.moveTo(c));
        int fallback = -1;
        for (int r = 6; r >= 1; r--) {
            int candidate = PackedPos.pack(PackedPos.x(anchor) + dx[leg] * r,
                    PackedPos.y(anchor) + dy[leg] * r, PackedPos.z(anchor));
            if (!walk.isWalkable(candidate)) {
                continue;
            }
            if (!com.trojia.sim.actor.CorridorPinch.isPinched(candidate, walk)) {
                return candidate;
            }
            if (fallback < 0) {
                fallback = candidate;
            }
        }
        return fallback < 0 ? anchor : fallback;
    }

    private static int worldGuardhouse() {
        // K34_GUARDHOUSE {106,85} at ZA(+11), packed the way DocksPopulation.worldCell does.
        return PackedPos.pack(com.trojia.sim.world.Coords.CHUNK_SIZE_X + 106,
                com.trojia.sim.world.Coords.CHUNK_SIZE_Y + 85,
                com.trojia.sim.world.Coords.CHUNK_SIZE_Z + 11);
    }
}
