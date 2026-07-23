package com.trojia.client.input;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ZLinkTable;

/**
 * Polls the CLIMB verb (Sprint 4 "the climb", the play-mode half of the sim's cross-z
 * seam): while driving an actor, {@code Up}/{@code Down} arrow arms a cross-z move intent
 * onto the baked stair/ramp connector under the played actor's feet —
 * {@link Actor#setPlayerMoveTarget} with the linked destination cell, which
 * {@code PlayerControlPolicy} resolves through {@code Actor.tryStepVertical} (the
 * connector-guarded, speed-gated, occupancy-capped vertical commit; an unlinked
 * destination is a deterministic sim-side no-op).
 *
 * <p><b>The arrows change meaning in Play mode.</b> Outside Play mode they scrub the
 * camera's z-level (Dwarf-Fortress style); while driving a soul they are YOUR legs on the
 * stair — the follow-camera keeps the viewed floor glued to the body, so a committed climb
 * carries the view along. The caller gates {@code CameraInput}'s z-scrub off while Play
 * mode is active so the two never fight over the same key.
 *
 * <p><b>Held, like WASD.</b> The vertical commit is speed-gated sim-side (the move
 * accumulator), so the key re-arms the intent every held frame until the step lands —
 * exactly the WASD movement contract. The "no way up/down from here" refusal toasts only
 * on the initial press (never once per held frame).
 *
 * <p>Mirrors {@link PlayModeInput}'s shape: thin {@code Gdx.input} wrapper around the
 * deterministic {@link #applyClimb} seam the scripted-playtest tape drives keyless.
 */
public final class ClimbInput {

    /** The climb refusal toasts (also the scripted tape's proof text). */
    public static final String NO_WAY_UP = "No stair or ramp leads up from here.";
    public static final String NO_WAY_DOWN = "No stair or ramp leads down from here.";

    private ClimbInput() {
    }

    /** Applies one frame's climb input ({@code Up}/{@code Down} held; toast on press). */
    public static void poll(PlayModeState playMode, ActorRegistry registry, ZLinkTable links,
            ToastQueue toasts) {
        if (!playMode.active()) {
            return;
        }
        if (Gdx.input.isKeyPressed(Input.Keys.UP)) {
            applyClimb(playMode, registry, links, toasts, +1,
                    Gdx.input.isKeyJustPressed(Input.Keys.UP));
        } else if (Gdx.input.isKeyPressed(Input.Keys.DOWN)) {
            applyClimb(playMode, registry, links, toasts, -1,
                    Gdx.input.isKeyJustPressed(Input.Keys.DOWN));
        }
    }

    /**
     * The deterministic climb application: resolves the first baked connector (ascending
     * baked order — the district's universal fixed-order tiebreak) whose near endpoint is
     * the played actor's own cell in the requested direction, and arms the sim's move
     * intent with its far endpoint. {@code toastOnMiss} narrates the refusal (the initial
     * press); a held-frame re-poll stays silent. A no-op while Play mode is inactive.
     *
     * @param direction {@code +1} climb up, {@code -1} climb down
     */
    public static void applyClimb(PlayModeState playMode, ActorRegistry registry,
            ZLinkTable links, ToastQueue toasts, int direction, boolean toastOnMiss) {
        if (!playMode.active() || direction == 0) {
            return;
        }
        Actor played = registry.get(playMode.playedActorId());
        int dest = connectorFrom(links, played.cell(), direction);
        if (dest == Actor.NONE) {
            if (toastOnMiss) {
                toasts.add(direction > 0 ? NO_WAY_UP : NO_WAY_DOWN);
            }
            return;
        }
        played.setPlayerMoveTarget(dest);
    }

    /**
     * The first baked connector destination from {@code cell} in {@code direction}
     * (ascending baked link order), or {@link Actor#NONE}. Package-private for the test.
     */
    static int connectorFrom(ZLinkTable links, int cell, int direction) {
        for (int i = 0; i < links.linkCount(); i++) {
            if (direction > 0 && links.low(i) == cell) {
                return links.high(i);
            }
            if (direction < 0 && links.high(i) == cell) {
                return links.low(i);
            }
        }
        return Actor.NONE;
    }
}
