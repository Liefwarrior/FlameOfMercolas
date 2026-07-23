package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRegistry;

import java.util.function.IntSupplier;

/**
 * Consumes the Sim team's {@link QuestLog} seam once per executed tick (Sprint 3 "The
 * Vanished Clerk"): every stage advance becomes one {@code "* <log line>"} feed entry (the
 * completed stage's authored journal prose — plus the ending's own line when a terminal
 * stage is entered), the OWNER's advances additionally toast ({@code "* Journal updated -
 * The Vanished Clerk"} / {@code "* Quest complete - ..."} on an ending), and a search
 * attempt that left the stage unmoved toasts the {@link CheckLineFormatter#searchLine}
 * failure line. GL-free; wired as an after-tick callback beside {@link EventLogTracker},
 * {@link SkillUpTracker} and {@link CrimeFeedTracker} on the same {@code SimulationDriver}
 * seam.
 *
 * <p><b>Read-only diffs.</b> Per entry, the tracker diffs {@link QuestLog#stageOf} (the
 * engine advances at most once per entry per tick, so one stage flip is one advance) and
 * {@link QuestLog#searchAttemptsOf} (monotonic — the failure-toast cursor the design
 * names). Whatever the log holds at construction is history, not news (the
 * {@link CrimeFeedTracker} baseline convention). It never mutates sim state.
 *
 * <p><b>Toasts are the owner's.</b> Feed lines narrate for the whole ward (the population
 * feed convention), but toasts fire only while the entry's owner IS the played body — the
 * {@link SkillUpTracker} gate, so driving someone else never rains another soul's quest
 * feedback on the player.
 */
public final class QuestFeedTracker {

    /** The quest-lane feed/toast marker (ASCII — the {@link TalkText#QUEST_MARK} ruling). */
    public static final String MARK = TalkText.QUEST_MARK + " ";

    private final QuestRegistry quests;
    private final QuestLog log;
    private final SkillTrackRegistry tracks;
    private final EventLog eventLog;
    private final ToastQueue toasts;
    /** Live "who is played this tick" read — {@code Actor.NONE} when nobody is. */
    private final IntSupplier playedActorId;

    private final int[] prevStage;
    private final long[] prevAttempts;

    public QuestFeedTracker(QuestRegistry quests, QuestLog log, SkillTrackRegistry tracks,
            EventLog eventLog, ToastQueue toasts, IntSupplier playedActorId) {
        this.quests = quests;
        this.log = log;
        this.tracks = tracks;
        this.eventLog = eventLog;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        // Baseline: whatever the log already holds is history, not this session's news.
        this.prevStage = new int[log.entryCount()];
        this.prevAttempts = new long[log.entryCount()];
        for (int e = 0; e < log.entryCount(); e++) {
            prevStage[e] = log.stageOf(e);
            prevAttempts[e] = log.searchAttemptsOf(e);
        }
    }

    /**
     * Narrates every advance/failed search since the last call. Call exactly once per
     * executed tick (the {@code SimulationDriver.setAfterTick} seam); a no-op on quest-less
     * fixtures (zero entries).
     */
    public void afterTick(long tick) {
        for (int e = 0; e < log.entryCount(); e++) {
            int q = log.questOrdinalOf(e);
            int stage = log.stageOf(e);
            int owner = log.ownerOf(e);
            boolean owned = owner != Actor.NONE && owner == playedActorId.getAsInt();
            if (stage != prevStage[e]) {
                // One advance (the engine's one-per-entry-per-tick rule): the feed tells
                // what just happened — the COMPLETED stage's authored line, and, on an
                // ending, the terminal stage's own closing line.
                eventLog.add(tick, MARK + quests.logLine(q, prevStage[e]));
                boolean done = quests.terminal(q, stage);
                if (done) {
                    eventLog.add(tick, MARK + quests.logLine(q, stage));
                }
                if (owned) {
                    toasts.add(MARK + (done ? "Quest complete - " : "Journal updated - ")
                            + quests.questTitle(q));
                }
                prevStage[e] = stage;
            } else if (log.searchAttemptsOf(e) != prevAttempts[e] && owned) {
                // An attempt that moved nothing is a failed pry: the visible-dice line.
                toasts.add(CheckLineFormatter.searchLine(tracks, owner,
                        quests.searchSkillRaw(q, stage), quests.searchResist(q, stage)));
            }
            prevAttempts[e] = log.searchAttemptsOf(e);
        }
    }
}
