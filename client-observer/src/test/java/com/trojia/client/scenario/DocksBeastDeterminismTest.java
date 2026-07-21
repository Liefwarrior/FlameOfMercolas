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
 * Twin-run determinism over the beast food channel (living-docks beast pass): two independent
 * builds of the docks fixture must produce identical ACTORS-section hashes at every checkpoint
 * across a horizon long enough to cover gull hunts, catch transitions and a full mouse revive
 * cycle. The section hash is the HARDENED one — it now covers {@code downedTimer} and the
 * {@code targetKind}/{@code targetKey} hunt lock (landmine F), so a hunt-only or revive-only
 * divergence fails here instead of slipping past.
 */
class DocksBeastDeterminismTest {

    private static final int TICKS = 9_000; // first hunts ~t2000; first revives ~t8000
    private static final int CHECKPOINT_PERIOD = 1_000;

    @Test
    void twinRunsProduceIdenticalHardenedActorHashesAtEveryCheckpoint() {
        FixtureWorldLoader.Loaded loadedA = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation a = DocksPopulation.build(loadedA.worldSeed(), loadedA.world());
        SimulationDriver driverA = new SimulationDriver(loadedA.world(), loadedA.worldSeed(),
                List.<SimulationSystem>of(a.system()));

        FixtureWorldLoader.Loaded loadedB = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation b = DocksPopulation.build(loadedB.worldSeed(), loadedB.world());
        SimulationDriver driverB = new SimulationDriver(loadedB.world(), loadedB.worldSeed(),
                List.<SimulationSystem>of(b.system()));

        for (int t = 1; t <= TICKS; t++) {
            driverA.requestStep();
            driverB.requestStep();
            if (t % CHECKPOINT_PERIOD == 0) {
                assertEquals(actorsHash(a.system()), actorsHash(b.system()),
                        "twin runs diverged at tick " + t);
            }
        }
    }

    private static long actorsHash(ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }
}
