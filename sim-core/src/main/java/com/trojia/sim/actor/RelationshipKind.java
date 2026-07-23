package com.trojia.sim.actor;

/**
 * The relationship-graph edge kinds (ACTORS-SPEC.md §11.3), append-only,
 * mirroring the §2.10 disposition FSM's convention. {@code EMPLOYEE} is
 * never itself a constructed kind — see {@link RelationshipRegistry}.
 */
public enum RelationshipKind {
    /** Symmetric — shares a Home (§11.1). */
    HOUSEHOLD,
    /** Directed — fromId employs/directs toId's job. */
    EMPLOYER,
    /** Query-facing inverse of EMPLOYER; never the constructed kind (test A50). */
    EMPLOYEE,
    /** Symmetric — separate Homes, geographically adjacent (flavor). */
    NEIGHBOR,
    /** Symmetric — social bond, no household/work tie required (flavor). */
    FRIEND,
    /** Directed — fromId trains/oversees toId (invention, canon-INFERRED, §11.3). */
    MENTOR,
    /**
     * Directed — fromId permanently resents toId (Sprint 3 quest outcomes: holder → object).
     * Never an authored-history kind ({@code HistoryRaws.ALLOWED_EDGES} does not admit it) —
     * only quest effects mint it. {@code BarkSelector} reads it speaker→listener: a grudge
     * outweighs old friendship (HOUSEHOLD &gt; GRUDGE &gt; FRIEND), so an ending stays
     * audible through the existing hostile greet tables forever.
     */
    GRUDGE
}
