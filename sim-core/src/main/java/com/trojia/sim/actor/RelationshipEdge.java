package com.trojia.sim.actor;

/**
 * One immutable relationship edge (ACTORS-SPEC.md §11.3). Directed kinds
 * ({@code EMPLOYER}, {@code MENTOR}) store {@code fromId} = the senior party.
 * Symmetric kinds ({@code HOUSEHOLD}, {@code NEIGHBOR}, {@code FRIEND}) store
 * {@code fromId = min(actorId), toId = max} — canonical construction, so
 * A-knows-B and B-knows-A are never two edges. {@code EMPLOYEE} is never the
 * constructed kind (test A50) — see {@link RelationshipRegistry#relationshipsOf}.
 *
 * @param fromId the lower-id (symmetric) or senior (directed) party
 * @param toId   the higher-id (symmetric) or junior (directed) party
 * @param kind   the edge kind
 */
public record RelationshipEdge(int fromId, int toId, RelationshipKind kind) {

    public RelationshipEdge {
        if (fromId == toId) {
            throw new IllegalArgumentException(
                    "an actor cannot be its own relation (fromId == toId == " + fromId + ")");
        }
        if (kind == RelationshipKind.EMPLOYEE) {
            throw new IllegalArgumentException(
                    "EMPLOYEE is a query-facing inverse label, never a constructed kind "
                            + "(construct EMPLOYER instead — test A50)");
        }
    }
}
