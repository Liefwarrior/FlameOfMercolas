package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.bark.BarkRawsLoader;
import com.trojia.sim.bark.BarkTableRegistry;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-2 micro-history DoD gates (S2-1 "the stories between them"): every authored
 * history in {@code names/histories.json} binds two real notables, realizes exactly its
 * declared {@code RelationshipRegistry} edge (directed for debts), lands both bio addenda on
 * the identity table, and tells its story in an authored gossip table — and twin bakes
 * realize byte-identical stories. The ruling's three anchors are pinned by name: the Watch
 * sergeant has a grudge, the widow is a debtor, the banker keeps a secret.
 */
class DocksMicroHistoryTest {

    private static DocksPopulation population;
    private static List<MicroHistoryBake.Bound> bound;
    private static Map<String, Integer> notableActor;
    private static BarkTableRegistry barkTables;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        bound = population.authoredHistories();
        IdentityRegistry identity = population.identity();
        notableActor = new HashMap<>();
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).notableId() != null) {
                notableActor.put(identity.get(i).notableId(), i);
            }
        }
        barkTables = BarkRawsLoader.load(RepoPaths.locate("content", "raws"));
    }

    @Test
    void everyHistoryBindsItsDeclaredNotables() {
        assertTrue(bound.size() >= 12, "the sprint authored ~12+ histories, saw " + bound.size());
        for (MicroHistoryBake.Bound b : bound) {
            assertEquals((int) notableActor.get(b.history().a()), b.actorA(),
                    b.history().id() + " party a binds the notable the file names");
            assertEquals((int) notableActor.get(b.history().b()), b.actorB(),
                    b.history().id() + " party b binds the notable the file names");
        }
    }

    @Test
    void everyHistoryRealizesExactlyItsDeclaredEdge() {
        RelationshipRegistry relationships = population.relationships();
        for (MicroHistoryBake.Bound b : bound) {
            boolean directed = b.history().edge() == RelationshipKind.MENTOR;
            int from = directed ? b.actorA() : Math.min(b.actorA(), b.actorB());
            int to = directed ? b.actorB() : Math.max(b.actorA(), b.actorB());
            boolean found = false;
            for (int i = 0; i < relationships.size(); i++) {
                RelationshipEdge edge = relationships.get(i);
                if (edge.fromId() == from && edge.toId() == to
                        && edge.kind() == b.history().edge()) {
                    found = true;
                    break;
                }
            }
            assertTrue(found, "history " + b.history().id() + " edge " + b.history().edge()
                    + " " + from + "->" + to + " is not in the registry");
        }
    }

    @Test
    void bothBioAddendaLandOnTheIdentityTable() {
        IdentityRegistry identity = population.identity();
        for (MicroHistoryBake.Bound b : bound) {
            assertTrue(identity.get(b.actorA()).bio().contains(b.history().bioA()),
                    b.history().id() + " bioA missing from actor#" + b.actorA());
            assertTrue(identity.get(b.actorB()).bio().contains(b.history().bioB()),
                    b.history().id() + " bioB missing from actor#" + b.actorB());
        }
    }

    @Test
    void everyHistoryTellsItsStoryInAnAuthoredGossipTable() {
        for (MicroHistoryBake.Bound b : bound) {
            assertTrue(barkTables.rowCount(b.history().gossip()) >= 2,
                    b.history().id() + " gossip table " + b.history().gossip()
                            + " is unauthored or too thin");
        }
    }

    /** The ruling's three named anchors, pinned so a raws edit cannot silently drop them. */
    @Test
    void theSergeantsGrudgeTheWidowsDebtAndTheBankersSecretExist() {
        Map<String, MicroHistoryBake.Bound> byId = new HashMap<>();
        for (MicroHistoryBake.Bound b : bound) {
            byId.put(b.history().id(), b);
        }
        MicroHistoryBake.Bound grudge = byId.get("vess-venn-grudge");
        assertTrue(grudge != null && grudge.history().kind().equals("feud")
                        && grudge.history().a().equals("vess"),
                "the Watch sergeant's grudge must exist as an authored feud");
        MicroHistoryBake.Bound debt = byId.get("netter-fenner-debt");
        assertTrue(debt != null && debt.history().kind().equals("debt")
                        && debt.history().edge() == RelationshipKind.MENTOR
                        && debt.history().b().equals("netter"),
                "the widow must be a debtor (directed creditor->debtor edge)");
        MicroHistoryBake.Bound secret = byId.get("gilt-crell-erasure");
        assertTrue(secret != null && secret.history().kind().equals("secret")
                        && secret.history().a().equals("gilt"),
                "the banker's secret must exist");
    }

    @Test
    void twinBakesRealizeByteIdenticalStories() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation twin = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        List<MicroHistoryBake.Bound> twinBound = twin.authoredHistories();
        assertEquals(bound.size(), twinBound.size());
        for (int i = 0; i < bound.size(); i++) {
            assertEquals(bound.get(i).history().id(), twinBound.get(i).history().id());
            assertEquals(bound.get(i).actorA(), twinBound.get(i).actorA());
            assertEquals(bound.get(i).actorB(), twinBound.get(i).actorB());
        }
        assertEquals(population.identity().canonicalTable(), twin.identity().canonicalTable(),
                "identity tables (with addenda) must stay twin-bake byte-identical");
        assertEquals(population.relationships().size(), twin.relationships().size(),
                "twin bakes must realize identical edge counts");
    }
}
