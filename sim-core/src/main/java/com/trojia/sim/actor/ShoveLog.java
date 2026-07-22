package com.trojia.sim.actor;

import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * A bounded ring buffer of recent shoves (density revisit, the push mechanic): one row of
 * {@code (tick, contestedCell, pusherId)} per successful push, overwriting the oldest row once
 * {@code capacity} is exceeded. Guards' riot detection ({@link ApprehendPolicy}) reads it at
 * their sense cadence to find clusters of excessive shoving, so it is <b>behavior-carrying
 * state</b>, NOT transient telemetry — it is therefore serialized and hashed by
 * {@link ActorsSystem} (the simplest correct choice: a run resumed from a save mid-riot-window
 * detects exactly what the continuous run would, and a log-only desync fails the twin-run hash,
 * landmine F).
 *
 * <p><b>Determinism.</b> Primitive parallel arrays only (the sim-core purity contract — no
 * {@code java.util} collection state, no float/double); iteration is oldest-to-newest by ring
 * arithmetic, a pure function of insertion order.
 */
public final class ShoveLog {

    /** A capacity-0 log for unwired contexts: never records, always empty. */
    public static final ShoveLog EMPTY = new ShoveLog(0);

    private final long[] ticks;
    private final int[] cells;
    private final int[] pusherIds;
    /** Total rows ever recorded; {@code min(totalRecorded, capacity)} rows are live. */
    private long totalRecorded;

    public ShoveLog(int capacity) {
        this.ticks = new long[capacity];
        this.cells = new int[capacity];
        this.pusherIds = new int[capacity];
    }

    public int capacity() {
        return ticks.length;
    }

    /** Live row count ({@code <= capacity()}). */
    public int size() {
        return (int) Math.min(totalRecorded, ticks.length);
    }

    /** Total shoves ever recorded (monotonic; survives overwrites). */
    public long totalRecorded() {
        return totalRecorded;
    }

    /** Records one shove, overwriting the oldest row when full. No-op on a capacity-0 log. */
    public void record(long tick, int contestedCell, int pusherId) {
        if (ticks.length == 0) {
            return;
        }
        int slot = (int) (totalRecorded % ticks.length);
        ticks[slot] = tick;
        cells[slot] = contestedCell;
        pusherIds[slot] = pusherId;
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

    public int pusherIdAt(int oldestFirstIndex) {
        return pusherIds[slotOf(oldestFirstIndex)];
    }

    /** Serializes the live rows oldest-first plus the monotonic total (canonical order). */
    public void serialize(DataOutput out) throws IOException {
        out.writeLong(totalRecorded);
        int size = size();
        out.writeInt(size);
        for (int i = 0; i < size; i++) {
            out.writeLong(tickAt(i));
            out.writeInt(cellAt(i));
            out.writeInt(pusherIdAt(i));
        }
    }

    /** Loads what {@link #serialize} wrote into this (fresh, same-capacity) log. */
    public void load(DataInput in) throws IOException {
        long total = in.readLong();
        int size = in.readInt();
        // Pre-position the monotonic counter so each replayed row lands on the exact ring slot
        // it occupied at save time (slot = totalRecorded % capacity) — the replay then advances
        // it back to the saved total, keeping slot arithmetic byte-faithful across the round trip.
        totalRecorded = total - size;
        for (int i = 0; i < size; i++) {
            record(in.readLong(), in.readInt(), in.readInt());
        }
    }

    /** Hashes the live rows oldest-first (landmine F: a log-only desync must fail the twin-run). */
    public void hashInto(WorldHasher.Sink sink) {
        sink.putLong(totalRecorded);
        int size = size();
        sink.putInt(size);
        for (int i = 0; i < size; i++) {
            sink.putLong(tickAt(i));
            sink.putInt(cellAt(i));
            sink.putInt(pusherIdAt(i));
        }
    }
}
