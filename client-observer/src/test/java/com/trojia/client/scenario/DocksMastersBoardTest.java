package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.inspect.MastersBoardSnapshot;
import com.trojia.client.inspect.MastersBoardText;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.engine.SimulationSystem;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-5 "everyone levels" acceptance probe on the masters board (item 3's DoD):
 * after HALF A DOCKS DAY (12,000 ticks — the full working window) of the awards wave,
 * the board names a best soul for every job-trained craft, the seeded masters head their
 * crafts from tick 0, and the climbers-since-dawn section shows real growth. Headless,
 * no GL — the exact lines the M pane renders.
 */
class DocksMastersBoardTest {

    /**
     * The crafts jobs.json declares as trained (the awards wave's working set; S6 adds
     * fishing — the fishbone crew's trade, its adept floor seeded by the master fisher).
     */
    private static final String[] JOB_TRAINED =
            {"channeling", "fieldcraft", "fishing", "kit_keeping", "seacraft", "streetwise"};

    private static final int HALF_DAY = 12_000;

    @Test
    void halfADocksDayFillsTheBoardWithMastersAndClimbers() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        SkillTrackRegistry tracks = population.system().skillTracks();
        MastersBoardSnapshot snapshot =
                new MastersBoardSnapshot(tracks, population.registry().size());

        // ---- tick 0: the seeded masters (World slice 2) already head their crafts ----
        List<String> atBoot = MastersBoardText.lines(tracks, population.registry(),
                population.identity(), snapshot);
        assertEquals("The Ward's Masters", atBoot.get(0));
        String kitKeeping = rowOf(atBoot, "Kit-Keeping");
        assertTrue(kitKeeping.contains("Grandmother Withy 50"),
                "the bake's best netmender heads her craft from tick 0: " + kitKeeping);
        assertTrue(rowOf(atBoot, "Seacraft").contains("Captain Sorrel Vane 50"),
                "the authored best sailor: " + rowOf(atBoot, "Seacraft"));
        assertEquals("(no growth yet today)", atBoot.get(atBoot.size() - 1),
                "seeded mastery is baseline, not climb");

        // ---- half a day of the awards wave ----
        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));
        for (int t = 0; t < HALF_DAY; t++) {
            driver.requestStep();
            snapshot.afterTick(driver.currentTick());
        }

        List<String> board = MastersBoardText.lines(tracks, population.registry(),
                population.identity(), snapshot);
        // Every job-trained craft names a best soul (nobody is played in this run, so
        // every master is a non-played soul — the "everyone levels" acceptance line).
        for (String key : JOB_TRAINED) {
            String display = tracks.skills().get(tracks.skills().id(key).raw()).displayName();
            String row = rowOf(board, display);
            assertTrue(!row.endsWith("(unschooled)"),
                    "half a day of work must produce a " + display + " best: " + row);
            assertTrue(row.contains("adept+"),
                    display + " must hold an adept census by mid-day "
                            + "(seeded masters guarantee the floor): " + row);
        }
        // The climbers section shows real growth since dawn.
        assertTrue(!board.get(board.size() - 1).equals("(no growth yet today)"),
                "half a working day must climb someone: " + board);
        String topClimber = board.get(board.indexOf("-- CLIMBERS SINCE DAWN --") + 1);
        assertTrue(topClimber.matches(".*\\+\\d+ levels?$"),
                "the top climber row carries its delta: " + topClimber);

        // Deterministic ordering contract: two reads of the same live state are identical.
        assertEquals(board, MastersBoardText.lines(tracks, population.registry(),
                population.identity(), snapshot));
    }

    private static String rowOf(List<String> lines, String displayName) {
        return lines.stream().filter(l -> l.startsWith(displayName)).findFirst()
                .orElseThrow(() -> new AssertionError(displayName + " row missing: " + lines));
    }
}
