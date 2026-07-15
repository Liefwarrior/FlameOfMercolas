package com.trojia.sim.actor;

/**
 * The global (placeholder) need-band thresholds (ACTORS-SPEC.md §3.1): all
 * comparisons are strict {@code </>=} on integers, boundary semantics pinned
 * by the spec's test A11 — CRITICAL is checked before LOW because every value
 * below CRITICAL is also below LOW.
 */
public final class NeedThresholds {

    /** Need-band policies activate below this reserve. */
    public static final int LOW = 3_000;

    /** Need scores jump a band below this reserve; barks turn desperate. */
    public static final int CRITICAL = 1_000;

    /**
     * Reserve level a NEED-band policy must climb back up to (via its at-home
     * recovery) before releasing the actor back to its job, rather than the
     * instant it merely stops being {@link #isLow}. Fixes the SEEK_FOOD /
     * RETURN_HOME <-> GOAL_PURSUE oscillation (arrive home -> one recovery
     * tick nudges the reserve just above {@code LOW} -> the NEED policy's
     * score drops to 0 -> GOAL_PURSUE immediately wins and walks the actor
     * straight back out -> repeat): double {@link #LOW}, comfortably inside
     * every committed type's HUNGER/REST {@code start} range (6000-9000), so
     * a single recovery tick standing on the home cell can never satisfy it —
     * the actor stays put, letting the always-unconditional home recovery
     * climb the reserve for a bounded few hundred ticks (at the documented
     * +12/+6-per-tick recovery rates), then releases.
     */
    public static final int RECOVERED = 6_000;

    /** Saturating clamp ceiling for every need reserve. */
    public static final int MAX = 10_000;

    private NeedThresholds() {
    }

    /** {@code true} iff {@code reserve < CRITICAL} (checked before {@link #isLow}). */
    public static boolean isCritical(int reserve) {
        return reserve < CRITICAL;
    }

    /** {@code true} iff {@code reserve < LOW} (CRITICAL implies LOW). */
    public static boolean isLow(int reserve) {
        return reserve < LOW;
    }

    /** {@code true} iff {@code reserve >= RECOVERED} — see {@link #RECOVERED}'s javadoc. */
    public static boolean isRecovered(int reserve) {
        return reserve >= RECOVERED;
    }

    /** Saturating clamp to {@code [0, MAX]} (ACTORS-SPEC.md §3.2 — test A12). */
    public static int clamp(int reserve) {
        if (reserve < 0) {
            return 0;
        }
        return Math.min(reserve, MAX);
    }
}
