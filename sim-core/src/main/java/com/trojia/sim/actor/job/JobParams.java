package com.trojia.sim.actor.job;

/**
 * The immutable, raws-bound data half of a {@link Job} (ACTORS-SPEC.md §10):
 * classes define type identity + goal behavior, JSON defines these numbers.
 * One instance per bound leaf, injected by {@link JobBinder}; a leaf never
 * mutates it (test-equivalent of A34's statelessness rule).
 *
 * <p>This foundation milestone uses one shared param shape for every job's
 * goal mechanics (the generic anchor-cycle behavior in {@link JobBehaviors}):
 * the richer per-goal-kind fields in ACTORS-SPEC.md §10.3's worked examples
 * ({@code liftChanceQ16}, {@code marksPerDay}, …) are a later extension that
 * would add fields here without changing the binder's 1:1 contract.
 *
 * @param goalKind         legibility/validation tag (§10.3 item 5)
 * @param priority         base JOB-band score, {@code [100, 299]} (§10.3 item 6)
 * @param rhythmWindowStart tick-of-day window start, {@code [0, DAY)}
 * @param rhythmWindowEnd   tick-of-day window end, {@code [0, DAY]}, {@code >= start}
 * @param rhythmBonus      score bonus while inside the window
 * @param workTicksPerUnit ticks of {@code pursue()} per progress unit
 * @param unitsToComplete  progress units to reach {@code isComplete()}
 * @param renewMode        what happens after completion
 * @param cooldownTicks    cooldown length when {@code renewMode == COOLDOWN}
 */
public record JobParams(
        GoalKind goalKind,
        int priority,
        int rhythmWindowStart,
        int rhythmWindowEnd,
        int rhythmBonus,
        int workTicksPerUnit,
        int unitsToComplete,
        RenewMode renewMode,
        int cooldownTicks) {

    /** The JOB behavior score band (ACTORS-SPEC.md §1.2). */
    public static final int JOB_BAND_MIN = 100;
    public static final int JOB_BAND_MAX = 299;

    public JobParams {
        if (priority < JOB_BAND_MIN || priority > JOB_BAND_MAX) {
            throw new IllegalArgumentException(
                    "priority must be in the JOB band [100,299]: " + priority);
        }
        if (rhythmWindowStart < 0 || rhythmWindowEnd < rhythmWindowStart) {
            throw new IllegalArgumentException("invalid rhythm window ["
                    + rhythmWindowStart + ", " + rhythmWindowEnd + "]");
        }
        if (workTicksPerUnit < 1) {
            throw new IllegalArgumentException("workTicksPerUnit must be >= 1: " + workTicksPerUnit);
        }
        if (unitsToComplete < 1) {
            throw new IllegalArgumentException("unitsToComplete must be >= 1: " + unitsToComplete);
        }
        if (cooldownTicks < 0) {
            throw new IllegalArgumentException("cooldownTicks must be >= 0: " + cooldownTicks);
        }
    }

    /** Whether {@code tickOfDay} falls inside {@code [rhythmWindowStart, rhythmWindowEnd)}. */
    public boolean inWindow(long tickOfDay) {
        return tickOfDay >= rhythmWindowStart && tickOfDay < rhythmWindowEnd;
    }
}
