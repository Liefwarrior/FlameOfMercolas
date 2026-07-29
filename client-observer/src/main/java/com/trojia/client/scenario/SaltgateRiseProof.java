package com.trojia.client.scenario;

import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.PatrolRouteTable;

import java.util.ArrayList;
import java.util.List;

/**
 * The Saltgate Rise cross-z patrol proof — walkers, band trail and waypoint THROUGHPUT,
 * measured EARLY <em>and</em> LATE.
 *
 * <p>This exists because the old proof was a boolean and a boolean cannot see a collapse.
 * Its verdict was {@code visitedBothBands && headArrivals > 0 && footArrivals > 0}, so when
 * S7 slice 2 moved the second K21 watch's anchor off route 3's head waypoint — halving the
 * beat's staffing and dropping the 15,000-tick arrivals from 224/243 to 96/99 — the report
 * printed PASS and the regression shipped.
 *
 * <p><b>Round 3: a run-total is not a proof either.</b> The first version of this class pinned
 * its floors at exactly {@link #FLOOR_HORIZON_TICKS} = 15,000 ticks, and Sergeant #371 worked
 * the climb until roughly tick 15,000 and then stopped for the remaining 45,000. Every floor
 * here was met — by work done before the stall — and every 60,000-tick figure the report
 * printed read like steady state. A proof calibrated to the exact horizon at which the bug
 * begins cannot see the bug. So the floors now come in two kinds and BOTH must hold:
 *
 * <ul>
 *   <li><b>Horizon-free structural checks</b>, which the soak report enforces on every run at
 *       any length: the beat is STAFFED ({@link #WALKER_FLOOR}), and every soul bound to the
 *       route reaches BOTH ends of the climb. Either one alone catches the S7 slice-2
 *       regression — staffing went 2 → 1 — and neither depends on run length.
 *   <li><b>Absolute throughput floors at an EARLY and a LATE window.</b> The early floors are
 *       the measured 15,000-tick numbers; the late floors are stated over the final
 *       {@link #LATE_WINDOW_TICKS} of the run, per walker as well as ward-wide. A soul that
 *       quits mid-soak posts zero in its late window no matter how much it banked before, so
 *       the late per-walker floor is the check that #371 could not have passed. Absolute
 *       numbers at fixed windows, never a rate: the beat's arrival rate is not
 *       horizon-invariant (~154 head arrivals per 10,000 ticks over the first 15,000, ~98 over
 *       60,000), so a rate floor loose enough for the long run is too loose to catch a halving
 *       on the short one.
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

    /** The horizon the EARLY absolute arrival floors below are stated at. */
    public static final int FLOOR_HORIZON_TICKS = 15_000;

    /**
     * The horizon the committed proof soak ({@link DocksSaltgateThroughputTest}) runs to —
     * twice {@link #FLOOR_HORIZON_TICKS}, so a beat that works its first 15,000 ticks and then
     * stops has a whole late window in which to show it. A stall the test never runs long
     * enough to reach is a stall the test cannot fail on.
     */
    public static final int SOAK_HORIZON_TICKS = 30_000;

    /** The trailing slice of a run the LATE floors are stated over. */
    public static final int LATE_WINDOW_TICKS = 10_000;

    /**
     * Head-waypoint (z:+13) arrivals over {@link #FLOOR_HORIZON_TICKS}. Measured at 232 for
     * the two-man beat; the regressed one-man beat managed 96. The floor sits between them
     * with room on both sides — well clear of ordinary variation, and nowhere near loose
     * enough to let a halved beat through.
     */
    public static final int HEAD_ARRIVALS_FLOOR = 150;

    /** Foot-waypoint (z:+11) arrivals over the same horizon: measured 192, regressed 99. */
    public static final int FOOT_ARRIVALS_FLOOR = 130;

    /**
     * Ward-wide head arrivals in the FINAL {@link #LATE_WINDOW_TICKS} of the soak. Placed well
     * under the healthy measurement so ordinary window-to-window variation cannot trip it —
     * the check that matters for a stall is the per-walker one below; this is the coarse net.
     */
    public static final int LATE_HEAD_ARRIVALS_FLOOR = 30;

    /** Ward-wide foot arrivals in the same final window. */
    public static final int LATE_FOOT_ARRIVALS_FLOOR = 30;

    /**
     * Arrivals EACH walker must post at EACH end of the climb inside the final window. This is
     * the floor a mid-soak stall cannot clear: #371's late-window score was 0 head and 0 foot
     * while his run totals (104/96 over 60,000) still read like a working beat. Deliberately
     * small — one round trip in the last sixth of the run is a very low bar for a soul whose
     * job is to walk that climb, and the point is to separate "working" from "stopped", not to
     * grade the pace.
     */
    public static final int LATE_PER_WALKER_END_FLOOR = 2;

    /** Arrivals per 10,000 ticks — reported for legibility, never used as a floor. */
    public static long per10k(long arrivals, long ticks) {
        return ticks <= 0 ? 0 : arrivals * 10_000L / ticks;
    }

    /** The first tick of the late window for a run of {@code ticks} ticks. */
    public static long lateWindowStart(long ticks) {
        return Math.max(0, ticks - LATE_WINDOW_TICKS);
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
     * The verdict the soak report prints and {@link DocksSaltgateThroughputTest} asserts: the
     * beat is staffed to strength, some soul genuinely crossed all three bands, EVERY soul
     * bound to the route reached both ends of the climb over the run, and — the clause a run
     * total cannot express — every one of them was STILL reaching both ends in the final
     * window. The last clause is the whole point: it is what stops a soul banking a good
     * number early and then standing in a stairwell for the rest of the soak.
     *
     * @param perWalkerHeadAndFoot per walker, {@code {headArrivals, footArrivals}} over the run
     * @param perWalkerLateEnds    the same two counts restricted to the final
     *                             {@link #LATE_WINDOW_TICKS} ticks
     */
    public static boolean passes(int walkerCount, boolean visitedAllBands,
            List<int[]> perWalkerHeadAndFoot, List<int[]> perWalkerLateEnds) {
        if (!visitedAllBands || walkerCount < WALKER_FLOOR) {
            return false;
        }
        for (int[] ends : perWalkerHeadAndFoot) {
            if (ends[0] <= 0 || ends[1] <= 0) {
                return false;
            }
        }
        for (int[] late : perWalkerLateEnds) {
            if (late[0] < LATE_PER_WALKER_END_FLOOR || late[1] < LATE_PER_WALKER_END_FLOOR) {
                return false;
            }
        }
        return true;
    }

    /** Human-readable "why" for the report line, so a FAIL names the check it missed. */
    public static String verdictDetail(int walkerCount, boolean visitedAllBands,
            List<Integer> walkerIds, List<int[]> perWalkerHeadAndFoot,
            List<int[]> perWalkerLateEnds) {
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
        for (int i = 0; i < perWalkerLateEnds.size(); i++) {
            int[] late = perWalkerLateEnds.get(i);
            if (late[0] < LATE_PER_WALKER_END_FLOOR || late[1] < LATE_PER_WALKER_END_FLOOR) {
                why.append("walker #").append(walkerIds.get(i))
                        .append(" STOPPED WORKING: last ").append(LATE_WINDOW_TICKS)
                        .append(" ticks head ").append(late[0]).append(" / foot ")
                        .append(late[1]).append(" < ").append(LATE_PER_WALKER_END_FLOOR)
                        .append("; ");
            }
        }
        return why.length() == 0 ? "" : "  <- " + why;
    }
}
