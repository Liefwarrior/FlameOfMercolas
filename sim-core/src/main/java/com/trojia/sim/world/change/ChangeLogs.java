package com.trojia.sim.world.change;

import com.trojia.sim.engine.SystemId;
import com.trojia.sim.world.LaneId;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.Arrays;

/**
 * Per-lane packed-int change logs: the high-volume half of the two-channel
 * plumbing rule (ARCHITECTURE.md §6) — field deltas travel here, semantic
 * facts travel as SimEvents. Appends are allocation-free packed
 * {@code PackedPos} ints, duplicate-tolerant (consumers dedupe via
 * {@link ActiveSet}).
 *
 * <p>Reader registration is sealed before the first tick; lanes with no
 * readers skip appends entirely ({@link #hasReaders}), which the ChunkWriter
 * checks on every write. Logs are compacted at TICK_END; a reader lagging past
 * the cap is asserted at commit (fail loudly, never wrap silently).
 *
 * <p><b>Lag cap:</b> an entry appended during tick T is consumable during T
 * and T+1 — mirroring the one-lap event retirement rule. Concretely, at
 * {@link #compact} every reader must have consumed every entry that already
 * existed at the <em>previous</em> compact; a reader lagging further throws
 * {@code IllegalStateException} in all builds (not an {@code assert} — the
 * failure must be identical with and without {@code -ea}).
 *
 * <p><b>Persistence:</b> the one-lap backlog is sim state — {@link #serialize}
 * and {@link #load} carry the retained entry tails and reader cursors through
 * the engine's {@code CHNG} save section so the §6 save/load equivalence holds
 * for readers positioned before late-phase writers.
 *
 * <p><b>Allocation discipline:</b> {@link #append} and reader pulls allocate
 * nothing in steady state; the only allocations are amortized log-array
 * doubling (capacity is a retained high-water mark, never shrunk) and
 * cold-path registration. {@link #compact} shifts entries in place with a
 * single {@code arraycopy} and allocates nothing.
 */
public final class ChangeLogs {

    private static final int INITIAL_LOG_CAPACITY = 256;

    /** Indexed by {@link LaneId#index()}; {@code null} = lane has no readers. */
    private LaneLog[] logs = new LaneLog[0];
    private boolean sealed;

    /** Creates the log set for one world; one instance per world, engine-wired. */
    public ChangeLogs() {
    }

    /**
     * Registers {@code reader} as a consumer of {@code lane}'s changes and
     * returns its private cursor. Legal only before {@link #seal()}; the
     * subscription set is part of the determinism contract. Throws
     * {@code IllegalStateException} once sealed and
     * {@code IllegalArgumentException} if {@code reader} is already registered
     * on {@code lane} (a duplicate cursor would double-consume).
     */
    public ChangeLogReader register(SystemId reader, LaneId lane) {
        if (reader == null || lane == null) {
            throw new IllegalArgumentException("reader and lane must be non-null");
        }
        if (sealed) {
            throw new IllegalStateException(
                    "change-log registration is sealed; cannot register '"
                            + reader.name() + "' on lane '" + lane.name() + "'");
        }
        int index = lane.index();
        if (index >= logs.length) {
            logs = Arrays.copyOf(logs, index + 1);
        }
        LaneLog log = logs[index];
        if (log == null) {
            log = new LaneLog(lane);
            logs[index] = log;
        }
        return log.addReader(reader);
    }

    /** Forbids further registration; called once by the engine before tick 1. */
    public void seal() {
        if (sealed) {
            throw new IllegalStateException("change logs already sealed");
        }
        sealed = true;
    }

    /** Whether {@code lane} has at least one registered reader (writer-side skip check). */
    public boolean hasReaders(LaneId lane) {
        int index = lane.index();
        return index < logs.length && logs[index] != null;
    }

