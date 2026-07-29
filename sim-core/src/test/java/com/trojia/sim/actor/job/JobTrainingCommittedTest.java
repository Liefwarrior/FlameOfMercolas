package com.trojia.sim.actor.job;

import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.progression.SkillRegistry;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Cross-file contract for the Sprint-5 job training map (PROGRESSION-SPEC.md
 * &sect;2): every committed {@code trainsSkill} key resolves against the
 * committed skills raws, the civic map matches the agreed table exactly, and
 * the non-training set (villains — their crimes already train — beasts, the
 * PC seam, streetlife — scavenging already trains streetwise) stays empty.
 */
final class JobTrainingCommittedTest {

    /** The agreed Sprint-5 (+S6 carter/fisher, +S7 night roster) map, in jobs.json order. */
    private static final Map<String, String> EXPECTED_SKILL = Map.ofEntries(
            Map.entry("serf.farmer", "fieldcraft"),
            Map.entry("serf.laborer", "kit_keeping"),
            Map.entry("serf.carter", "kit_keeping"),
            Map.entry("watch.patrol", "streetwise"),
            Map.entry("watch.nightwatch", "streetwise"),
            Map.entry("clergy.shepherd", "channeling"),
            Map.entry("clergy.acolyte", "channeling"),
            Map.entry("maritime.sailor", "seacraft"),
            Map.entry("maritime.fisher", "fishing"),
            Map.entry("trade.stallkeep", "streetwise"),
            Map.entry("trade.trader", "streetwise"),
            Map.entry("husbandry.keeper", "fieldcraft"));

    private static final List<String> UNTRAINED = List.of(
            "wastrel.streetlife", "villain.robber", "villain.cutpurse", "villain.skyrunner",
            "flame_of_merc", "beast.chattel", "beast.feral", "beast.prey", "beast.prowl");

    private static List<JobRaw> raws;
    private static SkillRegistry skills;

    @BeforeAll
    static void loadCommitted() {
        Path rawsRoot = locateRawsDir();
        raws = JobRawsLoader.load(rawsRoot.resolve("jobs").resolve("jobs.json"));
        skills = SkillRawsLoader.load(rawsRoot);
    }

    @Test
    void everyTrainsSkillResolvesAgainstTheCommittedSkillRaws() {
        for (JobRaw raw : raws) {
            if (raw.trainsSkill() != null) {
                assertTrue(skills.contains(raw.trainsSkill()),
                        raw.id() + " trainsSkill \"" + raw.trainsSkill()
                                + "\" must resolve in skills.json");
            }
        }
    }

    @Test
    void civicTrainingMapMatchesTheAgreedTable() {
        for (JobRaw raw : raws) {
            String expected = EXPECTED_SKILL.get(raw.id().toString());
            if (expected != null) {
                assertEquals(expected, raw.trainsSkill(), raw.id() + " trainsSkill");
                assertTrue(raw.trainCp() >= 1, raw.id() + " trainCp must be >= 1");
            }
        }
        assertEquals(EXPECTED_SKILL.size() + UNTRAINED.size(), raws.size(),
                "every committed job must be covered by this test's two tables");
    }

    @Test
    void patrolIsPricedBelowAnchorWork() {
        // Patrol waypoints are each a fresh satiation context (multi-context inflation is
        // the known hot spot) — the raws price them below the anchor-cycle 25 cp scale.
        JobRaw patrol = byId("watch.patrol");
        assertTrue(patrol.trainCp() < 25,
                "watch.patrol trainCp must undercut the anchor scale, got " + patrol.trainCp());
    }

    @Test
    void villainsBeastsAndThePcSeamTrainNothing() {
        for (String id : UNTRAINED) {
            JobRaw raw = byId(id);
            assertNull(raw.trainsSkill(), id + " must not declare trainsSkill");
            assertEquals(0, raw.trainCp(), id + " trainCp");
        }
    }

    private static JobRaw byId(String id) {
        for (JobRaw raw : raws) {
            if (raw.id().toString().equals(id)) {
                return raw;
            }
        }
        throw new AssertionError("no committed job with id " + id);
    }

    /** The SkillRawsLoaderCommittedTest walk-up: repo root containing content/raws. */
    private static Path locateRawsDir() {
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
