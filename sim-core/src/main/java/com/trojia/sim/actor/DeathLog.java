package com.trojia.sim.actor;

import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * A bounded ring buffer of deaths (Sprint 6, Eli's bug 7): one row of {@code (tick, actorId,
 * causeOrdinal)} per death — execution or terminal starvation — overwriting the oldest once
 * {@code capacity} is exceeded. The client feed announces each row BY NAME; the monotonic
 * {@link #totalRecorded()} is the district's death toll. Serialized and hashed with the
 * {@link ShoveLog} discipline (the state is tiny and a log-only divergence should fail the
 * twin run rather than slip), though no sim behavior reads it back — death itself lives on
 * {@link StatusBit#DEAD}.
 *
 * <p>Primitive parallel arrays only (the sim-core purity contract); iteration oldest-first
 * by ring arithmetic.
 */
public final class DeathLog {

    /** A capacity-0 log for unwired contexts: never records, always empty. */
    public static final DeathLog EMPTY = new DeathLog(0);

    private final long[] ticks;
    private final int[] actorIds;
    private final byte[] causes;
    private long totalRecorded;

    public DeathLog(int capacity) {
        this.ticks = new long[capacity];
        this.actorIds = new int[capacity];
        this.causes = new byte[capacity];
    }

    public int capacity() {
        return ticks.length;
    }

    /** Live row count ({@code <= capacity()}). */
    public int size() {
        return (int) Math.min(totalRecorded, ticks.length);
    }

    /** Total deaths ever recorded (monotonic — the district's death toll). */
    public long totalRecorded() {
        return totalRecorded;
    }

    /** Records one death, overwriting the oldest row when full. No-op on a capacity-0 log. */
    public void record(long tick, int actorId, ReasonCode cause) {
        if (ticks.length == 0) {
            return;
        }
        int slot = (int) (totalRecorded % ticks.length);
        ticks[slot] = tick;
        actorIds[slot] = actorId;
        causes[slot] = (byte) cause.ordinal();
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

    public int actorIdAt(int oldestFirstIndex) {
        return actorIds[slotOf(oldestFirstIndex)];
    }

    public ReasonCode causeAt(int oldestFirstIndex) {
        return ReasonCode.values()[causes[slotOf(oldestFirstIndex)]];
    }

    /** Serializes the live rows oldest-first plus the monotonic total (canonical order). */
    public void serialize(DataOutput out) throws IOException {
        out.writeLong(totalRecorded);
        int size = size();
        out.writeInt(size);
        for (int i = 0; i < size; i++) {
            out.writeLong(tickAt(i));
            out.writeInt(actorIdAt(i));
            out.writeByte(causes[slotOf(i)]);
        }
    }

    /** Loads what {@link #serialize} wrote into this (fresh, same-capacity) log. */
    public void load(DataInput in) throws IOException {
        long total = in.readLong();
        int size = in.readInt();
        totalRecorded = total - size; // pre-position so replayed rows land on their exact slots
        for (int i = 0; i < size; i++) {
            long tick = in.readLong();
            int actorId = in.readInt();
            byte cause = in.readByte();
            if (ticks.length > 0) {
                int slot = (int) (totalRecorded % ticks.length);
                ticks[slot] = tick;
                actorIds[slot] = actorId;
                causes[slot] = cause;
            }
            totalRecorded++;
        }
    }

    /** Hashes the live rows oldest-first (landmine F). */
    public void hashInto(WorldHasher.Sink sink) {
        sink.putLong(totalRecorded);
        int size = size();
        sink.putInt(size);
        for (int i = 0; i < size; i++) {
            sink.putLong(tickAt(i));
            sink.putInt(actorIdAt(i));
            sink.putByte(causes[slotOf(i)]);
        }
    }
}