    /**
     * Appends a changed cell to {@code lane}'s log. ChunkWriter-only; no-op
     * contract violation if the lane has no readers (callers must check
     * {@link #hasReaders} first).
     */
    public void append(LaneId lane, int packedPos) {
        int index = lane.index();
        if (index >= logs.length) {
            return;
        }
        LaneLog log = logs[index];
        if (log == null) {
            return;
        }
        log.append(packedPos);
    }

    /**
     * TICK_END compaction: drops entries every reader has consumed and asserts
     * the reader-lag cap (see class doc). Engine-only. Lanes are compacted in
     * ascending lane index — the canonical lane order.
     */
    public void compact(long tick) {
        for (int i = 0; i < logs.length; i++) {
            LaneLog log = logs[i];
            if (log != null) {
                log.compact(tick);
            }
        }
    }

    /**
     * Serializes the change-log carry-over (the engine's {@code CHNG} save
     * section): per lane in ascending lane index, the retained (unconsumed)
     * entry tail plus every registered reader's identity and position within
     * it. Pure. Legal only at a TICK_END boundary (after {@link #compact}),
     * where the retained tail is exactly the one-lap backlog the §6 save/load
     * contract ({@code run K+N ≡ save@K, load, run N}) must preserve —
     * without it, readers positioned before late-phase writers would silently
     * miss their tick-K wake-ups after a load.
     */
    public void serialize(DataOutput out) throws IOException {
        int laneLogCount = 0;
        for (LaneLog log : logs) {
            if (log != null) {
                laneLogCount++;
            }
        }
        out.writeInt(laneLogCount);
        for (int i = 0; i < logs.length; i++) {
            LaneLog log = logs[i];
            if (log == null) {
                continue;
            }
            out.writeInt(i);
            out.writeInt(log.size);
            for (int e = 0; e < log.size; e++) {
                out.writeInt(log.entries[e]);
            }
            out.writeInt(log.readerCount);
            for (int r = 0; r < log.readerCount; r++) {
                Cursor cursor = log.readers[r];
                out.writeUTF(cursor.owner.name());
                out.writeLong(cursor.owner.salt());
                out.writeInt((int) (cursor.pos - log.base));
            }
        }
    }

    /**
     * Restores the state written by {@link #serialize} into this instance's
     * <em>already registered</em> logs: the boot registration list must
     * reproduce the saved lane/reader structure exactly (same lanes, same
     * readers per lane in the same registration order) — any mismatch is a
     * hard {@code IOException}, never a silent partial restore. Replaces every
     * retained entry tail and reader cursor position.
     */
    public void load(DataInput in) throws IOException {
        int registered = 0;
        for (LaneLog log : logs) {
            if (log != null) {
                registered++;
            }
        }
        int laneLogCount = in.readInt();
        if (laneLogCount != registered) {
            throw new IOException("change-log mismatch: save carries " + laneLogCount
                    + " lane logs, registration has " + registered);
        }
        for (int k = 0; k < laneLogCount; k++) {
            int laneIndex = in.readInt();
            LaneLog log = laneIndex >= 0 && laneIndex < logs.length ? logs[laneIndex] : null;
            if (log == null) {
                throw new IOException("change-log mismatch: save carries lane index "
                        + laneIndex + " but no reader is registered on it");
            }
            int entryCount = in.readInt();
            if (entryCount < 0) {
                throw new IOException("corrupt change-log section: lane '" + log.lane.name()
                        + "' carries negative entry count " + entryCount);
            }
            if (entryCount > log.entries.length) {
                log.entries = new int[entryCount];
            }
            for (int e = 0; e < entryCount; e++) {
                log.entries[e] = in.readInt();
            }
            log.size = entryCount;
            log.base = 0;
            log.lagWatermark = entryCount;
            int readerCount = in.readInt();
            if (readerCount != log.readerCount) {
                throw new IOException("change-log mismatch on lane '" + log.lane.name()
                        + "': save has " + readerCount + " readers, registration has "
                        + log.readerCount);
            }
            for (int r = 0; r < readerCount; r++) {
                String name = in.readUTF();
                long salt = in.readLong();
                Cursor cursor = log.readers[r];
                if (!cursor.owner.name().equals(name) || cursor.owner.salt() != salt) {
                    throw new IOException("change-log mismatch on lane '" + log.lane.name()
                            + "' reader " + r + ": save has '" + name
                            + "', registration has '" + cursor.owner.name()
                            + "' (boot registration must reproduce the saved order)");
                }
                int pos = in.readInt();
                if (pos < 0 || pos > entryCount) {
                    throw new IOException("corrupt change-log section: lane '"
                            + log.lane.name() + "' reader '" + name + "' position " + pos
                            + " outside retained tail [0, " + entryCount + "]");
                }
                cursor.pos = pos;
            }
        }
    }

