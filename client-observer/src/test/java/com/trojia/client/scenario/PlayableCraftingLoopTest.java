package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.input.SpellInput;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.SpellAvailability;
import com.trojia.client.inspect.SpellBar;
import com.trojia.client.inspect.SpellFeedbackTracker;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.actor.spell.SpellRegistry;
import com.trojia.sim.actor.spell.SpellVerb;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.progression.AttributeId;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * THE THREE AXES, WALKED IN THE REAL WARD (the {@code PlayableScalpLoopTest} shape): a named
 * soul in the live Docks bake steps into Play mode and works one crafting of each axis Eli
 * named — change temperature, harm, change stat — driven ONLY through the seam a click on the
 * craftings bar goes through. No console commands, no test-only backdoor: every state change
 * below travels {@code SpellInput.applyCast} -> the sim's cast intent -> {@code
 * SpellVerb.resolveCast}, the same road a player's finger takes.
 *
 * <p>The one thing this test does that a player would not is CHOOSE its soul by scanning — a
 * player uses their eyes. Everything after the choice is the ordinary game, latch and all.
 */
class PlayableCraftingLoopTest {

    /** Let the ward settle into its routes before anybody starts working craftings. */
    private static final int WARMUP_TICKS = 400;
    /** Attempts allowed per crafting: every check in this game has a ceiling under 1000. */
    private static final int MAX_ATTEMPTS = 12;
    /** Ticks to spend waiting for somebody to come within arm's reach. */
    private static final int NEIGHBOUR_WAIT_TICKS = 400;

