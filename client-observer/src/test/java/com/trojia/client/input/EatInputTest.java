package com.trojia.client.input;

import com.trojia.client.inspect.EatFeedbackTracker;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ReasonCode;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The GL-free halves of the Sprint-4 EAT verb: {@link EatInput#applyEat} lands the intent
 * on the sim's own seam ({@code Actor.setPlayerEatIntent}) and toasts the attempt;
 * {@link EatFeedbackTracker} turns the resolving tick's reason stamp into the outcome
 * toast, with a bounded pending window. Headless, no GL.
 */
class EatInputTest {

    private static ActorRegistry population() {
        return CompoundBlockPopulation.build(1234L).registry();
    }

    @Test
    void applyEatArmsTheSimIntentToastsAndArmsTheFeedbackWatch() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        ToastQueue toasts = new ToastQueue();
        EatFeedbackTracker feedback = new EatFeedbackTracker(registry, toasts,
                playMode::playedActorId);
        try {
            EatInput.applyEat(playMode, registry, toasts, feedback);

            assertTrue(actor.playerEatIntent(),
                    "the intent must land on the sim's own play-mode seam");
            assertEquals(List.of(EatInput.ARM_TOAST),
                    toasts.visible().stream().map(ToastQueue.Toast::text).toList());
        } finally {
            actor.setPlayerEatIntent(false);
        }
    }

    @Test
    void applyEatIsANoOpOutsidePlayMode() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        ToastQueue toasts = new ToastQueue();
        EatInput.applyEat(new PlayModeState(), registry, toasts,
                new EatFeedbackTracker(registry, toasts, () -> Actor.NONE));
        assertFalse(actor.playerEatIntent());
        assertTrue(toasts.visible().isEmpty());
    }

    @Test
    void feedbackToastsTheOutcomeReasonOnceThenDisarms() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        ReasonCode before = actor.lastReasonCode();
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        ToastQueue toasts = new ToastQueue();
        EatFeedbackTracker feedback = new EatFeedbackTracker(registry, toasts,
                playMode::playedActorId);
        try {
            feedback.arm();
            actor.setLastReasonCode(ReasonCode.NO_MEAL_IN_REACH);
            feedback.afterTick(100L);
            assertEquals(List.of(EatFeedbackTracker.NOTHING),
                    toasts.visible().stream().map(ToastQueue.Toast::text).toList());
            // Disarmed: the standing stamp never re-toasts on later ticks.
            feedback.afterTick(101L);
            assertEquals(1, toasts.visible().size());
        } finally {
            actor.setLastReasonCode(before);
        }
    }

    @Test
    void feedbackExpiresSilentlyWhenNoOutcomeLands() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        ReasonCode before = actor.lastReasonCode();
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        ToastQueue toasts = new ToastQueue();
        EatFeedbackTracker feedback = new EatFeedbackTracker(registry, toasts,
                playMode::playedActorId);
        try {
            actor.setLastReasonCode(ReasonCode.PLAYER_CONTROLLED);
            feedback.arm();
            for (int t = 0; t <= EatFeedbackTracker.PENDING_TICKS + 2; t++) {
                feedback.afterTick(t);
            }
            assertTrue(toasts.visible().isEmpty(),
                    "a never-resolving intent expires without a stale toast");
        } finally {
            actor.setLastReasonCode(before);
        }
    }

    @Test
    void everyEatOutcomeReasonMapsToItsSentenceAndOthersToNull() {
        assertEquals(EatFeedbackTracker.ATE,
                EatFeedbackTracker.outcomeLine(ReasonCode.ATE_FOOD));
        assertEquals(EatFeedbackTracker.BOUGHT,
                EatFeedbackTracker.outcomeLine(ReasonCode.BOUGHT_FOOD));
        assertEquals(EatFeedbackTracker.SCAVENGED,
                EatFeedbackTracker.outcomeLine(ReasonCode.SCAVENGED_FOOD));
        assertEquals(EatFeedbackTracker.NOTHING,
                EatFeedbackTracker.outcomeLine(ReasonCode.NO_MEAL_IN_REACH));
        assertNull(EatFeedbackTracker.outcomeLine(ReasonCode.PLAYER_CONTROLLED));
        assertNull(EatFeedbackTracker.outcomeLine(null));
    }
}
