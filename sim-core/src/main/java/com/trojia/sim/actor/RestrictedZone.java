package com.trojia.sim.actor;

import com.trojia.sim.actor.job.JobId;

/**
 * One baked restricted zone (Phase-0 job/access F3): a set of tagged cells that
 * only actors <em>presenting</em> a given job may access, plus the cell of the
 * guard stationed to enforce it. Immutable and baked (injected through the
 * {@link ActorsSystem} constructor exactly like {@code arrestHoldCell}), never a
 * runtime lane — so it carries no mutable state and rides no save.
 *
 * <p>The gate is {@link ActorContext#canAccess(Actor, RestrictedZone)}, which
 * compares this zone's {@link #requiredJob()} against the actor's PRESENTED job
 * ({@link ActorContext#presentedJob(Actor)}), never its true {@code jobOrdinal}
 * — reading the true job would be a disguise-bypass bug (PLAY-MODE §4).
 */
public final class RestrictedZone {

    private final JobId requiredJob;
    private final int stationedGuardCell;
    private final int[] cells;

    public RestrictedZone(JobId requiredJob, int stationedGuardCell, int[] cells) {
        this.requiredJob = requiredJob;
        this.stationedGuardCell = stationedGuardCell;
        this.cells = cells.clone();
    }

    /** The job a presenter must show to enter this zone. */
    public JobId requiredJob() {
        return requiredJob;
    }

    /** The cell the enforcing guard is stationed at, or {@link Actor#NONE} if none. */
    public int stationedGuardCell() {
        return stationedGuardCell;
    }

    /** {@code true} iff {@code cell} is one of this zone's tagged cells. */
    public boolean contains(int cell) {
        for (int c : cells) {
            if (c == cell) {
                return true;
            }
        }
        return false;
    }

    /** Number of tagged cells. */
    public int cellCount() {
        return cells.length;
    }

    /** The tagged cell at {@code index} (dense, bake order). */
    public int cellAt(int index) {
        return cells[index];
    }
}
