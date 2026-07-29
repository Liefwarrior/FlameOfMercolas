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
 * The Saltgate Rise proof that cannot lie — a real soak with real floors.
 *
 * <p>Every other guard against this regression was a boolean. The bake test checks that the
 * anchors BIND to route 3; the soak report checked that SOMEBODY reached each end at least
 * once. Both of those stayed green through S7 slice 2, which moved the second K21 watch's
 * anchor one street east, off the route's head waypoint, and cut the ward's only cross-z
 * patrol in half: 15,000-tick head/foot arrivals 224/243 -&gt; 96/99, on-shift coverage
 * -49%/-35%/-42% across the three K21 souls. Nothing failed. It shipped.
 *
 * <p>A count is not a floor and a floor is not a boolean. This test runs the real population
 * on the real baked world and asserts, against {@link SaltgateRiseProof}: how many souls walk
 * the Rise, how much traffic actually crosses each end of the climb, and — the check ward
 * TOTALS cannot make — that EVERY soul bound to the route reaches both ends itself, so one man
 * doing all the work while his partner is wedged somewhere cannot pass.
 *
 * <p>{@link SaltgateRiseProof#FLOOR_HORIZON_TICKS} ticks: the horizon the regression was
 * measured at, and half the cost of the committed {@code DocksBeastSoakTest}. The arrival
 * floors are absolute numbers AT that horizon rather than a rate, because the beat's rate is
 * not horizon-invariant (~154 head arrivals per 10,000 ticks over 15,000, ~65 over 60,000) and
 * a rate loose enough for the long run would not catch a halving on the short one.
 * Deterministic throughout: ascending scans, a {@link TreeMap} band trail, integer arithmetic.
 */
class DocksSaltgateThroughputTest {

    private static final int TICKS = SaltgateRiseProof.FLOOR_HORIZON_TICKS;

    private static int walkerCount;
    private static long headArrivals;
    private static long footArrivals;
    private static boolean visitedAllBands;
    private static List<int[]> perWalkerEnds;
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

        List<TreeMap<Integer, Integer>> bandTicks = new ArrayList<>();
        perWalkerEnds = new ArrayList<>();
        for (int i = 0; i < walkerCount; i++) {
            bandTicks.add(new TreeMap<>());
            perWalkerEnds.add(new int[2]);
        }

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int t = 0; t < TICKS; t++) {
            driver.requestStep();
            for (int i = 0; i < walkerCount; i++) {
                int cell = registry.get(walkers.get(i)).cell();
                bandTicks.get(i).merge(PackedPos.z(cell), 1, Integer::sum);
                if (cell == head) {
                    headArrivals++;
                    perWalkerEnds.get(i)[0]++;
                } else if (cell == foot) {
                    footArrivals++;
                    perWalkerEnds.get(i)[1]++;
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
    void theClimbCarriesRealTrafficInBothDirections() {
        assertTrue(headArrivals >= SaltgateRiseProof.HEAD_ARRIVALS_FLOOR,
                "head(z13) arrivals " + headArrivals + " over " + TICKS + " ticks, floor "
                        + SaltgateRiseProof.HEAD_ARRIVALS_FLOOR + " (the two-man beat measures"
                        + " 232 here; the halved beat that shipped in S7 managed 96)");
        assertTrue(footArrivals >= SaltgateRiseProof.FOOT_ARRIVALS_FLOOR,
                "foot(z11) arrivals " + footArrivals + " over " + TICKS + " ticks, floor "
                        + SaltgateRiseProof.FOOT_ARRIVALS_FLOOR + " (the two-man beat measures"
                        + " 192 here; the halved beat that shipped in S7 managed 99)");
    }

    @Test
    void everyRiseWalkerActuallyWorksTheClimb() {
        // The floors above are ward totals, so one soul doing all the work while its partner
        // stands in a doorway would still pass them. Each walker must reach the head itself.
        assertTrue(visitedAllBands, "no Rise walker crossed z11+z12+z13");
        for (int i = 0; i < walkerCount; i++) {
            assertTrue(perWalkerEnds.get(i)[0] > 0 && perWalkerEnds.get(i)[1] > 0,
                    "Rise walker #" + walkers.get(i) + " reached head "
                            + perWalkerEnds.get(i)[0] + " / foot " + perWalkerEnds.get(i)[1]
                            + " in " + TICKS + " ticks -- it is bound to the route but not"
                            + " working the climb");
        }
    }

    @Test
    void theShippedVerdictAgreesWithTheseFloors() {
        // The report's PASS/FAIL and this test read the same constants through the same
        // method, so a green build and a green report can never disagree again.
        assertTrue(SaltgateRiseProof.passes(walkerCount, visitedAllBands, perWalkerEnds),
                "SaltgateRiseProof.passes() disagrees with the asserted floors"
                        + SaltgateRiseProof.verdictDetail(walkerCount, visitedAllBands,
                                walkers, perWalkerEnds));
    }
}
