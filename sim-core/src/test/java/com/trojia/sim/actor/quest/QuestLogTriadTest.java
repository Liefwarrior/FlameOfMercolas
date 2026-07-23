package com.trojia.sim.actor.quest;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.engine.SystemId;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The QuestLog persisted triad (Sprint 3): serialize/load/hashInto round-trip mid-stage
 * with a bound owner, non-zero cursors and an occupied talk latch; the entry-count and
 * stage-count frame guards throw on a raws mismatch (the {@code FactionStandings}
 * precedent); the degraded empty frame is byte-constant.
 */
final class QuestLogTriadTest {

    private static QuestRegistry fixtureRegistry() {
        return QuestRegistry.bind(QuestTestFixtures.parseFixture(),
                QuestTestFixtures.bindings(3, 7, 0, 4242, 5, 4));
    }

    private static QuestLog midStageLog(QuestRegistry registry) {
        QuestLog log = new QuestLog(registry);
        log.bindOwner(0, 3);
        log.advanceStage(0, 1, 100L);
        log.advanceStage(0, 2, 250L);
        log.noteSearchAttempt(0, 300L);
        log.noteSearchAttempt(0, 325L);
        log.noteTalk(3, 7, 326L);
        log.setCrimeCursor(12L);
        return log;
    }

    private static byte[] serialize(QuestLog log) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        log.serialize(new DataOutputStream(bytes));
        return bytes.toByteArray();
    }

    private static long hash(QuestLog log) {
        WorldHasher hasher = new WorldHasher();
        SystemId id = SystemId.of("questlog", "QLOG");
        log.hashInto(hasher.sectionSink(id));
        return hasher.sectionHash(id);
    }

    @Test
    void serializeLoadSerializeIsByteIdenticalAndHashStable() throws IOException {
        QuestRegistry registry = fixtureRegistry();
        QuestLog source = midStageLog(registry);
        byte[] first = serialize(source);

        QuestLog loaded = new QuestLog(registry);
        loaded.load(new DataInputStream(new ByteArrayInputStream(first)));
        assertArrayEquals(first, serialize(loaded), "serialize(load(serialize(x))) == serialize(x)");
        assertEquals(hash(source), hash(loaded), "hashInto mirrors serialize byte-for-byte");

        // Spot-check the loaded state (the reads the engine and client depend on).
        assertEquals(3, loaded.ownerOf(0));
        assertEquals(2, loaded.stageOf(0));
        assertEquals(250L, loaded.stageEnteredTickOf(0));
        assertEquals(325L, loaded.lastCheckTickOf(0));
        assertEquals(2L, loaded.searchAttemptsOf(0));
        assertEquals(100L, loaded.completedTickOf(0, 0));
        assertEquals(250L, loaded.completedTickOf(0, 1));
        assertEquals(-1L, loaded.completedTickOf(0, 2));
        assertEquals(2L, loaded.totalAdvances());
        assertEquals(12L, loaded.crimeCursor());
        assertEquals(3, loaded.latchTalkerId());
        assertEquals(7, loaded.latchTargetId());
        assertEquals(326L, loaded.latchTick());
    }

    @Test
    void theEntryCountFrameGuardThrowsOnAQuestUniverseMismatch() throws IOException {
        byte[] bytes = serialize(midStageLog(fixtureRegistry()));
        QuestLog empty = new QuestLog(QuestRegistry.EMPTY);
        IOException e = assertThrows(IOException.class,
                () -> empty.load(new DataInputStream(new ByteArrayInputStream(bytes))));
        assertTrue(e.getMessage().contains("questCount"), e.getMessage());
    }

    @Test
    void theStageCountFrameGuardThrowsOnAStageListMismatch() throws IOException {
        byte[] bytes = serialize(midStageLog(fixtureRegistry()));
        // Same quest COUNT (one), different stage count (two vs the fixture's four).
        QuestRaws twoStage = QuestRawsLoader.parse("""
                {
                  "id": "quests",
                  "quests": [
                    { "id": "test-quest", "title": "A Test Quest",
                      "binding": "first_talker",
                      "parties": ["alice"], "items": [], "zones": [], "cells": [],
                      "stages": [
                        { "key": "start", "objective": "o0", "log": "l0",
                          "advance": [ {"kind": "talk", "party": "alice", "to": "end"} ] },
                        { "key": "end", "objective": "o1", "log": "l1", "terminal": true }
                      ] }
                  ]
                }
                """.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        QuestRegistry mismatched = QuestRegistry.bind(twoStage,
                QuestTestFixtures.bindings(3, 7, 0, 4242, 5, 4));
        QuestLog target = new QuestLog(mismatched);
        IOException e = assertThrows(IOException.class,
                () -> target.load(new DataInputStream(new ByteArrayInputStream(bytes))));
        assertTrue(e.getMessage().contains("stageCount"), e.getMessage());
    }

    @Test
    void theDegradedEmptyFrameIsByteConstantAndNoteTalkNoOps() throws IOException {
        byte[] unwired = serialize(QuestLog.UNWIRED);
        QuestLog.UNWIRED.noteTalk(1, 2, 3L); // must no-op — UNWIRED is a shared instance
        assertArrayEquals(unwired, serialize(QuestLog.UNWIRED),
                "noteTalk on a zero-entry log writes nothing");
        assertEquals(Actor.NONE, QuestLog.UNWIRED.latchTalkerId());
        QuestLog empty = new QuestLog(QuestRegistry.EMPTY);
        assertArrayEquals(unwired, serialize(empty),
                "every zero-quest frame is the same constant bytes");
    }
}
