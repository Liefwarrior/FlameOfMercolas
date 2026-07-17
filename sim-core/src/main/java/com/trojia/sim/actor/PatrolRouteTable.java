package com.trojia.sim.actor;

import java.util.List;

/**
 * The baked ordered patrol-route side-table (law &amp; order pass, Pass 13): each route is a
 * fixed, ordered list of single-z waypoint cells (the Tarwalk / quay / Ropewynd markers),
 * injected through the {@link ActorsSystem} constructor exactly like {@link PrisonCellRegistry}
 * — immutable baked {@code int[][]}, never a runtime lane, rides no save. Purity-gate clean
 * (dense arrays, no {@code Map}/{@code Set}, no float).
 *
 * <p><b>Binding rule (draw-free).</b> A Watch actor is bound to a route by its {@code
 * anchorCell}: {@link #routeContaining(int)} returns the first (lowest-index) route whose
 * waypoint list contains that anchor. {@code JobBehaviors.pursueRoutePatrol} then walks the
 * waypoints in order, wrapping forever, reusing the persisted {@code goalProgress} as the
 * current waypoint index — no new persisted scalar. A Watch whose anchor is on no route keeps
 * the legacy square beat (the stationed shop/bank/roof guards).
 *
 * <p>{@link #EMPTY} is what the world-less bootstrap and route-free bakes inject: every
 * lookup misses and every Watch square-beats exactly as before this pass.
 */
public final class PatrolRouteTable {

    /** The degraded empty table the world-less/no-routes bake injects. */
    public static final PatrolRouteTable EMPTY = new PatrolRouteTable(new int[0][]);

    private final int[][] routes;

    public PatrolRouteTable(int[][] routes) {
        this.routes = new int[routes.length][];
        for (int i = 0; i < routes.length; i++) {
            this.routes[i] = routes[i].clone();
        }
    }

    /** Convenience over the boxed shape scenario bakes produce (ordered, bake order kept). */
    public static PatrolRouteTable of(List<List<Integer>> routeLists) {
        int[][] routes = new int[routeLists.size()][];
        for (int i = 0; i < routeLists.size(); i++) {
            List<Integer> route = routeLists.get(i);
            routes[i] = new int[route.size()];
            for (int j = 0; j < route.size(); j++) {
                routes[i][j] = route.get(j);
            }
        }
        return new PatrolRouteTable(routes);
    }

    /** Number of routes. */
    public int routeCount() {
        return routes.length;
    }

    /** Number of ordered waypoints on route {@code routeIndex}. */
    public int waypointCount(int routeIndex) {
        return routes[routeIndex].length;
    }

    /** The waypoint cell at {@code waypointIndex} on route {@code routeIndex} (bake order). */
    public int waypoint(int routeIndex, int waypointIndex) {
        return routes[routeIndex][waypointIndex];
    }

    /**
     * The first (lowest-index) route whose waypoint list contains {@code cell}, or {@code -1}
     * if no route does. Ascending fixed-order scan — deterministic.
     */
    public int routeContaining(int cell) {
        for (int r = 0; r < routes.length; r++) {
            for (int wp : routes[r]) {
                if (wp == cell) {
                    return r;
                }
            }
        }
        return -1;
    }
}
