package com.trojia.client.input;

import com.trojia.client.inspect.FishFeedbackTracker;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FishingSpots;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The GL-free halves of the Sprint-6 FISH verb: {@link FishInput#applyFish} lands the
 * intent on the sim's own seam ({@code Actor.setPlayerFishIntent}) and toasts the attempt;
 * {@link FishFeedbackTracker} turns the resolving tick's reason stamp into the outcome
 * toast, with a bounded pending window. Headless, no GL — the {@code EatInputTest} shape.
 */
class FishInputTest {

    private static ActorRegistry population() {
        return CompoundBlockPopulation.build(1234L).registry();
    }

    private static FishFeedbackTracker tracker(ActorRegistry registry, ToastQueue toasts,
            PlayModeState playMode) {
        return new FishFeedbackTracker(registry, toasts, playMode::playedActorId,
                SkillTrackRegistry.UNWIRED, FishingSpots.EMPTY);
    }

    @Test
    void applyFishArmsTheSimIntentToastsAndArmsTheFeedbackWatch() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        ToastQueue toasts = new ToastQueue();
        try {
            FishInput.applyFish(playMode, registry, toasts,
                    tracker(registry, toasts, playMode));

            assertTrue(actor.playerFishIntent(),
                    "the intent must land on the sim's own play-mode seam");
            assertEquals(List.of(FishInput.ARM_TOAST),
                    toasts.visible().stream().map(ToastQueue.Toast::text).toList());
        } finally {
            actor.setPlayerFishIntent(false);
        }
    }

    @Test
    void applyFishIsANoOpOutsidePlayMode() {
        ActorRegistry registry = population();
        Actor actor = registry.get(2);
        ToastQueue toasts = new ToastQueue();
        FishInput.applyFish(new PlayModeState(), registry, toasts,
                tracker(registry, toasts, new PlayModeState()));
        assertFalse(actor.playerFishIntent());
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
        FishFeedbackTracker feedback = tracker(registry, toasts, playMode);
        try {
            feedback.arm();
            actor.setLastReasonCode(ReasonCode.NO_SPOT_IN_REACH);
            feedback.afterTick(100L);
            assertEquals(List.of(FishFeedbackTracker.NO_SPOT),
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
        FishFeedbackTracker feedback = tracker(registry, toasts, playMode);
        try {
            actor.setLastReasonCode(ReasonCode.PLAYER_CONTROLLED);
            feedback.arm();
            for (int t = 0; t <= FishFeedbackTracker.PENDING_TICKS + 2; t++) {
                feedback.afterTick(t);
            }
            assertTrue(toasts.visible().isEmpty(),
                    "a never-resolving intent expires without a stale toast");
        } finally {
            actor.setLastReasonCode(before);
        }
    }

    @Test
    void everyFishOutcomeReasonMapsToItsSentenceAndOthersToNull() {
        assertEquals(FishFeedbackTracker.CAUGHT,
                FishFeedbackTracker.outcomeLine(ReasonCode.CAUGHT_FISH));
        assertEquals(FishFeedbackTracker.GOT_AWAY,
                FishFeedbackTracker.outcomeLine(ReasonCode.FISH_GOT_AWAY));
        assertEquals(FishFeedbackTracker.NO_SPOT,
                FishFeedbackTracker.outcomeLine(ReasonCode.NO_SPOT_IN_REACH));
        assertNull(FishFeedbackTracker.outcomeLine(ReasonCode.PLAYER_CONTROLLED));
        assertNull(FishFeedbackTracker.outcomeLine(ReasonCode.ATE_FOOD));
        assertNull(FishFeedbackTracker.outcomeLine(null));
    }
}
