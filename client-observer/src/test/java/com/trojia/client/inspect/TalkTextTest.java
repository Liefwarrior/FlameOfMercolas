package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.quest.QuestBindings;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRaws;
import com.trojia.sim.actor.quest.QuestRegistry;
import com.trojia.sim.bark.BarkTableRegistry;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link TalkText}'s degraded modes and key parsing (the un-forged compound fixture: EMPTY
 * identity, EMPTY bark tables, UNWIRED standings) — the named/authored surfaces are pinned
 * by {@code DocksTalkTest} against the forged bake — plus the Sprint-3 quest-beat lane:
 * a declared party of a live quest entry greets through the
 * {@code quest.<questId>.<stageKey>.<partySymbol>} key with the {@code *} marker and the
 * {@code [*]} tag; every other combination (non-party, foreign owner, terminal stage, the
 * quest-less overload) keeps the stock path byte-identical. Headless, no GL.
 */
class TalkTextTest {

    @Test
    void dispositionTagParsesTheSelectorsKeyShapes() {
        assertEquals("[cold]", TalkText.dispositionTag("greet.watch.cold.night"));
        assertEquals("[kin]", TalkText.dispositionTag("greet.serf.kin"));
        assertEquals("[held]", TalkText.dispositionTag("mood.held"));
        assertEquals("[?]", TalkText.dispositionTag("weird"));
    }

    @Test
    void unauthoredTablesDegradeToSaysNothingNeverSilence() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        int listener = strangerTo(p, 2); // no kin/friend tie: the neutral greeting lane
        TalkText.Exchange exchange = TalkText.greet(1234L, 100L, 2, listener, p.registry(),
                p.jobs(), IdentityRegistry.EMPTY, p.system().factionStandings(),
                p.relationships(), BarkTableRegistry.EMPTY);

        assertEquals(TalkText.SAYS_NOTHING, exchange.barkLine(),
                "no authored tables: the panel still shows SOMETHING");
        assertTrue(exchange.nameLine().startsWith("Serf #2"),
                "un-forged fixture: the pre-names fallback style: " + exchange.nameLine());
        assertEquals("[neutral]", exchange.contextLine(), "unwired standings read neutral");

