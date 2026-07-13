package com.trojia.sim.world.change;

import com.trojia.sim.engine.SystemId;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.Lanes;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Contract tests for {@link ChangeLogs}: append/pull ordering, duplicate
 * tolerance, reader-less lane skip, sealing, independent cursors, TICK_END
 * compaction with the one-commit reader-lag cap, cross-instance determinism
 * of identical operation sequences, and CHNG-section serialize/load carrying
 * the one-lap backlog + reader cursors across a save boundary.
 */
final class ChangeLogsTest {

    private static final LaneId MATERIAL = new LaneId(0, Lanes.MATERIAL, 2);
    private static final LaneId FORM = new LaneId(1, Lanes.FORM, 1);
    private static final LaneId FLUID = new LaneId(4, Lanes.FLUID, 2);

    @Test
    void entriesFlowToTheReaderInAppendOrderIncludingDuplicates() {
        ChangeLogs logs = new ChangeLogs();
        ChangeLogReader reader = logs.register(SystemId.of("thermal"), MATERIAL);
        logs.seal();

        logs.append(MATERIAL, 10);
        logs.append(MATERIAL, 20);
        logs.append(MATERIAL, 10); // duplicate-tolerant: appears twice
        logs.append(MATERIAL, 30);

        assertEquals(MATERIAL, reader.lane());
        assertEquals(List.of(10, 20, 10, 30), drain(reader));
        assertFalse(reader.hasNext());
    }

    @Test
    void readerLessLanesSkipAppends() {
        ChangeLogs logs = new ChangeLogs();
        logs.register(SystemId.of("thermal"), MATERIAL);
        logs.seal();

        assertTrue(logs.hasReaders(MATERIAL));
        assertFalse(logs.hasReaders(FORM));
        assertFalse(logs.hasReaders(FLUID));
        // Contract violation by the writer, but a silent skip, not a crash.
        logs.append(FORM, 42);
        logs.append(FLUID, 42);
        logs.compact(1);
    }

    @Test
    void registrationAfterSealFails() {
        ChangeLogs logs = new ChangeLogs();
        logs.register(SystemId.of("thermal"), MATERIAL);
        logs.seal();

        assertThrows(IllegalStateException.class,
                () -> logs.register(SystemId.of("fluids"), MATERIAL));
        assertThrows(IllegalStateException.class, logs::seal);
    }

    @Test
    void duplicateRegistrationOnOneLaneFails() {
        ChangeLogs logs = new ChangeLogs();
        logs.register(SystemId.of("light"), MATERIAL);

        assertThrows(IllegalArgumentException.class,
                () -> logs.register(SystemId.of("light"), MATERIAL));
        // Same system on a DIFFERENT lane is fine.
        logs.register(SystemId.of("light"), FORM);
    }

    @Test
    void multipleReadersHaveIndependentCursors() {
        ChangeLogs logs = new ChangeLogs();
        ChangeLogReader thermal = logs.register(SystemId.of("thermal"), MATERIAL);
        ChangeLogReader light = logs.register(SystemId.of("light"), MATERIAL);
        logs.seal();

        logs.append(MATERIAL, 7);
        logs.append(MATERIAL, 8);

        assertEquals(List.of(7, 8), drain(thermal));
        assertEquals(List.of(7, 8), drain(light)); // unaffected by thermal's drain
    }

    @Test
    void compactRetainsUnconsumedEntriesWithinTheLagCap() {
        ChangeLogs logs = new ChangeLogs();
        ChangeLogReader reader = logs.register(SystemId.of("thermal"), MATERIAL);
        logs.seal();

        logs.append(MATERIAL, 1);
        logs.append(MATERIAL, 2);
        logs.append(MATERIAL, 3);
        assertEquals(1, reader.next());
        assertEquals(2, reader.next());
        logs.compact(1); // entry 3 is from the current tick: within the cap

        assertTrue(reader.hasNext());
        assertEquals(3, reader.next());
        logs.append(MATERIAL, 4);
        assertEquals(4, reader.next());
        logs.compact(2); // fully drained: nothing retained
        assertFalse(reader.hasNext());
    }

    @Test
    void readerLaggingPastOneCommitFailsLoudlyAtCompact() {
        ChangeLogs logs = new ChangeLogs();
        logs.register(SystemId.of("sleeper"), MATERIAL);
        logs.seal();

        logs.append(MATERIAL, 99);
        logs.compact(1); // first commit after the append: still legal

        IllegalStateException failure =
                assertThrows(IllegalStateException.class, () -> logs.compact(2));
        assertTrue(failure.getMessage().contains("sleeper"));
        assertTrue(failure.getMessage().contains(Lanes.MATERIAL));
    }

    @Test
    void compactionShiftDoesNotDisturbCursorsOrOrder() {
        ChangeLogs logs = new ChangeLogs();
        ChangeLogReader reader = logs.register(SystemId.of("thermal"), MATERIAL);
        logs.seal();

        // Several laps of append/partial-drain/compact so the physical shift
        // and the virtual cursor positions diverge repeatedly.
        List<Integer> pulled = new ArrayList<>();
        int next = 0;
        for (long tick = 1; tick <= 50; tick++) {
            for (int i = 0; i < 5; i++) {
                logs.append(MATERIAL, next++);
            }
            for (int i = 0; i < 4; i++) { // always leave one entry lagging one tick
                pulled.add(reader.next());
            }
            logs.compact(tick);
            pulled.add(reader.next()); // consume the leftover next lap
        }

        List<Integer> expected = new ArrayList<>();
        for (int i = 0; i < 250; i++) {
            expected.add(i);
        }
        assertEquals(expected, pulled);
    }

