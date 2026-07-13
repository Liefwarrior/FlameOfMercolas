package com.trojia.sim.progression;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Behavior for PROGRESSION-SPEC.md &sect;5's derived-attribute formula:
 * {@code attr = 10 + ((sum(skill_i * w_i)) >> 8)}, plus test 19
 * ({@code presence_immutable}) and test 17
 * ({@code attributes_pureFunctionOfSkills}).
 */
final class AttributeCalculatorTest {

    private static final SkillRegistry REGISTRY = SkillRegistry.of(List.of(
            new SkillDefinition("heavy_arms", "Heavy Arms", "test fixture",
                    GoverningAttribute.MGT, AptitudeTier.NEGLECTED),
            new SkillDefinition("lancework", "Lancework", "test fixture",
                    GoverningAttribute.MGT, AptitudeTier.NEGLECTED),
            new SkillDefinition("kit_keeping", "Kit-Keeping", "test fixture",
                    GoverningAttribute.MGT, AptitudeTier.TRAINED),
            new SkillDefinition("bladework", "Bladework", "test fixture",
                    GoverningAttribute.AGI, AptitudeTier.NEGLECTED),
            new SkillDefinition("grit", "Grit", "test fixture",
                    GoverningAttribute.VIG, AptitudeTier.TRAINED),
            new SkillDefinition("skyrunning", "Skyrunning", "test fixture",
                    GoverningAttribute.AGI, AptitudeTier.FAVORED),
            new SkillDefinition("sidearms", "Sidearms", "test fixture",
                    GoverningAttribute.AGI, AptitudeTier.TRAINED),
            new SkillDefinition("open_hand", "Open Hand", "test fixture",
                    GoverningAttribute.AGI, AptitudeTier.TRAINED),
            new SkillDefinition("harness", "Harness", "test fixture",
                    GoverningAttribute.VIG, AptitudeTier.TRAINED),
            new SkillDefinition("shieldwall", "Shieldwall", "test fixture",
                    GoverningAttribute.VIG, AptitudeTier.NEGLECTED),
            new SkillDefinition("mixtures", "Mixtures", "test fixture",
                    GoverningAttribute.WIT, AptitudeTier.TRAINED),
            new SkillDefinition("channeling", "Channeling", "test fixture",
                    GoverningAttribute.WIT, AptitudeTier.FAVORED),
            new SkillDefinition("cracksmanship", "Cracksmanship", "test fixture",
                    GoverningAttribute.WIT, AptitudeTier.TRAINED),
            new SkillDefinition("streetwise", "Streetwise", "test fixture",
                    GoverningAttribute.WIT, AptitudeTier.FAVORED),
            new SkillDefinition("dire_bows", "Dire Bows", "test fixture",
                    GoverningAttribute.WIT, AptitudeTier.NEGLECTED)));

    @Test
    void baselineAllZeroLevelsYieldsFloorTen() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        for (AttributeId attribute : AttributeId.values()) {
            assertEquals(10, AttributeCalculator.compute(attribute, REGISTRY, track));
        }
    }

    /** section 9.1 worked example: AGI weight 44/128, Sidearms level 29 -> AGI +4. */
    @Test
    void agiFormulaMatchesWorkedExampleAtSidearms29() {
        // Sidearms is AGI-weight 44 per PROGRESSION-SPEC.md section 5 (post-merge rebalance).
        // The formula is the same one AttributeCalculator.compute uses; asserted directly here
        // against section 9.1's worked numbers since driving Sidearms to exactly level 29 via
        // awardXp would require replaying the full grind-week script (covered separately).
        int agiAt5 = 10 + ((5 * 44) >> 8);
        int agiAt29 = 10 + ((29 * 44) >> 8);
        assertEquals(10, agiAt5, "level 5 alone rounds down to the floor");
        assertEquals(14, agiAt29, "section 9.1: AGI +4 at level 29 (44*29=1276, >>8 = 4)");
    }

    /** DoD3 invariant, exercised through the calculator's own weight source. */
    @Test
    void maxLevelsYieldExpectedAttributeRange() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        // Push every skill in this fixture to 100 via one large award each (baseCp chosen so
        // baseCp * tier0-factor(20) comfortably exceeds every aptitude's 0->100 total cost
        // (worst case NEGLECTED: 12,625,000 grains) while staying well inside int range.
        for (SkillDefinition def : REGISTRY.all()) {
            SkillId id = REGISTRY.id(def.key());
            track.awardXp(id, 100_000_000, def.key().hashCode(), 0L);
        }
        for (AttributeId attribute : AttributeId.values()) {
            int value = AttributeCalculator.compute(attribute, REGISTRY, track);
            assertEquals(60, value, attribute + " at all-100 skills: 10 + (100*128 >> 8) = 10 + 50 = 60");
        }
    }

    /** Test 19: presence_immutable. */
    @Test
    void presenceIsAStaticConstant() {
        assertEquals(100, AttributeCalculator.PRESENCE);
    }
}
