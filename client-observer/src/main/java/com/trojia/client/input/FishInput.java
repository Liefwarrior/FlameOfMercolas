package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.FishFeedbackTracker;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.sim.actor.ActorRegistry;

/**
 * Polls the FISH verb (Sprint 6 fishing — the played soul works the water too): while
 * driving an actor, {@code R} arms the played actor's cast intent
 * ({@code Actor.setPlayerFishIntent}) — the SIM resolves it next tick inside
 * {@code PlayerControlPolicy.act} against a live spot within reach that the soul actually
 * PERCEIVES (the same per-actor sight gate the AI fishers obey), through the shared cast
 * attempt: FISHING XP on the attempt, the {@code check.fishing} draw, FISH minted on
 * success. Outcome feedback arrives through {@link FishFeedbackTracker}'s reason-code
 * read (CAUGHT_FISH / FISH_GOT_AWAY / NO_SPOT_IN_REACH) plus the catch-check line.
 *
 * <p>{@code R} was unbound (the verify-free-then-bind rule; taken keys: SPACE F PERIOD G
 * T P I C E N J L M WASD arrows brackets ESC). Mirrors {@link EatInput}'s shape: a thin
 * {@code Gdx.input} wrapper around the deterministic {@link #applyFish} seam the
 * scripted-playtest tape drives keyless.
 */
public final class FishInput {

    /** The arming toast (immediate feedback even while the driver is PAUSED). */
    public static final String ARM_TOAST = "You read the water and cast a line...";

    private FishInput() {
    }

    /** Applies one frame's fish input ({@code R} just pressed). */
    public static void poll(PlayModeState playMode, ActorRegistry registry,
            ToastQueue toasts, FishFeedbackTracker feedback) {
        if (Gdx.input.isKeyJustPressed(Input.Keys.R)) {
            applyFish(playMode, registry, toasts, feedback);
        }
    }

    /**
     * The deterministic fish application (the {@code applyEat} convention): arms the
     * sim-side intent, toasts the attempt, and arms the feedback tracker so the outcome
     * reason lands as its own toast after the resolving tick. A no-op while Play mode is
     * inactive.
     */
    public static void applyFish(PlayModeState playMode, ActorRegistry registry,
            ToastQueue toasts, FishFeedbackTracker feedback) {
        if (!playMode.active()) {
            return;
        }
        registry.get(playMode.playedActorId()).setPlayerFishIntent(true);
        toasts.add(ARM_TOAST);
        feedback.arm();
    }
}
