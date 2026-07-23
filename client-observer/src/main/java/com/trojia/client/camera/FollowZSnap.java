package com.trojia.client.camera;

/**
 * Edge-triggered z-snap for the follow camera (Sprint 4 playtest fix: the follow camera
 * used to re-snap the viewed z-level to the followed actor's floor EVERY frame, silently
 * eating any manual z-scrub — the input registered and was reverted one frame later).
 *
 * <p>The contract now: while following, the viewed floor snaps to the actor's z only on an
 * EDGE — the follow target changing, follow (re)activating, or the actor itself changing
 * bands (a climb carries the view along). Between edges the level scrub is yours: peek at
 * the roof-slums above a followed quayside porter, and the view stays put until he takes a
 * stair. Pure state, no libGDX dependency — the x/y centering itself stays per-frame in
 * {@code ObserverApp} (unchanged).
 */
public final class FollowZSnap {

    private static final int NONE = Integer.MIN_VALUE;

    private int lastActorId = NONE;
    private int lastZ = NONE;

    /**
     * Whether this frame should snap the viewed z to {@code actorZ}: {@code true} on a
     * follow-target change or an actor band change, {@code false} on every quiet frame
     * (manual scrub survives). Call once per followed frame.
     */
    public boolean shouldSnap(int actorId, int actorZ) {
        boolean snap = actorId != lastActorId || actorZ != lastZ;
        lastActorId = actorId;
        lastZ = actorZ;
        return snap;
    }

    /** Forgets the followed target (call while follow is off; the next follow snaps). */
    public void reset() {
        lastActorId = NONE;
        lastZ = NONE;
    }
}
