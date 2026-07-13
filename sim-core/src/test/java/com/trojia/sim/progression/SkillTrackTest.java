package com.trojia.sim.progression;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Behavior 2 tests: XP awards, aptitude-dependent levelling, satiation
 * diminishing returns/decay, and level boundary safety
 * (PROGRESSION-SPEC.md &sect;1, &sect;3; spec &sect;10 tests 3, 4, 5, 6, 7, 8).
 */
final class SkillTrackTest {

    private static final SkillRegistry REGISTRY = SkillRegistry.of(List.of(
            new SkillDefinition("favored_skill", "Favored Skill", "test fixture",
                    GoverningAttribute.AGI, AptitudeTier.FAVORED),
            new SkillDefinition("trained_skill", "Trained Skill", "test fixture",
                    GoverningAttribute.AGI, AptitudeTier.TRAINED),
            new SkillDefinition("neglected_skill", "Neglected Skill", "test fixture",
                    GoverningAttribute.AGI, AptitudeTier.NEGLECTED)));

    /** DoD4: same grains, Favored levels higher than Neglected. */
    @Test
    void favoredLevelsFasterThanNeglectedForIdenticalAwards() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        SkillId favored = REGISTRY.id("favored_skill");
        SkillId neglected = REGISTRY.id("neglected_skill");

        // 200 identical awards, fresh contextKey each time so satiation never engages
        // (isolates the aptitude effect from the satiation effect).
        for (int i = 0; i < 200; i++) {
            track.awardXp(favored, 500, i, 0L);
            track.awardXp(neglected, 500, i, 0L);
        }

