package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Saltgate Rise proof that cannot lie — a real soak with real floors, EARLY and LATE.
 *
 * <p>Every earlier guard against this regression was a boolean. The bake test checks that the
 * anchors BIND to route 3; the soak report checked that SOMEBODY reached each end at least
 * once. Both stayed green through S7 slice 2, which moved the second K21 watch's anchor one
 * street east, off the route's head waypoint, and cut the ward's only cross-z patrol in half:
 * 15,000-tick head/foot arrivals 224/243 -&gt; 96/99. Nothing failed. It shipped.
 *
 * <p><b>And then the fix for THAT shipped a proof with the same shape of hole.</b> Round 2's
 * version of this test ran to exactly 15,000 ticks and asserted totals at that horizon —
 * which is, to within a few hundred ticks, the moment Sergeant #371 stopped working the climb
 * and started shuffling in a stairwell for the next 45,000. Everything it asserted was true.
 * Everything it asserted was earned before the bug started. A floor pinned at the horizon
 * where the stall begins is a floor the stall walks straight over.
 *
 * <p>So this runs to {@link SaltgateRiseProof#SOAK_HORIZON_TICKS} — twice the floor horizon,
 * enough that a beat which quits at 15,000 has a whole empty window to be caught in — and
 * asserts three separate things a stall cannot all satisfy: the early totals (the slice-2
 * halving), the late ward totals, and, the one that matters, that EVERY walker is still
 * reaching BOTH ends of the climb inside the final {@link SaltgateRiseProof#LATE_WINDOW_TICKS}.
 * Deterministic throughout: ascending scans, a {@link TreeMap} band trail, integer arithmetic.
 */
class DocksSaltgateThroughputTest {

    private static final int TICKS = SaltgateRiseProof.SOAK_HORIZON_TICKS;
    private static final int EARLY = SaltgateRiseProof.FLOOR_HORIZON_TICKS;

    private static int walkerCount;
    private static long earlyHeadArrivals;
    private static long earlyFootArrivals;
    private static long lateHeadArrivals;
    private static long lateFootArrivals;
    private static boolean visitedAllBands;
    private static List<int[]> perWalkerEnds;
    private static List<int[]> perWalkerLateEnds;
    private static List<Integer> walkers;

    @BeforeAll
    static void soak() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        ActorRegistry registry = population.registry();

        walkers = SaltgateRiseProof.walkers(registry);
        walkerCount = walkers.size();
        int head = SaltgateRiseProof.headWaypoint();
        int foot = SaltgateRiseProof.footWaypoint();
        long lateStart = SaltgateRiseProof.lateWindowStart(TICKS);

        List<TreeMap<Integer, Integer>> bandTicks = new ArrayList<>();
        perWalkerEnds = new ArrayList<>();
        perWalkerLateEnds = new ArrayList<>();
        for (int i = 0; i < walkerCount; i++) {
            bandTicks.add(new TreeMap<>());
            perWalkerEnds.add(new int[2]);
            perWalkerLateEnds.add(new int[2]);
        }

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int t = 0; t < TICKS; t++) {
            driver.requestStep();
            boolean early = t < EARLY;
            boolean late = t >= lateStart;
            for (int i = 0; i < walkerCount; i++) {
                int cell = registry.get(walkers.get(i)).cell();
                bandTicks.get(i).merge(PackedPos.z(cell), 1, Integer::sum);
                if (cell == head) {
                    perWalkerEnds.get(i)[0]++;
                    if (early) {
                        earlyHeadArrivals++;
                    }
                    if (late) {
                        lateHeadArrivals++;
                        perWalkerLateEnds.get(i)[0]++;
                    }
                } else if (cell == foot) {
                    perWalkerEnds.get(i)[1]++;
                    if (early) {
                        earlyFootArrivals++;
                    }
                    if (late) {
                        lateFootArrivals++;
                        perWalkerLateEnds.get(i)[1]++;
                    }
                }
            }
        }
        for (TreeMap<Integer, Integer> byZ : bandTicks) {
            visitedAllBands |= byZ.containsKey(19) && byZ.containsKey(20) && byZ.containsKey(21);
        }
    }

    @Test
    void theRiseIsStillATwoManBeat() {
        assertTrue(walkerCount >= SaltgateRiseProof.WALKER_FLOOR,
                "the Saltgate Rise is staffed by " + walkerCount + " watch (floor "
                        + SaltgateRiseProof.WALKER_FLOOR + ") -- walkers " + walkers
                        + ". A watch anchor that drifts off a route waypoint silently"
                        + " unbinds from the route and falls back to a blind square beat;"
                        + " that is how the beat lost half its staffing without any test"
                        + " going red.");
    }

    @Test
    void theClimbCarriesRealTrafficInBothDirectionsEarly() {
        assertTrue(earlyHeadArrivals >= SaltgateRiseProof.HEAD_ARRIVALS_FLOOR,
                "head(z13) arrivals " + earlyHeadArrivals + " over the first " + EARLY
                        + " ticks, floor " + SaltgateRiseProof.HEAD_ARRIVALS_FLOOR
                        + " (the two-man beat measures 232 here; the halved beat that shipped"
                        + " in S7 managed 96)");
        assertTrue(earlyFootArrivals >= SaltgateRiseProof.FOOT_ARRIVALS_FLOOR,
                "foot(z11) arrivals " + earlyFootArrivals + " over the first " + EARLY
                        + " ticks, floor " + SaltgateRiseProof.FOOT_ARRIVALS_FLOOR
                        + " (the two-man beat measures 192 here; the halved beat that shipped"
                        + " in S7 managed 99)");
    }

    @Test
    void theClimbIsSTILLCarryingTrafficAtTheEndOfTheSoak() {
        // The horizon-robustness half. Early floors are met by work done before a mid-soak
        // stall; these are not.
        assertTrue(lateHeadArrivals >= SaltgateRiseProof.LATE_HEAD_ARRIVALS_FLOOR,
                "head(z13) arrivals " + lateHeadArrivals + " in the FINAL "
                        + SaltgateRiseProof.LATE_WINDOW_TICKS + " ticks, floor "
                        + SaltgateRiseProof.LATE_HEAD_ARRIVALS_FLOOR
                        + " -- the beat banked its numbers early and stopped");
        assertTrue(lateFootArrivals >= SaltgateRiseProof.LATE_FOOT_ARRIVALS_FLOOR,
                "foot(z11) arrivals " + lateFootArrivals + " in the FINAL "
                        + SaltgateRiseProof.LATE_WINDOW_TICKS + " ticks, floor "
                        + SaltgateRiseProof.LATE_FOOT_ARRIVALS_FLOOR
                        + " -- the beat banked its numbers early and stopped");
    }

    @Test
    void everyRiseWalkerActuallyWorksTheClimbAndIsStillWorkingItAtTheEnd() {
        // Ward totals let one soul cover for another; run totals let the past cover for the
        // present. This is the assertion that survives both: each walker, both ends, still.
        assertTrue(visitedAllBands, "no Rise walker crossed z11+z12+z13");
        for (int i = 0; i < walkerCount; i++) {
            assertTrue(perWalkerEnds.get(i)[0] > 0 && perWalkerEnds.get(i)[1] > 0,
                    "Rise walker #" + walkers.get(i) + " reached head "
                            + perWalkerEnds.get(i)[0] + " / foot " + perWalkerEnds.get(i)[1]
                            + " in " + TICKS + " ticks -- it is bound to the route but not"
                            + " working the climb");
        }
        for (int i = 0; i < walkerCount; i++) {
            int[] late = perWalkerLateEnds.get(i);
            assertTrue(late[0] >= SaltgateRiseProof.LATE_PER_WALKER_END_FLOOR
                            && late[1] >= SaltgateRiseProof.LATE_PER_WALKER_END_FLOOR,
                    "Rise walker #" + walkers.get(i) + " STOPPED WORKING mid-soak: in the"
                            + " final " + SaltgateRiseProof.LATE_WINDOW_TICKS + " ticks it"
                            + " reached head " + late[0] + " / foot " + late[1] + " (floor "
                            + SaltgateRiseProof.LATE_PER_WALKER_END_FLOOR + " each), against"
                            + " run totals of head " + perWalkerEnds.get(i)[0] + " / foot "
                            + perWalkerEnds.get(i)[1] + ". A run total earned before a stall"
                            + " reads exactly like a working beat; this is the check that"
                            + " tells them apart.");
        }
    }

    @Test
    void theShippedVerdictAgreesWithTheseFloors() {
        // The report's PASS/FAIL and this test read the same constants through the same
        // method, so a green build and a green report can never disagree again.
        assertTrue(SaltgateRiseProof.passes(walkerCount, visitedAllBands, perWalkerEnds,
                        perWalkerLateEnds),
                "SaltgateRiseProof.passes() disagrees with the asserted floors"
                        + SaltgateRiseProof.verdictDetail(walkerCount, visitedAllBands,
                                walkers, perWalkerEnds, perWalkerLateEnds));
    }
}
