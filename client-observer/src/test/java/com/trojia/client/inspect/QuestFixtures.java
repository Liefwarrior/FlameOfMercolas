package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.quest.QuestBindings;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRaws;
import com.trojia.sim.actor.quest.QuestRegistry;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.List;

/**
 * Shared synthetic-quest fixtures for the S3 client tests ({@code JournalTextTest},
 * {@code QuestFeedTrackerTest}): a three-stage quest — {@code rumor} (talk-advance) →
 * {@code pry} (search-advance, resist 12) → terminal {@code done} — bound to caller-chosen
 * ids, plus a mid-quest {@link QuestLog} state builder that goes through the log's own
 * PUBLIC triad (the client has no package access to the engine's mutators, and needs
 * none — the serialized frame IS the documented contract).
 */
final class QuestFixtures {

    static final String QUEST_ID = "test-errand";
    static final String TITLE = "A Test Errand";
    static final int SEARCH_RESIST = 12;

    private QuestFixtures() {
    }

    /** The three-stage synthetic quest, party {@code ally} bound to {@code allyActorId}. */
    static QuestRegistry threeStageQuest(int allyActorId) {
        QuestRaws.Trigger talkAlly = new QuestRaws.Trigger(QuestRaws.TriggerKind.TALK,
                "ally", null, null, null, null, null, 0, null, 0, null, 0, 0L, "pry");
        QuestRaws.Trigger search = new QuestRaws.Trigger(QuestRaws.TriggerKind.SEARCH,
                null, null, null, "leaf", "desk", "streetwise", SEARCH_RESIST, null, 25,
                null, 0, 0L, "done");
        QuestRaws.Stage rumor = new QuestRaws.Stage("rumor", "Find who remembers.",
                "Heard the name.", List.of(), List.of(talkAlly), List.of(), false);
        QuestRaws.Stage pry = new QuestRaws.Stage("pry", "Open the drawer.",
                "The drawer gave.", List.of(), List.of(search), List.of(), false);
        QuestRaws.Stage done = new QuestRaws.Stage("done", "It is done.",
                "The tale is told.", List.of(), List.of(), List.of(), true);
        QuestRaws raws = new QuestRaws(List.of(new QuestRaws.Quest(QUEST_ID, TITLE,
                QuestRaws.Binding.FIRST_TALKER, List.of("ally"), List.of("leaf"),
                List.of(), List.of("desk"), List.of(rumor, pry, done))));
        return QuestRegistry.bind(raws, new QuestBindings() {
            @Override
            public int partyActorId(String questId, String partySymbol) {
                return "ally".equals(partySymbol) ? allyActorId : -1;
            }

            @Override
            public short itemKind(String questId, String itemSymbol) {
                return (short) ("leaf".equals(itemSymbol) ? 7 : -1);
            }

            @Override
            public int zoneId(String questId, String zoneSymbol) {
                return -1;
            }

            @Override
            public int cell(String questId, String cellSymbol) {
                return "desk".equals(cellSymbol) ? 0 : -1;
            }

            @Override
            public int skillRaw(String skillKey) {
                return 0; // any wired raw: the fixture's tracks resolve names themselves
            }

            @Override
            public int factionId(String factionKey) {
                return -1;
            }
        });
    }

    /**
     * A mid-quest log state loaded through {@link QuestLog#load}: {@code owner} at
     * {@code stage}, the given per-stage completion ticks ({@code -1} = never), and the
     * given monotonic search-attempt count.
     */
    static QuestLog logAt(QuestRegistry quests, int owner, int stage, long[] completedTicks,
            long searchAttempts) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bytes);
            out.writeInt(1);            // questCount frame guard
            out.writeInt(0);            // questOrdinal
            out.writeInt(owner);
            out.writeInt(stage);
            out.writeLong(0L);          // stageEnteredTick
            out.writeLong(0L);          // lastCheckTick
            out.writeLong(searchAttempts);
            out.writeInt(quests.stageCount(0)); // stage-count frame guard
            for (int s = 0; s < quests.stageCount(0); s++) {
                out.writeLong(s < completedTicks.length ? completedTicks[s] : -1L);
            }
            out.writeInt(Actor.NONE);   // latch talker
            out.writeInt(Actor.NONE);   // latch target
            out.writeLong(-1L);         // latch tick
            long advances = 0;
            for (long tick : completedTicks) {
                if (tick >= 0) {
                    advances++;
                }
            }
            out.writeLong(advances);    // totalAdvances
            out.writeLong(0L);          // crimeCursor
            QuestLog log = new QuestLog(quests);
            log.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));
            return log;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /** {@link #logAt} into an EXISTING log (same registry) — the tracker-diff mutator. */
    static void reloadAt(QuestLog log, QuestRegistry quests, int owner, int stage,
            long[] completedTicks, long searchAttempts) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            QuestLog source = logAt(quests, owner, stage, completedTicks, searchAttempts);
            source.serialize(new DataOutputStream(bytes));
            log.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }
}
