package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorTypeId;

import java.util.List;

/**
 * One parsed-but-unbound {@code jobs.json} entry (ACTORS-SPEC.md §10.3),
 * before {@link JobBinder} matches it against a {@link Job} leaf class.
 *
 * <p>Sprint-5 training fields (PROGRESSION-SPEC.md §2's job training map):
 * {@code trainsSkill} is the skills.json key this job's completed work trains
 * ({@code null} when the job trains nothing — villains, beasts, the PC seam),
 * and {@code trainCp} is the §3.1 base award in cp per discrete work event
 * ({@code 0} when {@code trainsSkill} is {@code null}). The raws pair is
 * parsed and carried here; resolving the key against the skill registry and
 * wiring the award seams is the Sim lane's binder/behavior work.</p>
 *
 * <p>Sprint-6 DUTY field: {@code dutyPerUnit} is the DUTY reserve one discrete
 * work event restores ({@code 0} when the entry omits it — the non-civic set).</p>
 *
 * <p>S8 YIELD pair: {@code yieldItem} is the {@link com.trojia.sim.actor.TradeGoods} symbol
 * this job's completed work mints ({@code null} when it produces nothing you can hold — which
 * is every pre-S8 job), and {@code yieldPerUnit} is how many units per discrete work event.
 * The symbol is resolved to an {@link com.trojia.sim.actor.ItemKinds} id by {@link
 * JobBinder}, the same way {@code trainsSkill} resolves to a skill raw index.</p>
 */
record JobRaw(
        String file,
        JobId id,
        GoalKind goalKind,
        int priority,
        int rhythmStart,
        int rhythmEnd,
        int rhythmBonus,
        int workTicksPerUnit,
        int unitsToComplete,
        RenewMode renewMode,
        int cooldownTicks,
        List<AssignWeight> assign,
        List<ActorTypeId> defaultFor,
        boolean secret,
        CoverSpec cover,
        String trainsSkill,
        int trainCp,
        int dutyPerUnit,
        String yieldItem,
        int yieldPerUnit) {

    /**
     * Yield-less convenience constructor (the pre-S8 shape): every field as before, no yield.
     * Kept so no existing binder test has to mention the S8 pair.
     */
    JobRaw(String file, JobId id, GoalKind goalKind, int priority, int rhythmStart, int rhythmEnd,
            int rhythmBonus, int workTicksPerUnit, int unitsToComplete, RenewMode renewMode,
            int cooldownTicks, List<AssignWeight> assign, List<ActorTypeId> defaultFor,
            boolean secret, CoverSpec cover, String trainsSkill, int trainCp, int dutyPerUnit) {
        this(file, id, goalKind, priority, rhythmStart, rhythmEnd, rhythmBonus, workTicksPerUnit,
                unitsToComplete, renewMode, cooldownTicks, assign, defaultFor, secret, cover,
                trainsSkill, trainCp, dutyPerUnit, null, 0);
    }

    record AssignWeight(ActorTypeId actorType, int weight) {
    }
}
