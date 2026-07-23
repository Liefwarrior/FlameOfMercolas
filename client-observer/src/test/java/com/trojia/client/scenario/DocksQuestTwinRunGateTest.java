package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * The Sprint-3 phase gate: with THE VANISHED CLERK baked (quest registry bound, leaf on
 * the desk, key and a payable pocket on Gilt), two independent INPUTLESS builds of the
 * docks stay byte-identical on the hardened ACTORS-section hash — which now covers the
 * QuestLog frame — at every checkpoint across 15,000 ticks. And the quest's inputless
 * guarantees hold at soak scale: no talk intent ever fires, so the owner never binds, the
 * engine idles at {@code rumor} with zero advances and zero search attempts, the leaf
 * never leaves the desk, and — the watcher's negative contract — a district of live
 * ambient thieves dipping Gilt's prime 40-Royal pocket can NEVER take the vault key
 * (their lifts move COIN only; the token moves only on the bound owner's own dip).
 */
class DocksQuestTwinRunGateTest {

    private static final int TICKS = 15_000;
    private static final int CHECKPOINT_PERIOD = 1_000;

    @Test
    void inputlessTwinsStayByteIdenticalAndTheQuestIdlesUntouchedFor15kTicks() {
        FixtureWorldLoader.Loaded loadedA = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation a = DocksPopulation.build(loadedA.worldSeed(), loadedA.world());
        SimulationDriver driverA = new SimulationDriver(loadedA.world(), loadedA.worldSeed(),
                List.<SimulationSystem>of(a.system()));

        FixtureWorldLoader.Loaded loadedB = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation b = DocksPopulation.build(loadedB.worldSeed(), loadedB.world());
        SimulationDriver driverB = new SimulationDriver(loadedB.world(), loadedB.worldSeed(),
                List.<SimulationSystem>of(b.system()));

        assertEquals(2, a.questRegistry().questCount(),
                "The Vanished Clerk + The Widow's Paper are baked (S4)");

        for (int t = 1; t <= TICKS; t++) {
            driverA.requestStep();
            driverB.requestStep();
            if (t % CHECKPOINT_PERIOD == 0) {
                assertEquals(actorsHash(a.system()), actorsHash(b.system()),
                        "quest-baked twins diverged at tick " + t);
            }
        }

        // The inputless guarantee, at soak scale (both twins — they hashed identical).
        assertEquals(0, a.system().questLog().stageOf(0), "the engine idled at rumor");
        assertEquals(Actor.NONE, a.system().questLog().ownerOf(0), "no owner ever bound");
        assertEquals(0, a.system().questLog().stageOf(1), "the paper quest idled at trouble");
        assertEquals(Actor.NONE, a.system().questLog().ownerOf(1),
                "no owner ever bound the paper quest");
        assertEquals(0L, a.system().questLog().totalAdvances());
        assertEquals(0L, a.system().questLog().searchAttemptsOf(0), "zero draws burned");
        assertEquals(0L, a.system().questLog().searchAttemptsOf(1),
                "zero draws burned on the paper quest");
        assertEquals(1, a.items().countOnCellOfKind(DocksPopulation.clerksDeskCell(),
                ItemKinds.LEDGER_LEAF), "the leaf never left the desk");
        assertEquals(1, a.items().countOnCellOfKind(DocksPopulation.fennerStrongboxCell(),
                ItemKinds.DEBT_PAPER),
                "the widow's paper never left the strongbox (S4 — ambient lifts move COIN only)");
        assertEquals(1, a.items().liveOfKind(ItemKinds.DEBT_PAPER),
                "the strongbox paper is the only debt paper in the world");
        Map<String, Integer> notables = NameForge.bindNotableActors(a.registry(), a.homes(),
                NotableRaws.load(com.trojia.client.boot.RepoPaths.locate("content", "raws")
                        .resolve("names").resolve("notables.json")),
                DocksPopulation.notableSpawnSites());
        assertEquals(1, a.items().countCarriedOfKind(notables.get("gilt"),
                ItemKinds.VAULT_KEY),
                "15k ticks of live ambient theft never moved the vault key off Gilt "
                        + "(thefts=" + a.system().theftCount() + ")");
    }

    private static long actorsHash(ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }
}
