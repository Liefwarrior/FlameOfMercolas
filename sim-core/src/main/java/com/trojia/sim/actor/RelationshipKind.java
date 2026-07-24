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
    GRUDGE,
    /**
     * Symmetric — open known adversaries (Sprint 5 vocabulary completion, the S4 debt):
     * the authored feuds' proper kind, migrated off the NEIGHBOR placeholder.
     * {@code BarkSelector} greets a rival hostile (below GRUDGE in tie priority — a
     * quest-minted grudge is personal and directed; a rivalry is mutual sport).
     */
    RIVAL,
    /**
     * Directed — fromId holds paper on toId (creditor → debtor; Sprint 5 vocabulary
     * completion): the authored debts' proper kind, migrated off the MENTOR placeholder's
     * "oversees" sense. No tick-path consumer yet — legibility first, mechanics when the
     * economy grows a debt-collection verb.
     */
    DEBTOR,
    /**
     * Symmetric — a courting or kept-quiet attachment (Sprint 5 vocabulary completion):
     * the authored romances' proper kind, migrated off the FRIEND placeholder.
     * {@code BarkSelector} greets it as a friend and {@code Barter} keeps the kitchen-price
     * discount — the exact behavior the placeholder bought, now legible on the sheet.
     */
    ROMANCE,
    /**
     * Symmetric — blood family (Sprint 5 kinship pass): the compound family houses' members
     * are KIN over and above sharing a Home. HOUSEHOLD stays the co-residence tie (lodgers,
     * bunked crews); KIN is the blood tie — the Rows' eighteen hammocks are one household
     * and zero kin. Reads as "kin" everywhere HOUSEHOLD does.
     */
    KIN
}
