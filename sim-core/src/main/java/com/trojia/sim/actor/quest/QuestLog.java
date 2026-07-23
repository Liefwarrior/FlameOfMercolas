package com.trojia.sim.actor.quest;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * The persisted quest state (Sprint 3 "The Vanished Clerk"): one dense entry per baked
 * (quest, owner) pair — TVC bakes exactly one entry per quest with the owner unbound until
 * {@code first_talker} binds it. Player-scoped now, NPC-ready by shape (owner-per-entry).
 * Everything here is behavior-carrying state (the engine reads it every tick), so the whole
 * table is the persisted triad (serialize/load/hashInto) appended LAST inside the
 * {@code ActorsSystem} chunk, with entry-count and per-entry stage-count frame guards (the
 * {@code FactionStandings} precedent — the same quest raws must boot before a load).
 *
 * <p><b>The talk latch.</b> One log-level slot {@code (talkerId, targetId, tick)} written by
 * {@code PlayerControlPolicy} when a play-mode talk intent validates, and matched by the
 * engine's TALK triggers the SAME tick ({@code latch.tick == now} — stale latches are inert
 * by tick mismatch, so no clearing is needed). Serialized and hashed with everything else,
 * so a save mid-tick and the twin-run identity both stay exact.
 *
 * <p>{@link #UNWIRED} is the {@code ActorContext} default: {@link #noteTalk} no-ops (as it
 * does on any zero-entry log) and the triad writes a constant empty frame.
 */
public final class QuestLog {

    /** The degraded no-op instance (world-less bootstrap, test doubles, old constructors). */
    public static final QuestLog UNWIRED = new QuestLog(QuestRegistry.EMPTY);

    /** "Never checked" sentinel for {@link #lastCheckTick} — far enough back that the first
     * search attempt is never cooldown-blocked, without underflowing tick arithmetic. */
    static final long NEVER = Long.MIN_VALUE / 4;

    private final QuestRegistry quests;

    // ---- per-entry parallel arrays (dense; entry order == quest ordinal order) ----
    private final int[] questOrdinal;
    private final int[] ownerActorId;      // NONE until bound; the TRUE id drives the quest
    private final int[] stageOrdinal;
    private final long[] stageEnteredTick;
    private final long[] lastCheckTick;    // search-retry cooldown clock
    private final long[] searchAttempts;   // monotonic (the client failure-toast cursor)
    private final long[][] completedTick;  // [entry][stage], -1 sentinel; stamped when LEFT

    // ---- log-level ----
    private int latchTalkerId = Actor.NONE;
    private int latchTargetId = Actor.NONE;
    private long latchTick = -1;
    /** Monotonic advance counter — the client quest-feed cursor. */
    private long totalAdvances;
    /** The consumed {@code CrimeLog.totalRecorded()} position of the key-lift watcher. */
    private long crimeCursor;

    /** Builds the log for {@code quests}: one entry per quest, owner unbound, stage 0. */
    public QuestLog(QuestRegistry quests) {
        this.quests = quests;
        int n = quests.questCount();
        this.questOrdinal = new int[n];
        this.ownerActorId = new int[n];
        this.stageOrdinal = new int[n];
        this.stageEnteredTick = new long[n];
        this.lastCheckTick = new long[n];
        this.searchAttempts = new long[n];
        this.completedTick = new long[n][];
        for (int e = 0; e < n; e++) {
            questOrdinal[e] = e;
            ownerActorId[e] = Actor.NONE;
            lastCheckTick[e] = NEVER;
            completedTick[e] = new long[quests.stageCount(e)];
            java.util.Arrays.fill(completedTick[e], -1L);
        }
    }

    /** The wired registry this log's ordinals index into. */
    public QuestRegistry quests() {
        return quests;
    }

    // ---------------------------------------------------------------- the talk latch

    /**
     * Notes that {@code talkerId} talked to adjacent {@code targetId} at {@code tick} —
     * called by {@code PlayerControlPolicy} after validating reach. One slot: a second talk
     * the same tick overwrites (ascending-id actor order makes the winner deterministic;
     * with one played actor it never happens). No-op on a zero-entry log ({@link #UNWIRED}
     * and every pre-quest system), so degraded frames stay byte-constant.
     */
    public void noteTalk(int talkerId, int targetId, long tick) {
        if (questOrdinal.length == 0) {
            return;
        }
        latchTalkerId = talkerId;
        latchTargetId = targetId;
        latchTick = tick;
    }

    // ---------------------------------------------------------------- reads

    public int entryCount() {
        return questOrdinal.length;
    }

    public int questOrdinalOf(int entry) {
        return questOrdinal[entry];
    }

    /** The bound owner's TRUE actor id, or {@link Actor#NONE} while unbound. */
    public int ownerOf(int entry) {
        return ownerActorId[entry];
    }

    public int stageOf(int entry) {
        return stageOrdinal[entry];
    }

    public long stageEnteredTickOf(int entry) {
        return stageEnteredTick[entry];
    }

    public long lastCheckTickOf(int entry) {
        return lastCheckTick[entry];
    }

