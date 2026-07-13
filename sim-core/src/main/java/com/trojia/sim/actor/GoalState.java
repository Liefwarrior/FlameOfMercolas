package com.trojia.sim.actor;

/**
 * The Job goal lifecycle state (ACTORS-SPEC.md §10.1). {@code COMPLETE} and
 * {@code RENEW} are transient steps applied within the same tick a goal
 * finishes {@code PURSUING} (never stored) — the renew rule immediately
 * lands the actor back in {@code SELECTING} or {@code COOLDOWN}.
 */
public enum GoalState {
    SELECTING,
    PURSUING,
    COOLDOWN
}
