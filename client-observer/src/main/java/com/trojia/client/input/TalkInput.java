package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.AdjacentTargets;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.TalkState;
import com.trojia.client.inspect.TalkText;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.FactionStandings;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.bark.BarkTableRegistry;

/**
 * Polls the TALK verb (Sprint 2 item 1, PLAY-MODE adjacency interaction): while driving an
 * actor, {@code T} opens (or re-greets) the speech panel against the adjacent soul —
 * {@link AdjacentTargets}' lowest-id rule — and {@code ESC} closes it. Mirrors
 * {@link PlayModeInput}'s shape: a thin {@code Gdx.input} wrapper around a deterministic
 * {@link #applyTalk} seam a debug/verification caller can drive without a keyboard.
 *
 * <p><b>ESC ownership:</b> {@link #poll} returns {@code true} when it consumed this frame's
 * {@code ESC} to close the panel — the caller must then skip its own quit-on-ESC branch for
 * the frame, so closing a conversation never also closes the observer.
 */
public final class TalkInput {

    private TalkInput() {
    }

    /**
     * Applies one frame's talk input. Returns whether this call consumed the frame's
     * {@code ESC} (panel closed — the caller skips app-exit for this frame).
     */
    public static boolean poll(TalkState talk, PlayModeState playMode, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, BarkTableRegistry barks, ToastQueue toasts,
            long worldSeed, long tick) {
        // Leaving Play mode always drops the conversation (no floating panel over free-cam).
        if (talk.open() && !playMode.active()) {
            talk.close();
        }
        if (talk.open() && Gdx.input.isKeyJustPressed(Input.Keys.ESCAPE)) {
            talk.close();
            return true;
        }
        if (Gdx.input.isKeyJustPressed(Input.Keys.T)) {
            applyTalk(talk, playMode, registry, jobs, identity, standings, relationships,
                    barks, toasts, worldSeed, tick);
        }
        return false;
    }

    /**
     * The deterministic talk application (split from the live {@code Gdx.input} read above,
     * the {@code PlayModeInput.applyMovement} convention): resolves the adjacent target and
     * opens the panel on a fresh {@link TalkText#greet} exchange; toasts when nobody is in
     * reach. A no-op while Play mode is inactive.
     */
    public static void applyTalk(TalkState talk, PlayModeState playMode, ActorRegistry registry,
            JobRegistry jobs, IdentityRegistry identity, FactionStandings standings,
            RelationshipRegistry relationships, BarkTableRegistry barks, ToastQueue toasts,
            long worldSeed, long tick) {
        if (!playMode.active()) {
            return;
        }
        int played = playMode.playedActorId();
        int target = AdjacentTargets.lowestIdAdjacent(registry, played, true);
        if (target == Actor.NONE) {
            toasts.add("No one within reach to talk to.");
            return;
        }
        talk.open(TalkText.greet(worldSeed, tick, target, played, registry, jobs, identity,
                standings, relationships, barks));
    }
}