        List<String> lines = exchange.panelLines();
        assertEquals(exchange.nameLine(), lines.get(0));
        assertTrue(lines.get(1).endsWith("[neutral]"), lines.get(1));
        assertEquals(TalkText.SAYS_NOTHING, lines.get(lines.size() - 1));
    }

    // ================================================================== S3 quest beats

    /** A one-party, two-stage synthetic quest ({@code ask} -> terminal {@code done}). */
    private static QuestRegistry testQuest(int markActorId) {
        QuestRaws.Trigger talkMark = new QuestRaws.Trigger(QuestRaws.TriggerKind.TALK,
                "mark", null, null, null, null, null, 0, null, 0, null, 0, 0L, "done");
        QuestRaws.Stage ask = new QuestRaws.Stage("ask", "Ask the mark.", "Asked the mark.",
                List.of(), List.of(talkMark), List.of(), false);
        QuestRaws.Stage done = new QuestRaws.Stage("done", "Done.", "It is done.",
                List.of(), List.of(), List.of(), true);
        QuestRaws raws = new QuestRaws(List.of(new QuestRaws.Quest("test-errand",
                "A Test Errand", QuestRaws.Binding.FIRST_TALKER, List.of("mark"), List.of(),
                List.of(), List.of(), List.of(ask, done))));
        return QuestRegistry.bind(raws, new QuestBindings() {
            @Override
            public int partyActorId(String questId, String partySymbol) {
                return "mark".equals(partySymbol) ? markActorId : -1;
            }

            @Override
            public short itemKind(String questId, String itemSymbol) {
                return -1;
            }

            @Override
            public int zoneId(String questId, String zoneSymbol) {
                return -1;
            }

            @Override
            public int cell(String questId, String cellSymbol) {
                return -1;
            }

            @Override
            public int skillRaw(String skillKey) {
                return -1;
            }

            @Override
            public int factionId(String factionKey) {
                return -1;
            }
        });
    }

    /**
     * A mid-quest {@link QuestLog} state reached through the log's own PUBLIC triad (the
     * client has no package access to the engine's mutators, and needs none): the exact
     * serialized frame {@code QuestLog.serialize} documents, loaded into a fresh log.
     */
    private static QuestLog logAt(QuestRegistry quests, int owner, int stage) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bytes);
            out.writeInt(1);            // questCount frame guard
            out.writeInt(0);            // questOrdinal
            out.writeInt(owner);
            out.writeInt(stage);
            out.writeLong(0L);          // stageEnteredTick
            out.writeLong(0L);          // lastCheckTick
            out.writeLong(0L);          // searchAttempts
            out.writeInt(quests.stageCount(0)); // stage-count frame guard
            for (int s = 0; s < quests.stageCount(0); s++) {
                out.writeLong(s < stage ? s : -1L); // stages below current read completed
            }
            out.writeInt(Actor.NONE);   // latch talker
            out.writeInt(Actor.NONE);   // latch target
            out.writeLong(-1L);         // latch tick
            out.writeLong(stage);       // totalAdvances
            out.writeLong(0L);          // crimeCursor
            QuestLog log = new QuestLog(quests);
            log.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));
            return log;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    @Test
    void aQuestPartyGreetsThroughTheQuestKeyWithMarkerAndTag() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        int mark = 2;
        int listener = strangerTo(p, mark);
        QuestRegistry quests = testQuest(mark);
        QuestLog log = new QuestLog(quests); // fresh: owner unbound, stage 0 ("ask")
        BarkTableRegistry barks = BarkTableRegistry.of(List.of(
                new BarkTableRegistry.BarkTable("quest.test-errand.ask.mark",
                        List.of("The clerk? Ask the tide."))));

        TalkText.Exchange exchange = TalkText.greet(1234L, 100L, mark, listener,
                p.registry(), p.jobs(), IdentityRegistry.EMPTY,
                p.system().factionStandings(), p.relationships(), barks, quests, log);

        assertTrue(exchange.nameLine().startsWith(TalkText.QUEST_MARK + " "),
                "the name line carries the quest marker: " + exchange.nameLine());
        assertEquals(TalkText.QUEST_TAG, exchange.contextLine());
        assertEquals("The clerk? Ask the tide.", exchange.barkLine(),
                "the beat resolves the stage x party quest table");
    }

    @Test
    void aQuestBeatFallsBackToTheQuestFloorTable() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        int mark = 2;
        int listener = strangerTo(p, mark);
        QuestRegistry quests = testQuest(mark);
        BarkTableRegistry floorOnly = BarkTableRegistry.of(List.of(
                new BarkTableRegistry.BarkTable("quest.test-errand",
                        List.of("Word gets around."))));

        TalkText.Exchange exchange = TalkText.greet(1234L, 100L, mark, listener,
                p.registry(), p.jobs(), IdentityRegistry.EMPTY,
                p.system().factionStandings(), p.relationships(), floorOnly, quests,
                new QuestLog(quests));

        assertEquals(TalkText.QUEST_TAG, exchange.contextLine());
        assertEquals("Word gets around.", exchange.barkLine(),
                "the stock dot-prefix fallback chain catches the beat");
    }

    @Test
    void nonPartyForeignOwnerAndTerminalStageAllKeepTheStockPath() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        int mark = 2;
        int listener = strangerTo(p, mark);
        QuestRegistry quests = testQuest(mark);

        // A speaker who is no declared party: stock disposition tag, no marker.
        int nonParty = mark == 3 ? 4 : 3;
        TalkText.Exchange stockSpeaker = TalkText.greet(1234L, 100L, nonParty, listener,
                p.registry(), p.jobs(), IdentityRegistry.EMPTY,
                p.system().factionStandings(), p.relationships(), BarkTableRegistry.EMPTY,
                quests, new QuestLog(quests));
        assertFalse(stockSpeaker.nameLine().startsWith(TalkText.QUEST_MARK + " "));
        assertFalse(TalkText.QUEST_TAG.equals(stockSpeaker.contextLine()));

        // The quest bound to ANOTHER body: this listener gets no beat (it cannot advance).
        TalkText.Exchange foreignOwner = TalkText.greet(1234L, 100L, mark, listener,
                p.registry(), p.jobs(), IdentityRegistry.EMPTY,
                p.system().factionStandings(), p.relationships(), BarkTableRegistry.EMPTY,
                quests, logAt(quests, mark == 0 ? 1 : 0, 0));
        assertFalse(foreignOwner.nameLine().startsWith(TalkText.QUEST_MARK + " "),
                "a quest owned by another body serves no beat");

        // A terminal stage: the quest is over — post-ending reactivity is the stock path.
        TalkText.Exchange terminal = TalkText.greet(1234L, 100L, mark, listener,
                p.registry(), p.jobs(), IdentityRegistry.EMPTY,
                p.system().factionStandings(), p.relationships(), BarkTableRegistry.EMPTY,
                quests, logAt(quests, listener, 1));
        assertFalse(terminal.nameLine().startsWith(TalkText.QUEST_MARK + " "),
                "a finished quest serves no beat");

        // And the quest-less overload stays byte-identical to Sprint 2 for a party speaker.
        TalkText.Exchange stockOverload = TalkText.greet(1234L, 100L, mark, listener,
                p.registry(), p.jobs(), IdentityRegistry.EMPTY,
                p.system().factionStandings(), p.relationships(), BarkTableRegistry.EMPTY);
        assertFalse(stockOverload.nameLine().startsWith(TalkText.QUEST_MARK + " "));
        assertEquals("[neutral]", stockOverload.contextLine());
    }

    @Test
    void aDisguisedListenerStillGetsItsOwnQuestBeat() {
        CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
        int mark = 2;
        int listener = strangerTo(p, mark);
        int cover = listener == 0 ? 1 : 0;
        QuestRegistry quests = testQuest(mark);
        try {
            p.registry().get(listener).setActAs(cover);
            TalkText.Exchange exchange = TalkText.greet(1234L, 100L, mark, listener,
                    p.registry(), p.jobs(), IdentityRegistry.EMPTY,
                    p.system().factionStandings(), p.relationships(),
                    BarkTableRegistry.EMPTY, quests, logAt(quests, listener, 0));
            assertTrue(exchange.nameLine().startsWith(TalkText.QUEST_MARK + " "),
                    "quest matching is TRUE-id (bodies talk to bodies): the mask keeps the beat");
            assertEquals(TalkText.QUEST_TAG, exchange.contextLine());
        } finally {
            p.registry().get(listener).setActAs(listener);
        }
    }

    /** The lowest-id actor with NO relationship edge at all to {@code selfId}. */
    private static int strangerTo(CompoundBlockPopulation p, int selfId) {
        outer:
        for (int i = 0; i < p.registry().size(); i++) {
            if (i == selfId) {
                continue;
            }
            for (int e = 0; e < p.relationships().size(); e++) {
                var edge = p.relationships().get(e);
                if ((edge.fromId() == selfId && edge.toId() == i)
                        || (edge.fromId() == i && edge.toId() == selfId)) {
                    continue outer;
                }
            }
            return i;
        }
        throw new AssertionError("everyone knows #" + selfId);
    }
}
