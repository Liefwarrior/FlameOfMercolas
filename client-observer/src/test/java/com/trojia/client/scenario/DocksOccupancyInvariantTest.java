package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The occupancy-cap invariant over the real Docks-ward scenario (Eli's "at most 2 people to a
 * space" directive): spawns the deterministic {@link DocksPopulation} onto the baked
 * {@code docks_surface} world and asserts that <em>no cell ever holds more than</em>
 * {@link Actor#MAX_OCCUPANTS_PER_CELL} <em>actors</em> — at spawn (t=0) and after each of several
 * hundred ticks of the real {@code ACTORS} phase. Also pins the population size so the spread
 * refactor cannot silently drop or duplicate actors.
 */
class DocksOccupancyInvariantTest {

    /**
     * The whole-district roster size. 630 baseline + 10 Phase-1 living-docks stationed actors
     * (the K36 banker, its 2 flanking bank guards, and 7 shop guards) + 13 money-gated-market
     * victuallers (3 on-hull + 4 Band-B + 6 Band-C FOOD vendors) + 38 beast-food-channel
     * beasts (30 quay mice at their dens + 8 wharf cats). Grows with any further stationed
     * spawns.
     */
    private static final int EXPECTED_ACTOR_COUNT = 691;

    /** Enough ticks to exercise commuting, return-home crowding, patrols, and wander. */
    private static final int TICKS = 600;

    @Test
    void noCellEverHoldsMoreThanTwoActors() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        ActorRegistry registry = population.registry();

        assertEquals(EXPECTED_ACTOR_COUNT, registry.size(),
                "the docks roster size must be unchanged by the occupancy-spread refactor");

        assertOccupancyCapHolds(registry, "t=0 (spawn)");

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int i = 0; i < TICKS; i++) {
            driver.requestStep();
            assertOccupancyCapHolds(registry, "after tick " + driver.currentTick());
        }
    }

    /** Groups every actor by its current cell and asserts no cell exceeds the cap. */
    private static void assertOccupancyCapHolds(ActorRegistry registry, String when) {
        Map<Integer, Integer> perCell = new HashMap<>();
        int worstCell = Actor.NONE;
        int worstCount = 0;
        for (int i = 0; i < registry.size(); i++) {
            int cell = registry.get(i).cell();
            int count = perCell.merge(cell, 1, Integer::sum);
            if (count > worstCount) {
                worstCount = count;
                worstCell = cell;
            }
        }
        assertTrue(worstCount <= Actor.MAX_OCCUPANTS_PER_CELL,
                "occupancy cap breached " + when + ": cell " + xyz(worstCell) + " held "
                        + worstCount + " actors (cap is " + Actor.MAX_OCCUPANTS_PER_CELL + ")");
    }

    private static String xyz(int cell) {
        return "(" + PackedPos.x(cell) + "," + PackedPos.y(cell) + "," + PackedPos.z(cell) + ")";
    }
}
