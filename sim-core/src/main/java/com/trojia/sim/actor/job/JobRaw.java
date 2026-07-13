package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorTypeId;

import java.util.List;

/**
 * One parsed-but-unbound {@code jobs.json} entry (ACTORS-SPEC.md §10.3),
 * before {@link JobBinder} matches it against a {@link Job} leaf class.
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
        CoverSpec cover) {

    record AssignWeight(ActorTypeId actorType, int weight) {
    }
}
