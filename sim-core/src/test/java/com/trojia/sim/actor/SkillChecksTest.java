package com.trojia.sim.actor;

import com.trojia.sim.progression.SkillId;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.progression.SkillRegistry;

import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The skill-check core (Sprint 1, rank 3): one pure permille-threshold function, clamped,
 * draw-decided — and the DoD fixture: a level-5 and a level-50 actor produce MEASURABLY
 * different check outcomes over a draw sweep on the {@code check.push} named stream, with
 * drawIndex attribution pinned (same-tick draws at different indices differ; the same
 * address reproduces).
 */
final class SkillChecksTest {

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

    // ======================================================================
    // The pure threshold function
    // ======================================================================

    @Test
    void equalScoresSitAtTheBaseAndTheClampsHold() {
        assertEquals(SkillChecks.PUSH_BASE_PERMILLE,
                SkillChecks.successPermille(15, 15, SkillChecks.PUSH_BASE_PERMILLE,
                        SkillChecks.PUSH_FLOOR_PERMILLE, SkillChecks.PUSH_CEIL_PERMILLE));
        assertEquals(SkillChecks.PUSH_FLOOR_PERMILLE,
                SkillChecks.successPermille(10, 90, SkillChecks.PUSH_BASE_PERMILLE,
                        SkillChecks.PUSH_FLOOR_PERMILLE, SkillChecks.PUSH_CEIL_PERMILLE),
                "a hopeless pusher still gets the liveness floor");
        assertEquals(SkillChecks.PUSH_CEIL_PERMILLE,
                SkillChecks.successPermille(90, 10, SkillChecks.PUSH_BASE_PERMILLE,
                        SkillChecks.PUSH_FLOOR_PERMILLE, SkillChecks.PUSH_CEIL_PERMILLE),
                "mastery never buys certainty (ceiling clamp)");
        // Unclamped middle: each score point is worth exactly POINTS_TO_PERMILLE.
        assertEquals(SkillChecks.PUSH_BASE_PERMILLE - 2 * SkillChecks.POINTS_TO_PERMILLE,
                SkillChecks.successPermille(13, 15, SkillChecks.PUSH_BASE_PERMILLE,
                        SkillChecks.PUSH_FLOOR_PERMILLE, SkillChecks.PUSH_CEIL_PERMILLE));
    }

    // ======================================================================
    // The DoD fixture: level 5 vs level 50 differ measurably
    // ======================================================================

    /**
     * Awards exactly enough tier-0 XP to reach {@code targetLevel} in a TRAINED (aptNum 20)
     * skill: cumulative 0->L cost is {@code sum_{k=1..L} k*100*20} grains; one tier-0 award
     * of {@code cost/20} cp lands exactly at the level with zero spare.
     */
    private static void trainTo(SkillTrackRegistry tracks, int actorId, int skillRaw,
            int targetLevel) {
        long grains = 0;
        for (int k = 1; k <= targetLevel; k++) {
            grains += k * 100L * 20L;
        }
        tracks.award(actorId, skillRaw, (int) (grains / 20), 999L, 0L);
        assertEquals(targetLevel, tracks.level(actorId, skillRaw));
    }

