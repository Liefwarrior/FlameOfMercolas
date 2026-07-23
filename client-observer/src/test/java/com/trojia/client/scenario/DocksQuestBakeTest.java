package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.sim.actor.ItemKinds;
import com.trojia.sim.actor.RestrictedZone;
import com.trojia.sim.actor.quest.QuestRaws;
import com.trojia.sim.actor.quest.QuestRawsLoader;
import com.trojia.sim.actor.quest.QuestRegistry;
import com.trojia.sim.world.Walkability;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The S3 WORLD bake gates for THE VANISHED CLERK: the committed quest raws bind fully
 * against the real docks bake (every party a live notable body, the desk a legal search
 * cell inside the Watch-gated hall, the physical props staged exactly once), the new
 * quest-cast notable (sedge) lands on an EXISTING spawned lodger — roster unchanged — and
 * twin bakes bind byte-identically (the boot-determinism contract for authored content).
 */
class DocksQuestBakeTest {

    private static FixtureWorldLoader.Loaded loaded;
    private static DocksPopulation population;
    private static QuestRaws.Quest quest;
    private static Map<String, Integer> notables;

    @BeforeAll
    static void bake() {
        loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        quest = QuestRawsLoader.load(RepoPaths.locate("content", "raws")).quests().get(0);
        notables = NameForge.bindNotableActors(population.registry(), population.homes(),
                NotableRaws.load(RepoPaths.locate("content", "raws")
                        .resolve("names").resolve("notables.json")),
                DocksPopulation.notableSpawnSites());
    }

    @Test
    void theDeskIsALegalSearchCellInsideTheWatchGatedHall() {
        int desk = DocksPopulation.clerksDeskCell();
        assertTrue(Walkability.isWalkable(loaded.world().cursor().moveTo(desk)),
                "the clerk's desk cell must be walkable floor (the search verb stands on it)");
        RestrictedZone hall =
                DocksPopulation.restrictedZoneTable().get(DocksPopulation.bankHallZoneIndex());
        assertTrue(hall.contains(desk), "the desk sits inside the bank-hall zone");
        assertFalse(DocksPopulation.bankQueue().contains(desk),
                "the desk must not be a legitimate queue slot (the hall gate is the knob)");
        assertNotEquals(DocksPopulation.bankVaultChestCell(), desk,
                "the desk is the clerk's drawer, not the Royal vault chest");
    }

    @Test
    void thePropsAreStagedExactlyOnce() {
        int gilt = notables.get("gilt");
        assertEquals(1, population.items().countOnCellOfKind(DocksPopulation.clerksDeskCell(),
                ItemKinds.LEDGER_LEAF), "one torn leaf waits in the drawer");
        assertEquals(1, population.items().liveOfKind(ItemKinds.LEDGER_LEAF),
                "the leaf on the desk is the only leaf in the world");
        assertEquals(1, population.items().countCarriedOfKind(gilt, ItemKinds.VAULT_KEY),
                "Gilt carries the one vault key");
        assertEquals(1, population.items().liveOfKind(ItemKinds.VAULT_KEY),
                "Gilt's key is the only key in the world");
        assertTrue(population.items().countCarriedOfKind(gilt, ItemKinds.COIN)
                        >= DocksPopulation.VANISHED_CLERK_GILT_POCKET,
                "Gilt's pocket funds end_gilt's forty-Royal pay");
    }

    @Test
    void everyDeclaredPartyBindsALiveNotableBody() {
        QuestRegistry quests = population.questRegistry();
        assertEquals(1, quests.questCount());
        assertEquals("vanished-clerk", quests.questId(0));
        assertTrue(quest.parties().contains("sedge"),
                "the landlady is a declared party (the folded S2 cut)");
        for (String party : quest.parties()) {
            Integer actorId = notables.get(party);
            assertNotNull(actorId, "party \"" + party + "\" is not a bound notable");
            assertEquals(party, quests.partySymbol(0, actorId),
                    "the registry bound party \"" + party + "\" to a different body "
                            + "than the notable map's");
        }
    }

    @Test
    void sedgeRidesAnExistingLodgerAndTheRosterHolds() {
        assertEquals(692, population.registry().size(),
                "the clerk never spawns and neither does his landlady's body — roster 692");
        int sedge = notables.get("sedge");
        assertEquals("serf", population.registry().get(sedge).typeId().key(),
                "sedge binds the existing lodger-serf at the rooming house");
        assertEquals("Widow Maren Sedge", population.identity().get(sedge).fullName());
    }

    @Test
    void twinBakesBindByteIdentically() {
        FixtureWorldLoader.Loaded twinLoaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation twin = DocksPopulation.build(twinLoaded.worldSeed(), twinLoaded.world());
        Map<String, Integer> twinNotables = NameForge.bindNotableActors(twin.registry(),
                twin.homes(), NotableRaws.load(RepoPaths.locate("content", "raws")
                        .resolve("names").resolve("notables.json")),
                DocksPopulation.notableSpawnSites());
        assertEquals(notables, twinNotables, "twin bakes bind the same notable bodies");
        for (String party : quest.parties()) {
            assertEquals(party, twin.questRegistry().partySymbol(0, twinNotables.get(party)),
                    "twin quest binding differs for party \"" + party + "\"");
        }
        assertEquals(population.identity().canonicalTable(), twin.identity().canonicalTable(),
                "twin bakes forge byte-identical identity tables");
    }
}
