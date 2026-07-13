package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorRawsValidationException;
import com.trojia.sim.actor.ActorTypeId;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Job binder's fail-fast 1:1 contract, both directions (ACTORS-SPEC.md
 * §10.2/§10.3, test A34's shape).
 */
final class JobBinderTest {

    private static final List<ActorTypeId> KNOWN_TYPES = List.of(
            ActorTypeId.of("militia_watch"), ActorTypeId.of("serf"), ActorTypeId.of("wastrel"),
            ActorTypeId.of("priest_of_the_flame"), ActorTypeId.of("disciple_of_the_flame"),
            ActorTypeId.of("shopkeeper"), ActorTypeId.of("animal_keeper"),
            ActorTypeId.of("animal"), ActorTypeId.of("feral"));

    @TempDir
    Path temp;

    static Path committedJobsJson() {
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

    @Test
    void bindsTheCommittedJobsJsonWithExactlyOneEntryPerLeaf() {
        JobRegistry registry = JobBinder.bind(committedJobsJson(), KNOWN_TYPES);
        assertEquals(Jobs.ALL.size(), registry.size());
        for (Jobs.Registration reg : Jobs.ALL) {
            assertTrue(registry.ordinalOf(reg.id()) >= 0,
                    "every registered leaf must be bound: " + reg.id());
        }
    }

    @Test
    void everyKnownActorTypeGetsExactlyOneDefaultJob() {
        JobRegistry registry = JobBinder.bind(committedJobsJson(), KNOWN_TYPES);
        for (ActorTypeId type : KNOWN_TYPES) {
            // Throws if no default is bound; this call is the assertion.
            registry.defaultOrdinalFor(type);
        }
    }

    @Test
    void jsonEntryWithNoRegisteredLeafClassFailsFast() throws IOException {
        List<JobRaw> raws = new java.util.ArrayList<>(loadCommitted());
        raws.add(unknownLeafRaw());
        ActorRawsValidationException e = assertThrows(ActorRawsValidationException.class,
                () -> JobBinder.bind(raws, KNOWN_TYPES));
        assertTrue(e.getMessage().contains("no registered Job leaf class"),
                "unexpected message: " + e.getMessage());
    }

    @Test
    void registeredLeafWithNoJsonEntryFailsFast() {
        List<JobRaw> raws = new java.util.ArrayList<>(loadCommitted());
        raws.removeIf(r -> r.id().equals(Job.Beast.Feral.ID));
        ActorRawsValidationException e = assertThrows(ActorRawsValidationException.class,
                () -> JobBinder.bind(raws, KNOWN_TYPES));
        assertTrue(e.getMessage().contains("has no jobs.json entry"),
                "unexpected message: " + e.getMessage());
    }

    @Test
    void actorTypeMissingFromEveryDefaultForFailsFast() {
        List<JobRaw> raws = loadCommitted();
        List<ActorTypeId> withUnbound = new java.util.ArrayList<>(KNOWN_TYPES);
        withUnbound.add(ActorTypeId.of("ratcatcher")); // present as a "known type", absent from defaultFor
        ActorRawsValidationException e = assertThrows(ActorRawsValidationException.class,
                () -> JobBinder.bind(raws, withUnbound));
        assertTrue(e.getMessage().contains("no default job"),
                "unexpected message: " + e.getMessage());
    }

    @Test
    void secretJobWithoutCoverFailsFast() {
        List<JobRaw> raws = new java.util.ArrayList<>(loadCommitted());
        int i = indexOf(raws, Job.Villain.Cutpurse.ID);
        JobRaw original = raws.get(i);
        raws.set(i, new JobRaw(original.file(), original.id(), original.goalKind(),
                original.priority(), original.rhythmStart(), original.rhythmEnd(),
                original.rhythmBonus(), original.workTicksPerUnit(), original.unitsToComplete(),
                original.renewMode(), original.cooldownTicks(), original.assign(),
                original.defaultFor(), true, null));
        ActorRawsValidationException e = assertThrows(ActorRawsValidationException.class,
                () -> JobBinder.bind(raws, KNOWN_TYPES));
        assertTrue(e.getMessage().contains("must declare a cover block"),
                "unexpected message: " + e.getMessage());
    }

    private static int indexOf(List<JobRaw> raws, JobId id) {
        for (int i = 0; i < raws.size(); i++) {
            if (raws.get(i).id().equals(id)) {
                return i;
            }
        }
        throw new IllegalStateException("not found: " + id);
    }

    private static List<JobRaw> loadCommitted() {
        return JobRawsLoader.load(committedJobsJson());
    }

    private static JobRaw unknownLeafRaw() {
        return new JobRaw("test", JobId.of("nonexistent.job"), GoalKind.HAUL_WORK, 150,
                0, 1000, 0, 10, 1, RenewMode.IMMEDIATE, 0, List.of(), List.of(), false, null);
    }
}
