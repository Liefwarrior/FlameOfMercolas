package com.trojia.sim.actor;

import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * A bounded ring buffer of recent skill level-ups (Sprint 1 "the character sheet comes
 * alive"): one row of {@code (tick, actorId, skillRaw, newLevel)} per
 * {@link com.trojia.sim.progression.SkillLevelledEvent}, overwriting the oldest row once
 * {@code capacity} is exceeded. This is the CLIENT-READABLE SEAM the progression wiring
 * promised: the observer's event log reads it to narrate growth ("Serf #212 is now
 * Streetwise 3") without any bus plumbing — the exact {@link ShoveLog} shape.
 *
 * <p><b>Determinism.</b> No behavior reads this log (presentation-only), but it lives next to
 * behavior-carrying state inside the {@code ActorsSystem} chunk and is therefore serialized,
 * loaded and hashed like {@link ShoveLog} — the simplest correct choice: a resumed run
 * narrates exactly what the continuous run would, and a log-only desync fails the twin-run
 * hash (landmine F) instead of slipping past. Primitive parallel arrays only; iteration is
 * oldest-to-newest by ring arithmetic.
 */
public final class SkillLevelLog {

    /** A capacity-0 log for unwired contexts: never records, always empty. */
    public static final SkillLevelLog EMPTY = new SkillLevelLog(0);

    private final long[] ticks;
    private final int[] actorIds;
    private final int[] skillRaws;
    private final int[] newLevels;
    /** Total rows ever recorded; {@code min(totalRecorded, capacity)} rows are live. */
    private long totalRecorded;

    public SkillLevelLog(int capacity) {
        this.ticks = new long[capacity];
        this.actorIds = new int[capacity];
        this.skillRaws = new int[capacity];
        this.newLevels = new int[capacity];
    }

    public int capacity() {
        return ticks.length;
    }

    /** Live row count ({@code <= capacity()}). */
    public int size() {
        return (int) Math.min(totalRecorded, ticks.length);
    }

    /** Total level-ups ever recorded (monotonic; survives overwrites). */
    public long totalRecorded() {
        return totalRecorded;
    }

    /** Records one level-up, overwriting the oldest row when full. No-op on a capacity-0 log. */
    public void record(long tick, int actorId, int skillRaw, int newLevel) {
        if (ticks.length == 0) {
            return;
        }
        int slot = (int) (totalRecorded % ticks.length);
        ticks[slot] = tick;
        actorIds[slot] = actorId;
        skillRaws[slot] = skillRaw;
        newLevels[slot] = newLevel;
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

    public int skillRawAt(int oldestFirstIndex) {
        return skillRaws[slotOf(oldestFirstIndex)];
    }

    public int newLevelAt(int oldestFirstIndex) {
        return newLevels[slotOf(oldestFirstIndex)];
    }

    /** Serializes the live rows oldest-first plus the monotonic total (canonical order). */
    public void serialize(DataOutput out) throws IOException {
        out.writeLong(totalRecorded);
        int size = size();
        out.writeInt(size);
        for (int i = 0; i < size; i++) {
            out.writeLong(tickAt(i));
            out.writeInt(actorIdAt(i));
            out.writeInt(skillRawAt(i));
            out.writeInt(newLevelAt(i));
        }
    }

    /** Loads what {@link #serialize} wrote into this (fresh, same-capacity) log. */
    public void load(DataInput in) throws IOException {
        long total = in.readLong();
        int size = in.readInt();
        // Pre-position the monotonic counter so each replayed row lands on the exact ring slot
        // it occupied at save time (the ShoveLog.load slot-arithmetic trick).
        totalRecorded = total - size;
        for (int i = 0; i < size; i++) {
            record(in.readLong(), in.readInt(), in.readInt(), in.readInt());
        }
    }

    /** Hashes the live rows oldest-first (landmine F: a log-only desync must fail the twin-run). */
    public void hashInto(WorldHasher.Sink sink) {
        sink.putLong(totalRecorded);
        int size = size();
        sink.putInt(size);
        for (int i = 0; i < size; i++) {
            sink.putLong(tickAt(i));
            sink.putInt(actorIdAt(i));
            sink.putInt(skillRawAt(i));
            sink.putInt(newLevelAt(i));
        }
    }
}
