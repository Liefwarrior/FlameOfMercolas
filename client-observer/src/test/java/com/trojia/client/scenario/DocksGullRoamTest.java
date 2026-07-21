package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The gull-oscillation regression pin (Eli's report: "gulls are stuck switching between two
 * cells"): on the pre-fix HEAD every gull ping-ponged between its roost and one adjacent cell
 * forever — RETURN_HOME's 24-hour "night" window (Defect A) alternating with the wander job at
 * a one-cell boundary, then the beast-unusable SEEK_FOOD (Defect B) pinning it outright. This
 * test FAILS on that behavior (2-3 distinct cells) and passes once gulls genuinely range: after
 * 3,000 ticks every main-band gull must have visited well more than a two-cell shuttle, and the
 * water-clipped Beaching Strand gull must at least beat the shuttle.
 */
class DocksGullRoamTest {

    private static final int TICKS = 3_000;
    private static final int MIN_DISTINCT_CELLS_MAIN_BAND = 8;
    private static final int MIN_DISTINCT_CELLS_STRAND = 3;

    @Test
    void everyGullRangesInsteadOfOscillatingBetweenTwoCells() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        ActorRegistry registry = population.registry();

        List<Integer> gulls = new ArrayList<>();
        Map<Integer, Integer> spawnZ = new HashMap<>();
        int mainBandZ = Integer.MIN_VALUE; // gulls never cross z; the modal (max) band is ZA
        for (int i = 0; i < registry.size(); i++) {
            if (registry.get(i).typeId().key().equals("feral")) {
                gulls.add(i);
                int z = PackedPos.z(registry.get(i).cell());
                spawnZ.put(i, z);
                mainBandZ = Math.max(mainBandZ, z);
            }
        }
        assertFalse(gulls.isEmpty(), "the docks fixture spawns gulls");

        Map<Integer, HashSet<Integer>> visited = new HashMap<>();
        for (int id : gulls) {
            visited.put(id, new HashSet<>());
        }
        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int t = 0; t < TICKS; t++) {
            driver.requestStep();
            for (int id : gulls) {
                visited.get(id).add(registry.get(id).cell());
            }
        }

        for (int id : gulls) {
            // The lone z:+10 Beaching Strand gull is water-clipped (narrow beach): it only has
            // to beat the two-cell shuttle, not the main-band roam floor.
            int floor = spawnZ.get(id) == mainBandZ
                    ? MIN_DISTINCT_CELLS_MAIN_BAND : MIN_DISTINCT_CELLS_STRAND;
            assertTrue(visited.get(id).size() >= floor,
                    "gull#" + id + " visited only " + visited.get(id).size() + " distinct cells in "
                            + TICKS + " ticks (floor " + floor
                            + ") — the two-cell oscillation is back");
        }
    }
}