    @Test
    void identicalOperationSequencesYieldIdenticalDrainSequences() {
        List<Integer> first = runScriptedSequence();
        List<Integer> second = runScriptedSequence();
        assertEquals(first, second);
    }

    /** A fixed multi-lane append/drain/compact script, drained to a journal. */
    private static List<Integer> runScriptedSequence() {
        ChangeLogs logs = new ChangeLogs();
        ChangeLogReader material = logs.register(SystemId.of("thermal"), MATERIAL);
        ChangeLogReader form = logs.register(SystemId.of("fluids"), FORM);
        logs.seal();

        List<Integer> journal = new ArrayList<>();
        for (long tick = 1; tick <= 20; tick++) {
            for (int i = 0; i < 7; i++) {
                int pos = (int) (tick * 31 + i * 17) & 0x3FFFFFFF;
                logs.append(MATERIAL, pos);
                if ((i & 1) == 0) {
                    logs.append(FORM, pos);
                }
            }
            journal.addAll(drain(material));
            journal.addAll(drain(form));
            logs.compact(tick);
        }
        return journal;
    }

    /**
     * The save/load equivalence hole of review F1-1: a backlog appended after
     * a reader's phase position must survive serialize/load — the loaded twin
     * must pull exactly the entries the original would have pulled at T+1,
     * and the restored lag watermark must still trip on a lagging reader.
     */
    @Test
    void serializeAndLoadCarryBacklogAndCursorsAcrossASaveBoundary() throws IOException {
        ChangeLogs original = new ChangeLogs();
        ChangeLogReader thermal = original.register(SystemId.of("thermal"), MATERIAL);
        ChangeLogReader light = original.register(SystemId.of("light"), MATERIAL);
        ChangeLogReader fluids = original.register(SystemId.of("fluids"), FORM);
        original.seal();

        // Tick 1: thermal consumes everything, light lags two entries, the
        // FORM lane keeps one undelivered entry.
        original.append(MATERIAL, 11);
        original.append(MATERIAL, 22);
        original.append(MATERIAL, 33);
        original.append(FORM, 44);
        assertEquals(List.of(11, 22, 33), drain(thermal));
        assertEquals(11, light.next());
        original.compact(1); // TICK_END of the save tick

        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        original.serialize(new DataOutputStream(bytes));

        // The loaded instance registers the same readers in the same order
        // (the boot registration list reproduces this deterministically).
        ChangeLogs loaded = new ChangeLogs();
        ChangeLogReader thermal2 = loaded.register(SystemId.of("thermal"), MATERIAL);
        ChangeLogReader light2 = loaded.register(SystemId.of("light"), MATERIAL);
        ChangeLogReader fluids2 = loaded.register(SystemId.of("fluids"), FORM);
        loaded.seal();
        loaded.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));

        assertFalse(thermal2.hasNext());
        assertEquals(List.of(22, 33), drain(light2));
        assertEquals(List.of(44), drain(fluids2));
        loaded.compact(2); // everyone consumed the carried backlog: legal

        // Twin check: the original behaves identically after its own compact.
        assertEquals(List.of(22, 33), drain(light));
        assertEquals(List.of(44), drain(fluids));
        original.compact(2);
    }

    /** The restored lag watermark still fails loudly on a lagging reader. */
    @Test
    void loadedBacklogStillEnforcesTheLagCap() throws IOException {
        ChangeLogs original = new ChangeLogs();
        original.register(SystemId.of("sleeper"), MATERIAL);
        original.seal();
        original.append(MATERIAL, 99);
        original.compact(1);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        original.serialize(new DataOutputStream(bytes));

        ChangeLogs loaded = new ChangeLogs();
        loaded.register(SystemId.of("sleeper"), MATERIAL);
        loaded.seal();
        loaded.load(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));

        IllegalStateException failure =
                assertThrows(IllegalStateException.class, () -> loaded.compact(2));
        assertTrue(failure.getMessage().contains("sleeper"));
    }

    /** A registration list that does not reproduce the saved shape is a hard fail. */
    @Test
    void loadRejectsAMismatchedRegistrationList() throws IOException {
        ChangeLogs original = new ChangeLogs();
        original.register(SystemId.of("thermal"), MATERIAL);
        original.seal();
        original.compact(1);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        original.serialize(new DataOutputStream(bytes));
        byte[] saved = bytes.toByteArray();

        ChangeLogs differentReader = new ChangeLogs();
        differentReader.register(SystemId.of("light"), MATERIAL);
        differentReader.seal();
        assertThrows(IOException.class, () -> differentReader.load(
                new DataInputStream(new ByteArrayInputStream(saved))));

        ChangeLogs differentLane = new ChangeLogs();
        differentLane.register(SystemId.of("thermal"), FORM);
        differentLane.seal();
        assertThrows(IOException.class, () -> differentLane.load(
                new DataInputStream(new ByteArrayInputStream(saved))));
    }

    private static List<Integer> drain(ChangeLogReader reader) {
        List<Integer> pulled = new ArrayList<>();
        while (reader.hasNext()) {
            pulled.add(reader.next());
        }
        return pulled;
    }
}
