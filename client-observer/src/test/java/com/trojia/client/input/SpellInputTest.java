package com.trojia.client.input;

import com.trojia.client.boot.RepoPaths;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.SpellAvailability;
import com.trojia.client.inspect.SpellFeedbackTracker;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.SkillTrackRegistry;
import com.trojia.sim.actor.spell.SpellRawsLoader;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.SpellVerb;
import com.trojia.sim.progression.SkillRawsLoader;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE BAR REFUSES HONESTLY (the {@code CullInputTest} shape, headless, no GL): a press that
 * cannot become a cast is turned away here, before the intent is armed, with the reason that
 * actually applies — and nothing narrates reaching for a link that was never reached for.
 *
 * <p>The happy path (a press that resolves into a real effect on a real body) is walked end to
 * end by {@code PlayableCraftingLoopTest}; this test owns the refusals and the arming contract.
 */
class SpellInputTest {

    private static final SpellRegistry SPELLS =
            SpellRawsLoader.load(RepoPaths.locate("content", "raws"));

    private static ActorRegistry population() {
        return CompoundBlockPopulation.build(1234L).registry();
    }

    private static SkillTrackRegistry wiredTracks() {
        return new SkillTrackRegistry(SkillRawsLoader.load(RepoPaths.locate("content", "raws")));
    }

    private static SpellFeedbackTracker tracker(ActorRegistry registry, PlayModeState playMode,
            ToastQueue toasts, SkillTrackRegistry tracks) {
        return new SpellFeedbackTracker(registry, toasts, playMode::playedActorId, tracks,
                SPELLS);
    }

