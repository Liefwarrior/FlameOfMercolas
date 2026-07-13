package com.trojia.sim.actor;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * The relationship-graph side-table (ACTORS-SPEC.md §11.3): a sparse, sorted
 * array — no actor's edge list is stored on {@link Actor} (unbounded data
 * stays off the base, per the §11 intro's design-pressure rule). Canonical
 * sort key {@code (min(fromId,toId), max(fromId,toId), kind)} ascending.
 *
 * <p>Query cost shape is a linear scan (documented simplification vs the
 * spec's binary-search-then-short-scan optimization, §11.3) — correct and
 * fully deterministic at the "hundreds, not millions" actor scale this
 * project targets (ARCHITECTURE.md §3).
 */
public final class RelationshipRegistry {

    /** One relationship as seen from a specific actor's point of view. */
    public record View(int otherId, RelationshipKind kindAsSeen) {
    }

    private final List<RelationshipEdge> edges = new ArrayList<>();

    /**
     * Adds a HOUSEHOLD/NEIGHBOR/FRIEND edge with automatic canonical
     * (min,max) ordering — symmetric kinds never care which id the caller
     * passes first.
     */
    public void addSymmetric(int actorA, int actorB, RelationshipKind kind) {
        int from = Math.min(actorA, actorB);
        int to = Math.max(actorA, actorB);
        add(new RelationshipEdge(from, to, kind));
    }

    /**
     * Adds a directed EMPLOYER/MENTOR edge: {@code seniorId} is the employer/
     * mentor, {@code juniorId} the employee/mentee.
     */
    public void addDirected(int seniorId, int juniorId, RelationshipKind kind) {
        add(new RelationshipEdge(seniorId, juniorId, kind));
    }

    private void add(RelationshipEdge edge) {
        edges.add(edge);
        edges.sort(Comparator
                .comparingInt(RelationshipEdge::fromId)
                .thenComparingInt(RelationshipEdge::toId)
                .thenComparing(RelationshipEdge::kind));
    }

    public int size() {
        return edges.size();
    }

    public RelationshipEdge get(int index) {
        return edges.get(index);
    }

    /**
     * Every relationship touching {@code actorId}, resolved to the kind as
     * seen from that actor's side (EMPLOYER/EMPLOYEE inverse resolution,
     * §11.3) — symmetric kinds report identically from both sides.
     */
    public List<View> relationshipsOf(int actorId) {
        List<View> result = new ArrayList<>();
        for (RelationshipEdge edge : edges) {
            if (edge.fromId() == actorId) {
                result.add(new View(edge.toId(), edge.kind()));
            } else if (edge.toId() == actorId) {
                RelationshipKind seen = edge.kind() == RelationshipKind.EMPLOYER
                        ? RelationshipKind.EMPLOYEE : edge.kind();
                result.add(new View(edge.fromId(), seen));
            }
        }
        return result;
    }

    /**
     * {@code -ea}-style audit (§11.4 step 5's sibling check, §4.8.3/§10.3
     * convention): asserts no self-edge and no stored {@code EMPLOYEE} kind
     * exists anywhere in the table. The constructors already forbid both at
     * insert time; this is the whole-table re-check.
     */
    public void auditInvariants() {
        for (RelationshipEdge edge : edges) {
            if (edge.fromId() == edge.toId()) {
                throw new IllegalStateException("self-edge found: actor " + edge.fromId());
            }
            if (edge.kind() == RelationshipKind.EMPLOYEE) {
                throw new IllegalStateException(
                        "stored EMPLOYEE edge found (fromId=" + edge.fromId()
                                + ", toId=" + edge.toId() + ") — construction should be impossible");
            }
        }
    }
}
