package com.trojia.client.inspect;

import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.CrimeLog;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.function.IntSupplier;

/**
 * Consumes the Sim team's {@link CrimeLog} seam once per executed tick (Sprint 2 "reactive
 * streets"): every theft becomes a named feed line, and the PLAYED actor's own attempts
 * additionally toast with a CRPG check-result line ({@link CheckLineFormatter}) — the
 * pickpocket verb's promised feedback either way. GL-free; wired as an after-tick callback
 * beside {@link EventLogTracker} and {@link SkillUpTracker} on the same
 * {@code SimulationDriver} seam. Read-only cursor over
 * {@link CrimeLog#totalRecorded()} — the {@link SkillUpTracker} convention, including its
 * treat-existing-rows-as-history baseline and its overwritten-rows-are-lost shrug.
 *
 * <p><b>The thief is named by the face the ward SAW.</b> A crime row carries both the true
 * body and the identity it presented; the feed prints the PRESENTED name (the Persona
 * rule), so a disguised cutpurse's lifts stain the cover in the ward's narration — exactly
 * as they stain its standings.
 */
public final class CrimeFeedTracker {

    private final CrimeLog log;
    private final SkillTrackRegistry tracks;
    private final ActorRegistry registry;
    private final IdentityRegistry identity;
    private final EventLog eventLog;
    private final ToastQueue toasts;
    /** Live "who is played this tick" read — {@code Actor.NONE} when nobody is. */
    private final IntSupplier playedActorId;
    /** The talk panel's check-line sink; an open panel shows the played roll inline. */
    private final TalkState talk;

    private long consumedRows;

    public CrimeFeedTracker(CrimeLog log, SkillTrackRegistry tracks, ActorRegistry registry,
            IdentityRegistry identity, EventLog eventLog, ToastQueue toasts,
            IntSupplier playedActorId, TalkState talk) {
        this.log = log;
        this.tracks = tracks;
        this.registry = registry;
        this.identity = identity;
        this.eventLog = eventLog;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
        this.talk = talk;
        // Baseline: whatever the log already holds is history, not this session's news.
        this.consumedRows = log.totalRecorded();
    }

    /**
     * Narrates every crime row recorded since the last call. Call exactly once per executed
     * tick (the {@code SimulationDriver.setAfterTick} seam).
     */
    public void afterTick(long tick) {
        long total = log.totalRecorded();
        if (total == consumedRows) {
            return;
        }
        int size = log.size();
        int start = (int) Math.max(0, consumedRows - (total - size));
        for (int i = start; i < size; i++) {
            narrate(log.tickAt(i), log.thiefIdAt(i), log.presentedIdAt(i), log.victimIdAt(i),
                    log.witnessedAt(i));
        }
        consumedRows = total;
    }

    private void narrate(long tick, int thiefId, int presentedId, int victimId,
            boolean witnessed) {
        String thief = PersonNames.fullNameOf(presentedId, registry, identity);
        String victim = PersonNames.fullNameOf(
                registry.get(victimId).identity().presentedId(), registry, identity);
        eventLog.add(tick, witnessed
                ? thief + " was caught with a hand in " + victim + "'s pocket"
                : thief + " picked " + victim + "'s pocket");
        if (thiefId == playedActorId.getAsInt()) {
            String line = CheckLineFormatter.pickpocketLine(tracks, thiefId, victimId, victim,
                    !witnessed);
            toasts.add((witnessed
                    ? "Caught! " + victim + " seizes your wrist. "
                    : "You lift " + victim + "'s pocket clean. ") + line);
            if (talk != null) {
                talk.setCheckLine(line);
            }
        }
    }
}
