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
import static org.junit.jupiter.api.Assertions.assertFalse;
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
        // ROUND-2 UN-BLESS (back to 2). S7 slice 2 moved the second K21 watch off the Rise
        // to break up a shared anchor, and it cost the ward HALF its cross-z patrol: the
        // 15,000-tick head/foot arrivals fell 224/243 -> 96/99 and this file's own boolean
        // proof still printed PASS. Staffing one route with two souls is not the bug —
        // that is what a beat with two men looks like — so the anchor is shared again and
        // the throughput floor is enforced for real by DocksSaltgateThroughputTest.
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

    /**
     * No two SQUARE-BEAT Watch may be baked onto the same work anchor.
     *
     * <p>Re-scoped in round 2. S7 slice 2 asserted this over EVERY Watch, which is wrong for
     * route-bound souls and is what pushed the second K21 watch off the Saltgate Rise and
     * halved its throughput. A patrol ROUTE is a shared object by design — two men on one
     * route is a staffed beat, they enter at staggered waypoints, and the measured cost of
     * pairing them is small next to the coverage they buy. A blind radius-6 SQUARE is the
     * opposite: two anchors on one cell produce two literally identical loops with no
     * restoring force to separate them ever again. The invariant is kept exactly where it
     * bites. Ascending scan, no unordered iteration.
     */
    @Test
    void noTwoSquareBeatWatchShareAWorkAnchor() {
        PatrolRouteTable routes = PatrolRouteTable.of(DocksPopulation.patrolRoutes());
        var registry = population.registry();
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (!a.typeId().key().equals("militia_watch")
                    || routes.routeContaining(a.anchorCell()) >= 0) {
                continue;
            }
            for (int j = i + 1; j < registry.size(); j++) {
                Actor b = registry.get(j);
                if (!b.typeId().key().equals("militia_watch")
                        || routes.routeContaining(b.anchorCell()) >= 0) {
                    continue;
                }
                assertNotEquals(a.anchorCell(), b.anchorCell(),
                        "square-beat watch #" + i + " and #" + j + " share anchor "
                                + PackedPos.x(a.anchorCell()) + "," + PackedPos.y(a.anchorCell())
                                + "," + PackedPos.z(a.anchorCell())
                                + " -- they will walk the same blind square forever");
            }
        }
    }

    /**
     * The K34 garrison pair must not be baked onto the SAME anchor, and slice 4's corner
     * rule must actually be honoured on their real beats.
     *
     * <p>Two earlier drafts of this test both pinned the BAKE instead of the RULE. The
     * first demanded the two radius-6 squares not intersect at all, which forced both beats
     * out of the building — measured WORST of three configurations at 60,000 ticks, because
     * the guardhouse's one door opens into a 1-TALL lane and any beat centred outside drags
     * its guard through that gut on every leg. The second demanded every corner be
     * un-pinched, which is a statement about the map, not about the code, and it only held
     * because slice 2 had moved the sergeant's anchor — content churn that measured worse
     * on the primary metric and was reverted in round 2.
     *
     * <p>What is asserted here is slice 4's actual contract, which is bake-independent: the
     * retarget PREFERS open ground and only falls back to a pinched corner when the whole
     * shrink budget on that leg offers nothing else. A pinched corner is therefore allowed
     * only where the guardhouse geometry leaves no alternative — and the count of such legs
     * is pinned, so a future bake cannot quietly add more.
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
        // Slice 4's contract: open ground WINS wherever the leg offers any, and a pinched
        // corner only ever survives as the documented last-resort fallback.
        com.trojia.sim.actor.Actor.WalkabilityQuery walk =
                c -> Walkability.isWalkable(cursor.moveTo(c));
        int legsWithNoOpenGround = 0;
        for (int anchor : anchors) {
            for (int leg = 0; leg < 4; leg++) {
                int corner = squareBeatCorner(anchor, leg, cursor);
                if (!com.trojia.sim.actor.CorridorPinch.isPinched(corner, walk)) {
                    continue;
                }
                legsWithNoOpenGround++;
                assertFalse(anyOpenGroundOnLeg(anchor, leg, cursor),
                        "garrison beat corner " + PackedPos.x(corner) + ","
                                + PackedPos.y(corner) + " (anchor " + PackedPos.x(anchor) + ","
                                + PackedPos.y(anchor) + " leg " + leg + ") is 1 cell wide even"
                                + " though the shrink budget on that leg DOES offer open ground"
                                + " -- the corner retarget is no longer preferring it");
            }
        }
        // The K34 watch room is a walled pocket: exactly one of the pair's eight legs has
        // nothing un-pinched anywhere in the retry budget. Pinned so a future bake cannot
        // quietly hand the garrison more cage legs while this test still says PASS.
        assertEquals(1, legsWithNoOpenGround,
                "garrison legs with no un-pinched corner available anywhere in budget");
    }

    /** Mirrors JobBehaviors.retargetPatrolCorner for BEAT_RADIUS=6, PATROL_RETRY_BUDGET=8. */
    private static int squareBeatCorner(int anchor, int leg, TileCursor cursor) {
        com.trojia.sim.actor.Actor.WalkabilityQuery walk =
                c -> Walkability.isWalkable(cursor.moveTo(c));
        int fallback = -1;
        for (int r = 6; r >= 1; r--) {
            int candidate = legCandidate(anchor, leg, r);
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

    /** Whether any radius in the shrink budget offers un-pinched ground on this leg. */
    private static boolean anyOpenGroundOnLeg(int anchor, int leg, TileCursor cursor) {
        com.trojia.sim.actor.Actor.WalkabilityQuery walk =
                c -> Walkability.isWalkable(cursor.moveTo(c));
        for (int r = 6; r >= 1; r--) {
            int candidate = legCandidate(anchor, leg, r);
            if (walk.isWalkable(candidate)
                    && !com.trojia.sim.actor.CorridorPinch.isPinched(candidate, walk)) {
                return true;
            }
        }
        return false;
    }

    private static int legCandidate(int anchor, int leg, int radius) {
        int[] dx = {1, 1, -1, -1};
        int[] dy = {1, -1, -1, 1};
        return PackedPos.pack(PackedPos.x(anchor) + dx[leg] * radius,
                PackedPos.y(anchor) + dy[leg] * radius, PackedPos.z(anchor));
    }

    private static int worldGuardhouse() {
        // K34_GUARDHOUSE {106,85} at ZA(+11), packed the way DocksPopulation.worldCell does.
        return PackedPos.pack(com.trojia.sim.world.Coords.CHUNK_SIZE_X + 106,
                com.trojia.sim.world.Coords.CHUNK_SIZE_Y + 85,
                com.trojia.sim.world.Coords.CHUNK_SIZE_Z + 11);
    }
}
