package com.trojia.sim.actor;

import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Play mode's save/load thaw repair (PLAY-MODE-SPEC.md §6): {@link StatusBit#PLAYER_CONTROLLED}
 * must be unconditionally cleared on load — the same shape as the existing goalTarget-at-thaw
 * repairs. Without it, an actor saved mid-Play-mode would permanently win
 * {@code PolicyStack.selectIndex} (score 2000 &gt; 0) with no human ever able to reattach after
 * a fresh app launch.
 */
final class ActorsSystemPlayModeThawTest {

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

    @Test
    void playerControlledStatusBitIsClearedOnLoad() throws IOException {
        ActorTypeStatsTable typeStats = ActorTypeStatsTable.of(
                List.of(ActorTestFixtures.stats(Serf.TYPE)));
        JobRegistry jobs = JobBinder.bind(committedJobsJson(), ActorTypes.allTypeIds());

        ActorRegistry sourceRegistry = new ActorRegistry();
        Actor actor = sourceRegistry.spawn(Serf.TYPE, typeStats.get(Serf.TYPE),
                PackedPos.pack(5, 5, 1));
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, true);
        assertTrue(actor.hasStatus(StatusBit.PLAYER_CONTROLLED), "sanity: bit set pre-save");

        ActorsSystem source = new ActorsSystem(1L, typeStats, jobs, sourceRegistry,
                new HomeRegistry(), new RelationshipRegistry(), new ItemsLiteRegistry());
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        source.serialize(new DataOutputStream(bytes));

        ActorRegistry loadedRegistry = new ActorRegistry();
        ActorsSystem loaded = new ActorsSystem(1L, typeStats, jobs, loadedRegistry,
                new HomeRegistry(), new RelationshipRegistry(), new ItemsLiteRegistry());
        loaded.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));

        assertFalse(loadedRegistry.get(0).hasStatus(StatusBit.PLAYER_CONTROLLED),
                "a fresh load must never leave an actor permanently winning selectIndex with "
                        + "no human reattached");
    }
}
