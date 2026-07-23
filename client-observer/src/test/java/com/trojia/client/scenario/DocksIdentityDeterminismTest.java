package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * The lead's binding S1 rule made executable: identity data is BAKE-SIDE and must not move
 * the sim tick hash. Two gates:
 * <ol>
 *   <li><b>Hash-unchanged:</b> a population that forges its identity table at bake ticks to
 *       exactly the same hardened ACTORS-section hashes, checkpoint for checkpoint, as a twin
 *       that never forges at all — the forge touches nothing the tick path reads.</li>
 *   <li><b>Pure of tick state:</b> forging after 2,000 live ticks yields a byte-identical
 *       table to forging straight off the bake — every input the forge reads (anchors, homes,
 *       jobs, HOUSEHOLD edges, raws) is bake-immutable, so WHEN you forge can never matter.</li>
 * </ol>
 */
class DocksIdentityDeterminismTest {

    private static final int TICKS = 2_000;
    private static final int CHECKPOINT_PERIOD = 500;

    @Test
    void forgingTheIdentityTableNeverMovesTheSimTickHash() {
        FixtureWorldLoader.Loaded loadedForged = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation forged = DocksPopulation.build(loadedForged.worldSeed(),
                loadedForged.world());
        forged.identity(); // forge at bake — the only difference between the twins
        SimulationDriver forgedDriver = new SimulationDriver(loadedForged.world(),
                loadedForged.worldSeed(), List.<SimulationSystem>of(forged.system()));

        FixtureWorldLoader.Loaded loadedBare = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation bare = DocksPopulation.build(loadedBare.worldSeed(), loadedBare.world());
        SimulationDriver bareDriver = new SimulationDriver(loadedBare.world(),
                loadedBare.worldSeed(), List.<SimulationSystem>of(bare.system()));

        for (int t = 1; t <= TICKS; t++) {
            forgedDriver.requestStep();
            bareDriver.requestStep();
            if (t % CHECKPOINT_PERIOD == 0) {
                assertEquals(actorsHash(bare.system()), actorsHash(forged.system()),
                        "identity forging moved the sim tick hash at tick " + t);
            }
        }
    }

    @Test
    void forgingAfterTwoThousandTicksYieldsTheBakeTimeTable() {
        FixtureWorldLoader.Loaded loadedAtBake = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation atBake = DocksPopulation.build(loadedAtBake.worldSeed(),
                loadedAtBake.world());
        String bakeTable = atBake.identity().canonicalTable();

        FixtureWorldLoader.Loaded loadedLate = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation late = DocksPopulation.build(loadedLate.worldSeed(), loadedLate.world());
        SimulationDriver driver = new SimulationDriver(loadedLate.world(),
                loadedLate.worldSeed(), List.<SimulationSystem>of(late.system()));
        for (int t = 0; t < TICKS; t++) {
            driver.requestStep();
        }
        assertEquals(bakeTable, late.identity().canonicalTable(),
                "the identity table must be a pure function of the bake, not of WHEN it is forged");
    }

    private static long actorsHash(ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }
}
