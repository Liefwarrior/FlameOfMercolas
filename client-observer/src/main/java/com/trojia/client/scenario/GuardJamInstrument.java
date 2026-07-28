package com.trojia.client.scenario;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.CorridorPinch;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.PolicyId;
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
     * A step attempt that actually FAILED. {@code goalWorkTicks} is the patrol's blocked-leg
     * clock; it is zeroed on every tick the guard moves, so 1 is indistinguishable from the
     * speed accumulator's ordinary sawtooth and only {@code >= 2} is a real blocked spell.
     */
    static final int BLOCKED_THRESHOLD = 2;

    private final List<Integer> watchIds;
    private final int n;
    private final Actor.WalkabilityQuery walk;
    private final int patrolJobOrdinal;

    // ---- pair accumulators, dense over the ordered pair (i,j), i<j -------------------
    private final long[] pairAdjacent;
    private final long[] pairFrozen;
    private final long[] pairAdjacentDay;
    private final long[] pairRunCurrent;
    private final long[] pairRunLongest;
    private final long[] pairDayFrozenRunCurrent;
    private final long[] pairDayFrozenRunLongest;

    // ---- per-guard accumulators -------------------------------------------------------
    private final int[] prevCell;
    private final byte[] prevPolicy;
    private final long[] guardTicks;
    private final long[] blockedTicks;
    private final long[] nightTicks;
    private final long[] nightAtHomeTicks;
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
            Actor.WalkabilityQuery walk, int patrolJobOrdinal, long coverageWindowStart) {
        this.watchIds = watchIds;
        this.n = watchIds.size();
        this.walk = walk;
        this.patrolJobOrdinal = patrolJobOrdinal;
        this.coverageWindowStart = coverageWindowStart;
        this.pairAdjacent = new long[n * n];
        this.pairFrozen = new long[n * n];
        this.pairAdjacentDay = new long[n * n];
        this.pairRunCurrent = new long[n * n];
        this.pairRunLongest = new long[n * n];
        this.pairDayFrozenRunCurrent = new long[n * n];
        this.pairDayFrozenRunLongest = new long[n * n];
        this.prevCell = new int[n];
        this.prevPolicy = new byte[n];
        this.guardTicks = new long[n];
        this.blockedTicks = new long[n];
        this.nightTicks = new long[n];
        this.nightAtHomeTicks = new long[n];
        this.flipsThisWindow = new long[n];
        this.flipsWorstWindow = new long[n];
        this.coverageCells = new ArrayList<>(n);
        for (int i = 0; i < n; i++) {
            Actor a = registry.get(watchIds.get(i));
            prevCell[i] = a.cell();
            prevPolicy[i] = a.policyOrdinal();
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
        for (int i = 0; i < n; i++) {
            Actor a = registry.get(watchIds.get(i));
            cell[i] = a.cell();
            frozen[i] = cell[i] == prevCell[i];
            onPatrolJob[i] = a.jobOrdinal() == patrolJobOrdinal;
            guardTicks[i]++;
            if (a.goalWorkTicks() >= BLOCKED_THRESHOLD) {
                blockedTicks[i]++;
            }
            byte policy = a.policyOrdinal();
            if (!day) {
                nightTicks[i]++;
                if (a.homeId() != Actor.NONE
                        && cell[i] == homes.get(a.homeId()).homeCell()) {
                    nightAtHomeTicks[i]++;
                }
                if (isPursueOrHome(policy) && isPursueOrHome(prevPolicy[i])
                        && policy != prevPolicy[i]) {
                    flipsThisWindow[i]++;
                }
            }
            prevPolicy[i] = policy;
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
                if (!adjacent) {
                    pairRunCurrent[idx] = 0;
                    pairDayFrozenRunCurrent[idx] = 0;
                    continue;
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
                if (day) {
                    if (onPatrolJob[i] && onPatrolJob[j]) {
                        pairAdjacentDay[idx]++;
                    }
                    if (bothFrozen) {
                        pairDayFrozenRunCurrent[idx]++;
                        if (pairDayFrozenRunCurrent[idx] > pairDayFrozenRunLongest[idx]) {
                            pairDayFrozenRunLongest[idx] = pairDayFrozenRunCurrent[idx];
                        }
                    } else {
                        pairDayFrozenRunCurrent[idx] = 0;
                    }
                } else {
                    pairDayFrozenRunCurrent[idx] = 0;
                }
                if (CorridorPinch.isPinched(cell[i], walk)
                        || CorridorPinch.isPinched(cell[j], walk)) {
                    pinchPairs++;
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

    /** RETURN_HOME &lt;-&gt; GOAL_PURSUE is the oscillation slice 3 exists to kill. */
    private static boolean isPursueOrHome(byte ordinal) {
        return ordinal == (byte) PolicyId.GOAL_PURSUE.ordinal()
                || ordinal == (byte) PolicyId.RETURN_HOME.ordinal();
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
        printCoverage(ticks);
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
        System.out.printf("    %-8s %-8s %10s %10s %10s %10s %10s%n",
                "pairA", "pairB", "adjTicks", "frozenRaw", "frozenExc", "longestRun", "dayFrzRun");
        int rows = Math.min(5, order.size());
        for (int r = 0; r < rows; r++) {
            int i = order.get(r)[0];
            int j = order.get(r)[1];
            int idx = i * n + j;
            System.out.printf("    #%-7d #%-7d %10d %10d %10d %10d %10d%n",
                    watchIds.get(i), watchIds.get(j), pairAdjacent[idx], pairFrozen[idx],
                    excess(pairFrozen[idx], pairAdjacent[idx]), pairRunLongest[idx],
                    pairDayFrozenRunLongest[idx]);
        }
        long worstRun = 0;
        int worstRunI = -1;
        int worstRunJ = -1;
        long worstDayFrozenRun = 0;
        int worstDayI = -1;
        int worstDayJ = -1;
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                int idx = i * n + j;
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
        System.out.println("    longest unbroken adjacency run: " + worstRun
                + (worstRunI < 0 ? "" : " (#" + watchIds.get(worstRunI) + " x #"
                        + watchIds.get(worstRunJ) + ")") + "   target <=3000 @60k");
        System.out.println("    longest DAY adjacent-and-both-frozen run: " + worstDayFrozenRun
                + (worstDayI < 0 ? "" : " (#" + watchIds.get(worstDayI) + " x #"
                        + watchIds.get(worstDayJ) + ")") + "   target <=300");
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
            long homePermille = permille(nightAtHomeTicks[i], nightTicks[i]);
            if (homePermille < minHomePermille) {
                minHomePermille = homePermille;
                minHomeId = i;
            }
        }
        System.out.println("  NIGHT OSCILLATION (RETURN_HOME<->GOAL_PURSUE acting-policy flips)");
        System.out.println("    worst flips in any " + FLIP_WINDOW_TICKS + "-tick night window: "
                + worstFlips + (worstId < 0 ? "" : " (#" + watchIds.get(worstId) + ")")
                + "   cap=" + FLIP_CAP + ";  souls over cap: " + overCap + "/" + n);
        System.out.println("    least-settled soul: " + (minHomeId < 0 ? "n/a"
                : "#" + watchIds.get(minHomeId)) + " at home for " + minHomePermille
                + " permille of the night window   target >=800");
    }

    private void printBlocked() {
        long blocked = 0;
        long total = 0;
        for (int i = 0; i < n; i++) {
            blocked += blockedTicks[i];
            total += guardTicks[i];
        }
        System.out.println("  BLOCKED SPELLS (goalWorkTicks>=" + BLOCKED_THRESHOLD
                + ": a step attempt that actually failed)");
        System.out.println("    " + blocked + " / " + total + " guard-ticks = "
                + permille(blocked, total) + " permille   target <90 permille (9%)");
    }

    private static long permille(long part, long whole) {
        return whole == 0 ? 0 : part * 1000 / whole;
    }
}
