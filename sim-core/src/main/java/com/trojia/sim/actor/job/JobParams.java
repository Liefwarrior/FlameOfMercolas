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
 * <p><b>Sprint-5 training pair</b> (PROGRESSION-SPEC.md §2's job training map): {@code
 * trainSkillRaw} is the skill this job's completed work trains, RESOLVED at bind time to its
 * raw registry index by {@link JobBinder} (or {@link #TRAINS_NOTHING} for the non-training
 * set — villains, beasts, the PC seam — and for legacy skill-less binds, where training lies
 * dormant); {@code trainCp} is the §3.1 base award in cp per discrete work event. Award
 * seams live in {@link JobBehaviors} (unit completion / waypoint arrival / dwell completion
 * — §3.2 rule 4: never per-tick).
 *
 * @param goalKind         legibility/validation tag (§10.3 item 5)
 * @param priority         base JOB-band score, {@code [100, 299]} (§10.3 item 6)
 * @param rhythmWindowStart tick-of-day window start, {@code [0, DAY)}
 * @param rhythmWindowEnd   tick-of-day window end, {@code [0, DAY]}, {@code >= start}
 * @param rhythmBonus      score bonus while inside the window
 * @param workTicksPerUnit ticks of {@code pursue()} per progress unit
 * @param unitsToComplete  progress units to reach {@code isComplete()}
 * <p><b>Sprint-6 DUTY pair</b> (Eli's live-ops bug 1 — "no one has a way to gain DUTY"):
 * {@code dutyPerUnit} is the DUTY reserve restored by one discrete work event, applied at
 * exactly the same three seams the training pair rides (unit completion / waypoint arrival /
 * dwell completion) — honest work at one's station IS how duty refills. Data-driven per job
 * (the {@code trainsSkill} pattern); {@code 0} for the non-civic set (beasts, villains, the
 * PC seam) and for every type whose DUTY never decays. The committed raws are pinned by
 * {@code JobDutyCommittedTest}: any actor type whose DUTY decays has a default job with a
 * positive {@code dutyPerUnit} — no employed type can bottom out.
 *
 * @param renewMode        what happens after completion
 * @param cooldownTicks    cooldown length when {@code renewMode == COOLDOWN}
 * @param trainSkillRaw    resolved skill raw this job trains, or {@link #TRAINS_NOTHING}
 * @param trainCp          §3.1 base cp per discrete work event ({@code 0} when untrained)
 * @param dutyPerUnit      DUTY reserve restored per discrete work event ({@code >= 0})
 * <p><b>S8 YIELD pair</b> ("The Ward Prices Itself" — the crafts finally produce something the
 * ward can price): {@code yieldKind} is the {@link com.trojia.sim.actor.ItemKinds} id one
 * discrete work event mints into the worker's own carry, and {@code yieldPerUnit} is how many
 * units. Both {@code 0} ({@link #YIELDS_NOTHING}) for every job that produces nothing you can
 * hold — which is every job that existed before S8. Applied at exactly the seams the S5
 * training pair and the S6 DUTY pair already ride (unit completion / waypoint arrival / dwell
 * completion) and NEVER per tick: a rope is finished, not accrued by the second. The pair is
 * both-or-neither, exactly like {@code trainSkillRaw}/{@code trainCp}.
 *
 * @param yieldKind        item kind minted per discrete work event, or {@link #YIELDS_NOTHING}
 * @param yieldPerUnit     units minted per discrete work event ({@code >= 0})
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
        int cooldownTicks,
        int trainSkillRaw,
        int trainCp,
        int dutyPerUnit,
        short yieldKind,
        int yieldPerUnit) {

    /** The JOB behavior score band (ACTORS-SPEC.md §1.2). */
    public static final int JOB_BAND_MIN = 100;
    public static final int JOB_BAND_MAX = 299;

    /** {@code trainSkillRaw} sentinel: this job's work trains no skill. */
    public static final int TRAINS_NOTHING = -1;

    /**
     * {@code yieldKind} sentinel: this job's work mints nothing. {@code 0} is safe as a
     * sentinel because {@link com.trojia.sim.actor.ItemKinds} ids start at 1 (COIN) and the
     * vocabulary is append-only, so no real kind can ever collide with it.
     */
    public static final short YIELDS_NOTHING = 0;

    /**
     * Training-less convenience constructor (the pre-Sprint-5 shape): every field as before,
     * {@code trainSkillRaw = }{@link #TRAINS_NOTHING}{@code , trainCp = 0}. Kept so goal-only
     * tests and synthetic params never mention training.
     */
    public JobParams(GoalKind goalKind, int priority, int rhythmWindowStart, int rhythmWindowEnd,
            int rhythmBonus, int workTicksPerUnit, int unitsToComplete, RenewMode renewMode,
            int cooldownTicks) {
        this(goalKind, priority, rhythmWindowStart, rhythmWindowEnd, rhythmBonus,
                workTicksPerUnit, unitsToComplete, renewMode, cooldownTicks, TRAINS_NOTHING, 0, 0);
    }

    /**
     * Duty-less convenience constructor (the pre-Sprint-6 canonical shape): every field as
     * before, {@code dutyPerUnit = 0}. Kept so the Sprint-5 training tests and synthetic
     * params never mention the DUTY pair.
     */
    public JobParams(GoalKind goalKind, int priority, int rhythmWindowStart, int rhythmWindowEnd,
            int rhythmBonus, int workTicksPerUnit, int unitsToComplete, RenewMode renewMode,
            int cooldownTicks, int trainSkillRaw, int trainCp) {
        this(goalKind, priority, rhythmWindowStart, rhythmWindowEnd, rhythmBonus,
                workTicksPerUnit, unitsToComplete, renewMode, cooldownTicks, trainSkillRaw,
                trainCp, 0);
    }

    /**
     * Yield-less convenience constructor (the pre-S8 canonical shape): every field as before,
     * {@code yieldKind = }{@link #YIELDS_NOTHING}{@code , yieldPerUnit = 0}. The exact shape of
     * the Sprint-6 duty-less constructor above, and kept for the same reason — no existing
     * test, and no synthetic param anywhere, has to learn about the YIELD pair to keep
     * compiling.
     */
    public JobParams(GoalKind goalKind, int priority, int rhythmWindowStart, int rhythmWindowEnd,
            int rhythmBonus, int workTicksPerUnit, int unitsToComplete, RenewMode renewMode,
            int cooldownTicks, int trainSkillRaw, int trainCp, int dutyPerUnit) {
        this(goalKind, priority, rhythmWindowStart, rhythmWindowEnd, rhythmBonus,
                workTicksPerUnit, unitsToComplete, renewMode, cooldownTicks, trainSkillRaw,
                trainCp, dutyPerUnit, YIELDS_NOTHING, 0);
    }

    public JobParams {
        if (priority < JOB_BAND_MIN || priority > JOB_BAND_MAX) {
            throw new IllegalArgumentException(
                    "priority must be in the JOB band [100,299]: " + priority);
        }
        if (rhythmWindowStart < 0 || rhythmWindowEnd < rhythmWindowStart) {
            throw new IllegalArgumentException("invalid rhythm window ["
                    + rhythmWindowStart + ", " + rhythmWindowEnd + "]");
        }
        if (priority + (long) rhythmBonus > JOB_BAND_MAX) {
            // GoalPursuePolicy.score() = priority + (inWindow ? rhythmBonus : 0), and the
            // NEED-band RETURN_HOME policy is priced at a fixed 305 specifically so it always
            // outranks every JOB-band score (ACTORS-SPEC.md's RETURN_HOME addendum: "this
            // deliberately outranks every JOB-band policy (100-299)... the same way a
            // NEED-band policy always outranks JOB today"). If priority + rhythmBonus could
            // exceed JOB_BAND_MAX, an in-window job could outscore RETURN_HOME and an actor
            // that ever strays from its home cell would never be able to walk back (its REST
            // need would decay forever). Keeping the in-window total inside the JOB band
            // makes that invariant hold by construction instead of by authoring discipline.
            throw new IllegalArgumentException("priority + rhythmBonus must not exceed the "
                    + "JOB band ceiling " + JOB_BAND_MAX + " (else an in-window job could "
                    + "outrank NEED-band RETURN_HOME, priced at 305): " + priority + " + "
                    + rhythmBonus);
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
        if (trainSkillRaw < 0 && trainSkillRaw != TRAINS_NOTHING) {
            throw new IllegalArgumentException(
                    "trainSkillRaw must be a raw index or TRAINS_NOTHING: " + trainSkillRaw);
        }
        if (trainSkillRaw == TRAINS_NOTHING && trainCp != 0) {
            throw new IllegalArgumentException(
                    "trainCp without a trained skill (the pair is both-or-neither): " + trainCp);
        }
        if (trainSkillRaw != TRAINS_NOTHING && trainCp < 1) {
            throw new IllegalArgumentException(
                    "a trained skill needs trainCp >= 1, got " + trainCp);
        }
        if (dutyPerUnit < 0) {
            throw new IllegalArgumentException("dutyPerUnit must be >= 0: " + dutyPerUnit);
        }
        if (yieldKind < 0) {
            throw new IllegalArgumentException("yieldKind must be an ItemKinds id or "
                    + "YIELDS_NOTHING: " + yieldKind);
        }
        if (yieldPerUnit < 0) {
            throw new IllegalArgumentException("yieldPerUnit must be >= 0: " + yieldPerUnit);
        }
        if ((yieldKind == YIELDS_NOTHING) != (yieldPerUnit == 0)) {
            throw new IllegalArgumentException("the yield pair is both-or-neither (kind "
                    + yieldKind + ", perUnit " + yieldPerUnit + ")");
        }
    }

    /** Whether this job's completed work trains a skill (resolved at bind time). */
    public boolean trains() {
        return trainSkillRaw != TRAINS_NOTHING;
    }

    /** Whether this job's completed work mints a trade good into the worker's carry. */
    public boolean yields() {
        return yieldKind != YIELDS_NOTHING && yieldPerUnit > 0;
    }

    /** Whether {@code tickOfDay} falls inside {@code [rhythmWindowStart, rhythmWindowEnd)}. */
    public boolean inWindow(long tickOfDay) {
        return tickOfDay >= rhythmWindowStart && tickOfDay < rhythmWindowEnd;
    }
}
