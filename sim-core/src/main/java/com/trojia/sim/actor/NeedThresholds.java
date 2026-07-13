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

    /** Saturating clamp to {@code [0, MAX]} (ACTORS-SPEC.md §3.2 — test A12). */
    public static int clamp(int reserve) {
        if (reserve < 0) {
            return 0;
        }
        return Math.min(reserve, MAX);
    }
}
