package com.trojia.sim.actor.job;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * The Job leaf registration list (ACTORS-SPEC.md §10.2's {@code Jobs.ALL}):
 * one constructor reference per leaf class, mirroring the {@code ActorTypes}
 * pattern (§1.4). {@link JobBinder} matches these against {@code jobs.json}
 * 1:1, fail-fast both directions.
 *
 * <p>Add a job (§10.2 walkthrough): one new nested {@link Job} leaf class +
 * one line here + one {@code jobs.json} entry. No engine change.
 */
public final class Jobs {

    /** One leaf's binding: its id and a factory taking raws-bound params (+ cover, if any). */
    public record Registration(JobId id, JobFactory factory) {
    }

    /** Factory signature shared by every leaf; {@code cover} is {@code null} outside Villain. */
    @FunctionalInterface
    public interface JobFactory {
        Job create(JobParams params, CoverSpec cover);
    }

    /** Every registered leaf, sorted by id — the append-only taxonomy roster. */
    public static final List<Registration> ALL = sorted(List.of(
            new Registration(Job.Serf.Farmer.ID, (p, c) -> new Job.Serf.Farmer(p)),
            new Registration(Job.Serf.Laborer.ID, (p, c) -> new Job.Serf.Laborer(p)),
            new Registration(Job.Wastrel.Streetlife.ID, (p, c) -> new Job.Wastrel.Streetlife(p)),
            new Registration(Job.Villain.Robber.ID, Job.Villain.Robber::new),
            new Registration(Job.Villain.Cutpurse.ID, Job.Villain.Cutpurse::new),
            new Registration(Job.FlameOfMerc.ID, (p, c) -> new Job.FlameOfMerc(p)),
            new Registration(Job.Watch.Patrol.ID, (p, c) -> new Job.Watch.Patrol(p)),
            new Registration(Job.Clergy.Shepherd.ID, (p, c) -> new Job.Clergy.Shepherd(p)),
            new Registration(Job.Clergy.Acolyte.ID, (p, c) -> new Job.Clergy.Acolyte(p)),
            new Registration(Job.Trade.Stallkeep.ID, (p, c) -> new Job.Trade.Stallkeep(p)),
            new Registration(Job.Husbandry.Keeper.ID, (p, c) -> new Job.Husbandry.Keeper(p)),
            new Registration(Job.Beast.Chattel.ID, (p, c) -> new Job.Beast.Chattel(p)),
            new Registration(Job.Beast.Feral.ID, (p, c) -> new Job.Beast.Feral(p))));

    private Jobs() {
    }

    private static List<Registration> sorted(List<Registration> raw) {
        List<Registration> copy = new ArrayList<>(raw);
        copy.sort(Comparator.comparing(r -> r.id().value()));
        return List.copyOf(copy);
    }
}
