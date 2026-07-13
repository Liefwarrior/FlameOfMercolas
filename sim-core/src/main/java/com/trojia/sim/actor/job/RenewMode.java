package com.trojia.sim.actor.job;

/**
 * How a completed goal renews (ACTORS-SPEC.md §10.1 RENEW step).
 * {@code RHYTHM} is simplified in this foundation milestone to behave as
 * {@code IMMEDIATE} (both land the actor back in {@code SELECTING}) — a
 * documented trim, since the rhythm-window gate is already enforced by
 * {@link JobParams}'s own score bonus; a later milestone can hold the actor
 * in {@code COOLDOWN} until the window re-opens without changing this enum.
 */
public enum RenewMode {
    IMMEDIATE,
    COOLDOWN,
    RHYTHM
}
