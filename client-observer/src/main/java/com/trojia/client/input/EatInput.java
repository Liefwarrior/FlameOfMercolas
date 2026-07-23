package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.EatFeedbackTracker;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.sim.actor.ActorRegistry;

/**
 * Polls the EAT verb (Sprint 4 item 3, PLAY-MODE need interaction): while driving an
 * actor, {@code E} arms the played actor's eat intent ({@code Actor.setPlayerEatIntent})
 * — the SIM resolves it next tick inside {@code PlayerControlPolicy.act} through
 * {@code SeekFoodPolicy}'s exact eat-in-reach chain (carried ration, counter at the
 * player's OWN barter quote, larder, commons, the broke's scavenge — identical sinks, XP
 * and faction effects). Outcome feedback arrives through {@link EatFeedbackTracker}'s
 * reason-code read (ATE_FOOD / BOUGHT_FOOD / SCAVENGED_FOOD / NO_MEAL_IN_REACH).
 *
 * <p>This closes the playtest's starvation hole: {@code PLAYER_CONTROL} outscores
 * {@code SEEK_FOOD}, so before this key a played soul could never feed itself and starved
 * on a long session.
 *
 * <p>Mirrors {@link TheftInput}'s shape: a thin {@code Gdx.input} wrapper around the
 * deterministic {@link #applyEat} seam the scripted-playtest tape drives keyless.
 */
public final class EatInput {

    /** The arming toast (immediate feedback even while the driver is PAUSED). */
    public static final String ARM_TOAST = "You look for something to eat...";

    private EatInput() {
    }

    /** Applies one frame's eat input ({@code E} just pressed). */
    public static void poll(PlayModeState playMode, ActorRegistry registry,
            ToastQueue toasts, EatFeedbackTracker feedback) {
        if (Gdx.input.isKeyJustPressed(Input.Keys.E)) {
            applyEat(playMode, registry, toasts, feedback);
        }
    }

    /**
     * The deterministic eat application (the {@code applyPickpocket} convention): arms the
     * sim-side intent, toasts the attempt, and arms the feedback tracker so the outcome
     * reason lands as its own toast after the resolving tick. A no-op while Play mode is
     * inactive.
     */
    public static void applyEat(PlayModeState playMode, ActorRegistry registry,
            ToastQueue toasts, EatFeedbackTracker feedback) {
        if (!playMode.active()) {
            return;
        }
        registry.get(playMode.playedActorId()).setPlayerEatIntent(true);
        toasts.add(ARM_TOAST);
        feedback.arm();
    }
}
