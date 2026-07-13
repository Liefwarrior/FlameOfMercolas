package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;

/**
 * The observer inspector's mutable selection + follow state (M-inspector Behaviors 1 &amp; 4):
 * which actor (by {@code ActorId}) is currently selected, and whether the camera is
 * following it. Pure state, no libGDX dependency — the input layer mutates it once per
 * frame, the panel/log renderer and the follow-camera step read it.
 *
 * <p><b>Ids, never references.</b> Selection is stored as an {@code ActorId} int, never a
 * cached {@link Actor} handle, so every consumer re-fetches live state from the
 * {@code ActorRegistry} each frame — the same "no staleness" contract
 * {@code ActorRenderer} holds for glyph positions. An {@code ActorId} is stable for the
 * life of the sim (actors are never removed — {@code ActorRegistry} javadoc), so a stored
 * selection never dangles.
 *
 * <p><b>Follow auto-off on deselect.</b> Clearing the selection (a click on empty space)
 * also clears follow, so the camera can never be left chasing "nothing"; re-selecting does
 * not auto-enable follow (the follow key is a deliberate, separate toggle).
 */
public final class InspectorState {

    private int selectedActorId = Actor.NONE;
    private boolean follow;

    /** The selected {@code ActorId}, or {@link Actor#NONE} when nothing is selected. */
    public int selectedActorId() {
        return selectedActorId;
    }

    /** Whether an actor is currently selected. */
    public boolean hasSelection() {
        return selectedActorId != Actor.NONE;
    }

    /** Whether the camera is actively following the selected actor. */
    public boolean followActive() {
        return follow && selectedActorId != Actor.NONE;
    }

    /**
     * Selects {@code actorId} (or deselects when {@link Actor#NONE}). Deselecting also
     * turns follow off; selecting a (possibly different) actor leaves follow untouched
     * unless it was a deselect.
     */
    public void select(int actorId) {
        this.selectedActorId = actorId;
        if (actorId == Actor.NONE) {
            this.follow = false;
        }
    }

    /**
     * Toggles camera-follow. A no-op while nothing is selected (there is nothing to
     * follow) — the follow flag stays off so a later selection does not silently inherit
     * a "pending" follow.
     */
    public void toggleFollow() {
        if (selectedActorId != Actor.NONE) {
            this.follow = !this.follow;
        }
    }
}
