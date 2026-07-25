package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorRawsLoader;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.ActorTypeStatsTable;
import com.trojia.sim.actor.ActorTypes;
import com.trojia.sim.actor.Need;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 slice 1, the committed-raws invariant behind Eli's bug 1: <b>any actor type whose
 * DUTY decays has a default job with a positive {@code dutyPerUnit}</b> — no employed type can
 * bottom out with no reachable gain source. Plus the pinned S6 duty facts (the tuned values,
 * the {@code JobBinderTrainingTest} precedent: pin the numbers, not the sheet formatting).
 */
final class JobDutyCommittedTest {

    private static ActorTypeStatsTable types;
    private static JobRegistry jobs;

    @BeforeAll
    static void loadCommittedRaws() {
        Path rawsRoot = locateRawsRoot();
        types = ActorRawsLoader.load(rawsRoot.resolve("actors"));
        jobs = JobBinder.bind(rawsRoot.resolve("jobs").resolve("jobs.json"),
                ActorTypes.allTypeIds());
    }

    /** The invariant: DUTY decay implies a duty-restoring default job (the gain is reachable). */
    @Test
    void everyDutyDecayingTypeHasADutyRestoringDefaultJob() {
        for (ActorTypeId typeId : ActorTypes.allTypeIds()) {
            ActorTypeStats stats = types.get(typeId);
            if (stats.need(Need.DUTY).decayPerKilotick() == 0) {
                continue; // DUTY never falls -> no gain source required
            }
            int defaultOrdinal = jobs.defaultOrdinalFor(typeId);
            JobParams params = jobs.get(defaultOrdinal).params();
            assertTrue(params.dutyPerUnit() > 0,
                    "actor type \"" + typeId + "\" decays DUTY ("
                            + stats.need(Need.DUTY).decayPerKilotick()
                            + "/kt) but its default job \"" + jobs.get(defaultOrdinal).id()
                            + "\" restores none — it would bottom out (Eli's bug 1)");
        }
    }

    /** Non-default serf trades need the gain too (serfs decay DUTY on every assignment). */
    @Test
    void everySerfAssignableCivicJobRestoresDuty() {
        for (String id : new String[] {"serf.farmer", "serf.laborer", "serf.carter",
                "maritime.sailor", "maritime.fisher", "trade.trader"}) {
            JobParams params = jobs.get(jobs.ordinalOf(JobId.of(id))).params();
            assertTrue(params.dutyPerUnit() > 0,
                    id + " is assigned to DUTY-decaying serfs and must restore DUTY");
        }
    }

    /** The tuned S6 committed values, pinned. */
    @Test
    void pinsTheTunedDutyPerUnitValues() {
        assertEquals(150, dutyOf("serf.farmer"));
        assertEquals(120, dutyOf("serf.laborer"));
        assertEquals(150, dutyOf("serf.carter"));
        assertEquals(120, dutyOf("maritime.sailor"));
        assertEquals(120, dutyOf("maritime.fisher"));
        assertEquals(100, dutyOf("trade.trader"));
        assertEquals(100, dutyOf("trade.stallkeep"));
        assertEquals(200, dutyOf("watch.patrol"));
        assertEquals(400, dutyOf("clergy.shepherd"));
        assertEquals(150, dutyOf("clergy.acolyte"));
        assertEquals(150, dutyOf("husbandry.keeper"));
        // The non-civic set stays at 0: beasts have no duty, villains gain none from crime,
        // wastrels' DUTY never decays, and the PC seam is player-driven.
        assertEquals(0, dutyOf("wastrel.streetlife"));
        assertEquals(0, dutyOf("villain.robber"));
        assertEquals(0, dutyOf("villain.cutpurse"));
        assertEquals(0, dutyOf("villain.skyrunner"));
        assertEquals(0, dutyOf("flame_of_merc"));
        assertEquals(0, dutyOf("beast.chattel"));
        assertEquals(0, dutyOf("beast.feral"));
        assertEquals(0, dutyOf("beast.prey"));
        assertEquals(0, dutyOf("beast.prowl"));
    }

    private static int dutyOf(String jobId) {
        return jobs.get(jobs.ordinalOf(JobId.of(jobId))).params().dutyPerUnit();
    }

    static Path locateRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        for (int i = 0; i < 6 && dir != null; i++, dir = dir.getParent()) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException(
                "content/raws not found above " + Path.of("").toAbsolutePath());
    }
}
