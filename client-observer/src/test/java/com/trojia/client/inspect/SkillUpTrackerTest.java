package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.SkillTrackRegistry;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link SkillUpTracker} contract (Sprint 1 item 3): the played actor's level-ups toast;
 * everyone else's land in the event feed; pre-existing log rows are history, not news; and
 * one multi-threshold award narrates every level it crossed (the multi-level single-tick
 * carry). Uses a REAL wired {@link SkillTrackRegistry} off the committed skills raws
 * ({@link DocksPopulation#freshSkillTracks()}) over the compound registry — headless, no
 * GL, zero sim writes (awards go straight into the standalone side table).
 */
class SkillUpTrackerTest {

    private static final int PLAYED_ID = 2;
    private static final int BYSTANDER_ID = 3;

    private record Rig(SkillTrackRegistry tracks, EventLog feed, ToastQueue toasts,
            SkillUpTracker tracker) {
    }

    private static Rig rig(int playedActorId) {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        SkillTrackRegistry tracks = DocksPopulation.freshSkillTracks();
        EventLog feed = new EventLog(30);
        ToastQueue toasts = new ToastQueue();
        SkillUpTracker tracker = new SkillUpTracker(tracks, p.registry(),
                IdentityRegistry.EMPTY, feed, toasts, () -> playedActorId);
        return new Rig(tracks, feed, toasts, tracker);
    }

    @Test
    void playedActorsLevelUpBecomesAToast() {
        Rig r = rig(PLAYED_ID);
        levelOnce(r.tracks(), PLAYED_ID, 7L);
        r.tracker().afterTick(7L);

        List<ToastQueue.Toast> toasts = r.toasts().visible();
        assertEquals(1, toasts.size());
        assertEquals("Streetwise increased to 1", toasts.get(0).text());
        assertEquals(0, r.feed().size(), "the played actor's growth never spams the feed");
    }

    @Test
    void populationLevelUpLandsInTheEventFeedAsAPerson() {
        Rig r = rig(PLAYED_ID);
        levelOnce(r.tracks(), BYSTANDER_ID, 9L);
        r.tracker().afterTick(9L);

        assertEquals(1, r.feed().size());
        EventLog.Entry entry = r.feed().recentNewestFirst(1).get(0);
        assertEquals(9L, entry.tick());
        assertEquals("Serf #3 is now Streetwise 1", entry.text());
        assertTrue(r.toasts().visible().isEmpty(), "a bystander's growth never toasts");
    }

    @Test
    void rowsRecordedBeforeConstructionAreHistoryNotNews() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        SkillTrackRegistry tracks = DocksPopulation.freshSkillTracks();
        levelOnce(tracks, BYSTANDER_ID, 3L); // recorded BEFORE the tracker exists

        EventLog feed = new EventLog(30);
        ToastQueue toasts = new ToastQueue();
        SkillUpTracker tracker = new SkillUpTracker(tracks, p.registry(),
                IdentityRegistry.EMPTY, feed, toasts, () -> Actor.NONE);
        tracker.afterTick(4L);

        assertEquals(0, feed.size(), "the spawn-baseline convention: old rows are history");
        assertTrue(toasts.visible().isEmpty());
    }

    @Test
    void oneMultiThresholdAwardNarratesEveryLevelInOrder() {
        Rig r = rig(Actor.NONE); // nobody played: everything lands in the feed
        int raw = r.tracks().streetwiseRaw();
        // One enormous award crosses several thresholds in a single tick
        // (SkillTrack#awardXp loops) — each level must get its own feed line, in order.
        r.tracks().award(BYSTANDER_ID, raw, 5_000_000, 42L, 11L);
        int levelled = r.tracks().level(BYSTANDER_ID, raw);
        assertTrue(levelled >= 2, "calibration: expected a multi-level award, got " + levelled);

        r.tracker().afterTick(11L);

        List<EventLog.Entry> newestFirst = r.feed().recentNewestFirst(r.feed().size());
        assertEquals(Math.min(levelled, r.feed().capacity()), newestFirst.size());
        assertEquals("Serf #3 is now Streetwise " + levelled, newestFirst.get(0).text(),
                "the newest line is the highest level reached");
        assertEquals("Serf #3 is now Streetwise " + (levelled - 1), newestFirst.get(1).text(),
                "levels narrate in crossing order");
    }

    @Test
    void aTickWithNoLevelUpsNarratesNothing() {
        Rig r = rig(PLAYED_ID);
        r.tracker().afterTick(1L);
        r.tracker().afterTick(2L);
        assertEquals(0, r.feed().size());
        assertTrue(r.toasts().visible().isEmpty());
    }

    /**
     * Awards exactly enough XP for ONE level-up of streetwise (level 0 -> 1): streetwise is
     * FAVORED (aptNum 15), so threshold(0) = 1500 grains = 75 cp at tier-0 satiation
     * (ProgressionMath's 20 grains/cp) — one award, one level, zero banked.
     */
    private static void levelOnce(SkillTrackRegistry tracks, int actorId, long tick) {
        int raw = tracks.streetwiseRaw();
        long before = tracks.levelLog().totalRecorded();
        tracks.award(actorId, raw, 75, 999L, tick);
        assertEquals(1, tracks.level(actorId, raw), "calibration: wanted exactly level 1");
        assertEquals(before + 1, tracks.levelLog().totalRecorded());
    }
}
