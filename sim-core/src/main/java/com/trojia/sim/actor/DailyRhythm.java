package com.trojia.sim.actor;

/**
 * The daily rhythm clock (ACTORS-SPEC.md §3.4, placeholder): {@code DAY =
 * 24,000} ticks (40 real minutes at 100 ms/tick); dawn 0, noon 6,000, dusk
 * 12,000, midnight 18,000. Rhythm windows add a JOB/NEED-band score bias —
 * schedules are additive scoring, never a scripting layer (dossier principle 6).
 */
public final class DailyRhythm {

    public static final long DAY = 24_000L;

    private DailyRhythm() {
    }

    public static long tickOfDay(long tick) {
        return tick % DAY;
    }
}
