package com.trojia.sim.actor.spell;

/**
 * HOW an effect sits in time — the second half of Daggerfall's duration axis, and the reason a
 * timed stat buff and a harm-over-time need no separate machinery.
 *
 * <p>Append-only: the ordinal is written into every save by {@link ActiveEffects}.
 */
public enum EffectMode {

    /**
     * Lands once at resolution and is over. Takes no slot, so it adds no persisted state at all
     * — an instant scald is just an hp write on the tick it happened.
     */
    INSTANT,

    /**
     * Trickles: the magnitude lands again every {@link ActiveEffects#OVER_TIME_PERIOD_TICKS}
     * until the effect's absolute end tick. Canon's own advice on how to move energy — "it would
     * be less taxing to do lots of tiny transfers than one big one" (L457).
     */
    OVER_TIME,

    /**
     * Holds: the magnitude is IN FORCE while the effect lives and simply stops when it expires.
     * Readers sum the live slots rather than banking a value anywhere, so a held warmth or a
     * held +1 AGI can never leak past its own expiry.
     */
    WHILE_ACTIVE;

    /** The mode with this ordinal (save/load path); throws on an unknown ordinal. */
    public static EffectMode of(int ordinal) {
        EffectMode[] all = values();
        if (ordinal < 0 || ordinal >= all.length) {
            throw new IllegalArgumentException("unknown EffectMode ordinal " + ordinal);
        }
        return all[ordinal];
    }
}
