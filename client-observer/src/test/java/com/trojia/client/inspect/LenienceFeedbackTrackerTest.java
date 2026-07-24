package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link LenienceFeedbackTracker} contract (Sprint 5 check lines): edge-triggered on the
 * played body's reason stamp — a transition ONTO {@code WARNED_MOVE_ALONG}/{@code
 * ARRESTED} toasts the lenience inputs line, a persisting stamp stays silent, a body
 * retarget re-baselines, nobody-played resets. Headless, reason stamps crafted directly
 * (the sim owns when they fire; this pins what the client does when they do).
 */
class LenienceFeedbackTrackerTest {

    private final CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);
    private final SkillTrackRegistry tracks = DocksPopulation.freshSkillTracks();
    private final FactionStandings standings = DocksPopulation.freshFactionStandings();
    private final ToastQueue toasts = new ToastQueue();

    private int played = Actor.NONE;
    private final LenienceFeedbackTracker tracker = new LenienceFeedbackTracker(
            p.registry(), tracks, standings, toasts, () -> played);

    @Test
    void warnTransitionToastsTheLenienceInputs() {
        ActorRegistry registry = p.registry();
        played = 2;
        registry.get(2).setLastReasonCode(ReasonCode.JOB_GOAL);
        tracker.afterTick(1L); // baseline
        registry.get(2).setLastReasonCode(ReasonCode.WARNED_MOVE_ALONG);
        tracker.afterTick(2L);

        assertEquals(1, toasts.visible().size());
        assertEquals("[the Watch weighs the face you wear: standing +0, Streetwise 0"
                + " -- WARNED]", toasts.visible().get(0).text());

        // The stamp persists: no re-toast tick after tick.
        tracker.afterTick(3L);
        assertEquals(1, toasts.visible().size());

        // The fine+custody path narrates its own outcome.
        registry.get(2).setLastReasonCode(ReasonCode.ARRESTED);
        tracker.afterTick(4L);
        assertEquals(2, toasts.visible().size());
        assertTrue(toasts.visible().get(1).text().endsWith("-- FINED & HELD]"));
    }

    @Test
    void firstObservationOfABodyIsBaselineNotNews() {
        ActorRegistry registry = p.registry();
        registry.get(4).setLastReasonCode(ReasonCode.WARNED_MOVE_ALONG);
        played = 4;
        tracker.afterTick(1L); // the stamp predates the retarget: history, not news
        assertTrue(toasts.visible().isEmpty());

        // ... and a LATER transition (off and back on) narrates normally.
        registry.get(4).setLastReasonCode(ReasonCode.IDLE_DEFAULT);
        tracker.afterTick(2L);
        registry.get(4).setLastReasonCode(ReasonCode.WARNED_MOVE_ALONG);
        tracker.afterTick(3L);
        assertEquals(1, toasts.visible().size());
    }

    @Test
    void otherTransitionsAndUnplayedTicksStaySilent() {
        ActorRegistry registry = p.registry();
        played = 3;
        registry.get(3).setLastReasonCode(ReasonCode.JOB_GOAL);
        tracker.afterTick(1L);
        registry.get(3).setLastReasonCode(ReasonCode.ARRESTED_FOR_THEFT);
        tracker.afterTick(2L); // the crime feed owns that one
        registry.get(3).setLastReasonCode(ReasonCode.BOUGHT_FOOD);
        tracker.afterTick(3L);
        assertTrue(toasts.visible().isEmpty());

        played = Actor.NONE;
        tracker.afterTick(4L);
        assertTrue(toasts.visible().isEmpty());
    }
}
