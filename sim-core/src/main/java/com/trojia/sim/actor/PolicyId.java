package com.trojia.sim.actor;

/**
 * Stable identity of a {@link BehaviorPolicy} (ACTORS-SPEC.md §1.2). Append
 * only — never reorder or remove entries once an id has shipped in a save.
 *
 * <p>This foundation milestone ships the starter library named by the F2.5
 * assignment: the two EMERGENCY-band overrides ({@link #DEFER_WIELDER},
 * {@link #FLEE}), the one JOB-band delegate to the Job taxonomy
 * ({@link #GOAL_PURSUE}, §10.1), the new NEED-band sleep-at-home policy from
 * the Home addendum ({@link #RETURN_HOME}, §11.1), and the universal IDLE
 * fallback ({@link #LOITER}). The full v1 library (§1.3 — APPREHEND, VEND,
 * PATROL, …) is a later extension of this same append-only enum; no engine
 * change is required to add a row (§1.4).
 */
public enum PolicyId {
    DEFER_WIELDER,
    FLEE,
    GOAL_PURSUE,
    RETURN_HOME,
    LOITER
}
