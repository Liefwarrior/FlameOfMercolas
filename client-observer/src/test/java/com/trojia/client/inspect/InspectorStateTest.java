package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** {@link InspectorState} selection + follow semantics — pure, headless. */
class InspectorStateTest {

    @Test
    void startsWithNothingSelectedOrFollowed() {
        InspectorState state = new InspectorState();
        assertFalse(state.hasSelection());
        assertEquals(Actor.NONE, state.selectedActorId());
        assertFalse(state.followActive());
    }

    @Test
    void selectingAnActorPersistsTheId() {
        InspectorState state = new InspectorState();
        state.select(7);
        assertTrue(state.hasSelection());
        assertEquals(7, state.selectedActorId());
    }

    @Test
    void followToggleRequiresASelection() {
        InspectorState state = new InspectorState();
        state.toggleFollow(); // no selection: no-op
        assertFalse(state.followActive());

        state.select(3);
        state.toggleFollow();
        assertTrue(state.followActive());
        state.toggleFollow();
        assertFalse(state.followActive());
    }

    @Test
    void deselectingClearsFollow() {
        InspectorState state = new InspectorState();
        state.select(3);
        state.toggleFollow();
        assertTrue(state.followActive());

        state.select(Actor.NONE); // click on empty space
        assertFalse(state.hasSelection());
        assertFalse(state.followActive());
    }

    @Test
    void reSelectingDoesNotAutoEnableFollow() {
        InspectorState state = new InspectorState();
        state.select(1);
        assertFalse(state.followActive(), "a fresh selection must not silently follow");
    }
}
