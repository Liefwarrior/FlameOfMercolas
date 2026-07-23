package com.trojia.sim.actor;

import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * A bounded ring buffer of recent thefts (Sprint 2 "reactive streets", the pickpocket
 * mechanic): one row of {@code (tick, cell, thiefId, presentedId, victimId, flags)} per
 * pickpocket ATTEMPT — successes land unwitnessed (nobody saw the lift), failures land
 * WITNESSED (the mark caught the hand in its pocket) — overwriting the oldest row once
 * {@code capacity} is exceeded. Built on the {@link ShoveLog} template because it carries the
 * same determinism obligation: guards' theft sensing ({@link ApprehendPolicy}) reads the
 * witnessed rows at their sense cadence, so this is <b>behavior-carrying state</b>, serialized
 * and hashed by {@link ActorsSystem} (a run resumed from a save mid-crime-window corrects
 * exactly what the continuous run would; a log-only desync fails the twin-run hash,
 * landmine F).
 *
 * <p><b>Identity columns.</b> {@code thiefId} is the TRUE actor id — the physical body a guard
 * must chase and cuff. {@code presentedId} is who witnesses BELIEVE did it (the Persona rule:
 * standing deltas at correction time land on the presented identity) — the row carries both so
 * Sprint 2's stretch unmasking can move the stain without a schema change.
 *
 * <p><b>The served flag.</b> A correction marks every live witnessed row of its culprit SERVED
 * ({@link #markServed}) so one crime is corrected once: without it, a maimed Skyrunner would be
 * re-sensed off the same row next cadence and hanged for a single lift. Serving is a plain
 * flag write on live rows (deterministic — flag state rides the triad like everything else).
 *
 * <p><b>Determinism.</b> Primitive parallel arrays only (the sim-core purity contract);
 * iteration is oldest-to-newest by ring arithmetic, a pure function of insertion order.
 */
public final class CrimeLog {

    /** A capacity-0 log for unwired contexts: never records, always empty. */
    public static final CrimeLog EMPTY = new CrimeLog(0);

    /** Flag bit: the theft was witnessed (a failed attempt — the mark noticed). */
    static final byte FLAG_WITNESSED = 1;
    /** Flag bit: a guard already corrected the culprit for this row. */
    static final byte FLAG_SERVED = 1 << 1;

    private final long[] ticks;
    private final int[] cells;
    private final int[] thiefIds;
    private final int[] presentedIds;
    private final int[] victimIds;
    private final byte[] flags;
    /** Total rows ever recorded; {@code min(totalRecorded, capacity)} rows are live. */
    private long totalRecorded;

    public CrimeLog(int capacity) {
        this.ticks = new long[capacity];
        this.cells = new int[capacity];
        this.thiefIds = new int[capacity];
        this.presentedIds = new int[capacity];
        this.victimIds = new int[capacity];
        this.flags = new byte[capacity];
    }

    public int capacity() {
        return ticks.length;
    }

    /** Live row count ({@code <= capacity()}). */
    public int size() {
        return (int) Math.min(totalRecorded, ticks.length);
    }

    /** Total thefts ever recorded (monotonic; survives overwrites). */
    public long totalRecorded() {
        return totalRecorded;
    }

    /** Records one theft attempt, overwriting the oldest row when full. No-op on capacity 0. */
    public void record(long tick, int cell, int thiefId, int presentedId, int victimId,
            boolean witnessed) {
        if (ticks.length == 0) {
            return;
        }
        int slot = (int) (totalRecorded % ticks.length);
        ticks[slot] = tick;
        cells[slot] = cell;
        thiefIds[slot] = thiefId;
        presentedIds[slot] = presentedId;
        victimIds[slot] = victimId;
        flags[slot] = witnessed ? FLAG_WITNESSED : 0;
        totalRecorded++;
    }

    // Rows are addressed oldest-first: index 0 = the oldest live row, size()-1 = the newest.

    private int slotOf(int oldestFirstIndex) {
        long oldest = totalRecorded - size();
        return (int) ((oldest + oldestFirstIndex) % ticks.length);
    }

    public long tickAt(int oldestFirstIndex) {
        return ticks[slotOf(oldestFirstIndex)];
    }

    public int cellAt(int oldestFirstIndex) {
        return cells[slotOf(oldestFirstIndex)];
    }

    public int thiefIdAt(int oldestFirstIndex) {
        return thiefIds[slotOf(oldestFirstIndex)];
    }

    public int presentedIdAt(int oldestFirstIndex) {
        return presentedIds[slotOf(oldestFirstIndex)];
    }

    public int victimIdAt(int oldestFirstIndex) {
        return victimIds[slotOf(oldestFirstIndex)];
    }

    public boolean witnessedAt(int oldestFirstIndex) {
        return (flags[slotOf(oldestFirstIndex)] & FLAG_WITNESSED) != 0;
    }

    public boolean servedAt(int oldestFirstIndex) {
        return (flags[slotOf(oldestFirstIndex)] & FLAG_SERVED) != 0;
    }

    /**
     * Marks every live WITNESSED row of {@code thiefId} served — a guard's correction
     * consumes all of the culprit's outstanding word at once (one arrest answers the spree;
     * the sentence, not the row count, is the punishment).
     */
    public void markServed(int thiefId) {
        for (int i = 0; i < size(); i++) {
            int slot = slotOf(i);
            if (thiefIds[slot] == thiefId && (flags[slot] & FLAG_WITNESSED) != 0) {
                flags[slot] |= FLAG_SERVED;
            }
        }
    }

    /** Serializes the live rows oldest-first plus the monotonic total (canonical order). */
    public void serialize(DataOutput out) throws IOException {
        out.writeLong(totalRecorded);
        int size = size();
        out.writeInt(size);
        for (int i = 0; i < size; i++) {
            int slot = slotOf(i);
            out.writeLong(ticks[slot]);
            out.writeInt(cells[slot]);
            out.writeInt(thiefIds[slot]);
            out.writeInt(presentedIds[slot]);
            out.writeInt(victimIds[slot]);
            out.writeByte(flags[slot]);
        }
    }

    /** Loads what {@link #serialize} wrote into this (fresh, same-capacity) log. */
    public void load(DataInput in) throws IOException {
        long total = in.readLong();
        int size = in.readInt();
        // Pre-position the monotonic counter so each replayed row lands on the exact ring slot
        // it occupied at save time (the ShoveLog.load slot-arithmetic trick), then restore the
        // flags byte verbatim (record() writes only the witnessed bit — served must survive).
        totalRecorded = total - size;
        for (int i = 0; i < size; i++) {
            long tick = in.readLong();
            int cell = in.readInt();
            int thiefId = in.readInt();
            int presentedId = in.readInt();
            int victimId = in.readInt();
            byte flag = in.readByte();
            int slot = (int) (totalRecorded % ticks.length);
            record(tick, cell, thiefId, presentedId, victimId, false);
            flags[slot] = flag;
        }
    }

    /** Hashes the live rows oldest-first (landmine F: a log-only desync must fail the twin-run). */
    public void hashInto(WorldHasher.Sink sink) {
        sink.putLong(totalRecorded);
        int size = size();
        sink.putInt(size);
        for (int i = 0; i < size; i++) {
            int slot = slotOf(i);
            sink.putLong(ticks[slot]);
            sink.putInt(cells[slot]);
            sink.putInt(thiefIds[slot]);
            sink.putInt(presentedIds[slot]);
            sink.putInt(victimIds[slot]);
            sink.putByte(flags[slot]);
        }
    }
}
