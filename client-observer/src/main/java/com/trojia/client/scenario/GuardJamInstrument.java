package com.trojia.client.scenario;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.CorridorPinch;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.List;

/**
 * S7 guard-routing pass — the ward's PERMANENT, honest jam readout.
 *
 * <p>It replaces the S6 instrument, which measured the wrong thing and then printed the
 * answer against a different metric's baseline at a different horizon. Three corrections,
 * all of them load-bearing:
 *
 * <ol>
 *   <li><b>The primary number is the corridor-pinch predicate.</b> Eli's bug is "guards
 *       patrol 1-tile-wide alleyways and pile up"; the S6 instrument counted EVERY
 *       watch-x-watch adjacency anywhere in the ward, including two guards walking past each
 *       other on an open quay and — decisively — guards asleep in adjacent bunks, which the
 *       bake authors deliberately. The pinch line counts only adjacency where at least one
 *       of the pair stands on ground whose narrower axis is one cell wide
 *       ({@link CorridorPinch}). The broad any-adjacency line is KEPT, but relabelled as a
 *       secondary and printed with its OWN baseline — never the pinch baseline.</li>
 *   <li><b>Day and night are never mixed.</b> The jam is overwhelmingly nocturnal, and
 *       15,000 ticks is 80% day while 60,000 ticks is 60% day, so two horizons of the same
 *       metric are not comparable. Every figure below carries its window and its horizon.</li>
 *   <li><b>Attribution is permanent.</b> The worst-offending-pair table and the per-guard
 *       coverage floor are part of the twin-compared report, so a regression names the
 *       offending pair and the starved guards without anyone re-instrumenting.</li>
 * </ol>
 *
 * <p><b>Frozen is reported as EXCESS, never raw share.</b> {@code militia_watch} has
 * {@code speedTicksPerStep=2}: two perfectly healthy guards walking past each other hold
 * their cells on up to half the ticks purely from the movement cadence. The cadence floor
 * is therefore {@code adjacentTicks/2} and only {@code frozenTicks - floor} is evidence of
 * wedging. Raw frozen counts are printed for auditability; a raw frozen PERCENTAGE never is.
 *
 * <p><b>Determinism.</b> {@code watchIds} is a registry-ordered list; every accumulator is a
 * dense {@code long[]} indexed by the ordered pair {@code (i,j) -> i*n+j}; emission walks
 * indices and sorts with a total order on {@code (adjacentTicks desc, i asc, j asc)}. No
 * unordered collection is ever iterated on the print path. Cell-coverage sets are the
 * established pattern in this file — membership is only ever COUNTED, never iterated.
 */
final class GuardJamInstrument {

    /** {@code militia_watch.speedTicksPerStep} — the cadence the frozen floor is derived from. */
    static final int WATCH_SPEED_TICKS_PER_STEP = 2;

    /** Coverage floor from the S7 definition of done: distinct cells over the final full day. */
    static final int COVERAGE_FLOOR = 120;

    /** Roster-median coverage bar from the S7 definition of done. */
    static final int COVERAGE_MEDIAN_BAR = 180;

    /** Night-oscillation window the flip cap is stated over. */
    static final int FLIP_WINDOW_TICKS = 3_000;

    /** Flip cap per watch soul per {@link #FLIP_WINDOW_TICKS} of night. */
    static final int FLIP_CAP = 50;

    /**
     * A leg that is NOT GETTING ANYWHERE. {@code goalWorkTicks} is the patrol's stall clock;
     * {@code >= 2} is a real stall spell rather than the speed accumulator's ordinary sawtooth.
     *
     * <p>S7 round 3 widened what this reads, and the widening is the point. The clock used to
     * zero on every tick the guard MOVED, so it could only ever see a guard standing dead
     * still — a pair live-locked in a 1-wide connector, stepping back and forth forever without
     * closing on anything, scored zero blocked ticks and looked healthy. The clock now zeroes
     * on PROGRESS toward the waypoint instead, and a dead-still tick is weighted 5 to keep the
     * S6 40-tick yield exact, so this readout counts live-locked ticks too and a still tick
     * crosses the threshold one tick sooner. The number is therefore NOT comparable with any
     * figure printed before that change: it is measuring a strictly larger set of failures.
     */
    static final int BLOCKED_THRESHOLD = 2;

    private final List<Integer> watchIds;
    private final int n;
    private final Actor.WalkabilityQuery walk;
    private final int patrolJobOrdinal;
    private final com.trojia.sim.actor.job.JobRegistry jobs;

