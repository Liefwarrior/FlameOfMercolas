package com.trojia.client.inspect;

import com.trojia.client.hud.DayPhase;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.SkillTrackRegistry;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link GrowthDigest} contract (Sprint 5 "the torrent"): population level-ups bank per
 * day-phase bucket and flush as ONE deterministic line at the phase turn — distinct souls
 * counted once, skills ordered soul-count-desc then raw-asc, the tail folded past
 * {@link GrowthDigest#MAX_NAMED_SKILLS}. Headless, no GL, zero sim writes.
 */
class GrowthDigestTest {

    private final SkillTrackRegistry tracks = DocksPopulation.freshSkillTracks();

    private int raw(String key) {
        return tracks.skills().id(key).raw();
    }

    @Test
    void firstTickOpensTheBucketSilently() {
        GrowthDigest digest = new GrowthDigest(tracks);
        assertNull(digest.maybeFlush(5L), "the first tick has no closed bucket to narrate");
    }

    @Test
    void anEmptyBucketTurnsOverSilently() {
        GrowthDigest digest = new GrowthDigest(tracks);
        digest.maybeFlush(5L);                       // Dawn bucket opens
        assertNull(digest.maybeFlush(DayPhase.DAY_START), "nothing levelled: no line");
    }

    @Test
    void aPhaseTurnFlushesOneDeterministicLine() {
        GrowthDigest digest = new GrowthDigest(tracks);
        digest.maybeFlush(5L);                       // Day 1 Dawn opens
        digest.observe(10, raw("fieldcraft"));
        digest.observe(11, raw("fieldcraft"));
        digest.observe(11, raw("fieldcraft"));       // same soul again: counted once
        digest.observe(12, raw("streetwise"));

        assertNull(digest.maybeFlush(1_999L), "same phase: no flush");
        assertEquals("Growth, Day 1 Dawn: 2 souls trained Fieldcraft, 1 Streetwise",
                digest.maybeFlush(DayPhase.DAY_START));
        assertNull(digest.maybeFlush(DayPhase.DAY_START + 1), "the bucket reset");
    }

    @Test
    void singleSoulReadsSingular() {
        GrowthDigest digest = new GrowthDigest(tracks);
        digest.maybeFlush(5L);
        digest.observe(7, raw("seacraft"));
        assertEquals("Growth, Day 1 Dawn: 1 soul trained Seacraft",
                digest.maybeFlush(DayPhase.DAY_START));
    }

    @Test
    void tiesBreakByRawIdAndTheTailFolds() {
        GrowthDigest digest = new GrowthDigest(tracks);
        digest.maybeFlush(5L);
        // Seven distinct skills, one soul each: six named (ties broken by raw id asc —
        // SkillRegistry raws are key-alphabetical), the seventh folds into "+1 more trades".
        String[] keys = {"sidearms", "bladework", "lancework", "heavy_arms", "dire_bows",
                "open_hand", "shieldwall"};
        for (int i = 0; i < keys.length; i++) {
            digest.observe(100 + i, raw(keys[i]));
        }
        assertEquals("Growth, Day 1 Dawn: 1 soul trained Bladework, 1 Dire Bows, "
                        + "1 Heavy Arms, 1 Lancework, 1 Open Hand, 1 Shieldwall, "
                        + "+1 more trades",
                digest.maybeFlush(DayPhase.DAY_START));
    }

    @Test
    void dayRolloverFlushesEvenWithinTheSamePhaseName() {
        GrowthDigest digest = new GrowthDigest(tracks);
        long lateNight = DailyRhythm.DAY - 10;       // Day 1 Night
        digest.maybeFlush(lateNight);
        digest.observe(3, raw("grit"));
        // Day 2's dawn is a NEW (day, phase) pair even though a Night->Dawn phase change
        // also happens; assert the day label names the closed bucket's day, not the new one.
        String line = digest.maybeFlush(DailyRhythm.DAY + 5);
        assertEquals("Growth, Day 1 Night: 1 soul trained Grit", line);
        assertTrue(DayPhase.of(lateNight) == DayPhase.NIGHT, "calibration");
    }
}
