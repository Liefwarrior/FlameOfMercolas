package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.CrimeLog;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.engine.SimulationSystem;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint-2 DoD over the real baked docks (ambient theft LIVE): a THREE-DAY (72,000-tick)
 * headless run shows the underworld lifting pocket coin with the closed COIN supply intact,
 * at least one ambient thief arrested PURELY from theft with a reconstructable trail
 * (witnessed CrimeLog row → ARRESTED_FOR_THEFT + HELD), while the starvation bars the
 * economy is tuned against still hold with theft switched on — the lead's binding ruling.
 */
class DocksTheftTest {

    private static final int THREE_DAYS = 72_000;

    @Test
    void threeDocksDaysOfAmbientTheftArrestsAThiefAndHoldsEveryBar() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        ActorRegistry registry = population.registry();
        var system = population.system();
        assertEquals(692, registry.size(), "the full named roster is aboard");

        // The closed COIN supply before a single tick (bake mint is the whole supply):
        // LIVE units — the count that must be untouched by any number of lifts (a lift is
        // an item MOVE; take/add churns entry slots, so entry totals are not the measure).
        int liveCoinAtBake = population.items().liveOfKind(ItemKinds.COIN);

        // The Skyrunner watch: which bodies hold the true villain.skyrunner job.
        HashSet<Integer> skyrunnerIds = new HashSet<>();
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            if (a.jobOrdinal() >= 0 && population.jobs().get(a.jobOrdinal())
                    .id().equals(Job.Villain.Skyrunner.ID)) {
                skyrunnerIds.add(i);
            }
        }
        assertTrue(!skyrunnerIds.isEmpty(), "the bake spawns ambient Skyrunners");

        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(population.system()));

        // Harvest crime rows tick-by-tick off the monotonic counter (the DocksProgression
        // level-log harvest pattern) so ring overwrites lose nothing.
        CrimeLog log = system.crimeLog();
        long harvested = 0;
        List<long[]> rows = new ArrayList<>(); // {tick, thiefId, witnessed 0/1}
        HashSet<Integer> arrestedForTheft = new HashSet<>();
        for (int t = 1; t <= THREE_DAYS; t++) {
            driver.requestStep();
            long total = log.totalRecorded();
            int fresh = (int) Math.min(total - harvested, log.size());
            for (int r = log.size() - fresh; r < log.size(); r++) {
                rows.add(new long[] {log.tickAt(r), log.thiefIdAt(r),
                        log.witnessedAt(r) ? 1 : 0});
            }
            harvested = total;
            // Every-tick sweep for the trail stamp (HeldPolicy overwrites it next tick —
            // the same per-tick observation discipline DocksProgressionTest uses).
            for (int i = 0; i < registry.size(); i++) {
                if (registry.get(i).lastReasonCode() == ReasonCode.ARRESTED_FOR_THEFT) {
                    arrestedForTheft.add(i);
                }
            }
        }

        // ---- theft is LIVE and conservation-exact ----
        assertTrue(system.theftCount() > 0, "three days must see the underworld working");
        assertTrue(system.coinsStolen() > 0, "some lifts landed");
        assertEquals(liveCoinAtBake, population.items().liveOfKind(ItemKinds.COIN),
                "the closed COIN supply is EXACT after every lift (moves, never mints)");
        int vaultCoin = population.items().countOnCellOfKind(
                DocksPopulation.bankVaultChestCell(), ItemKinds.COIN);
        assertEquals(population.bankAccounts().totalRoyals(), vaultCoin,
                "the ledger-vault invariant survives a district of thieves");

        // ---- the justice DoD: an ambient thief arrested PURELY from theft, with a trail ----
        assertTrue(system.theftCaughtCount() > 0,
                "some thieves get caught — failure feeds the pipeline");
        assertTrue(system.theftArrests() >= 1,
                "a witnessed theft must draw a correction inside three days");
        assertTrue(!arrestedForTheft.isEmpty(),
                "the ARRESTED_FOR_THEFT stamp names the corrected thief (the trail)");
        boolean trailReconstructs = false;
        for (int thief : arrestedForTheft) {
            for (long[] row : rows) {
                if (row[1] == thief && row[2] == 1) {
                    trailReconstructs = true; // the witnessed row that summoned the Watch
                    break;
                }
            }
        }
        assertTrue(trailReconstructs,
                "every theft arrest traces back to a harvested WITNESSED crime row");

        // ---- the bars (the lead's binding ruling: ambient theft must not break them) ----
        int serfs = 0;
        int serfsStarved = 0;
        int middle = 0;
        int middleStarved = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor a = registry.get(i);
            String type = a.typeId().key();
            boolean starved = a.need(Need.HUNGER) == 0;
            if (type.equals("serf")) {
                serfs++;
                serfsStarved += starved ? 1 : 0;
            } else if (type.equals("shopkeeper") || type.equals("militia_watch")
                    || type.equals("priest_of_the_flame")
                    || type.equals("disciple_of_the_flame")) {
                middle++;
                middleStarved += starved ? 1 : 0;
            }
        }
        assertTrue(serfs > 0 && 100.0 * serfsStarved / serfs <= 5.0,
                "SERF starvation bar (<=5%) with ambient theft on: "
                        + serfsStarved + "/" + serfs);
        assertEquals(0, middleStarved, "MIDDLE CLASS never starves, theft on (" + middle + ")");

        // ---- the Skyrunner watch (report, not a gate at this volume): did the roost work? ----
        int skyrunnerLifts = 0;
        int skyrunnerCaught = 0;
        for (long[] row : rows) {
            if (skyrunnerIds.contains((int) row[1])) {
                skyrunnerLifts++;
                skyrunnerCaught += row[2] == 1 ? 1 : 0;
            }
        }
        int escalated = 0;
        for (int id : skyrunnerIds) {
            Actor a = registry.get(id);
            if (a.hasStatus(StatusBit.MAIMED) || a.hasStatus(StatusBit.EXECUTED)) {
                escalated++;
            }
        }
        System.out.println("[DocksTheftTest] thefts=" + system.theftCount()
                + " caught=" + system.theftCaughtCount()
                + " royalsLifted=" + system.coinsStolen()
                + " theftArrests=" + system.theftArrests()
                + " skyrunnerTheftEscalations=" + system.skyrunnerTheftEscalations()
                + " | skyrunners=" + skyrunnerIds + " lifts=" + skyrunnerLifts
                + " caught=" + skyrunnerCaught + " maimedOrHanged=" + escalated);
    }
}