    @Test
    void aNamedSoulWarmsItsHandsStingsAHandAndSteadiesItsOwnGrip() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation pop = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(pop.system()));
        ActorRegistry registry = pop.registry();
        SpellRegistry spells = pop.system().spells();
        var tracks = pop.system().skillTracks();
        var effects = pop.system().activeEffects();

        assertTrue(spells.size() >= 3,
                "the public shelf must actually be bound at bake, or the bar is empty");

        for (int t = 0; t < WARMUP_TICKS; t++) {
            driver.requestStep();
        }

        int hero = crowdedNamedReader(pop);
        assertNotEquals(Actor.NONE, hero, "no named literate soul with neighbours in the ward");
        String heroName = pop.identity().get(hero).fullName();

        PlayModeState playMode = new PlayModeState();
        playMode.enable(hero);
        registry.get(hero).setStatus(StatusBit.PLAYER_CONTROLLED, true);
        ToastQueue toasts = new ToastQueue();
        SpellFeedbackTracker feedback = new SpellFeedbackTracker(registry, toasts,
                playMode::playedActorId, tracks, spells);

        // ---- The bar knows what this soul may press before anything is pressed. --------
        List<SpellBar.Button> bar = SpellBar.layout(spells, 1600f, 900f, true);
        assertEquals(spells.size(), bar.size(), "one button per crafting");
        assertTrue(SpellBar.known(spells, tracks, hero, spells.rawOf("warm_the_hands")),
                heroName + " reads at all, so the level-0 shelf is theirs");

        // ---- 1. CHANGE TEMPERATURE (self: no link to forge to your own hands). ---------
        assertEquals(0, effects.temperatureOffsetDeciK(hero));
        workUntilItTakes(driver, pop, playMode, toasts, feedback, spells,
                spells.rawOf("warm_the_hands"), heroName);
        assertEquals(15, effects.temperatureOffsetDeciK(hero),
                heroName + " worked Warm the Hands and is not any warmer");
        assertTrue(SpellBar.heldLine(effects, hero, driver.currentTick()).contains("+1.5 deg"),
                "and the bar's footer says so, so it is legible after the toast fades");

        // ---- 2. HARM (touch: the arm IS the link). -------------------------------------
        int stung = stingSomebody(driver, pop, playMode, toasts, feedback, spells, heroName);
        assertNotEquals(Actor.NONE, stung,
                heroName + " never got a hand on anybody in " + MAX_ATTEMPTS + " attempts");

        // ---- 3. CHANGE STAT (self). ----------------------------------------------------
        int agiBefore = tracks.attribute(hero, AttributeId.AGI);
        workUntilItTakes(driver, pop, playMode, toasts, feedback, spells,
                spells.rawOf("steady_the_hand"), heroName);
        assertEquals(agiBefore + 1, tracks.attribute(hero, AttributeId.AGI),
                heroName + " steadied their hand and the sheet did not notice");

        // ---- ...and the SCREEN said all of it. -----------------------------------------
        List<String> said = toasts.visible().stream().map(ToastQueue.Toast::text).toList();
        String all = String.join("\n", said);
        assertTrue(all.contains(SpellFeedbackTracker.WORKED), all);
        assertTrue(all.contains("[Linkcraft "), "the visible dice, like every other check:\n" + all);
        assertTrue(all.contains("THE LINK HOLDS]"), all);

        // ---- ...and the work itself taught the trade. ----------------------------------
        assertTrue(tracks.progressGrains(hero, tracks.linkcraftRaw()) > 0
                        || tracks.level(hero, tracks.linkcraftRaw()) > 0,
                heroName + " worked craftings all morning and learned nothing");
    }

    @Test
    void aBeastCannotUseALibrary() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation pop = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        int beast = Actor.NONE;
        for (int i = 0; i < pop.registry().size() && beast == Actor.NONE; i++) {
            if (!SpellVerb.isLiterate(pop.registry().get(i))) {
                beast = i;
            }
        }
        assertNotEquals(Actor.NONE, beast, "the ward bakes cats and gulls");

        PlayModeState playMode = new PlayModeState();
        playMode.enable(beast);
        ToastQueue toasts = new ToastQueue();
        SpellRegistry spells = pop.system().spells();
        SpellInput.applyCast(playMode, pop.registry(), pop.identity(), spells,
                pop.system().skillTracks(), spells.rawOf("warm_the_hands"), toasts,
                new SpellFeedbackTracker(pop.registry(), toasts, playMode::playedActorId,
                        pop.system().skillTracks(), spells),
                0L);

        assertEquals(SpellAvailability.ILLITERATE,
                toasts.visible().get(0).text(),
                "the libraries opened to any who can READ, and a gull cannot");
        assertEquals(Actor.NONE, pop.registry().get(beast).playerSpellRaw());
    }

    // ==================================================================
    // helpers — the player's eyes and the player's patience
    // ==================================================================

    /**
     * Presses a crafting until the link holds, waiting out the latch between attempts exactly
     * as a player must (the {@code PlayableScalpLoopTest} idiom — nothing here clears a
     * cooldown by hand).
     */
    private static void workUntilItTakes(SimulationDriver driver, DocksPopulation pop,
            PlayModeState playMode, ToastQueue toasts, SpellFeedbackTracker feedback,
            SpellRegistry spells, int spellRaw, String heroName) {
        int hero = playMode.playedActorId();
        for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
            waitOutTheLatch(driver, pop.registry(), hero);
            SpellInput.applyCast(playMode, pop.registry(), pop.identity(), spells,
                    pop.system().skillTracks(),
                    spellRaw, toasts, feedback, driver.currentTick());
            driver.requestStep();
            feedback.afterTick(driver.currentTick());
            ReasonCode stamp = pop.registry().get(hero).lastReasonCode();
            if (stamp == ReasonCode.SPELL_WORKED) {
                return;
            }
            assertTrue(stamp == ReasonCode.SPELL_FIZZLED || stamp == ReasonCode.NO_LINK_TO_TARGET,
                    heroName + " pressed a crafting and the verb did not resolve: " + stamp);
        }
        throw new AssertionError(heroName + " worked " + spells.get(spellRaw).displayName()
                + " " + MAX_ATTEMPTS + " times and the link never held");
    }

    /**
     * Works STING on whoever is within arm's reach, waiting for somebody to come by if nobody
     * is. Returns the body that took it, or {@link Actor#NONE}. The hp assertion lives here
     * because the target is pinned by the press — the sim refuses rather than re-aims if the
     * body steps away before the resolving tick.
     */
    private static int stingSomebody(SimulationDriver driver, DocksPopulation pop,
            PlayModeState playMode, ToastQueue toasts, SpellFeedbackTracker feedback,
            SpellRegistry spells, String heroName) {
        int hero = playMode.playedActorId();
        int spellRaw = spells.rawOf("sting");
        for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
            waitOutTheLatch(driver, pop.registry(), hero);
            int target = waitForNeighbour(driver, pop.registry(), hero, spells, spellRaw);
            if (target == Actor.NONE) {
                continue;
            }
            short hpBefore = pop.registry().get(target).hp();
            SpellInput.applyCast(playMode, pop.registry(), pop.identity(), spells,
                    pop.system().skillTracks(),
                    spellRaw, toasts, feedback, driver.currentTick());
            driver.requestStep();
            feedback.afterTick(driver.currentTick());
            if (pop.registry().get(hero).lastReasonCode() != ReasonCode.SPELL_WORKED) {
                continue;
            }
            assertEquals(hpBefore - 1, pop.registry().get(target).hp(),
                    heroName + "'s sting landed and the body it landed on is unmarked");
            assertTrue(pop.registry().get(target).hp() > 0,
                    "and it is very much alive -- no crafting on the shelf can kill");
            return target;
        }
        return Actor.NONE;
    }

    /** Ticks until this soul's crafting hand comes free (never clears the latch by hand). */
    private static void waitOutTheLatch(SimulationDriver driver, ActorRegistry registry,
            int hero) {
        while (SpellAvailability.cooldownTicksLeft(registry.get(hero), driver.currentTick()) > 0) {
            driver.requestStep();
        }
    }

    /** Ticks until somebody stands within the crafting's reach; the sim's own target rule. */
    private static int waitForNeighbour(SimulationDriver driver, ActorRegistry registry,
            int hero, SpellRegistry spells, int spellRaw) {
        for (int t = 0; t < NEIGHBOUR_WAIT_TICKS; t++) {
            int target = SpellVerb.targetInReach(registry.get(hero), registry,
                    spells.get(spellRaw));
            if (target != Actor.NONE) {
                return target;
            }
            driver.requestStep();
        }
        return Actor.NONE;
    }

    /**
     * The player's eyes: a NAMED, living, unheld soul that can read, standing where other
     * people are — the most crowded such tile, so the touch crafting has something to touch.
     */
    private static int crowdedNamedReader(DocksPopulation pop) {
        ActorRegistry registry = pop.registry();
        int best = Actor.NONE;
        int bestNeighbours = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (!SpellVerb.isLiterate(actor) || actor.isDead()
                    || actor.hasStatus(StatusBit.HELD) || actor.hasStatus(StatusBit.HOUSE_ARREST)
                    || i >= pop.identity().size() || !pop.identity().get(i).named()) {
                continue;
            }
            int neighbours = 0;
            for (int j = 0; j < registry.size(); j++) {
                Actor other = registry.get(j);
                if (j != i && !other.isDead()
                        && PackedPos.z(other.cell()) == PackedPos.z(actor.cell())
                        && ActorGeometry.chebyshev(actor.cell(), other.cell()) <= 3) {
                    neighbours++;
                }
            }
            if (neighbours > bestNeighbours) {
                bestNeighbours = neighbours;
                best = i;
            }
        }
        return best;
    }
}
