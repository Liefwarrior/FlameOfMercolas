package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-5 kinship pass gates (the S4 vocabulary debt, paid): the three named compound
 * families (Quayward/Netter/Saltgate mansions) are {@link RelationshipKind#KIN} cliques over
 * and above their HOUSEHOLD co-residence, and NOBODY else in the ward is kin — bunked crews,
 * shop staff and flophouse lodgers share Homes, not blood. Twin bakes realize identical
 * kinship.
 */
class DocksKinshipTest {

    /** The authored family sizes: Quayward 5, Netter 4, Saltgate 4 → 10+6+6 clique edges. */
    private static final int EXPECTED_KIN_EDGES = 22;

    private static DocksPopulation population;
    private static Map<String, Integer> notableActor;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        IdentityRegistry identity = population.identity();
        notableActor = new HashMap<>();
        for (int i = 0; i < identity.size(); i++) {
            if (identity.get(i).notableId() != null) {
                notableActor.put(identity.get(i).notableId(), i);
            }
        }
    }

    /** Each compound head is KIN to exactly its family (household size minus itself). */
    @Test
    void theCompoundHeadsAreKinToExactlyTheirFamilies() {
        assertEquals(4, kinOf(notableActor.get("quayward")).size(),
                "Ceffa Quayward keeps the mansion with four of her kin");
        assertEquals(3, kinOf(notableActor.get("netter")).size(),
                "the Netter mansion household is four strong");
        assertEquals(3, kinOf(notableActor.get("saltgate")).size(),
                "the Saltgate mansion household is four strong");
        // And every kin pair is also a HOUSEHOLD pair — blood lives together here.
        for (String head : List.of("quayward", "netter", "saltgate")) {
            int headId = notableActor.get(head);
            for (int kin : kinOf(headId)) {
                assertTrue(sharesHousehold(headId, kin),
                        head + " kin actor#" + kin + " must share the mansion household");
            }
        }
    }

    /** The whole ward holds exactly the three family cliques — lodgers are not kin. */
    @Test
    void nobodyOutsideTheThreeFamiliesIsKin() {
        RelationshipRegistry relationships = population.relationships();
        int kinEdges = 0;
        for (int i = 0; i < relationships.size(); i++) {
            if (relationships.get(i).kind() == RelationshipKind.KIN) {
                kinEdges++;
            }
        }
        assertEquals(EXPECTED_KIN_EDGES, kinEdges,
                "KIN is authored by the kinship pass alone: three family cliques, "
                        + "no crew/staff/lodger household ever qualifies");
    }

    /** Twin bakes realize byte-identical kinship (the bake stays a pure function). */
    @Test
    void twinBakesRealizeIdenticalKinship() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation twin = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        assertEquals(kinEdgeList(population.relationships()),
                kinEdgeList(twin.relationships()));
    }

    private static List<Integer> kinOf(int actorId) {
        List<Integer> kin = new ArrayList<>();
        for (RelationshipRegistry.View view
                : population.relationships().relationshipsOf(actorId)) {
            if (view.kindAsSeen() == RelationshipKind.KIN) {
                kin.add(view.otherId());
            }
        }
        return kin;
    }

    private static boolean sharesHousehold(int a, int b) {
        for (RelationshipRegistry.View view
                : population.relationships().relationshipsOf(a)) {
            if (view.otherId() == b && view.kindAsSeen() == RelationshipKind.HOUSEHOLD) {
                return true;
            }
        }
        return false;
    }

    private static List<String> kinEdgeList(RelationshipRegistry relationships) {
        List<String> edges = new ArrayList<>();
        for (int i = 0; i < relationships.size(); i++) {
            RelationshipEdge edge = relationships.get(i);
            if (edge.kind() == RelationshipKind.KIN) {
                edges.add(edge.fromId() + "-" + edge.toId());
            }
        }
        return edges;
    }
}
