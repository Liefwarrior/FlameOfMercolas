package com.trojia.sim.actor;

import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobRegistry;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * A minimal, deterministic {@link ActorContext} test double: no world seed
 * quirks, a real {@link JobRegistry} bound from the committed {@code
 * jobs.json} (against zero known actor types — every job is bound, none
 * defaulted), no Wielder present. Subclass to override individual methods
 * for a specific test probe.
 */
class NoOpActorContext implements ActorContext {

    private static final JobRegistry JOBS =
            JobBinder.bind(committedJobsJson(), ActorTypes.allTypeIds());

    private static Path committedJobsJson() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws").resolve("jobs").resolve("jobs.json");
            if (Files.isRegularFile(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws/jobs/jobs.json not found above "
                + Path.of("").toAbsolutePath());
    }

    private final ActorRegistry registry;
    private final HomeRegistry homes = new HomeRegistry();
    private final RelationshipRegistry relationships = new RelationshipRegistry();
    private final ItemsLiteRegistry items = new ItemsLiteRegistry();
    private long tick = 1L;
    private final int[] drawCounters;

    NoOpActorContext(ActorRegistry registry) {
        this.registry = registry;
        this.drawCounters = new int[Math.max(1, registry.size())];
    }

    void setTick(long tick) {
        this.tick = tick;
    }

    @Override
    public long tick() {
        return tick;
    }

    @Override
    public long worldSeed() {
        return 42L;
    }

    @Override
    public ActorRegistry registry() {
        return registry;
    }

    @Override
    public HomeRegistry homes() {
        return homes;
    }

    @Override
    public RelationshipRegistry relationships() {
        return relationships;
    }

    @Override
    public ItemsLiteRegistry items() {
        return items;
    }

    @Override
    public JobRegistry jobs() {
        return JOBS;
    }

    @Override
    public long draw(ActorRngStream stream, int actorId, int drawIndex) {
        return NamedDraws.draw(stream, worldSeed(), tick, actorId, drawIndex);
    }

    @Override
    public int nextDrawIndex(int actorId) {
        return drawCounters[actorId]++;
    }

    @Override
    public int wielderCell() {
        return Actor.NONE;
    }

    @Override
    public int wielderId() {
        return Actor.NONE;
    }

    @Override
    public boolean isWalkable(int cell) {
        return true; // no world quirks (class javadoc) — every cell reads as walkable
    }
}
