package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.AdjacentTargets;
import com.trojia.client.inspect.PersonNames;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;

/**
 * Polls the PICKPOCKET verb (Sprint 2 item 4, PLAY-MODE adjacency interaction): while
 * driving an actor, {@code G} arms the played actor's pickpocket intent
 * ({@link Actor#setPlayerPickpocketTarget}) against the adjacent soul —
 * {@link AdjacentTargets}' lowest-id rule, executed bodies excluded. The SIM resolves the
 * lift next tick inside {@code PlayerControlPolicy.act} (`TheftMechanics.pickpocket` — the
 * check, the coin move, the witnessed-crime consequence chain all happen sim-side); outcome
 * feedback arrives through {@code CrimeFeedTracker}'s toast + feed narration.
 *
 * <p>Mirrors {@link PlayModeInput}'s shape: a thin {@code Gdx.input} wrapper around the
 * deterministic {@link #applyPickpocket} seam.
 */
public final class TheftInput {

    private TheftInput() {
    }

    /** Applies one frame's pickpocket input ({@code G} just pressed). */
    public static void poll(PlayModeState playMode, ActorRegistry registry,
            IdentityRegistry identity, ToastQueue toasts) {
        if (Gdx.input.isKeyJustPressed(Input.Keys.G)) {
            applyPickpocket(playMode, registry, identity, toasts);
        }
    }

    /**
     * The deterministic pickpocket application (the {@code PlayModeInput.applyMovement}
     * convention): resolves the adjacent mark, arms the sim-side intent, and toasts the
     * attempt — legible immediately even while the driver is PAUSED (the lift itself lands
     * on the next executed tick). Toasts "no pocket" when nobody liftable is in reach. A
     * no-op while Play mode is inactive.
     */
    public static void applyPickpocket(PlayModeState playMode, ActorRegistry registry,
            IdentityRegistry identity, ToastQueue toasts) {
        if (!playMode.active()) {
            return;
        }
        int played = playMode.playedActorId();
        int mark = AdjacentTargets.lowestIdAdjacent(registry, played, false);
        if (mark == Actor.NONE) {
            toasts.add("No pocket within reach.");
            return;
        }
        registry.get(played).setPlayerPickpocketTarget(mark);
        // The mark is named as the ward sees it (presented identity — the Persona rule).
        toasts.add("You slip a hand toward " + PersonNames.fullNameOf(
                registry.get(mark).identity().presentedId(), registry, identity)
                + "'s pocket...");
    }
}