    @Test
    void levelFiveAndLevelFiftyShiftTheCheckOutcomeMeasurably() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SKILLS);
        int novice = 0;
        int master = 1;
        int resistor = 2; // a hard target (grit 60), so neither pusher ceiling-clamps
        trainTo(tracks, novice, tracks.openHandRaw(), 5);
        trainTo(tracks, master, tracks.openHandRaw(), 50);
        trainTo(tracks, resistor, tracks.gritRaw(), 60);

        int noviceThreshold = SkillChecks.pushContestPermille(tracks, novice, resistor);
        int masterThreshold = SkillChecks.pushContestPermille(tracks, master, resistor);
        assertTrue(masterThreshold > noviceThreshold,
                "50 levels of open_hand must raise the threshold: novice=" + noviceThreshold
                        + " master=" + masterThreshold);

        // Sweep the check.push stream across 4000 tick-addresses: the realized pass counts
        // must separate (the fixture DoD "outcomes differ", not just the thresholds).
        long worldSeed = 7L;
        int novicePasses = 0;
        int masterPasses = 0;
        for (long tick = 1; tick <= 4_000; tick++) {
            long noviceDraw = NamedDraws.draw(ActorRngStream.CHECK_PUSH, worldSeed, tick, novice, 0);
            long masterDraw = NamedDraws.draw(ActorRngStream.CHECK_PUSH, worldSeed, tick, master, 0);
            novicePasses += SkillChecks.passes(noviceDraw, noviceThreshold) ? 1 : 0;
            masterPasses += SkillChecks.passes(masterDraw, masterThreshold) ? 1 : 0;
        }
        assertTrue(masterPasses > novicePasses,
                "over 4000 draws the master must land more contests: novice=" + novicePasses
                        + " master=" + masterPasses);
        // Both stay inside the clamp band (sanity on the sweep itself).
        assertTrue(novicePasses > 4_000 * SkillChecks.PUSH_FLOOR_PERMILLE / 1000 / 2,
                "the liveness floor keeps even the novice mostly succeeding");
    }

    /** Attribution audit: the same draw address reproduces; a different index diverges. */
    @Test
    void drawAttributionIsPinnedPerActorPerIndex() {
        long a = NamedDraws.draw(ActorRngStream.CHECK_PUSH, 42L, 100L, 7, 0);
        assertEquals(a, NamedDraws.draw(ActorRngStream.CHECK_PUSH, 42L, 100L, 7, 0),
                "same (seed, tick, stream, actor, index) -> same draw, always");
        assertTrue(a != NamedDraws.draw(ActorRngStream.CHECK_PUSH, 42L, 100L, 7, 1),
                "the next drawIndex is a fresh draw");
        assertTrue(a != NamedDraws.draw(ActorRngStream.WATCH_LENIENCE, 42L, 100L, 7, 0),
                "streams are salted apart");
    }

    /** The search family (Sprint 3 quests): base at parity, clamps hold, skill+WIT feeds it. */
    @Test
    void searchFamilyBoundsAndSkillFeedTheThreshold() {
        // The pure-threshold shape at the search family's own constants.
        assertEquals(SkillChecks.SEARCH_BASE_PERMILLE,
                SkillChecks.successPermille(12, 12, SkillChecks.SEARCH_BASE_PERMILLE,
                        SkillChecks.SEARCH_FLOOR_PERMILLE, SkillChecks.SEARCH_CEIL_PERMILLE));
        assertEquals(SkillChecks.SEARCH_FLOOR_PERMILLE,
                SkillChecks.successPermille(0, 90, SkillChecks.SEARCH_BASE_PERMILLE,
                        SkillChecks.SEARCH_FLOOR_PERMILLE, SkillChecks.SEARCH_CEIL_PERMILLE),
                "a hopeless pry still gives occasionally (floor clamp)");
        assertEquals(SkillChecks.SEARCH_CEIL_PERMILLE,
                SkillChecks.successPermille(90, 0, SkillChecks.SEARCH_BASE_PERMILLE,
                        SkillChecks.SEARCH_FLOOR_PERMILLE, SkillChecks.SEARCH_CEIL_PERMILLE),
                "mastery never buys certainty (ceiling clamp)");

        // The wired read: an untrained body (level 0, WIT base 10) vs the quest's lock 12
        // sits just under base; training streetwise raises it measurably.
        SkillTrackRegistry tracks = new SkillTrackRegistry(SKILLS);
        int novice = 0;
        int sneak = 1;
        int lockResist = 12;
        int noviceThreshold = SkillChecks.searchPermille(tracks, novice, tracks.streetwiseRaw(),
                lockResist);
        assertEquals(SkillChecks.SEARCH_BASE_PERMILLE
                        + SkillChecks.POINTS_TO_PERMILLE * (10 - lockResist),
                noviceThreshold, "untrained: level 0 + WIT 10 against lock 12");
        // Streetwise is FAVORED (aptNum 15): cumulative 0->20 cost is sum k*100*15 grains.
        long grains = 0;
        for (int k = 1; k <= 20; k++) {
            grains += k * 100L * 15L;
        }
        tracks.award(sneak, tracks.streetwiseRaw(), (int) (grains / 20), 999L, 0L);
        assertEquals(20, tracks.level(sneak, tracks.streetwiseRaw()));
        int sneakThreshold = SkillChecks.searchPermille(tracks, sneak, tracks.streetwiseRaw(),
                lockResist);
        assertTrue(sneakThreshold > noviceThreshold,
                "20 levels of streetwise must raise the pry threshold: novice="
                        + noviceThreshold + " sneak=" + sneakThreshold);
    }

    /** The wired contest reads live attributes: training skyrunning raises AGI raises the odds. */
    @Test
    void attributesFeedTheContestLive() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SKILLS);
        int runner = 0;
        int before = SkillChecks.pushContestPermille(tracks, runner, 1);
        // Skyrunning carries AGI weight 64: +40 levels -> +(40*64)>>8 = +10 AGI, no open_hand.
        SkillId skyrunning = SKILLS.id(SkillTrackRegistry.KEY_SKYRUNNING);
        long grains = 0;
        for (int k = 1; k <= 40; k++) {
            grains += k * 100L * 15L; // FAVORED aptNum 15
        }
        tracks.award(runner, tracks.skyrunningRaw(), (int) (grains / 20), 5L, 0L);
        assertEquals(40, tracks.level(runner, skyrunning.raw()));
        int after = SkillChecks.pushContestPermille(tracks, runner, 1);
        assertTrue(after > before, "AGI is recomputed live from the track (§5): "
                + before + " -> " + after);
    }
}
