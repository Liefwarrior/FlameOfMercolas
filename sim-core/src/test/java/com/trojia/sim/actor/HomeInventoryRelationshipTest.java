package com.trojia.sim.actor;

import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Home/Inventory/Relationship invariants (ACTORS-SPEC.md §11, tests
 * A46-A50/A53's shape).
 */
final class HomeInventoryRelationshipTest {

    private static final HouseholdRaws RAWS = new HouseholdRaws(
            new int[] {20, 35, 25, 15, 5}, 0, 2, 0, 2, 0, 2);

    @Test
    void everyActorHasExactlyOneHomeAfterBake() {
        ActorRegistry registry = new ActorRegistry();
        HomeRegistry homes = new HomeRegistry();
        RelationshipRegistry relationships = new RelationshipRegistry();
        List<Actor> actors = new ArrayList<>();
        for (int i = 0; i < 12; i++) {
            actors.add(registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                    PackedPos.pack(i, 0, 1)));
        }
        HouseholdFormer.formHouseholds(actors, homes, relationships, 7L, RAWS);

        for (Actor actor : actors) {
            assertNotEquals(Actor.NONE, actor.homeId(), "actor " + actor.id() + " must have a home");
        }
    }

    @Test
    void coResidentsShareHomeAndHaveHouseholdEdges() {
        ActorRegistry registry = new ActorRegistry();
        HomeRegistry homes = new HomeRegistry();
        RelationshipRegistry relationships = new RelationshipRegistry();
        List<Actor> actors = new ArrayList<>();
        for (int i = 0; i < 20; i++) {
            actors.add(registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                    PackedPos.pack(i, 0, 1)));
        }
        HouseholdFormer.formHouseholds(actors, homes, relationships, 7L, RAWS);

        // Every pair sharing a homeId must have a HOUSEHOLD edge; no pair across
        // different homes may have one.
        for (int a = 0; a < actors.size(); a++) {
            int actorAId = actors.get(a).id();
            for (int b = a + 1; b < actors.size(); b++) {
                int actorBId = actors.get(b).id();
                boolean sameHome = actors.get(a).homeId() == actors.get(b).homeId();
                boolean hasHouseholdEdge = relationships.relationshipsOf(actorAId).stream()
                        .anyMatch(v -> v.otherId() == actorBId
                                && v.kindAsSeen() == RelationshipKind.HOUSEHOLD);
                assertEquals(sameHome, hasHouseholdEdge, "actors " + actorAId + "/" + actorBId);
            }
        }
    }

    @Test
    void relationshipQueryIsSymmetricFromBothSides() {
        RelationshipRegistry relationships = new RelationshipRegistry();
        relationships.addSymmetric(3, 7, RelationshipKind.NEIGHBOR);
        assertTrue(relationships.relationshipsOf(3).stream()
                .anyMatch(v -> v.otherId() == 7 && v.kindAsSeen() == RelationshipKind.NEIGHBOR));
        assertTrue(relationships.relationshipsOf(7).stream()
                .anyMatch(v -> v.otherId() == 3 && v.kindAsSeen() == RelationshipKind.NEIGHBOR));
    }

    @Test
    void employerEmployeeReportAsExactInverses() {
        RelationshipRegistry relationships = new RelationshipRegistry();
        relationships.addDirected(1, 2, RelationshipKind.EMPLOYER);
        assertTrue(relationships.relationshipsOf(1).stream()
                .anyMatch(v -> v.otherId() == 2 && v.kindAsSeen() == RelationshipKind.EMPLOYER));
        assertTrue(relationships.relationshipsOf(2).stream()
                .anyMatch(v -> v.otherId() == 1 && v.kindAsSeen() == RelationshipKind.EMPLOYEE));
        relationships.auditInvariants(); // no stored EMPLOYEE edge anywhere
    }

    @Test
    void noActorIsItsOwnRelation() {
        RelationshipRegistry relationships = new RelationshipRegistry();
        assertThrows(IllegalArgumentException.class,
                () -> relationships.addSymmetric(5, 5, RelationshipKind.FRIEND));
        assertThrows(IllegalArgumentException.class,
                () -> relationships.addDirected(5, 5, RelationshipKind.EMPLOYER));
    }

    @Test
    void employeeIsNeverAConstructedKind() {
        assertThrows(IllegalArgumentException.class,
                () -> new RelationshipEdge(1, 2, RelationshipKind.EMPLOYEE));
    }

    @Test
    void employerHireGetsAnEmployerEdgeToTheNearestCandidate() {
        ActorRegistry registry = new ActorRegistry();
        RelationshipRegistry relationships = new RelationshipRegistry();
        Actor shopkeeper = registry.spawn(Shopkeeper.TYPE, ActorTestFixtures.stats(Shopkeeper.TYPE),
                PackedPos.pack(10, 10, 1));
        Actor near = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(11, 10, 1));
        Actor far = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(50, 50, 1));

        HouseholdRaws staffOne = new HouseholdRaws(new int[] {1}, 1, 1, 0, 0, 0, 0);
        List<Integer> hired = HouseholdFormer.formEmployment(shopkeeper, List.of(near, far),
                relationships, 1L, staffOne);

        assertEquals(1, hired.size());
        assertEquals(near.id(), hired.get(0));
        assertTrue(relationships.relationshipsOf(shopkeeper.id()).stream()
                .anyMatch(v -> v.otherId() == near.id() && v.kindAsSeen() == RelationshipKind.EMPLOYER));
    }

    @Test
    void itemsLiteQuantityDefaultsToOne() {
        ItemsLiteRegistry items = new ItemsLiteRegistry();
        int itemId = items.mint((short) 1, Actor.NONE, 4, Actor.NONE, (short) 1);
        assertEquals(1, items.get(itemId).quantity());
    }

    @Test
    void itemsLiteMustDeclareExactlyOneLocation() {
        assertThrows(IllegalArgumentException.class,
                () -> new ItemsLiteEntry(0, (short) 1, Actor.NONE, Actor.NONE, Actor.NONE, (short) 1));
        assertThrows(IllegalArgumentException.class,
                () -> new ItemsLiteEntry(0, (short) 1, Actor.NONE, 4, 4, (short) 1));
    }
}
