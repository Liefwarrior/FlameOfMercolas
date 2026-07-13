package com.trojia.sim.actor.job;

import java.util.Objects;

/**
 * A {@code jobs.json} job identity (ACTORS-SPEC.md §10): a dotted, lower-case
 * key mirroring the {@link Job} nested-class path, e.g.
 * {@code "villain.cutpurse"} for {@code Job.Villain.Cutpurse}.
 *
 * @param value the dotted id
 */
public record JobId(String value) implements Comparable<JobId> {

    public JobId {
        Objects.requireNonNull(value, "value");
        if (value.isEmpty()) {
            throw new IllegalArgumentException("job id must be non-empty");
        }
    }

    public static JobId of(String value) {
        return new JobId(value);
    }

    @Override
    public int compareTo(JobId other) {
        return value.compareTo(other.value);
    }

    @Override
    public String toString() {
        return value;
    }
}
