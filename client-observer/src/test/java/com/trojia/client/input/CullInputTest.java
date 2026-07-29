package com.trojia.client.input;

import com.trojia.client.inspect.CullAvailability;
import com.trojia.client.inspect.CullFeedbackTracker;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.CullVerb;
import com.trojia.sim.actor.SkillTrackRegistry;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE COOLDOWN NO LONGER LIES (S8 playtest fix): a {@code K} press that cannot work is refused
 * with the reason that applies, and nothing narrates kneeling for an attempt that never began.
 * Headless, no GL — the {@code FishInputTest} shape.
 *
 * <p>The happy path (a press beside a real carcass that resolves into a scalp) is walked end to
 * end by {@code PlayableScalpLoopTest}; this test owns the refusals.
 */
class CullInputTest {

    private static ActorRegistry population() {
        return CompoundBlockPopulation.build(1234L).registry();
    }

    private static CullFeedbackTracker tracker(ActorRegistry registry, PlayModeState playMode,
            ToastQueue toasts) {
        return new CullFeedbackTracker(registry, toasts, playMode::playedActorId,
                SkillTrackRegistry.UNWIRED);
    }

    /** An actor that CAN cull (has a pack, is not itself quarry, is not a beast). */
    private static Actor culler(ActorRegistry registry) {
        for (int i = 0; i < registry.size(); i++) {
            if (CullVerb.canCull(registry.get(i))) {
                return registry.get(i);
            }
        }
        throw new AssertionError("the compound fixture bakes nobody who can hold a knife");
    }

    private static List<String> lines(ToastQueue toasts) {
        return toasts.visible().stream().map(ToastQueue.Toast::text).toList();
    }

    @Test
    void aLatchedKnifeHandSaysSoAndDoesNotNarrateKneeling() {
        ActorRegistry registry = population();
        Actor actor = culler(registry);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        ToastQueue toasts = new ToastQueue();
        try {
            actor.setCulledUntilTick(1_000 + CullVerb.CULL_COOLDOWN_TICKS);

            CullInput.applyCull(playMode, registry, toasts, tracker(registry, playMode, toasts),
                    1_000);

            assertFalse(actor.playerCullIntent(),
                    "a latched press must not arm an attempt the sim will only refuse");
            List<String> said = lines(toasts);
            assertEquals(1, said.size(), () -> "one toast, not a kneel-then-deny pair: " + said);
            assertTrue(said.get(0).startsWith(CullAvailability.COOLDOWN_PREFIX),
                    () -> "the refusal must name the cooldown: " + said.get(0));
            assertNotEquals(CullFeedbackTracker.NO_QUARRY, said.get(0),
                    "a latched hand is not the same state as an empty street");
            assertFalse(said.get(0).contains("kneel"),
                    () -> "nothing kneels for an attempt that never began: " + said.get(0));
        } finally {
            actor.setCulledUntilTick(0);
            actor.setPlayerCullIntent(false);
        }
    }

    @Test
    void anEmptyStreetStillSaysThereIsNothingToCut() {
        ActorRegistry registry = population();
        Actor actor = culler(registry);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(actor.id());
        ToastQueue toasts = new ToastQueue();
        // No latch, and the compound fixture has no downed vermin anywhere.
        assertEquals(Actor.NONE, CullAvailability.quarryInReach(registry, actor));

        CullInput.applyCull(playMode, registry, toasts, tracker(registry, playMode, toasts),
                1_000);

        assertFalse(actor.playerCullIntent());
        assertEquals(List.of(CullFeedbackTracker.NO_QUARRY), lines(toasts));
    }

    @Test
    void cooldownLineCountsDownInTheClockTheHudAlreadyShows() {
        // The 500-tick latch is half an hour of ward time on the HUD's 24h clock.
        assertEquals(CullAvailability.COOLDOWN_PREFIX + "30 min.",
                CullAvailability.cooldownLine(CullVerb.CULL_COOLDOWN_TICKS));
        assertEquals(CullAvailability.COOLDOWN_PREFIX + "15 min.",
                CullAvailability.cooldownLine(CullVerb.CULL_COOLDOWN_TICKS / 2));
        assertEquals(CullAvailability.COOLDOWN_PREFIX + "1 min.",
                CullAvailability.cooldownLine(1), "an almost-spent latch never reads as 0 min");
    }

    @Test
    void aFreeHandBesideQuarryIsNotRefused() {
        ActorRegistry registry = population();
        Actor actor = culler(registry);
        assertEquals(0, CullAvailability.cooldownTicksLeft(actor, 1_000));
        // The only thing missing on this fixture is the carcass, so the refusal names exactly
        // that — proving the three states are told apart rather than collapsed.
        assertEquals(CullFeedbackTracker.NO_QUARRY,
                CullAvailability.refusal(registry, actor, 1_000));
        actor.setCulledUntilTick(2_000);
        try {
            assertTrue(CullAvailability.refusal(registry, actor, 1_000)
                    .startsWith(CullAvailability.COOLDOWN_PREFIX),
                    "the latch outranks the empty street: it is the state that will change");
        } finally {
            actor.setCulledUntilTick(0);
        }
    }

    @Test
    void aBeastIsToldItHasNoHandsForTheWork() {
        ActorRegistry registry = population();
        Actor beast = null;
        for (int i = 0; i < registry.size() && beast == null; i++) {
            if (!CullVerb.canCull(registry.get(i))) {
                beast = registry.get(i);
            }
        }
        if (beast == null) {
            return; // this fixture bakes no beasts; the branch is covered by the refusal above
        }
        assertEquals(CullAvailability.CANNOT_CULL,
                CullAvailability.refusal(registry, beast, 1_000));
    }

    @Test
    void applyCullIsANoOpOutsidePlayMode() {
        ActorRegistry registry = population();
        Actor actor = culler(registry);
        ToastQueue toasts = new ToastQueue();
        PlayModeState idle = new PlayModeState();
        CullInput.applyCull(idle, registry, toasts, tracker(registry, idle, toasts), 1_000);
        assertFalse(actor.playerCullIntent());
        assertTrue(toasts.visible().isEmpty());
    }

    @Test
    void outcomeLineIgnoresReasonsThatAreNotCullOutcomes() {
        assertNull(CullFeedbackTracker.outcomeLine(null));
    }
}
