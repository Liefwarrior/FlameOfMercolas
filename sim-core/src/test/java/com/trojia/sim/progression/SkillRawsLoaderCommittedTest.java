package com.trojia.sim.progression;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Full load of the committed skills raw ({@code content/raws/skills/skills.json}):
 * count, deterministic id assignment, and spot-checked properties against
 * PROGRESSION-SPEC.md &sect;2's table (Behavior 1, DoD1/DoD2).
 */
final class SkillRawsLoaderCommittedTest {

    /** All 16 skill ids in id order: sorted string keys, first key = 0. */
    private static final List<String> EXPECTED_IDS = List.of(
            "bladework", "channeling", "cracksmanship", "dire_bows", "grit",
            "harness", "heavy_arms", "kit_keeping", "lancework", "mixtures",
            "open_hand", "shieldwall", "sidearms", "skyrunning", "streetwise",
            "the_flame");

    private static SkillRegistry registry;

    @BeforeAll
    static void loadCommittedRaws() {
        registry = SkillRawsLoader.load(locateRawsDir());
    }

    @Test
    void loadsSixteenSkills() {
        assertEquals(16, registry.size());
    }

    @Test
    void idAssignmentIsSortedStringKeyOrder() {
        for (int i = 0; i < EXPECTED_IDS.size(); i++) {
            assertEquals(EXPECTED_IDS.get(i), registry.get(i).key(), "id " + i);
            assertEquals(i, registry.id(EXPECTED_IDS.get(i)).raw());
        }
    }

    /** DoD2: spot-check at least 6 skills spanning all three ordinary aptitude tiers. */
    @Test
    void spotChecksGoverningAttributeAndAptitudeTier() {
        assertSkill("skyrunning", GoverningAttribute.AGI, AptitudeTier.FAVORED);
        assertSkill("channeling", GoverningAttribute.WIT, AptitudeTier.FAVORED);
        assertSkill("streetwise", GoverningAttribute.WIT, AptitudeTier.FAVORED);
        assertSkill("sidearms", GoverningAttribute.AGI, AptitudeTier.TRAINED);
        assertSkill("grit", GoverningAttribute.VIG, AptitudeTier.TRAINED);
        assertSkill("bladework", GoverningAttribute.AGI, AptitudeTier.NEGLECTED);
        assertSkill("lancework", GoverningAttribute.MGT, AptitudeTier.NEGLECTED);
        assertSkill("dire_bows", GoverningAttribute.WIT, AptitudeTier.NEGLECTED);
        assertSkill("the_flame", GoverningAttribute.NONE, AptitudeTier.FLAME);
    }

    private static void assertSkill(String key, GoverningAttribute gov, AptitudeTier apt) {
        SkillDefinition def = registry.get(registry.id(key));
        assertEquals(gov, def.governingAttribute(), key + " governingAttribute");
        assertEquals(apt, def.aptitudeTier(), key + " aptitudeTier");
    }

    /** DoD3: each attribute's weight row sums to exactly 128 (PROGRESSION-SPEC.md section 5). */
    @Test
    void attributeWeightsSumTo128Invariant() {
        for (AttributeId attribute : AttributeId.values()) {
            int sum = 0;
            for (SkillWeight weight : AttributeWeights.weights(attribute)) {
                sum += weight.weight();
                // every weighted skill must actually resolve against the loaded registry
                registry.id(weight.skillKey());
            }
            assertEquals(128, sum, attribute + " weight row must sum to 128");
        }
    }

    /**
     * Walks up from the test working directory (the sim-core module dir under
     * Gradle) to the repo root containing {@code content/raws}.
     */
    static Path locateRawsDir() {
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
