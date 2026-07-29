package com.trojia.client.scenario;

import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.PatrolRouteTable;

import java.util.ArrayList;
import java.util.List;

/**
 * The Saltgate Rise cross-z patrol proof — walkers, band trail and waypoint THROUGHPUT.
 *
 * <p>This exists because the old proof was a boolean and a boolean cannot see a collapse.
 * Its verdict was {@code visitedBothBands && headArrivals > 0 && footArrivals > 0}, so when
 * S7 slice 2 moved the second K21 watch's anchor off route 3's head waypoint — halving the
 * beat's staffing and dropping the 15,000-tick arrivals from 224/243 to 96/99 — the report
 * printed PASS and the regression shipped. One walker still visited all three bands, and one
 * arrival is still greater than zero.
 *
 * <p>Two kinds of guard replace it, and the split matters:
 *
 * <ul>
 *   <li><b>Horizon-free structural checks</b>, which the soak report itself now enforces on
 *       every run at any length: the beat is STAFFED ({@link #WALKER_FLOOR}), and every soul
 *       bound to the route actually reaches BOTH ends of the climb. Either one alone catches
 *       the S7 regression — staffing went 2 → 1 — and neither depends on how long the run is.
 *   <li><b>An absolute throughput floor at one pinned horizon</b>, enforced by
 *       {@link DocksSaltgateThroughputTest} at exactly {@link #FLOOR_HORIZON_TICKS} ticks.
 *       Arrival RATE is deliberately not used as the floor: it is not horizon-invariant here
 *       (the healthy beat measures ~154 head arrivals per 10,000 ticks over 15,000 ticks and
 *       ~65 over 60,000), so a rate floor loose enough for the long horizon is too loose to
 *       catch anything at the short one. A number at a fixed horizon has no such hole.
 * </ul>
 */
public final class SaltgateRiseProof {

    private SaltgateRiseProof() {
    }

    /**
     * The Rise is a two-man beat and must stay one. It was authored as two K21 watch on one
     * route (the sergeant plus a hand, entering at staggered waypoints); dropping to one is
     * precisely the regression this file is here to catch, and it is catchable without a soak.
     */
    public static final int WALKER_FLOOR = 2;

    /** The horizon the absolute arrival floors below are stated at. */
    public static final int FLOOR_HORIZON_TICKS = 15_000;

    /**
     * Head-waypoint (z:+13) arrivals over {@link #FLOOR_HORIZON_TICKS}. Measured at 232 for
     * the two-man beat; the regressed one-man beat managed 96. The floor sits between them
     * with room on both sides — well clear of ordinary variation, and nowhere near loose
     * enough to let a halved beat through.
     */
    public static final int HEAD_ARRIVALS_FLOOR = 150;

    /** Foot-waypoint (z:+11) arrivals over the same horizon: measured 192, regressed 99. */
    public static final int FOOT_ARRIVALS_FLOOR = 130;

    /** Arrivals per 10,000 ticks — reported for legibility, never used as a floor. */
    public static long per10k(long arrivals, long ticks) {
        return ticks <= 0 ? 0 : arrivals * 10_000L / ticks;
    }

    /** The souls whose work anchor binds them to the Saltgate route, ascending id. */
    public static List<Integer> walkers(ActorRegistry registry) {
        PatrolRouteTable routes = PatrolRouteTable.of(DocksPopulation.patrolRoutes());
        List<Integer> ids = new ArrayList<>();
        for (int i = 0; i < registry.size(); i++) {
            if (routes.routeContaining(registry.get(i).anchorCell())
                    == DocksPopulation.SALTGATE_ROUTE_INDEX) {
                ids.add(i);
            }
        }
        return ids;
    }

    /** The route's head waypoint (index 0) — the z:+13 end of the climb. */
    public static int headWaypoint() {
        return DocksPopulation.patrolRoutes().get(DocksPopulation.SALTGATE_ROUTE_INDEX).get(0);
    }

    /** The route's foot waypoint (index 2) — the z:+11 end of the climb. */
    public static int footWaypoint() {
        return DocksPopulation.patrolRoutes().get(DocksPopulation.SALTGATE_ROUTE_INDEX).get(2);
    }

    /**
     * The horizon-free verdict the soak report prints: the beat is staffed to strength, some
     * soul genuinely crossed all three bands, and EVERY soul bound to the route reached both
     * ends of the climb itself. The last clause is what stops one man doing all the work
     * while his partner is wedged somewhere counting as a "walker".
     */
    public static boolean passes(int walkerCount, boolean visitedAllBands,
            List<int[]> perWalkerHeadAndFoot) {
        if (!visitedAllBands || walkerCount < WALKER_FLOOR) {
            return false;
        }
        for (int[] ends : perWalkerHeadAndFoot) {
            if (ends[0] <= 0 || ends[1] <= 0) {
                return false;
            }
        }
        return true;
    }

    /** Human-readable "why" for the report line, so a FAIL names the check it missed. */
    public static String verdictDetail(int walkerCount, boolean visitedAllBands,
            List<Integer> walkerIds, List<int[]> perWalkerHeadAndFoot) {
        StringBuilder why = new StringBuilder();
        if (!visitedAllBands) {
            why.append("no walker visited z11+z12+z13; ");
        }
        if (walkerCount < WALKER_FLOOR) {
            why.append("walkers ").append(walkerCount).append(" < floor ")
                    .append(WALKER_FLOOR).append("; ");
        }
        for (int i = 0; i < perWalkerHeadAndFoot.size(); i++) {
            int[] ends = perWalkerHeadAndFoot.get(i);
            if (ends[0] <= 0 || ends[1] <= 0) {
                why.append("walker #").append(walkerIds.get(i)).append(" reached head ")
                        .append(ends[0]).append(" / foot ").append(ends[1]).append("; ");
            }
        }
        return why.length() == 0 ? "" : "  <- " + why;
    }
}
