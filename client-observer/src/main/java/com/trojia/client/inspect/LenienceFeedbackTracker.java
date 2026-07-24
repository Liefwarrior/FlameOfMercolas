package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;

import java.util.function.IntSupplier;

/**
 * Narrates the Watch's lenience weighing on the PLAYED soul (Sprint 5 "check lines
 * everywhere" — the justice pipeline's first skill read made visible): when the played
 * body's reason stamp transitions to {@link ReasonCode#WARNED_MOVE_ALONG} (lenience
 * granted) or {@link ReasonCode#ARRESTED} (the fine+custody path), one toast carries the
 * {@link CheckLineFormatter#lenienceLine} — the presented face's Watch standing and the
 * true body's streetwise, the exact inputs the sim's draw read. Zero sim writes; GL-free;
 * wired into the driver's after-tick seam beside the other trackers.
 *
 * <p><b>Edge-triggered.</b> Reason stamps persist across ticks; the tracker toasts only
 * on a CHANGE onto one of the two codes, and re-baselines silently whenever the played
 * body itself changes (driving a new soul never inherits the last one's stamps).
 *
 * <p><b>Honest scope.</b> A bare {@code ARRESTED} stamp on a played body comes from the
 * apprehend machinery's fine path (theft corrections stamp
 * {@code ARRESTED_FOR_THEFT}, which the crime feed already narrates with its own contest
 * line); if a future custody source adds new stamps, this tracker narrates only these two.
 */
public final class LenienceFeedbackTracker {

    static final String WARNED_OUTCOME = "WARNED";
    static final String FINED_OUTCOME = "FINED & HELD";

    private final ActorRegistry registry;
    private final SkillTrackRegistry tracks;
    private final FactionStandings standings;
    private final ToastQueue toasts;
    /** Live "who is played this tick" read — {@code Actor.NONE} when nobody is. */
    private final IntSupplier playedActorId;

    private int prevPlayed = Actor.NONE;
    private ReasonCode prevReason;

    public LenienceFeedbackTracker(ActorRegistry registry, SkillTrackRegistry tracks,
            FactionStandings standings, ToastQueue toasts, IntSupplier playedActorId) {
        this.registry = registry;
        this.tracks = tracks;
        this.standings = standings;
        this.toasts = toasts;
        this.playedActorId = playedActorId;
    }

    /** Call once per executed tick, after the tick ran (the driver's after-tick seam). */
    public void afterTick(long tick) {
        int played = playedActorId.getAsInt();
        if (played == Actor.NONE) {
            prevPlayed = Actor.NONE;
            prevReason = null;
            return;
        }
        Actor actor = registry.get(played);
        ReasonCode reason = actor.lastReasonCode();
        if (played != prevPlayed) {
            // Retarget baseline: the new body's current stamp is history, not news.
            prevPlayed = played;
            prevReason = reason;
            return;
        }
        if (reason != prevReason) {
            if (reason == ReasonCode.WARNED_MOVE_ALONG) {
                toast(actor, WARNED_OUTCOME);
            } else if (reason == ReasonCode.ARRESTED) {
                toast(actor, FINED_OUTCOME);
            }
        }
        prevReason = reason;
    }

    private void toast(Actor actor, String outcome) {
        toasts.add(CheckLineFormatter.lenienceLine(tracks, standings, actor.id(),
                actor.identity().presentedId(), outcome));
    }
}