    // ---- pair accumulators, dense over the ordered pair (i,j), i<j -------------------
    private final long[] pairAdjacent;
    private final long[] pairFrozen;
    private final long[] pairAdjacentDay;
    private final long[] pairRunCurrent;
    private final long[] pairRunLongest;
    private final long[] pairDayFrozenRunCurrent;
    private final long[] pairDayFrozenRunLongest;
    private final long[] pairPinch;
    private final long[] pairDutyRunCurrent;
    private final long[] pairDutyRunLongest;

    // ---- per-guard accumulators -------------------------------------------------------
    private final int[] prevCell;
    private final byte[] prevActing;
    private final long[] guardTicks;
    private final long[] blockedTicks;
    private final long[] offShiftTicks;
    private final long[] offShiftAtHomeTicks;
    private final long[] onStreetTicks;
    private final boolean[] nightRoster;
    private final long[] flipsThisWindow;
    private final long[] flipsWorstWindow;
    private final List<java.util.HashSet<Integer>> coverageCells;
    private long flipWindowIndex = -1;

    // ---- district totals ---------------------------------------------------------------
    private long pinchPairTicksDay;
    private long pinchPairTicksNight;
    private long pinchTicksDay;
    private long pinchTicksNight;
    private long broadPairTicksDay;
    private long broadPairTicksNight;
    private long broadTicksDay;
    private long broadTicksNight;
    private long dayTicks;
    private long nightTicksTotal;

    private final long coverageWindowStart;

    GuardJamInstrument(ActorRegistry registry, List<Integer> watchIds,
            Actor.WalkabilityQuery walk, int patrolJobOrdinal,
            com.trojia.sim.actor.job.JobRegistry jobs, long coverageWindowStart) {
        this.watchIds = watchIds;
        this.n = watchIds.size();
        this.walk = walk;
        this.patrolJobOrdinal = patrolJobOrdinal;
        this.jobs = jobs;
        this.coverageWindowStart = coverageWindowStart;
        this.pairAdjacent = new long[n * n];
        this.pairFrozen = new long[n * n];
        this.pairAdjacentDay = new long[n * n];
        this.pairRunCurrent = new long[n * n];
        this.pairRunLongest = new long[n * n];
        this.pairDayFrozenRunCurrent = new long[n * n];
        this.pairDayFrozenRunLongest = new long[n * n];
        this.pairPinch = new long[n * n];
        this.pairDutyRunCurrent = new long[n * n];
        this.pairDutyRunLongest = new long[n * n];
        this.prevCell = new int[n];
        this.prevActing = new byte[n];
        this.guardTicks = new long[n];
        this.blockedTicks = new long[n];
        this.offShiftTicks = new long[n];
        this.offShiftAtHomeTicks = new long[n];
        this.onStreetTicks = new long[n];
        this.nightRoster = new boolean[n];
        this.flipsThisWindow = new long[n];
        this.flipsWorstWindow = new long[n];
        this.coverageCells = new ArrayList<>(n);
        for (int i = 0; i < n; i++) {
            Actor a = registry.get(watchIds.get(i));
            prevCell[i] = a.cell();
            prevActing[i] = acting(a);
            nightRoster[i] = a.jobOrdinal() >= 0
                    && jobs.get(a.jobOrdinal()).worksThroughTheNight();
            coverageCells.add(new java.util.HashSet<>());
        }
    }

