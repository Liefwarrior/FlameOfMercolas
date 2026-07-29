package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.SellFeedbackTracker;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.sim.actor.ActorRegistry;

/**
 * Polls the SELL verb (S8 — the counter half of the playable loop): while driving an actor,
 * {@code B} arms the played actor's sell intent ({@code Actor.setPlayerSellIntent}) — the SIM
 * resolves it next tick inside {@code PlayerControlPolicy.act} through the shared
 * {@code SellVerb.sellMaterialsInReach}, which is the SAME {@code BankVerbs.sellToShop}
 * exchange the AI fishers already use: items move, Royals transfer, nothing mints or sinks.
 *
 * <p>{@code B} was unbound (the verify-free-then-bind rule; taken keys: SPACE F PERIOD G T P I
 * C E N J L M R K WASD arrows brackets ESC). Mirrors {@link CullInput}'s shape.
 */
public final class SellInput {

    /** The arming toast (immediate feedback even while the driver is PAUSED). */
    public static final String ARM_TOAST = "You spread your goods on the counter...";

    private SellInput() {
    }

    /** Applies one frame's sell input ({@code B} just pressed). */
    public static void poll(PlayModeState playMode, ActorRegistry registry,
            ToastQueue toasts, SellFeedbackTracker feedback) {
        if (Gdx.input.isKeyJustPressed(Input.Keys.B)) {
            applySell(playMode, registry, toasts, feedback);
        }
    }

    /** The deterministic sell application (the {@code applyCull} convention). */
    public static void applySell(PlayModeState playMode, ActorRegistry registry,
            ToastQueue toasts, SellFeedbackTracker feedback) {
        if (!playMode.active()) {
            return;
        }
        registry.get(playMode.playedActorId()).setPlayerSellIntent(true);
        toasts.add(ARM_TOAST);
        feedback.arm();
    }
}
