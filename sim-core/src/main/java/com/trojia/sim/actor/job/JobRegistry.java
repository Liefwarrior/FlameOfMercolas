package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorTypeId;

import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/**
 * The bound, immutable job table (ACTORS-SPEC.md §10.2): jobs sorted by
 * {@link JobId} — ordinals are the sorted index, append-only for
 * {@code ACTR} stability (a new job sorts wherever its id lands; since ids
 * are never renumbered mid-save-format-life this is stable within one raws
 * generation). Built only by {@link JobBinder}.
 */
public final class JobRegistry {

    /** One actor type's civic default job (§10.4), sorted by actor type id. */
    public record DefaultJob(ActorTypeId actorType, int jobOrdinal) {
    }

    private final Job[] jobs;
    private final DefaultJob[] defaults;

    JobRegistry(Job[] jobsSortedById, DefaultJob[] defaultsSortedByType) {
        this.jobs = jobsSortedById;
        this.defaults = defaultsSortedByType;
    }

    public int size() {
        return jobs.length;
    }

    /** The job bound at ordinal {@code jobOrdinal} (0-based, sorted-id order). */
    public Job get(int jobOrdinal) {
        return jobs[jobOrdinal];
    }

    /** The ordinal of {@code id}, or {@code -1} if unbound. */
    public int ordinalOf(JobId id) {
        int lo = 0;
        int hi = jobs.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            int cmp = jobs[mid].id().compareTo(id);
            if (cmp == 0) {
                return mid;
            }
            if (cmp < 0) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return -1;
    }

    /** The civic default job ordinal for {@code actorType}; every registered type has one. */
    public int defaultOrdinalFor(ActorTypeId actorType) {
        for (DefaultJob d : defaults) {
            if (d.actorType().equals(actorType)) {
                return d.jobOrdinal();
            }
        }
        throw new IllegalArgumentException(
                "no default job bound for actor type \"" + actorType + "\"");
    }

    /** All bound jobs, ascending ordinal order (canonical iteration, ArchUnit A34 shape). */
    public List<Job> all() {
        return List.of(jobs);
    }

    static JobRegistry of(List<Job> unsorted, List<DefaultJob> defaultsUnsorted) {
        Job[] sorted = unsorted.toArray(new Job[0]);
        Arrays.sort(sorted, Comparator.comparing(Job::id));
        DefaultJob[] defSorted = defaultsUnsorted.toArray(new DefaultJob[0]);
        Arrays.sort(defSorted, Comparator.comparing(d -> d.actorType().key()));
        return new JobRegistry(sorted, defSorted);
    }
}
