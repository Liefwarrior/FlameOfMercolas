package com.trojia.client.camera;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * {@link FollowZSnap} — the Sprint-4 edge-triggered follow z-snap (the playtest fix for
 * "the follow camera eats every manual z-scrub"). GL-free.
 */
class FollowZSnapTest {

    @Test
    void snapsOnFirstFollowThenStaysQuietWhileNothingChanges() {
        FollowZSnap snap = new FollowZSnap();
        assertTrue(snap.shouldSnap(371, 21), "a fresh follow snaps to the actor's floor");
        assertFalse(snap.shouldSnap(371, 21), "quiet frames never re-snap (peek survives)");
        assertFalse(snap.shouldSnap(371, 21));
    }

    @Test
    void snapsWhenTheFollowedActorChangesBands() {
        FollowZSnap snap = new FollowZSnap();
        snap.shouldSnap(371, 21);
        assertTrue(snap.shouldSnap(371, 20), "a climb carries the view along");
        assertFalse(snap.shouldSnap(371, 20));
    }

    @Test
    void snapsWhenTheFollowTargetChanges() {
        FollowZSnap snap = new FollowZSnap();
        snap.shouldSnap(371, 21);
        assertTrue(snap.shouldSnap(12, 21), "a retarget snaps even on the same floor");
    }

    @Test
    void resetForgetsSoTheNextFollowSnapsAgain() {
        FollowZSnap snap = new FollowZSnap();
        snap.shouldSnap(371, 21);
        snap.reset();
        assertTrue(snap.shouldSnap(371, 21), "follow off/on must re-snap");
    }
}