        assertTrue(track.level(favored) > track.level(neglected),
                "Favored (" + track.level(favored) + ") must out-level Neglected ("
                        + track.level(neglected) + ") for identical XP");
    }

    /** Test 3: levelUp_excessCarries_multiLevel. */
    @Test
    void multiLevelAwardCarriesExcessGrains() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        SkillId trained = REGISTRY.id("trained_skill");

        // baseCp 2500 at tier 0 (factor 20) = 50,000 grains in one award.
        List<SkillLevelledEvent> events = track.awardXp(trained, 2500, 99L, 0L);

        assertEquals(6, track.level(trained));
        assertEquals(8_000, track.progressGrains(trained));
        assertEquals(6, events.size());
        for (int i = 0; i < events.size(); i++) {
            assertEquals(i + 1, events.get(i).newLevel());
            assertEquals(1L, events.get(i).entityId());
        }
    }

    /** Test 4 + DoD6: skill_capsAt100_discardsOverflow. */
    @Test
    void capsAt100AndDiscardsFurtherAwards() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        SkillId favored = REGISTRY.id("favored_skill");

        // Total 0->100 cost at Favored (aptNum 15): sum_{L=0}^{99}(L+1)*100*15 = 7,575,000.
        // A single huge award drives the skill straight to the cap in one call.
        List<SkillLevelledEvent> firstEvents = track.awardXp(favored, 500_000, 1L, 0L);
        assertEquals(100, track.level(favored));
        assertEquals(100, firstEvents.size());
        int progressAfterCap = track.progressGrains(favored);

        // Further XP at level 100 is discarded entirely: no event, no progress change.
        List<SkillLevelledEvent> secondEvents = track.awardXp(favored, 500_000, 2L, 100L);
        assertEquals(100, track.level(favored), "level must never exceed 100");
        assertEquals(progressAfterCap, track.progressGrains(favored),
                "progress must not change once capped");
        assertEquals(0, secondEvents.size(), "no level-up event once capped");
    }

    /** DoD6: level never goes below 0 and awarding never errors near Integer.MAX_VALUE. */
    @Test
    void neverNegativeAndNoOverflowNearIntMax() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        SkillId trained = REGISTRY.id("trained_skill");
        assertEquals(0, track.level(trained));
        assertEquals(0, track.progressGrains(trained));

        int nearMaxBaseCp = Integer.MAX_VALUE / ProgressionMath.GRAINS_PER_CP;
        // Two huge awards back to back must not throw and must land at the cap.
        track.awardXp(trained, nearMaxBaseCp, 1L, 0L);
        track.awardXp(trained, nearMaxBaseCp, 1L, 0L);

        assertEquals(100, track.level(trained));
        assertTrue(track.progressGrains(trained) >= 0);
    }

    /** DoD5 + test 6: satiation_floorIsFive_neverZero (diminishing then floored, never 0). */
    @Test
    void satiationDiminishesThenFloorsAt25Percent() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        SkillId trained = REGISTRY.id("trained_skill");
        long context = 42L;

        int firstLevel = track.level(trained);
        track.awardXp(trained, 100, context, 0L); // tier0 -> 2000 grains
        track.awardXp(trained, 100, context, 0L); // tier1 -> 1600 grains
        track.awardXp(trained, 100, context, 0L); // tier2 -> 1200 grains
        track.awardXp(trained, 100, context, 0L); // tier3 -> 800 grains
        track.awardXp(trained, 100, context, 0L); // tier4 -> 500 grains (floor)
        int progressAtFloor = track.progressGrains(trained);
        // 2000+1600+1200+800+500 = 6100, level0 threshold at Trained = 2000, level1 = 4000 -> total 6000 consumed for 2 levels, 100 remaining
        assertEquals(2, track.level(trained) - firstLevel);
        assertEquals(100, progressAtFloor);

        // Further same-context awards stay at the floor (never drop to 0 cp).
        track.awardXp(trained, 100, context, 0L);
        int progressAfterSixth = track.progressGrains(trained);
        assertEquals(progressAtFloor + 500, progressAfterSixth,
                "sixth award still pays the 500-grain floor");
    }

    /** Test 7: satiation_tierDecay_deterministic (3,000-tick boundary, exact). */
    @Test
    void satiationTierDecaysExactlyAtThreeThousandTicks() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        SkillId trained = REGISTRY.id("trained_skill");
        long context = 7L;

        // Drive the context to tier 4 with five awards at tick 0.
        for (int i = 0; i < 5; i++) {
            track.awardXp(trained, 100, context, 0L);
        }
        assertEquals(4, track.effectiveSatiationTier(trained, context, 0L));

        assertEquals(4, track.effectiveSatiationTier(trained, context, 2_999L), "2,999 ticks idle: still tier 4");
        assertEquals(3, track.effectiveSatiationTier(trained, context, 3_000L), "3,000 ticks idle: exactly tier 3");
    }

    /** DoD5: recovery after the decay window fully passes. */
    @Test
    void satiationFullyRecoversAfterFourDecayWindows() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        SkillId trained = REGISTRY.id("trained_skill");
        long context = 7L;
        for (int i = 0; i < 5; i++) {
            track.awardXp(trained, 100, context, 0L);
        }
        assertEquals(4, track.effectiveSatiationTier(trained, context, 0L));

        long fullRecoveryTick = 4 * ProgressionMath.SATIATION_DECAY_TICKS;
        assertEquals(0, track.effectiveSatiationTier(trained, context, fullRecoveryTick));

        // A fresh award after full recovery is priced at the full (tier 0) rate again:
        // 100 cp * factor 20 = 2,000 grains, with no level-up expected at this progress.
        int levelBefore = track.level(trained);
        int progressBefore = track.progressGrains(trained);
        track.awardXp(trained, 100, context, fullRecoveryTick);
        assertEquals(levelBefore, track.level(trained));
        assertEquals(progressBefore + 2_000, track.progressGrains(trained));
        assertEquals(1, track.effectiveSatiationTier(trained, context, fullRecoveryTick),
                "tier advances from the recovered 0 back to 1 after the fresh award");
    }

    /** Test 8: satiation_contextKeysIsolated. */
    @Test
    void satiationContextKeysAreIsolated() {
        SkillTrack track = new SkillTrack(1L, REGISTRY);
        SkillId trained = REGISTRY.id("trained_skill");
        long contextA = 1L;
        long contextB = 2L;

        for (int i = 0; i < 5; i++) {
            track.awardXp(trained, 100, contextA, 0L);
        }
        assertEquals(4, track.effectiveSatiationTier(trained, contextA, 0L));
        assertEquals(0, track.effectiveSatiationTier(trained, contextB, 0L),
                "context B must be untouched by context A's awards");
    }
}
