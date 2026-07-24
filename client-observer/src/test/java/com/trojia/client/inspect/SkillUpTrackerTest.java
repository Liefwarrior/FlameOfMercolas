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
 * {@link SkillUpTracker} contract (Sprint 1 item 3, Sprint 5 the torrent's flood control):
 * the played actor's level-ups toast verbatim; the population's ordinary level-ups BANK
 * into the growth digest and surface as one line at the day-phase turn; milestone levels
 * pass through as immediate named lines; every feed line rides the GROWTH channel;
 * pre-existing log rows are history, not news. Uses a REAL wired
 * {@link SkillTrackRegistry} off the committed skills raws
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
    void populationLevelUpBanksIntoTheDigestAndFlushesAtThePhaseTurn() {
        Rig r = rig(PLAYED_ID);
        levelOnce(r.tracks(), BYSTANDER_ID, 9L);
        r.tracker().afterTick(9L);
        assertEquals(0, r.feed().size(),
                "an ordinary level-up is banked, not narrated per-line (the torrent rule)");

        r.tracker().afterTick(com.trojia.client.hud.DayPhase.DAY_START); // Dawn -> Day
        assertEquals(1, r.feed().size());
        EventLog.Entry entry = r.feed().recentNewestFirst(1).get(0);
        assertEquals(com.trojia.client.hud.DayPhase.DAY_START, entry.tick());
        assertEquals(EventLog.Channel.GROWTH, entry.channel());
        assertEquals("Growth, Day 1 Dawn: 1 soul trained Streetwise", entry.text());
        assertTrue(r.toasts().visible().isEmpty(), "a bystander's growth never toasts");
    }

    @Test
    void milestoneLevelsPassTheDigestAsImmediateNamedLines() {
        Rig r = rig(PLAYED_ID);
        int raw = r.tracks().streetwiseRaw();
        // One tier-0 award big enough to cross level 25 (FAVORED: cumulative grains to L
        // = 1500 * L(L+1)/2; 20 grains/cp at a fresh context). 40,000 cp = 800,000 grains
        // -> L(L+1) <= 1066 -> level 32: milestone 25 crossed once.
        r.tracks().award(BYSTANDER_ID, raw, 40_000, 77L, 9L);
        assertTrue(r.tracks().level(BYSTANDER_ID, raw) >= 25, "calibration: crossed 25");

        r.tracker().afterTick(9L);
        assertEquals(1, r.feed().size(), "exactly the milestone line is immediate");
        EventLog.Entry entry = r.feed().recentNewestFirst(1).get(0);
        assertEquals("Serf #3 reached Streetwise 25", entry.text());
        assertEquals(EventLog.Channel.GROWTH, entry.channel());
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
    void oneMultiThresholdAwardNamesEachMilestoneAndBanksTheSoulOnce() {
        Rig r = rig(Actor.NONE); // nobody played: everything is population growth
        int raw = r.tracks().streetwiseRaw();
        // One enormous award crosses many thresholds in a single tick (SkillTrack#awardXp
        // loops) — under the torrent rule only the MILESTONES surface immediately, each
        // named in crossing order; the ordinary levels bank into the digest as one soul.
        r.tracks().award(BYSTANDER_ID, raw, 5_000_000, 42L, 11L);
        int levelled = r.tracks().level(BYSTANDER_ID, raw);
        assertTrue(levelled >= 50, "calibration: expected a multi-milestone award, got "
                + levelled);

        r.tracker().afterTick(11L);
        int milestones = levelled / SkillUpTracker.MILESTONE_STEP;
        List<EventLog.Entry> newestFirst = r.feed().recentNewestFirst(r.feed().size());
        assertEquals(milestones, newestFirst.size(),
                "exactly the milestone levels are immediate lines");
        assertEquals("Serf #3 reached Streetwise " + milestones * SkillUpTracker.MILESTONE_STEP,
                newestFirst.get(0).text(), "the newest line is the highest milestone");

        r.tracker().afterTick(com.trojia.client.hud.DayPhase.DAY_START);
        assertEquals("Growth, Day 1 Dawn: 1 soul trained Streetwise",
                r.feed().recentNewestFirst(1).get(0).text(),
                "the multi-level carry still counts as ONE soul in the phase census");
    }

    @Test
    void thePlayedActorsMultiLevelCarryToastsEveryLevel() {
        Rig r = rig(PLAYED_ID);
        int raw = r.tracks().streetwiseRaw();
        r.tracks().award(PLAYED_ID, raw, 5_000_000, 43L, 12L);
        int levelled = r.tracks().level(PLAYED_ID, raw);
        r.tracker().afterTick(12L);

        List<ToastQueue.Toast> toasts = r.toasts().visible();
        assertEquals(ToastQueue.MAX_VISIBLE, toasts.size(),
                "every level toasts; the queue keeps the newest " + ToastQueue.MAX_VISIBLE);
        assertEquals("Streetwise increased to " + levelled,
                toasts.get(toasts.size() - 1).text(),
                "the played soul's growth stays verbatim — the Morrowind moment");
        assertEquals(0, r.feed().size(), "the played actor's growth never spams the feed");
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
