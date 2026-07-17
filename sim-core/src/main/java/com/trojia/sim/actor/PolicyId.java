package com.trojia.sim.actor;

/**
 * Stable identity of a {@link BehaviorPolicy} (ACTORS-SPEC.md §1.2). Append
 * only — never reorder or remove entries once an id has shipped in a save.
 *
 * <p>This foundation milestone ships the starter library named by the F2.5
 * assignment: the two EMERGENCY-band overrides ({@link #DEFER_WIELDER},
 * {@link #FLEE}), the one JOB-band delegate to the Job taxonomy
 * ({@link #GOAL_PURSUE}, §10.1), the two NEED-band urgency policies
 * ({@link #RETURN_HOME} from the Home addendum, §11.1, and {@link #SEEK_FOOD}
 * from the needs-hierarchy pass, §3.3), and the universal IDLE fallback
 * ({@link #LOITER}). The full v1 library (§1.3 — APPREHEND, VEND, PATROL, …)
 * is a later extension of this same append-only enum; no engine change is
 * required to add a row (§1.4).
 *
 * <p>{@link #SEEK_FOOD} is appended last (enum declaration order is
 * append-only and independent of any actor's stack order, §1.2) even though
 * it conceptually sits between {@link #FLEE} and {@link #RETURN_HOME} in
 * every stack that declares it.
 */
public enum PolicyId {
    DEFER_WIELDER,
    FLEE,
    GOAL_PURSUE,
    RETURN_HOME,
    LOITER,
    SEEK_FOOD,
    /** In-custody EMERGENCY-band override (arrest/hold/release, ARREST-SPEC addendum). */
    HELD,
    /** Permanent EMERGENCY-band override for a hanged Skyrunner (ARREST-SPEC addendum). */
    EXECUTED,
    /** Play mode's direct-control override (PLAY-MODE-SPEC.md §5.2). */
    PLAYER_CONTROL,
    /**
     * Watch-side enforcement (law &amp; order pass, Pass 11): sense an offender in a
     * restricted/policed zone (or a bank-queue jumper), intercept, then correct —
     * warn → fine → arrest. The guard-side inversion of the old villain-side
     * self-arrest hack.
     */
    APPREHEND
}