    /** Observe one already-stepped tick. Ascending scans only. */
    void observe(ActorRegistry registry, com.trojia.sim.actor.HomeRegistry homes, long tick) {
        long tod = DailyRhythm.tickOfDay(tick);
        boolean day = tod < DailyRhythm.DAY / 2;
        if (day) {
            dayTicks++;
        } else {
            nightTicksTotal++;
        }
        long window = tick / FLIP_WINDOW_TICKS;
        if (window != flipWindowIndex) {
            for (int i = 0; i < n; i++) {
                if (flipsThisWindow[i] > flipsWorstWindow[i]) {
                    flipsWorstWindow[i] = flipsThisWindow[i];
                }
                flipsThisWindow[i] = 0;
            }
            flipWindowIndex = window;
        }

        int[] cell = new int[n];
        boolean[] frozen = new boolean[n];
        boolean[] onPatrolJob = new boolean[n];
        boolean[] onDuty = new boolean[n];
        for (int i = 0; i < n; i++) {
            Actor a = registry.get(watchIds.get(i));
            cell[i] = a.cell();
            frozen[i] = cell[i] == prevCell[i];
            onPatrolJob[i] = a.jobOrdinal() == patrolJobOrdinal;
            boolean atHome = a.homeId() != Actor.NONE
                    && cell[i] == homes.get(a.homeId()).homeCell();
            // ON DUTY = this soul's OWN shift window is open AND it is not standing on its
            // own home cell. Read off the bound job's rhythm window rather than assumed to
            // be daylight, because since the night roster landed the two are no longer the
            // same thing for every soul: the three watch.nightwatch guards are on duty at
            // exactly the hours the other sixteen are asleep. For a day guard this is
            // bit-for-bit the old "day &&" test -- watch.patrol's window IS [0,12000).
            // Adjacency between two souls OFF duty is the bake's own doing (#371/#372/#373
            // share a bunkroom, #378/#379 share a guardhouse) and counting a night of sleep
            // as a "wedge" would make the metric unpassable by construction and would reward
            // a build whose guards stopped going home.
            boolean onShift = onShift(a, tod);
            onDuty[i] = onShift && !atHome;
            if (onDuty[i]) {
                onStreetTicks[i]++;
            }
            guardTicks[i]++;
            if (a.goalWorkTicks() >= BLOCKED_THRESHOLD) {
                blockedTicks[i]++;
            }
            byte acting = acting(a);
            // The settle/flip metrics are stated over each soul's OWN off-shift window --
            // the hours it is supposed to be in bed. That is where the oscillation lived and
            // it is the only window in which "at home" is the right answer. Identical to the
            // old night-window reading for the sixteen day guards; for a night-roster soul it
            // correctly asks whether it settles by DAY.
            if (!onShift) {
                offShiftTicks[i]++;
                if (atHome) {
                    offShiftAtHomeTicks[i]++;
                }
                if (acting != ACTING_OTHER && prevActing[i] != ACTING_OTHER
                        && acting != prevActing[i]) {
                    flipsThisWindow[i]++;
                }
            }
            prevActing[i] = acting;
            if (tick > coverageWindowStart) {
                coverageCells.get(i).add(cell[i]);
            }
        }

        int broadPairs = 0;
        int pinchPairs = 0;
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                int idx = i * n + j;
                boolean adjacent = PackedPos.z(cell[i]) == PackedPos.z(cell[j])
                        && chebyshev(cell[i], cell[j]) <= 1;
                boolean bothOnDuty = onDuty[i] && onDuty[j];
                if (!adjacent) {
                    pairRunCurrent[idx] = 0;
                    pairDayFrozenRunCurrent[idx] = 0;
                    pairDutyRunCurrent[idx] = 0;
                    continue;
                }
                if (bothOnDuty) {
                    pairDutyRunCurrent[idx]++;
                    if (pairDutyRunCurrent[idx] > pairDutyRunLongest[idx]) {
                        pairDutyRunLongest[idx] = pairDutyRunCurrent[idx];
                    }
                } else {
                    pairDutyRunCurrent[idx] = 0;
                }
                broadPairs++;
                pairAdjacent[idx]++;
                pairRunCurrent[idx]++;
                if (pairRunCurrent[idx] > pairRunLongest[idx]) {
                    pairRunLongest[idx] = pairRunCurrent[idx];
                }
                boolean bothFrozen = frozen[i] && frozen[j];
                if (bothFrozen) {
                    pairFrozen[idx]++;
                }
                if (day && onPatrolJob[i] && onPatrolJob[j]) {
                    pairAdjacentDay[idx]++;
                }
                // Gated on DUTY, not merely on day: two bunkmates napping off a REST need in
                // their own quarters at noon are adjacent and both frozen, and counting that
                // as a wedge is the same confound that made the S6 metric unreadable.
                if (bothOnDuty && bothFrozen) {
                    pairDayFrozenRunCurrent[idx]++;
                    if (pairDayFrozenRunCurrent[idx] > pairDayFrozenRunLongest[idx]) {
                        pairDayFrozenRunLongest[idx] = pairDayFrozenRunCurrent[idx];
                    }
                } else {
                    pairDayFrozenRunCurrent[idx] = 0;
                }
                if (CorridorPinch.isPinched(cell[i], walk)
                        || CorridorPinch.isPinched(cell[j], walk)) {
                    pinchPairs++;
                    pairPinch[idx]++;
                }
            }
        }
        if (day) {
            broadPairTicksDay += broadPairs;
            pinchPairTicksDay += pinchPairs;
            if (broadPairs > 0) {
                broadTicksDay++;
            }
            if (pinchPairs > 0) {
                pinchTicksDay++;
            }
        } else {
            broadPairTicksNight += broadPairs;
            pinchPairTicksNight += pinchPairs;
            if (broadPairs > 0) {
                broadTicksNight++;
            }
            if (pinchPairs > 0) {
                pinchTicksNight++;
            }
        }
        System.arraycopy(cell, 0, prevCell, 0, n);
    }

    private static final byte ACTING_OTHER = 0;
    private static final byte ACTING_GOAL_PURSUE = 1;
    private static final byte ACTING_RETURN_HOME = 2;

    /**
     * Which of the two oscillating policies acted this tick. {@code Actor.policyOrdinal()} is
     * an index into the TYPE's policy stack, not a {@link com.trojia.sim.actor.PolicyId}, and
     * the stack is not reachable from outside sim-core -- so the acting policy is read off the
     * reason code each policy's own {@code act()} stamps: {@code JOB_GOAL} is GOAL_PURSUE,
     * {@code NEED_REST_LOW}/{@code RHYTHM_NIGHT_HOME} are RETURN_HOME's two branches. Anything
     * else (APPREHEND, FLEE, SEEK_FOOD, ...) is neither, and a transition through it is not
     * counted as a flip -- only a direct RETURN_HOME&lt;-&gt;GOAL_PURSUE swap is.
     */
    private static byte acting(Actor a) {
        ReasonCode reason = a.lastReasonCode();
        if (reason == ReasonCode.JOB_GOAL) {
            return ACTING_GOAL_PURSUE;
        }
        if (reason == ReasonCode.NEED_REST_LOW || reason == ReasonCode.RHYTHM_NIGHT_HOME) {
            return ACTING_RETURN_HOME;
        }
        return ACTING_OTHER;
    }

    /** Whether {@code a}'s own bound job puts it on shift at this tick-of-day. */
    private boolean onShift(Actor a, long tod) {
        return a.jobOrdinal() >= 0 && jobs.get(a.jobOrdinal()).params().inWindow(tod);
    }

    private static int chebyshev(int cellA, int cellB) {
        return com.trojia.sim.actor.ActorGeometry.chebyshev(cellA, cellB);
    }

    /** The cadence floor: at {@code speedTicksPerStep=2} a healthy pair holds half its ticks. */
    private static long cadenceFloor(long adjacentTicks) {
        return adjacentTicks * (WATCH_SPEED_TICKS_PER_STEP - 1) / WATCH_SPEED_TICKS_PER_STEP;
    }

    private static long excess(long frozenTicks, long adjacentTicks) {
        long over = frozenTicks - cadenceFloor(adjacentTicks);
        return over > 0 ? over : 0;
    }

    /**
     * The AUTHORED half of the primary metric: a patrol-route waypoint standing on pinched
     * ground is a beat parked in a 1-wide gut by content, not by behaviour, and no sim-side
     * fix can move it. Printed every run so the next reader inherits the list instead of
     * re-instrumenting for it. Deterministic: the route table is an ordered dense table,
     * walked by index.
     */
    static void printRouteWaypointPinchAudit(List<List<Integer>> routes,
            Actor.WalkabilityQuery walk) {
        System.out.println("  AUTHORED PATROL WAYPOINTS ON PINCHED GROUND (content defects --"
                + " no sim fix can move these; each needs a marker + gen-script edit)");
        int found = 0;
        for (int r = 0; r < routes.size(); r++) {
            List<Integer> route = routes.get(r);
            for (int w = 0; w < route.size(); w++) {
                int c = route.get(w);
                if (CorridorPinch.isPinched(c, walk)) {
                    found++;
                    System.out.println("    route " + r + " waypoint " + w + " ("
                            + PackedPos.x(c) + "," + PackedPos.y(c) + "," + PackedPos.z(c)
                            + ") is 1 cell wide on its narrower axis");
                }
            }
        }
        if (found == 0) {
            System.out.println("    (none)");
        }
    }

    void print(long ticks) {
        long pinchPairTicks = pinchPairTicksDay + pinchPairTicksNight;
        long pinchTicks = pinchTicksDay + pinchTicksNight;
        long broadPairTicks = broadPairTicksDay + broadPairTicksNight;
        long broadTicks = broadTicksDay + broadTicksNight;
        System.out.println();
        System.out.println("================ S7 GUARD JAMS (the honest readout) =========================");
        System.out.println("  horizon: " + ticks + " ticks (" + dayTicks + " day / "
                + nightTicksTotal + " night);  watch souls: " + n);
        System.out.println("  PRIMARY predicate 'corridor-pinch': a watch pair at same-z chebyshev<=1");
        System.out.println("    with at least one of the pair on a cell whose narrower walkable axis");
        System.out.println("    is 1 cell wide. This is the 1-tile-alley pile-up Eli reported.");
        System.out.printf("    pinch pair-ticks   : %8d  (day %d / night %d)   target <=2000 @60k,"
                + " hold-line <=3500 @60k%n", pinchPairTicks, pinchPairTicksDay, pinchPairTicksNight);
        System.out.printf("    pinch jam ticks    : %8d / %d = %d permille   target <=30,"
                + " hold-line <=57  [predicate: corridor-pinch, horizon: this run]%n",
                pinchTicks, ticks, permille(pinchTicks, ticks));
        System.out.println("  SECONDARY predicate 'any-adjacency': EVERY watch-x-watch adjacency in the");
        System.out.println("    ward, open quay and shared bunkroom included. It has its OWN baseline and");
        System.out.println("    a floor well above zero -- the bake quarters guards in adjacent bunks on");
        System.out.println("    purpose, so a near-zero reading here means the guards stopped going home.");
        System.out.printf("    any-adj pair-ticks : %8d  (day %d / night %d)"
                + "  [predicate: any-adjacency, horizon: this run]%n",
                broadPairTicks, broadPairTicksDay, broadPairTicksNight);
        System.out.printf("    any-adj jam ticks  : %8d / %d = %d permille (day %d / night %d permille)%n",
                broadTicks, ticks, permille(broadTicks, ticks),
                permille(broadTicksDay, dayTicks), permille(broadTicksNight, nightTicksTotal));
        long dayPatrolPairTicks = 0;
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                dayPatrolPairTicks += pairAdjacentDay[i * n + j];
            }
        }
        System.out.printf("    DAY component, both souls on watch.patrol: %d pair-ticks"
                + "  [predicate: any-adjacency + day + on-job, horizon: this run]%n",
                dayPatrolPairTicks);
        printPairTable();
        printPinchTable();
        printRouteWaypointPinchAudit(DocksPopulation.patrolRoutes(), walk);
        printCoverage(ticks);
        printNightRoster();
        printOscillation();
        printBlocked();
        System.out.println("============================================================================");
    }

    private void printPairTable() {
        List<int[]> order = new ArrayList<>();
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (pairAdjacent[i * n + j] > 0) {
                    order.add(new int[] {i, j});
                }
            }
        }
        // Total order: adjacentTicks desc, then i asc, then j asc -- no ties can reorder.
        order.sort((a, b) -> {
            long ta = pairAdjacent[a[0] * n + a[1]];
            long tb = pairAdjacent[b[0] * n + b[1]];
            if (ta != tb) {
                return Long.compare(tb, ta);
            }
            if (a[0] != b[0]) {
                return Integer.compare(a[0], b[0]);
            }
            return Integer.compare(a[1], b[1]);
        });
        System.out.println("  WORST-OFFENDING PAIRS (top 5 by adjacent ticks; distinct pairs ever"
                + " adjacent: " + order.size() + ")");
        System.out.printf("    %-8s %-8s %10s %10s %10s %10s %10s %10s %10s%n",
                "pairA", "pairB", "adjTicks", "pinchTick", "frozenRaw", "frozenExc",
                "rawRun", "dutyRun", "dutyFrzRun");
        int rows = Math.min(5, order.size());
        for (int r = 0; r < rows; r++) {
            int i = order.get(r)[0];
            int j = order.get(r)[1];
            int idx = i * n + j;
            System.out.printf("    #%-7d #%-7d %10d %10d %10d %10d %10d %10d %10d%n",
                    watchIds.get(i), watchIds.get(j), pairAdjacent[idx], pairPinch[idx],
                    pairFrozen[idx], excess(pairFrozen[idx], pairAdjacent[idx]),
                    pairRunLongest[idx], pairDutyRunLongest[idx],
                    pairDayFrozenRunLongest[idx]);
        }
        long worstDutyRun = 0;
        int worstDutyI = -1;
        int worstDutyJ = -1;
        long worstRun = 0;
        int worstRunI = -1;
        int worstRunJ = -1;
        long worstDayFrozenRun = 0;
        int worstDayI = -1;
        int worstDayJ = -1;
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                int idx = i * n + j;
                if (pairDutyRunLongest[idx] > worstDutyRun) {
                    worstDutyRun = pairDutyRunLongest[idx];
                    worstDutyI = i;
                    worstDutyJ = j;
                }
                if (pairRunLongest[idx] > worstRun) {
                    worstRun = pairRunLongest[idx];
                    worstRunI = i;
                    worstRunJ = j;
                }
                if (pairDayFrozenRunLongest[idx] > worstDayFrozenRun) {
                    worstDayFrozenRun = pairDayFrozenRunLongest[idx];
                    worstDayI = i;
                    worstDayJ = j;
                }
            }
        }
        System.out.println("    frozenExc = frozenRaw - adjTicks/" + WATCH_SPEED_TICKS_PER_STEP
                + " (the speedTicksPerStep=" + WATCH_SPEED_TICKS_PER_STEP + " cadence floor);"
                + " raw frozen is never a percentage here.");
        System.out.println("    longest ON-DUTY unbroken adjacency run -- THE WEDGE METRIC"
                + " (both on shift, neither on its own home cell): " + worstDutyRun
                + (worstDutyI < 0 ? "" : " (#" + watchIds.get(worstDutyI) + " x #"
                        + watchIds.get(worstDutyJ) + ")") + "   target <=3000 @60k");
        System.out.println("    longest RAW adjacency run, shared bunks included: " + worstRun
                + (worstRunI < 0 ? "" : " (#" + watchIds.get(worstRunI) + " x #"
                        + watchIds.get(worstRunJ) + ")")
                + "   (no target: bunkmates are adjacent all night by bake)");
        System.out.println("    longest ON-DUTY adjacent-and-both-frozen run: " + worstDayFrozenRun
                + (worstDayI < 0 ? "" : " (#" + watchIds.get(worstDayI) + " x #"
                        + watchIds.get(worstDayJ) + ")") + "   target <=300");
    }

    /**
     * The same dense table re-emitted ordered by the PRIMARY metric, so the corridor-pinch
     * number names its own offenders instead of leaving the next reader to re-instrument.
     */
    private void printPinchTable() {
        List<int[]> order = new ArrayList<>();
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (pairPinch[i * n + j] > 0) {
                    order.add(new int[] {i, j});
                }
            }
        }
        order.sort((a, b) -> {
            long ta = pairPinch[a[0] * n + a[1]];
            long tb = pairPinch[b[0] * n + b[1]];
            if (ta != tb) {
                return Long.compare(tb, ta);
            }
            if (a[0] != b[0]) {
                return Integer.compare(a[0], b[0]);
            }
            return Integer.compare(a[1], b[1]);
        });
        System.out.println("  WORST PINCH PAIRS (top 5 by pinch pair-ticks: the PRIMARY metric's"
                + " own offenders; pairs ever pinched: " + order.size() + ")");
        for (int r = 0; r < Math.min(5, order.size()); r++) {
            int i = order.get(r)[0];
            int j = order.get(r)[1];
            System.out.printf("    #%-7d #%-7d pinchTicks=%-8d of adjTicks=%d%n",
                    watchIds.get(i), watchIds.get(j), pairPinch[i * n + j],
                    pairAdjacent[i * n + j]);
        }
    }

    private void printCoverage(long ticks) {
        List<Integer> sizes = new ArrayList<>();
        StringBuilder starved = new StringBuilder();
        int belowFloor = 0;
        for (int i = 0; i < n; i++) {
            int size = coverageCells.get(i).size();
            sizes.add(size);
            if (size < COVERAGE_FLOOR) {
                belowFloor++;
                if (starved.length() > 0) {
                    starved.append(", ");
                }
                starved.append('#').append(watchIds.get(i)).append('=').append(size);
            }
        }
        List<Integer> sorted = new ArrayList<>(sizes);
        sorted.sort(Integer::compare);
        int min = sorted.isEmpty() ? 0 : sorted.get(0);
        int median = sorted.isEmpty() ? 0 : sorted.get(sorted.size() / 2);
        System.out.println("  PER-GUARD COVERAGE (distinct cells over the final "
                + Math.min(ticks, DailyRhythm.DAY) + "-tick day)");
        System.out.println("    min=" + min + "  median=" + median + "  floor=" + COVERAGE_FLOOR
                + "  median bar=" + COVERAGE_MEDIAN_BAR + "  below floor: " + belowFloor + "/" + n);
        System.out.println("    starved: " + (starved.length() == 0 ? "(none)" : starved));
        StringBuilder all = new StringBuilder();
        for (int i = 0; i < n; i++) {
            if (i > 0 && i % 6 == 0) {
                System.out.println("    " + all);
                all.setLength(0);
            }
            all.append(String.format("#%d=%-5d", watchIds.get(i), sizes.get(i)));
        }
        if (all.length() > 0) {
            System.out.println("    " + all);
        }
    }

    private void printOscillation() {
        long worstFlips = 0;
        int worstId = -1;
        long minHomePermille = 1000;
        int minHomeId = -1;
        int overCap = 0;
        for (int i = 0; i < n; i++) {
            long flips = flipsWorstWindow[i] > flipsThisWindow[i]
                    ? flipsWorstWindow[i] : flipsThisWindow[i];
            if (flips > worstFlips) {
                worstFlips = flips;
                worstId = i;
            }
            if (flips > FLIP_CAP) {
                overCap++;
            }
            long homePermille = permille(offShiftAtHomeTicks[i], offShiftTicks[i]);
            if (homePermille < minHomePermille) {
                minHomePermille = homePermille;
                minHomeId = i;
            }
        }
        System.out.println("  OFF-SHIFT OSCILLATION (RETURN_HOME<->GOAL_PURSUE acting-policy"
                + " flips, counted in each soul's OWN off-shift window -- the night for a day"
                + " guard, the day for the night roster)");
        System.out.println("    worst flips in any " + FLIP_WINDOW_TICKS + "-tick window: "
                + worstFlips + (worstId < 0 ? "" : " (#" + watchIds.get(worstId) + ")")
                + "   cap=" + FLIP_CAP + ";  souls over cap: " + overCap + "/" + n);
        System.out.println("    least-settled soul: " + (minHomeId < 0 ? "n/a"
                : "#" + watchIds.get(minHomeId)) + " at home for " + minHomePermille
                + " permille of its off-shift window   target >=800");
    }

    /**
     * The night roster's own line: who draws it, and whether the ward is actually manned
     * after dark. A skeleton watch that never leaves its bunk is the same unpoliced night
     * slice 3 shipped, just with a job id on it, so this prints the ON-STREET tick share
     * rather than merely naming the roster.
     */
    private void printNightRoster() {
        System.out.println("  NIGHT ROSTER (watch.nightwatch: on shift while the ward sleeps)");
        int rostered = 0;
        for (int i = 0; i < n; i++) {
            if (!nightRoster[i]) {
                continue;
            }
            rostered++;
            System.out.println("    #" + watchIds.get(i) + "  on-shift ticks "
                    + (guardTicks[i] - offShiftTicks[i]) + " of " + guardTicks[i]
                    + ";  on-street (on shift, off its own home cell) " + onStreetTicks[i]
                    + " = " + permille(onStreetTicks[i], guardTicks[i] - offShiftTicks[i])
                    + " permille of its shift   target >=700");
        }
        if (rostered == 0) {
            System.out.println("    (none -- the ward is unpoliced between dusk and dawn)");
        }
        System.out.println("    souls on the night roster: " + rostered + "/" + n
                + "   (a SKELETON watch by design: most of the Watch stays home, which is"
                + " the oscillation fix)");
    }

    private void printBlocked() {
        long blocked = 0;
        long total = 0;
        for (int i = 0; i < n; i++) {
            blocked += blockedTicks[i];
            total += guardTicks[i];
        }
        System.out.println("  STALL SPELLS (goalWorkTicks>=" + BLOCKED_THRESHOLD
                + ": a leg not closing on its waypoint -- wedged OR live-locked. Since S7"
                + " round 3 this counts live-locks too, so it is NOT comparable with the"
                + " pre-round-3 'blocked spells' figure)");
        System.out.println("    " + blocked + " / " + total + " guard-ticks = "
                + permille(blocked, total) + " permille   target <90 permille (9%)");
    }

    private static long permille(long part, long whole) {
        return whole == 0 ? 0 : part * 1000 / whole;
    }
}
