package com.trojia.sim.actor;

/**
 * The five-element needs vector (ACTORS-SPEC.md §3.1), append-only. Stored as
 * a reserve {@code 0..10,000} on {@link Actor#needs()} (high = satisfied); the
 * two clergy types relabel {@link #DUTY} as "FAITH" in UI only (§3.1) — same
 * mechanism, same slot.
 */
public enum Need {
    HUNGER,
    REST,
    COIN,
    SAFETY,
    DUTY;

    /** The fixed vector width; {@link Actor#needs()} is always this long. */
    public static final int COUNT = values().length;
}
