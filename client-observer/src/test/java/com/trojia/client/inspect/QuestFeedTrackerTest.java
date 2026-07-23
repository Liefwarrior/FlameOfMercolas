package com.trojia.client.inspect;

import com.trojia.client.boot.RepoPaths;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRegistry;
import com.trojia.sim.progression.SkillRawsLoader;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link QuestFeedTracker}'s golden narration over the synthetic {@link QuestFixtures}
 * quest: construction baselines are history; one stage flip is one {@code "* <log line>"}
 * feed entry (+ the ending's closing line on a terminal flip); toasts fire only for the
 * played OWNER; a search attempt with an unmoved stage toasts the
 * {@link CheckLineFormatter#searchLine} failure line. Headless, no GL; every state change
 * lands through {@code QuestLog}'s own PUBLIC triad ({@link QuestFixtures#reloadAt}).
 */
class QuestFeedTrackerTest {

    private static final int OWNER = 2;

    @Test
    void constructionStateIsHistoryNotNews() {
        QuestRegistry quests = QuestFixtures.threeStageQuest(5);
        QuestLog log = QuestFixtures.logAt(quests, OWNER, 1, new long[] {10, -1, -1}, 4);
        EventLog feed = new EventLog(10);
        ToastQueue toasts = new ToastQueue();
        QuestFeedTracker tracker = new QuestFeedTracker(quests, log,
                SkillTrackRegistry.UNWIRED, feed, toasts, () -> OWNER);

        tracker.afterTick(11);

        assertEquals(0, feed.size(), "pre-existing quest state narrates nothing");
        assertTrue(toasts.visible().isEmpty());
    }

    @Test
    void oneAdvanceFeedsTheCompletedStagesLineAndToastsTheOwner() {
        QuestRegistry quests = QuestFixtures.threeStageQuest(5);
        QuestLog log = new QuestLog(quests);
        EventLog feed = new EventLog(10);
        ToastQueue toasts = new ToastQueue();
        QuestFeedTracker tracker = new QuestFeedTracker(quests, log,
                SkillTrackRegistry.UNWIRED, feed, toasts, () -> OWNER);

        QuestFixtures.reloadAt(log, quests, OWNER, 1, new long[] {100, -1, -1}, 0);
        tracker.afterTick(100);

        assertEquals(1, feed.size());
        assertEquals("* Heard the name.", feed.recentNewestFirst(1).get(0).text());
        assertEquals(100L, feed.recentNewestFirst(1).get(0).tick());
        assertEquals(1, toasts.visible().size());
        assertEquals("* Journal updated - A Test Errand", toasts.visible().get(0).text());
    }

    @Test
    void aTerminalAdvanceAlsoFeedsTheEndingLineAndToastsQuestComplete() {
        QuestRegistry quests = QuestFixtures.threeStageQuest(5);
        QuestLog log = QuestFixtures.logAt(quests, OWNER, 1, new long[] {100, -1, -1}, 0);
        EventLog feed = new EventLog(10);
        ToastQueue toasts = new ToastQueue();
        QuestFeedTracker tracker = new QuestFeedTracker(quests, log,
                SkillTrackRegistry.UNWIRED, feed, toasts, () -> OWNER);

        QuestFixtures.reloadAt(log, quests, OWNER, 2, new long[] {100, 200, -1}, 0);
        tracker.afterTick(200);

        assertEquals(2, feed.size(), "the completed stage's line AND the ending's own line");
        assertEquals("* The tale is told.", feed.recentNewestFirst(2).get(0).text());
        assertEquals("* The drawer gave.", feed.recentNewestFirst(2).get(1).text());
        assertEquals(1, toasts.visible().size());
        assertEquals("* Quest complete - A Test Errand", toasts.visible().get(0).text());
    }

    @Test
    void advancesOfAForeignOwnerFeedTheWardButNeverToast() {
        QuestRegistry quests = QuestFixtures.threeStageQuest(5);
        QuestLog log = new QuestLog(quests);
        EventLog feed = new EventLog(10);
        ToastQueue toasts = new ToastQueue();
        QuestFeedTracker tracker = new QuestFeedTracker(quests, log,
                SkillTrackRegistry.UNWIRED, feed, toasts, () -> Actor.NONE);

        QuestFixtures.reloadAt(log, quests, OWNER, 1, new long[] {100, -1, -1}, 0);
        tracker.afterTick(100);

        assertEquals(1, feed.size(), "the ward's feed still narrates the advance");
        assertTrue(toasts.visible().isEmpty(), "no played owner: no toast");
    }

    @Test
    void aFailedSearchAttemptToastsTheCheckLineForTheOwnerOnly() {
        QuestRegistry quests = QuestFixtures.threeStageQuest(5);
        QuestLog log = QuestFixtures.logAt(quests, OWNER, 1, new long[] {50, -1, -1}, 0);
        EventLog feed = new EventLog(10);
        ToastQueue toasts = new ToastQueue();
        SkillTrackRegistry tracks = new SkillTrackRegistry(
                SkillRawsLoader.load(RepoPaths.locate("content", "raws")));
        QuestFeedTracker tracker = new QuestFeedTracker(quests, log, tracks, feed, toasts,
                () -> OWNER);

        QuestFixtures.reloadAt(log, quests, OWNER, 1, new long[] {50, -1, -1}, 1);
        tracker.afterTick(75);

        assertEquals(0, feed.size(), "failures are the owner's business, not the ward's");
        assertEquals(1, toasts.visible().size());
        // The fixture binds the search skill to raw 0; the line reads the wired registry.
        String skill = tracks.skills().get(0).displayName();
        assertEquals("[" + skill + " 0 vs lock 12 - the drawer holds]",
                toasts.visible().get(0).text());

        // The same attempt bump with nobody played toasts nothing.
        ToastQueue foreign = new ToastQueue();
        QuestFeedTracker bystander = new QuestFeedTracker(quests, log, tracks, feed,
                foreign, () -> Actor.NONE);
        QuestFixtures.reloadAt(log, quests, OWNER, 1, new long[] {50, -1, -1}, 2);
        bystander.afterTick(100);
        assertTrue(foreign.visible().isEmpty());
    }

    @Test
    void theUnwiredSearchLineDegradesNumberless() {
        assertEquals("[search - the drawer holds]",
                CheckLineFormatter.searchLine(SkillTrackRegistry.UNWIRED, OWNER, 0, 12));
        SkillTrackRegistry tracks = new SkillTrackRegistry(
                SkillRawsLoader.load(RepoPaths.locate("content", "raws")));
        assertEquals("[search - the drawer holds]",
                CheckLineFormatter.searchLine(tracks, OWNER, -1, 12),
                "a stage with no declared search degrades the same way");
    }
}
