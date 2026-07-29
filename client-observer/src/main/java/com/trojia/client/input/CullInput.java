package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.CullFeedbackTracker;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.sim.actor.ActorRegistry;

/**
 * Polls the CULL verb (S8 scalps — the played soul works the vermin bounty): while driving an
 * actor, {@code K} arms the played actor's cull intent ({@code Actor.setPlayerCullIntent}) —
 * the SIM resolves it next tick inside {@code PlayerControlPolicy.act} against a DOWNED
 * scalpable body within knife reach, through the same shared {@code CullVerb.resolveCull} the
 * AI's work-event seam uses: FIELDCRAFT XP on the attempt, the {@code check.cull} draw, the
 * named scalp minted on success, the per-culler latch stamped either way. Outcome feedback
 * arrives through {@link CullFeedbackTracker}'s reason-code read (TOOK_SCALP / SCALP_RUINED /
 * NO_QUARRY_IN_REACH) plus the cull-check line.
 *
 * <p>{@code K} was unbound (the verify-free-then-bind rule; taken keys: SPACE F PERIOD G T P I
 * C E N J L M R WASD arrows brackets ESC). Mirrors {@link FishInput}'s shape exactly: a thin
 * {@code Gdx.input} wrapper around the deterministic {@link #applyCull} seam the
 * scripted-playtest tape drives keyless.
 */
public final class CullInput {

    /** The arming toast (immediate feedback even while the driver is PAUSED). */
    public static final String ARM_TOAST = "You kneel to the carcass, knife out...";

    private CullInput() {
    }

    /** Applies one frame's cull input ({@code K} just pressed). */
    public static void poll(PlayModeState playMode, ActorRegistry registry,
            ToastQueue toasts, CullFeedbackTracker feedback) {
        if (Gdx.input.isKeyJustPressed(Input.Keys.K)) {
            applyCull(playMode, registry, toasts, feedback);
        }
    }

    /**
     * The deterministic cull application (the {@code applyFish} convention): arms the sim-side
     * intent, toasts the attempt, and arms the feedback tracker so the outcome reason lands as
     * its own toast after the resolving tick. A no-op while Play mode is inactive.
     */
    public static void applyCull(PlayModeState playMode, ActorRegistry registry,
            ToastQueue toasts, CullFeedbackTracker feedback) {
        if (!playMode.active()) {
            return;
        }
        registry.get(playMode.playedActorId()).setPlayerCullIntent(true);
        toasts.add(ARM_TOAST);
        feedback.arm();
    }
}