    /** A body that reads (not a beast) and has somebody standing next to it. */
    private static Actor reader(ActorRegistry registry) {
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (SpellVerb.isLiterate(actor) && !actor.isDead()) {
                return actor;
            }
        }
        throw new AssertionError("the compound fixture bakes nobody who can read");
    }

    private static List<String> lines(ToastQueue toasts) {
        return toasts.visible().stream().map(ToastQueue.Toast::text).toList();
    }

    @Test
    void pressingTheBarWithNobodyBeingDrivenSaysHowToTakeTheWheel() {
        ActorRegistry registry = population();
        PlayModeState playMode = new PlayModeState();
        ToastQueue toasts = new ToastQueue();
        SkillTrackRegistry tracks = wiredTracks();

        SpellInput.applyCast(playMode, registry, SPELLS, tracks,
                SPELLS.rawOf("warm_the_hands"), toasts, tracker(registry, playMode, toasts,
                        tracks), 100L);

        assertEquals(List.of(SpellAvailability.NO_BODY), lines(toasts));
    }

    @Test
    void aCraftingYouHaveNotReadIsRefusedWithItsGateRatherThanArmed() {
        ActorRegistry registry = population();
        Actor caster = reader(registry);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(caster.id());
        ToastQueue toasts = new ToastQueue();
        SkillTrackRegistry tracks = wiredTracks();
        int deep = SPELLS.rawOf("sap_the_step");

        SpellInput.applyCast(playMode, registry, SPELLS, tracks, deep, toasts,
                tracker(registry, playMode, toasts, tracks), 100L);

        assertEquals(Actor.NONE, caster.playerSpellRaw(),
                "an unread crafting must not arm an attempt the sim will only refuse");
        assertEquals(1, lines(toasts).size());
        assertTrue(lines(toasts).get(0).startsWith(SpellAvailability.UNLEARNED_PREFIX),
                lines(toasts).get(0));
        assertTrue(lines(toasts).get(0).contains("Sap the Step"),
                "the refusal names the crafting the player actually clicked");
    }

    @Test
    void aHandStillOnTheLastLinkSaysHowLongAndNarratesNoReachingOut() {
        ActorRegistry registry = population();
        Actor caster = reader(registry);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(caster.id());
        ToastQueue toasts = new ToastQueue();
        SkillTrackRegistry tracks = wiredTracks();
        caster.setCastUntilTick(1_600L);

        SpellInput.applyCast(playMode, registry, SPELLS, tracks,
                SPELLS.rawOf("steady_the_hand"), toasts,
                tracker(registry, playMode, toasts, tracks), 1_000L);

        assertEquals(Actor.NONE, caster.playerSpellRaw());
        assertEquals(1, lines(toasts).size(), "one refusal, and no 'you reach for the link'");
        assertTrue(lines(toasts).get(0).startsWith(SpellAvailability.COOLDOWN_PREFIX),
                lines(toasts).get(0));
    }

    @Test
    void aSelfCraftingAlwaysHasATargetAndArmsWithItsName() {
        ActorRegistry registry = population();
        Actor caster = reader(registry);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(caster.id());
        ToastQueue toasts = new ToastQueue();
        SkillTrackRegistry tracks = wiredTracks();
        int steady = SPELLS.rawOf("steady_the_hand");

        assertNull(SpellAvailability.refusal(registry, SPELLS, tracks, caster, steady, 100L),
                "a self crafting a novice knows can never be refused for want of a target");
        SpellFeedbackTracker feedback = tracker(registry, playMode, toasts, tracks);
        SpellInput.applyCast(playMode, registry, SPELLS, tracks, steady, toasts, feedback, 100L);

        assertEquals(steady, caster.playerSpellRaw(), "the intent is armed for the sim");
        assertEquals(caster.id(), caster.playerSpellTargetId(), "on the caster's own body");
        assertEquals(steady, feedback.armedSpellRaw(), "and the outcome watch is armed with it");
        assertTrue(lines(toasts).get(0).contains("Steady the Hand"), lines(toasts).get(0));
    }

    @Test
    void theOutcomeTracksThroughToThreeToastsWhenTheLinkHolds() {
        ActorRegistry registry = population();
        Actor caster = reader(registry);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(caster.id());
        ToastQueue toasts = new ToastQueue();
        SkillTrackRegistry tracks = wiredTracks();
        SpellFeedbackTracker feedback = tracker(registry, playMode, toasts, tracks);

        feedback.arm(SPELLS.rawOf("warm_the_hands"), 1);
        caster.setLastReasonCode(ReasonCode.SPELL_WORKED);
        feedback.afterTick(10L);

        List<String> said = lines(toasts);
        assertEquals(3, said.size(), "what happened, what it did, and the dice: " + said);
        assertEquals(SpellFeedbackTracker.WORKED, said.get(0));
        assertTrue(said.get(1).contains("deg"), "the middle line reads the raws: " + said.get(1));
        assertTrue(said.get(2).startsWith("[Linkcraft "), said.get(2));
        assertTrue(said.get(2).contains("Warm the Hands"), said.get(2));
        assertTrue(said.get(2).endsWith("THE LINK HOLDS]"), said.get(2));
    }

    @Test
    void aFizzleSaysSoAndStillShowsTheOddsItMissed() {
        ActorRegistry registry = population();
        Actor caster = reader(registry);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(caster.id());
        ToastQueue toasts = new ToastQueue();
        SkillTrackRegistry tracks = wiredTracks();
        SpellFeedbackTracker feedback = tracker(registry, playMode, toasts, tracks);

        feedback.arm(SPELLS.rawOf("sting"), 1);
        caster.setLastReasonCode(ReasonCode.SPELL_FIZZLED);
        feedback.afterTick(10L);

        List<String> said = lines(toasts);
        assertEquals(2, said.size(), "a fizzle did nothing, so there is nothing to describe");
        assertEquals(SpellFeedbackTracker.FIZZLED, said.get(0));
        assertTrue(said.get(1).endsWith("THE LINK SLIPS]"), said.get(1));
    }

    @Test
    void theEffectLineReadsAnyCraftingIncludingOnesNobodyAuthored() {
        SpellRegistry invented = SpellRawsLoader.parse("""
                { "id": "spells", "spells": [
                  { "id": "odd", "displayName": "Odd", "skill": "linkcraft",
                    "target": "RANGED", "range": 4,
                    "components": [
                      { "effect": "VITALITY", "mode": "OVER_TIME",
                        "magnitude": -3, "durationTicks": 30 },
                      { "effect": "ATTRIBUTE", "mode": "WHILE_ACTIVE",
                        "magnitude": -2, "param": "VIG", "durationTicks": 400 } ] } ] }
                """);
        String line = SpellFeedbackTracker.effectLine(invented.get(invented.rawOf("odd")));
        assertTrue(line.contains("-3 hp"), line);
        assertTrue(line.contains("x3"), "three doses over its life: " + line);
        assertTrue(line.contains("-2 VIG"), line);
        assertNotEquals("", line);
    }
}
