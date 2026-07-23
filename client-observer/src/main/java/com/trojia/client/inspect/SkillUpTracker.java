package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.SkillLevelLog;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.function.IntSupplier;

/**
 * Consumes the Sim team's {@link SkillLevelLog} seam once per executed tick (Sprint 1
 * item 3): the PLAYED actor's level-ups become bottom-center toasts ("Skyrunning increased
 * to 32"); everyone else's land in the population event feed as people ("Ditta Pilchard is
 * now Streetwise 3"). GL-free; wired as an after-tick callback beside
 * {@link EventLogTracker} on the same {@code SimulationDriver} seam, so no tick's
 * level-ups are missed on a FAST frame nor double-seen on a re-rendered one.
 *
 * <p><b>Read-only cursor.</b> The tracker keeps one monotonic cursor over
 * {@link SkillLevelLog#totalRecorded()} and reads only the rows recorded since its last
 * call — it never mutates sim state. Rows already in the ring at construction are treated
 * as history, not news (the {@code EventLogTracker} spawn-baseline convention). If a burst
 * ever outruns the ring capacity between reads, the overwritten rows are simply lost to
 * narration (the ring keeps the newest — presentation-only, so nothing else can care).
 *
 * <p><b>Multi-level carry.</b> One award can cross several thresholds in a single tick
 * ({@code SkillTrack#awardXp} loops); each emits its own log row, so each gets its own
 * toast/feed line, in level order.
 */
public final class SkillUpTracker {

    private final SkillTrackRegistry tracks;
    private final ActorRegistry registry;
    private final IdentityRegistry identity;
    private final EventLog eventLog;
    private final ToastQueue toasts;
    /** Live "who is played this tick" read — {@code Actor.NONE} when nobody is. */
    private final IntSupplier playedActorId;

    private long consumedRows;

    public SkillUpTracker(SkillTrackRegistry tracks, ActorRegistry registry,
            IdentityRegistry identity, EventLog eventLog, ToastQueue toasts,
            IntSupplier playedActorId) {
        this.tracks = tracks;
        this.registry = registry;
        this.identity = identity;
        this.eventLog = eventLog;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        // Baseline: whatever the log already holds is history, not this session's news.
        this.consumedRows = tracks.levelLog().totalRecorded();
    }

    /**
     * Narrates every level-up recorded since the last call. Call exactly once per executed
     * tick (the {@code SimulationDriver.setAfterTick} seam); a no-op when nothing levelled
     * or the registry is unwired.
     */
    public void afterTick(long tick) {
        SkillLevelLog log = tracks.levelLog();
        long total = log.totalRecorded();
        if (total == consumedRows) {
            return;
        }
        int size = log.size();
        // Oldest-first ring index of the first unconsumed row; rows the ring already
        // overwrote (absolute row < total - size) are unrecoverable and skipped.
        int start = (int) Math.max(0, consumedRows - (total - size));
        for (int i = start; i < size; i++) {
            narrate(log.tickAt(i), log.actorIdAt(i), log.skillRawAt(i), log.newLevelAt(i));
        }
        consumedRows = total;
    }

    private void narrate(long tick, int actorId, int skillRaw, int newLevel) {
        String skill = tracks.skills().get(skillRaw).displayName();
        if (actorId == playedActorId.getAsInt()) {
            toasts.add(skill + " increased to " + newLevel);
        } else {
            eventLog.add(tick, PersonNames.fullNameOf(actorId, registry, identity)
                    + " is now " + skill + " " + newLevel);
        }
    }
}
