package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** {@link PlayModeState} Play-mode/impersonation-pick semantics — pure, headless. */
class PlayModeStateTest {

    @Test
    void startsInactiveWithNoPlayedActor() {
        PlayModeState state = new PlayModeState();
        assertFalse(state.active());
        assertEquals(Actor.NONE, state.playedActorId());
        assertFalse(state.impersonatePickArmed());
    }

    @Test
    void enablingTracksTheGivenActor() {
        PlayModeState state = new PlayModeState();
        state.enable(7);
        assertTrue(state.active());
        assertEquals(7, state.playedActorId());
    }

    @Test
    void disablingClearsEverything() {
        PlayModeState state = new PlayModeState();
        state.enable(7);
        state.armImpersonatePick();
        assertTrue(state.impersonatePickArmed());

        state.disable();

        assertFalse(state.active());
        assertEquals(Actor.NONE, state.playedActorId());
        assertFalse(state.impersonatePickArmed());
    }

    @Test
    void retargetChangesThePlayedActorWithoutLeavingPlayMode() {
        PlayModeState state = new PlayModeState();
        state.enable(7);

        state.retarget(9);

        assertTrue(state.active());
        assertEquals(9, state.playedActorId());
    }

    @Test
    void armImpersonatePickIsANoOpWhileInactive() {
        PlayModeState state = new PlayModeState();
        state.armImpersonatePick();
        assertFalse(state.impersonatePickArmed(), "nothing to impersonate-pick for while inactive");
    }

    @Test
    void clearImpersonatePickTurnsTheArmedFlagOffWithoutDeactivating() {
        PlayModeState state = new PlayModeState();
        state.enable(7);
        state.armImpersonatePick();

        state.clearImpersonatePick();

        assertFalse(state.impersonatePickArmed());
        assertTrue(state.active(), "clearing the pick must not exit Play mode");
    }
}
