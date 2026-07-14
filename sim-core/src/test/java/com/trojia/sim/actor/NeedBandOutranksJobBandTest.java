package com.trojia.sim.actor;

import com.trojia.sim.actor.job.JobParams;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Re-proves, under the needs-hierarchy pass's revised numbers, the invariant
 * {@code JobParams}'s compact constructor was tightened to protect (its own
 * javadoc: "the NEED-band RETURN_HOME policy is priced at a fixed 305
 * specifically so it always outranks every JOB-band score"): every committed
 * actor type's NEED-band base priorities ({@code returnHomePriority},
 * {@code seekFoodPriority}) must exceed {@link JobParams#JOB_BAND_MAX}, so no
 * in-window job can ever outscore a need-urgent RETURN_HOME/SEEK_FOOD. Adding
 * the urgency-bonus term to both policies' scoring (this pass) only widens
 * this margin further — it can never narrow it, since the bonus is always
 * {@code >= 0} — but this test locks in the base-priority half of that
 * guarantee directly against the real committed raws, independent of any
 * particular actor/job scenario.
 */
final class NeedBandOutranksJobBandTest {

    private static Path committedActorsRawsDir() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws").resolve("actors");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws/actors not found above "
                + Path.of("").toAbsolutePath());
    }

    @Test
    void everyActorTypesNeedBandPrioritiesExceedTheJobBandCeiling() {
        ActorTypeStatsTable table = ActorRawsLoader.load(committedActorsRawsDir());
        for (ActorTypeStats stats : table.all()) {
            assertTrue(stats.returnHomePriority() > JobParams.JOB_BAND_MAX,
                    stats.typeId() + ": returnHomePriority (" + stats.returnHomePriority()
                            + ") must exceed JOB_BAND_MAX (" + JobParams.JOB_BAND_MAX + ")");
            assertTrue(stats.seekFoodPriority() > JobParams.JOB_BAND_MAX,
                    stats.typeId() + ": seekFoodPriority (" + stats.seekFoodPriority()
                            + ") must exceed JOB_BAND_MAX (" + JobParams.JOB_BAND_MAX + ")");
        }
    }
}
