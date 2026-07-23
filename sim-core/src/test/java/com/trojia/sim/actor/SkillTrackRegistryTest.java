package com.trojia.sim.actor;

import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.progression.SkillRegistry;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The per-actor skill-track side table (Sprint 1): dense lazy materialization, award
 * routing into the {@link SkillLevelLog} client seam, the UNWIRED degradation contract,
 * and the persisted triad — serialize / load / hashInto round-trips byte-identically
 * MID-PROGRESSION (banked grains + live satiation rows, not just clean levels), with the
 * skill-count frame guard failing a wiring mismatch loudly.
 */
final class SkillTrackRegistryTest {

    private static final SkillRegistry SKILLS = SkillRawsLoader.load(committedRawsRoot());

    private static Path committedRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found above " + Path.of("").toAbsolutePath());
    }

    @Test
    void awardsMaterializeTracksLevelSkillsAndFeedTheLevelLog() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SKILLS);
        assertTrue(tracks.isWired());
        assertEquals(0, tracks.level(12, tracks.streetwiseRaw()), "no track yet: level 0");

        // streetwise is FAVORED (aptNum 15): level 0->1 needs 1500 grains; one 100-cp
        // tier-0 award = 2000 grains -> level 1 with 500 banked.
        tracks.award(12, tracks.streetwiseRaw(), 100, 555L, 40L);
        assertEquals(1, tracks.level(12, tracks.streetwiseRaw()));
        assertEquals(1, tracks.levelLog().size(), "the level-up landed in the client seam");
        assertEquals(40L, tracks.levelLog().tickAt(0));
        assertEquals(12, tracks.levelLog().actorIdAt(0));
        assertEquals(tracks.streetwiseRaw(), tracks.levelLog().skillRawAt(0));
        assertEquals(1, tracks.levelLog().newLevelAt(0));

        // Other actors are untouched (dense isolation).
        assertEquals(0, tracks.level(11, tracks.streetwiseRaw()));
        assertEquals(0, tracks.level(13, tracks.streetwiseRaw()));
    }

    @Test
    void unwiredDegradesToNoOpAwardsAndBaseReads() {
        SkillTrackRegistry unwired = SkillTrackRegistry.UNWIRED;
        unwired.award(3, 0, 10_000, 1L, 1L); // must not throw, must not record
        assertEquals(0, unwired.level(3, 0));
        assertEquals(10, unwired.attribute(3, com.trojia.sim.progression.AttributeId.AGI),
                "the skill-less §5 base");
        assertEquals(0, unwired.levelLog().size());
    }

    @Test
    void theTriadRoundTripsByteIdenticallyMidProgression() throws IOException {
        SkillTrackRegistry source = new SkillTrackRegistry(SKILLS);
        // Mid-progression state: repeated same-context awards (satiation tiers live),
        // partial grains banked, several actors, sparse ids (null slots in between).
        for (int round = 0; round < 4; round++) {
            source.award(2, source.openHandRaw(), 90, 7L, 100L + round);
            source.award(9, source.gritRaw(), 150, 2L, 120L + round);
        }
        source.award(5, source.streetwiseRaw(), 100, 900L, 400L);

        byte[] first = serialize(source);
        SkillTrackRegistry reloaded = new SkillTrackRegistry(SKILLS);
        reloaded.load(new DataInputStream(new ByteArrayInputStream(first)));
        byte[] second = serialize(reloaded);

        assertArrayEquals(first, second, "serialize -> load -> serialize must be byte-identical");
        assertEquals(hash(source), hash(reloaded), "hashInto must match after load");
        assertEquals(source.level(2, source.openHandRaw()),
                reloaded.level(2, source.openHandRaw()));
        assertEquals(source.levelLog().totalRecorded(), reloaded.levelLog().totalRecorded());

        // Behavioral equivalence after load, not just byte equality: the NEXT award prices
        // identically (satiation tiers and banked grains truly round-tripped).
        source.award(2, source.openHandRaw(), 90, 7L, 200L);
        reloaded.award(2, source.openHandRaw(), 90, 7L, 200L);
        assertArrayEquals(serialize(source), serialize(reloaded),
                "post-load awards must reproduce the continuous run");
    }

    @Test
    void theFrameGuardFailsAWiringMismatchLoudly() throws IOException {
        SkillTrackRegistry source = new SkillTrackRegistry(SKILLS);
        source.award(0, source.gritRaw(), 150, 1L, 1L);
        byte[] bytes = serialize(source);

        assertThrows(IOException.class, () -> SkillTrackRegistry.UNWIRED
                        .load(new DataInputStream(new ByteArrayInputStream(bytes))),
                "loading a wired save into an unwired system must fail loudly");
    }

    private static byte[] serialize(SkillTrackRegistry tracks) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        tracks.serialize(new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static long hash(SkillTrackRegistry tracks) {
        WorldHasher hasher = new WorldHasher();
        var id = com.trojia.sim.engine.SystemId.of("test", "TEST");
        tracks.hashInto(hasher.sectionSink(id));
        return hasher.sectionHash(id);
    }
}
