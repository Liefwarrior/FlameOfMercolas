package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorRawsValidationException;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.progression.SkillRegistry;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-5 training RESOLUTION contract on {@link JobBinder} (the awards wave's bind
 * half): binding with the boot-built skill universe resolves every {@code trainsSkill} key
 * to a raw index on the bound {@link JobParams}; an unknown key is a LOUD bind failure (the
 * frame-guard discipline); the legacy skill-less bind leaves training dormant; and the
 * non-training set (villains, beasts, the PC seam, streetlife) binds trainless — the
 * raws-level pin ({@code JobTrainingCommittedTest}) carried through to the live params the
 * award seams actually read.
 */
final class JobBinderTrainingTest {

    private static final List<ActorTypeId> KNOWN_TYPES = List.of(
            ActorTypeId.of("militia_watch"), ActorTypeId.of("serf"), ActorTypeId.of("wastrel"),
            ActorTypeId.of("priest_of_the_flame"), ActorTypeId.of("disciple_of_the_flame"),
            ActorTypeId.of("shopkeeper"), ActorTypeId.of("animal_keeper"),
            ActorTypeId.of("animal"), ActorTypeId.of("feral"),
            ActorTypeId.of("mouse"), ActorTypeId.of("cat"));

    private static SkillRegistry skills;
    private static JobRegistry bound;

    @BeforeAll
    static void bindCommitted() {
        skills = SkillRawsLoader.load(locateRawsDir());
        bound = JobBinder.bind(
                locateRawsDir().resolve("jobs").resolve("jobs.json"), KNOWN_TYPES, skills);
    }

    @Test
    void civicJobsBindWithTheirTrainingPairResolvedToRawIndices() {
        assertTraining(Job.Serf.Farmer.ID, "fieldcraft", 25);
        assertTraining(Job.Serf.Laborer.ID, "kit_keeping", 25);
        assertTraining(Job.Maritime.Sailor.ID, "seacraft", 25);
        assertTraining(Job.Trade.Stallkeep.ID, "streetwise", 25);
        assertTraining(Job.Trade.Trader.ID, "streetwise", 25);
        assertTraining(Job.Husbandry.Keeper.ID, "fieldcraft", 25);
        assertTraining(Job.Clergy.Shepherd.ID, "channeling", 25);
        assertTraining(Job.Clergy.Acolyte.ID, "channeling", 25);
        assertTraining(Job.Watch.Patrol.ID, "streetwise", 10);
    }

    @Test
    void villainsBeastsThePcSeamAndStreetlifeBindTrainless() {
        for (JobId id : List.of(Job.Wastrel.Streetlife.ID, Job.Villain.Robber.ID,
                Job.Villain.Cutpurse.ID, Job.Villain.Skyrunner.ID, Job.FlameOfMerc.ID,
                Job.Beast.Chattel.ID, Job.Beast.Feral.ID, Job.Beast.Prey.ID,
                Job.Beast.Prowler.ID)) {
            JobParams params = paramsOf(id);
            assertFalse(params.trains(), id + " must bind trainless");
            assertEquals(JobParams.TRAINS_NOTHING, params.trainSkillRaw(), id + " raw");
            assertEquals(0, params.trainCp(), id + " cp");
        }
    }

    @Test
    void unknownTrainsSkillKeyFailsTheBindLoudly() {
        List<JobRaw> raws = new ArrayList<>(
                JobRawsLoader.load(locateRawsDir().resolve("jobs").resolve("jobs.json")));
        int i = indexOf(raws, Job.Serf.Farmer.ID);
        JobRaw original = raws.get(i);
        raws.set(i, new JobRaw(original.file(), original.id(), original.goalKind(),
                original.priority(), original.rhythmStart(), original.rhythmEnd(),
                original.rhythmBonus(), original.workTicksPerUnit(), original.unitsToComplete(),
                original.renewMode(), original.cooldownTicks(), original.assign(),
                original.defaultFor(), original.secret(), original.cover(),
                "nonesuchcraft", original.trainCp()));
        ActorRawsValidationException e = assertThrows(ActorRawsValidationException.class,
                () -> JobBinder.bind(raws, KNOWN_TYPES, skills));
        assertTrue(e.getMessage().contains("unknown skill"),
                "unexpected message: " + e.getMessage());
    }

    @Test
    void legacySkillLessBindLeavesTrainingDormant() {
        JobRegistry legacy = JobBinder.bind(
                locateRawsDir().resolve("jobs").resolve("jobs.json"), KNOWN_TYPES);
        for (int i = 0; i < legacy.size(); i++) {
            assertFalse(legacy.get(i).params().trains(),
                    legacy.get(i).id() + " must stay dormant under the legacy bind");
        }
    }

    private static void assertTraining(JobId id, String skillKey, int cp) {
        JobParams params = paramsOf(id);
        assertTrue(params.trains(), id + " must bind trained");
        assertEquals(skills.id(skillKey).raw(), params.trainSkillRaw(),
                id + " must resolve trainsSkill \"" + skillKey + "\" to its raw index");
        assertEquals(cp, params.trainCp(), id + " trainCp");
    }

    private static JobParams paramsOf(JobId id) {
        return bound.get(bound.ordinalOf(id)).params();
    }

    private static int indexOf(List<JobRaw> raws, JobId id) {
        for (int i = 0; i < raws.size(); i++) {
            if (raws.get(i).id().equals(id)) {
                return i;
            }
        }
        throw new IllegalStateException("not found: " + id);
    }

    private static Path locateRawsDir() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException(
                "content/raws not found above " + Path.of("").toAbsolutePath());
    }
}
