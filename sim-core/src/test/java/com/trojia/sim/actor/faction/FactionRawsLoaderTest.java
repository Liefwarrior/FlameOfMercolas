package com.trojia.sim.actor.faction;

import com.trojia.sim.actor.ActorRawsValidationException;

import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The faction raws (Sprint 1): the COMMITTED {@code content/raws/factions/factions.json}
 * loads into the five-faction registry with platform-stable sorted-key ids and a validated
 * one-faction-per-job membership map; malformed raws fail fast.
 */
final class FactionRawsLoaderTest {

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
    void committedRawsLoadTheFiveFactionsWithSortedStableIds() {
        FactionRegistry factions = FactionRawsLoader.load(committedRawsRoot());
        assertEquals(5, factions.size(), "the Sprint-1 five: watch/skyrunners/temple/merchants/dockhands");
        // Sorted-key id assignment (the SkillRegistry convention) — pinned so a future
        // append cannot silently renumber the persisted standing columns.
        assertEquals("dockhands", factions.get(0).key());
        assertEquals("merchants", factions.get(1).key());
        assertEquals("skyrunners", factions.get(2).key());
        assertEquals("temple", factions.get(3).key());
        assertEquals("watch", factions.get(4).key());
        for (FactionDefinition def : factions.all()) {
            assertTrue(!def.displayName().isEmpty());
        }
    }

    @Test
    void committedMembershipResolvesJobsToTheirOneFaction() {
        FactionRegistry factions = FactionRawsLoader.load(committedRawsRoot());
        assertEquals(factions.rawId("watch"), factions.factionOfJob("watch.patrol"));
        assertEquals(factions.rawId("skyrunners"), factions.factionOfJob("villain.skyrunner"));
        assertEquals(factions.rawId("skyrunners"), factions.factionOfJob("villain.cutpurse"));
        assertEquals(factions.rawId("temple"), factions.factionOfJob("clergy.shepherd"));
        assertEquals(factions.rawId("merchants"), factions.factionOfJob("trade.stallkeep"));
        assertEquals(factions.rawId("dockhands"), factions.factionOfJob("serf.laborer"));
        assertEquals(factions.rawId("dockhands"), factions.factionOfJob("maritime.sailor"));
        assertEquals(-1, factions.factionOfJob("wastrel.streetlife"),
                "streetlife is deliberately unaffiliated");
        assertEquals(-1, factions.factionOfJob("flame_of_merc"),
                "the Wielder answers to no chapterhouse");
    }

    @Test
    void aJobClaimedByTwoFactionsFailsFast() {
        String raws = """
                {"id":"factions","factions":[
                  {"id":"a","displayName":"A","memberJobs":["watch.patrol"]},
                  {"id":"b","displayName":"B","memberJobs":["watch.patrol"]}]}""";
        assertThrows(ActorRawsValidationException.class,
                () -> FactionRawsLoader.parse(raws.getBytes(StandardCharsets.UTF_8)));
    }

    @Test
    void unknownFieldsAndMissingRequiredsFailFast() {
        assertThrows(ActorRawsValidationException.class, () -> FactionRawsLoader.parse(
                "{\"id\":\"factions\",\"factions\":[{\"id\":\"a\",\"displayName\":\"A\"}]}"
                        .getBytes(StandardCharsets.UTF_8)),
                "memberJobs is required (may be empty, must be present)");
        assertThrows(ActorRawsValidationException.class, () -> FactionRawsLoader.parse(
                "{\"id\":\"factions\",\"factions\":[{\"id\":\"a\",\"displayName\":\"A\",\"memberJobs\":[],\"bogus\":1}]}"
                        .getBytes(StandardCharsets.UTF_8)),
                "unknown fields fail fast");
    }
}
