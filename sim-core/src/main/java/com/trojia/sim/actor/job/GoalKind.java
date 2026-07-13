package com.trojia.sim.actor.job;

/**
 * The append-only goal-kind vocabulary (ACTORS-SPEC.md §10.1): one entry per
 * {@link Job} leaf, used only for observer legibility (§10.5) and raws
 * validation (§10.3 item 5) — the goal <em>mechanics</em> in this foundation
 * milestone are the shared generic pursue-at-anchor cycle
 * ({@link JobBehaviors}); richer per-kind behavior (purse-lifting, route
 * waylay, …) is a later extension that does not change this enum's shape.
 */
public enum GoalKind {
    TEND_PLOT,
    HAUL_WORK,
    STREETLIFE,
    WAYLAY_ROUTE,
    LIFT_PURSE,
    PC_SEAM,
    PATROL_ROUTE,
    ALMS_CYCLE,
    CARRY_RUN,
    STALL_CYCLE,
    TEND_BEASTS,
    STAY_NEAR_OWNER,
    SCAVENGE_CIRCUIT,
    BURGLE_ROOST
}
