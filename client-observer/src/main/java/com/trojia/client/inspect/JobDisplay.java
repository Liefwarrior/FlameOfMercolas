package com.trojia.client.inspect;

import com.trojia.sim.actor.job.Job;

/**
 * Legibility formatting for an actor's job identity (ACTORS-SPEC.md §10.4): the true job
 * id and the <em>presented</em> job id a villain rides under a cover. The presented id is
 * DERIVED from the {@link Job.Villain} cover, never stored — the single-source-of-truth
 * rule the population bake also follows. GL-free so panel text is unit-testable.
 *
 * <p>This is the one shared home for the "true vs presented" derivation the compound boot
 * path already needed ({@code CompoundBlockActorsMain.presentedJob}); both the headless
 * roster listing and the observer's live inspector panel resolve it here.
 */
public final class JobDisplay {

    /** Shown for an actor with no bound job (never happens post-bake, but kept total). */
    public static final String NONE_LABEL = "-";

    private JobDisplay() {
    }

    /** The actor's true job id, or {@link #NONE_LABEL} when {@code job} is {@code null}. */
    public static String trueJobId(Job job) {
        return job == null ? NONE_LABEL : job.id().value();
    }

    /**
     * The job id the actor presents as holding: for a {@link Job.Villain}, its cover's
     * presented job (§10.4, derived); for any other job, its own id; {@link #NONE_LABEL}
     * when {@code job} is {@code null}.
     */
    public static String presentedJobId(Job job) {
        if (job == null) {
            return NONE_LABEL;
        }
        if (job instanceof Job.Villain villain) {
            return villain.cover().presentedJob().value();
        }
        return job.id().value();
    }

    /** Whether the actor's presented job differs from its true job (i.e. it is a cover). */
    public static boolean isSecret(Job job) {
        return job != null && !trueJobId(job).equals(presentedJobId(job));
    }
}