    /**
     * One lane's log: a growable int array addressed by monotonically
     * increasing <em>virtual</em> positions so cursor state survives the
     * in-place compaction shift ({@code physical = virtual - base}).
     */
    private static final class LaneLog {

        private final LaneId lane;
        private int[] entries = new int[INITIAL_LOG_CAPACITY];
        /** Number of physically retained entries. */
        private int size;
        /** Virtual position of {@code entries[0]}. */
        private long base;
        /** Total appended as of the previous compact — the lag-cap line. */
        private long lagWatermark;
        private Cursor[] readers = new Cursor[2];
        private int readerCount;

        LaneLog(LaneId lane) {
            this.lane = lane;
        }

        Cursor addReader(SystemId reader) {
            for (int i = 0; i < readerCount; i++) {
                if (readers[i].owner.equals(reader)) {
                    throw new IllegalArgumentException("system '" + reader.name()
                            + "' is already registered on lane '" + lane.name() + "'");
                }
            }
            if (readerCount == readers.length) {
                readers = Arrays.copyOf(readers, readerCount * 2);
            }
            Cursor cursor = new Cursor(reader, this);
            readers[readerCount++] = cursor;
            return cursor;
        }

        void append(int packedPos) {
            if (size == entries.length) {
                entries = Arrays.copyOf(entries, size * 2);
            }
            entries[size++] = packedPos;
        }

        long totalAppended() {
            return base + size;
        }

        int entryAt(long virtualPos) {
            return entries[(int) (virtualPos - base)];
        }

        void compact(long tick) {
            long minPos = Long.MAX_VALUE;
            for (int i = 0; i < readerCount; i++) {
                minPos = Math.min(minPos, readers[i].pos);
            }
            if (minPos < lagWatermark) {
                throw new IllegalStateException("change-log reader '"
                        + laggard(minPos).owner.name() + "' on lane '" + lane.name()
                        + "' lagged past the compaction cap at tick " + tick
                        + " (position " + minPos + " < watermark " + lagWatermark + ")");
            }
            int drop = (int) (minPos - base);
            if (drop > 0) {
                System.arraycopy(entries, drop, entries, 0, size - drop);
                size -= drop;
                base = minPos;
            }
            lagWatermark = base + size;
        }

        /** The first registered reader at the lagging position (for the failure message). */
        private Cursor laggard(long minPos) {
            for (int i = 0; i < readerCount; i++) {
                if (readers[i].pos == minPos) {
                    return readers[i];
                }
            }
            throw new AssertionError("no reader at minimum position " + minPos);
        }
    }

    /**
     * The private per-reader cursor: a single virtual position into its lane's
     * log. Pulls are two array reads and an increment — no bounds check beyond
     * the caller's {@link #hasNext()} obligation.
     */
    private static final class Cursor implements ChangeLogReader {

        private final SystemId owner;
        private final LaneLog log;
        private long pos;

        Cursor(SystemId owner, LaneLog log) {
            this.owner = owner;
            this.log = log;
        }

        @Override
        public LaneId lane() {
            return log.lane;
        }

        @Override
        public boolean hasNext() {
            return pos < log.totalAppended();
        }

        @Override
        public int next() {
            return log.entryAt(pos++);
        }
    }
}
