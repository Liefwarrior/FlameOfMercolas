package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.sim.actor.Barter;
import com.trojia.sim.actor.FactionStandings;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-2 faction-leanings DoD gates: every authored leaning in
 * {@code factions/leanings.json} is seeded onto its notable's bound actor at bake (readable
 * from tick zero), the seeds obey the bars guard (no authored watch seed can reach the
 * Barter surcharge or refusal bands — food access untouched), and twin bakes seed
 * identically. The ruling's coarseness examples are pinned: a Temple-devout serf captain
 * and a Merchant-hostile wastrel both exist.
 */
class DocksFactionLeaningsTest {

    private static DocksPopulation population;
    private static List<LeaningRaws.Leaning> leanings;
    private static Map<String, Integer> notableActor;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        leanings = LeaningRaws.load(
                RepoPaths.locate("content", "raws").resolve("factions").resolve("leanings.json"));
        IdentityRegistry identity = population.identity();
        notableActor = new HashMap<>();
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).notableId() != null) {
                notableActor.put(identity.get(i).notableId(), i);
            }
        }
    }

    @Test
    void everyAuthoredLeaningIsSeededAtBake() {
        FactionStandings standings = population.system().factionStandings();
        assertTrue(leanings.size() >= 10, "a meaningful leaning roster, saw " + leanings.size());
        for (LeaningRaws.Leaning leaning : leanings) {
            Integer actorId = notableActor.get(leaning.notable());
            assertTrue(actorId != null, "leaning names unbound notable " + leaning.notable());
            int faction = standings.factions().rawId(leaning.faction());
            assertEquals(leaning.standing(), standings.standingOf(actorId, faction),
                    leaning.notable() + "/" + leaning.faction() + " seed");
        }
    }

    /** The bars guard, asserted from the live ledger side: food access is untouchable. */
    @Test
    void noSeedCanReachTheBarterSurchargeOrRefusalBands() {
        FactionStandings standings = population.system().factionStandings();
        for (LeaningRaws.Leaning leaning : leanings) {
            int actorId = notableActor.get(leaning.notable());
            int watch = standings.watchStanding(actorId);
            assertTrue(watch > -Barter.WATCH_PER_SURCHARGE,
                    leaning.notable() + " watch standing " + watch
                            + " enters the Barter surcharge band");
            assertTrue(watch > Barter.REFUSAL_WATCH_STANDING,
                    leaning.notable() + " watch standing " + watch + " reaches counter refusal");
        }
    }

    /** The ruling's coarseness examples exist: job->faction mapping alone can't express these. */
    @Test
    void theDevoutSerfAndTheHostileWastrelArePinned() {
        boolean devoutSerf = false;
        boolean hostileWastrel = false;
        for (LeaningRaws.Leaning leaning : leanings) {
            int actorId = notableActor.get(leaning.notable());
            String type = population.registry().get(actorId).typeId().key();
            if (type.equals("serf") && leaning.faction().equals("temple")
                    && leaning.standing() > 0) {
                devoutSerf = true;
            }
            if (type.equals("wastrel") && leaning.faction().equals("merchants")
                    && leaning.standing() < 0) {
                hostileWastrel = true;
            }
        }
        assertTrue(devoutSerf, "a Temple-devout serf leaning must exist");
        assertTrue(hostileWastrel, "a Merchant-hostile wastrel leaning must exist");
    }

    @Test
    void twinBakesSeedIdentically() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation twin = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        FactionStandings a = population.system().factionStandings();
        FactionStandings b = twin.system().factionStandings();
        for (LeaningRaws.Leaning leaning : leanings) {
            int actorId = notableActor.get(leaning.notable());
            int faction = a.factions().rawId(leaning.faction());
            assertEquals(a.standingOf(actorId, faction), b.standingOf(actorId, faction),
                    leaning.notable() + "/" + leaning.faction() + " twin seed");
        }
    }
}
