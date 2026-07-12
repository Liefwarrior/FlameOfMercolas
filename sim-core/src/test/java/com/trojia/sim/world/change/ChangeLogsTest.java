package com.trojia.sim.world.change;

import com.trojia.sim.engine.SystemId;
import com.trojia.sim.world.LaneId;
import com.trojia.sim.world.Lanes;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Contract tests for {@link ChangeLogs}: append/pull ordering, duplicate
 * tolerance, reader-less lane skip, sealing, independent cursors, TICK_END
 * compaction with the one-commit reader-lag cap, and cross-instance
 * determinism of identical operation sequences.
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

    private static List<Integer> drain(ChangeLogReader reader) {
        List<Integer> pulled = new ArrayList<>();
        while (reader.hasNext()) {
            pulled.add(reader.next());
        }
        return pulled;
    }
}
