package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;

/**
 * Play mode's observer-only UI state (PLAY-MODE-SPEC.md §5.1): whether an actor is currently
 * under direct player control, which actor id that is, and whether "impersonation-pick" mode
 * is armed (the next click chooses who the played actor presents as). Pure state, no libGDX
 * dependency — mirrors {@link InspectorState}'s shape.
 *
 * <p>The played actor is tracked here (not just read off {@link InspectorState#selectedActorId()}
 * live) so {@code PlayModeInput} can detect the one frame the selection changes out from under
 * Play mode (a click via the ordinary {@code InspectorInput} path while Play mode is active,
 * PLAY-MODE-SPEC.md §3 item 1) and hand control to the newly selected actor instead of leaving
 * the old one permanently pinned to {@code StatusBit.PLAYER_CONTROLLED}.
 */
public final class PlayModeState {

    private boolean active;
    private int playedActorId = Actor.NONE;
    private boolean impersonatePickArmed;

    public boolean active() {
        return active;
    }

    public int playedActorId() {
        return playedActorId;
    }

    public boolean impersonatePickArmed() {
        return impersonatePickArmed;
    }

    /** Turns Play mode on for {@code actorId}. */
    public void enable(int actorId) {
        this.active = true;
        this.playedActorId = actorId;
    }

    /** Turns Play mode off entirely (no actor played, impersonation-pick disarmed). */
    public void disable() {
        this.active = false;
        this.playedActorId = Actor.NONE;
        this.impersonatePickArmed = false;
    }

    /** Re-targets the played actor without leaving Play mode (the selection-follows case). */
    public void retarget(int actorId) {
        this.playedActorId = actorId;
    }

    /** Arms impersonation-pick; a no-op while Play mode is inactive. */
    public void armImpersonatePick() {
        if (active) {
            this.impersonatePickArmed = true;
        }
    }

    public void clearImpersonatePick() {
        this.impersonatePickArmed = false;
    }
}
