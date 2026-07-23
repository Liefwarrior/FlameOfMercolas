package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.DailyRhythm;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRegistry;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * {@link JournalText}'s golden states over the synthetic {@link QuestFixtures} quest —
 * unclaimed, mid-quest, finished, branch-ordered story, and the quest-less degrade — with
 * the un-forged compound roster's {@code "Serf #2"} owner-naming fallback. Headless, no GL;
 * every mid-quest state is reached through {@code QuestLog}'s own PUBLIC triad.
 */
class JournalTextTest {

    @Test
    void anUnclaimedQuestReadsUnclaimedAtItsFirstObjective() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        QuestRegistry quests = QuestFixtures.threeStageQuest(2);

        List<String> lines = JournalText.lines(quests, new QuestLog(quests), p.registry(),
                IdentityRegistry.EMPTY);

        assertEquals(List.of(
                "A Test Errand - (no one has taken this up)",
                "-- OBJECTIVE --",
                "Find who remembers.",
                "-- THE STORY SO FAR --",
                "(nothing yet)"), lines);
    }

    @Test
    void aMidQuestJournalNamesTheOwnerAndDayStampsTheStory() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        QuestRegistry quests = QuestFixtures.threeStageQuest(2);
        QuestLog log = QuestFixtures.logAt(quests, 2, 1, new long[] {10, -1, -1}, 3);

        List<String> lines = JournalText.lines(quests, log, p.registry(),
                IdentityRegistry.EMPTY);

        assertEquals(List.of(
                "A Test Errand - Serf #2's story",
                "-- OBJECTIVE --",
                "Open the drawer.",
                "-- THE STORY SO FAR --",
                "Day 1 - Heard the name."), lines);
    }

    @Test
    void aFinishedQuestReadsTheEndingObjectiveOverTheWholeStory() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        QuestRegistry quests = QuestFixtures.threeStageQuest(2);
        QuestLog log = QuestFixtures.logAt(quests, 2, 2,
                new long[] {10, DailyRhythm.DAY + 5, -1}, 0);

        List<String> lines = JournalText.lines(quests, log, p.registry(),
                IdentityRegistry.EMPTY);

        assertEquals(List.of(
                "A Test Errand - Serf #2's story",
                "-- OBJECTIVE --",
                "It is done.",
                "-- THE STORY SO FAR --",
                "Day 1 - Heard the name.",
                "Day 2 - The drawer gave."), lines);
    }

    @Test
    void theStoryReadsInCompletionOrderNotStageOrder() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        QuestRegistry quests = QuestFixtures.threeStageQuest(2);
        // A (hypothetical) path that completed stage 1 before stage 0: the story must
        // follow the ticks, not the ordinals — branchy quests read in visit order.
        QuestLog log = QuestFixtures.logAt(quests, 2, 2,
                new long[] {DailyRhythm.DAY + 5, 10, -1}, 0);

        List<String> lines = JournalText.lines(quests, log, p.registry(),
                IdentityRegistry.EMPTY);

        assertEquals("Day 1 - The drawer gave.", lines.get(4));
        assertEquals("Day 2 - Heard the name.", lines.get(5));
    }

    @Test
    void aQuestlessFixtureReadsTheEmptyJournal() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        List<String> lines = JournalText.lines(QuestRegistry.EMPTY, QuestLog.UNWIRED,
                p.registry(), IdentityRegistry.EMPTY);
        assertEquals(List.of(JournalText.EMPTY_JOURNAL), lines);
        assertEquals(0, QuestLog.UNWIRED.entryCount());
    }
}