    /** Monotonic search-attempt count (the client's failure-toast cursor). */
    public long searchAttemptsOf(int entry) {
        return searchAttempts[entry];
    }

    /** The tick stage {@code stage} was LEFT (completed), or -1 if never. */
    public long completedTickOf(int entry, int stage) {
        return completedTick[entry][stage];
    }

    /** Monotonic advance counter (the client quest-feed cursor). */
    public long totalAdvances() {
        return totalAdvances;
    }

    long crimeCursor() {
        return crimeCursor;
    }

    int latchTalkerId() {
        return latchTalkerId;
    }

    int latchTargetId() {
        return latchTargetId;
    }

    long latchTick() {
        return latchTick;
    }

    // ---------------------------------------------------------------- engine mutators

    /**
     * Binds the owner at BAKE time (the {@code fixed} binding mode — NPC-scoped shape).
     * Public for the scenario bake; {@code first_talker} quests never call it.
     */
    public void bindOwnerAtBake(int entry, int trueActorId) {
        ownerActorId[entry] = trueActorId;
    }

    void bindOwner(int entry, int trueActorId) {
        ownerActorId[entry] = trueActorId;
    }

    void advanceStage(int entry, int toStage, long tick) {
        completedTick[entry][stageOrdinal[entry]] = tick;
        stageOrdinal[entry] = toStage;
        stageEnteredTick[entry] = tick;
        lastCheckTick[entry] = NEVER; // a fresh stage's search is never pre-cooled
        totalAdvances++;
    }

    void noteSearchAttempt(int entry, long tick) {
        lastCheckTick[entry] = tick;
        searchAttempts[entry]++;
    }

    void setCrimeCursor(long cursor) {
        crimeCursor = cursor;
    }

    // ======================================================================
    // The persisted triad (appended LAST in the ActorsSystem chunk)
    // ======================================================================

    /** Serializes the entry-count frame guard, every entry, the latch, then the cursors. */
    public void serialize(DataOutput out) throws IOException {
        out.writeInt(questOrdinal.length);
        for (int e = 0; e < questOrdinal.length; e++) {
            out.writeInt(questOrdinal[e]);
            out.writeInt(ownerActorId[e]);
            out.writeInt(stageOrdinal[e]);
            out.writeLong(stageEnteredTick[e]);
            out.writeLong(lastCheckTick[e]);
            out.writeLong(searchAttempts[e]);
            out.writeInt(completedTick[e].length); // inner stage-count frame guard
            for (long tick : completedTick[e]) {
                out.writeLong(tick);
            }
        }
        out.writeInt(latchTalkerId);
        out.writeInt(latchTargetId);
        out.writeLong(latchTick);
        out.writeLong(totalAdvances);
        out.writeLong(crimeCursor);
    }

    /**
     * Loads what {@link #serialize} wrote into this fresh log. The wired quest count and
     * every stage count must match the serialized frame — loading quest state against
     * different quest raws is a config error and fails loudly here rather than desyncing
     * downstream (the {@code FactionStandings} precedent).
     */
    public void load(DataInput in) throws IOException {
        int count = in.readInt();
        if (count != questOrdinal.length) {
            throw new IOException("quest-log frame mismatch: serialized questCount=" + count
                    + " but the loading system wires " + questOrdinal.length
                    + " (same quest raws must be booted before load)");
        }
        for (int e = 0; e < count; e++) {
            questOrdinal[e] = in.readInt();
            ownerActorId[e] = in.readInt();
            stageOrdinal[e] = in.readInt();
            stageEnteredTick[e] = in.readLong();
            lastCheckTick[e] = in.readLong();
            searchAttempts[e] = in.readLong();
            int stageCount = in.readInt();
            if (stageCount != completedTick[e].length) {
                throw new IOException("quest-log frame mismatch: entry " + e
                        + " serialized stageCount=" + stageCount + " but the loading system"
                        + " wires " + completedTick[e].length
                        + " (same quest raws must be booted before load)");
            }
            for (int s = 0; s < stageCount; s++) {
                completedTick[e][s] = in.readLong();
            }
        }
        latchTalkerId = in.readInt();
        latchTargetId = in.readInt();
        latchTick = in.readLong();
        totalAdvances = in.readLong();
        crimeCursor = in.readLong();
    }

    /** Hashes the exact state {@link #serialize} writes, in the same canonical order. */
    public void hashInto(WorldHasher.Sink sink) {
        sink.putInt(questOrdinal.length);
        for (int e = 0; e < questOrdinal.length; e++) {
            sink.putInt(questOrdinal[e]);
            sink.putInt(ownerActorId[e]);
            sink.putInt(stageOrdinal[e]);
            sink.putLong(stageEnteredTick[e]);
            sink.putLong(lastCheckTick[e]);
            sink.putLong(searchAttempts[e]);
            sink.putInt(completedTick[e].length);
            for (long tick : completedTick[e]) {
                sink.putLong(tick);
            }
        }
        sink.putInt(latchTalkerId);
        sink.putInt(latchTargetId);
        sink.putLong(latchTick);
        sink.putLong(totalAdvances);
        sink.putLong(crimeCursor);
    }
}
