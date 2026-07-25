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
        int dutyPerUnit) {

    record AssignWeight(ActorTypeId actorType, int weight) {
    }
}
