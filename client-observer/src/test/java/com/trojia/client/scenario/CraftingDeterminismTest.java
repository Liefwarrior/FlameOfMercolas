package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.input.SpellInput;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.SpellFeedbackTracker;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.time.SimulationDriver;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.StatusBit;
import com.trojia.sim.actor.spell.SpellVerb;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.world.io.WorldHasher;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * MAGIC IS DETERMINISTIC, AND IT IS NOT A NO-OP — the two halves that have to be proved
 * together, because either one alone is easy to fake.
 *
 * <p>The project's twin-run gate soaks the ward with no player in it, and no AI works craftings
 * this pass, so that gate would report green over a magic system that never fired. This test
 * drives the SAME scripted cast tape through two independent builds of one seed, in lockstep,
 * and demands the hardened ACTORS-section hash — which now covers the lingering-effect table
 * and the per-caster crafting latch — match at every checkpoint. Then it runs a third build
 * with the tape removed and demands the hash DIFFER, so a green run cannot mean "the casts did
 * nothing".
 */
class CraftingDeterminismTest {

    private static final int WARMUP_TICKS = 300;
    private static final int TICKS = 3_000;
    private static final int CHECKPOINT_PERIOD = 500;
    /** Ticks between scripted presses — comfortably past the longest authored latch. */
    private static final int PRESS_PERIOD = 750;

    /** One build plus the handful of client objects a scripted press needs. */
    private record Run(DocksPopulation pop, SimulationDriver driver, PlayModeState playMode,
            ToastQueue toasts, SpellFeedbackTracker feedback, int hero) {
    }

    @Test
    void twinsRunningTheSameCastTapeStayByteIdenticalForThreeThousandTicks() {
        Run a = build(true);
        Run b = build(true);
        assertEquals(a.hero, b.hero, "both twins take the wheel of the same soul");

        int[] tape = tape(a);
        int next = 0;
        for (int t = 1; t <= TICKS; t++) {
            if (t % PRESS_PERIOD == 0) {
                int spellRaw = tape[next++ % tape.length];
                press(a, spellRaw);
                press(b, spellRaw);
            }
            step(a);
            step(b);
            if (t % CHECKPOINT_PERIOD == 0) {
                assertEquals(actorsHash(a.pop.system()), actorsHash(b.pop.system()),
                        "craftings twins diverged at tick " + t);
            }
        }
        assertTrue(a.pop.system().activeEffects().liveCountOn(a.hero) > 0
                        || a.pop.system().skillTracks()
                            .progressGrains(a.hero, a.pop.system().skillTracks().linkcraftRaw())
                                > 0,
                "the tape must have reached the sim at all");
    }

    @Test
    void theTapeActuallyChangesTheWard() {
        Run cast = build(true);
        Run silent = build(false);
        int[] tape = tape(cast);
        int next = 0;
        for (int t = 1; t <= TICKS; t++) {
            if (t % PRESS_PERIOD == 0) {
                press(cast, tape[next++ % tape.length]);
            }
            step(cast);
            step(silent);
        }
        assertNotEquals(actorsHash(silent.pop.system()), actorsHash(cast.pop.system()),
                "a magic system that does nothing also hashes deterministically, and that is "
                        + "not what the twin above is proving");
    }

    // ==================================================================
    // helpers
    // ==================================================================

    private static Run build(boolean takeTheWheel) {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation pop = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        SimulationDriver driver = new SimulationDriver(loaded.world(), loaded.worldSeed(),
                List.<SimulationSystem>of(pop.system()));
        for (int t = 0; t < WARMUP_TICKS; t++) {
            driver.requestStep();
        }
        int hero = lowestIdNamedReader(pop);
        assertTrue(hero != Actor.NONE, "the ward bakes named readers");
        PlayModeState playMode = new PlayModeState();
        ToastQueue toasts = new ToastQueue();
        SpellFeedbackTracker feedback = new SpellFeedbackTracker(pop.registry(), toasts,
                playMode::playedActorId, pop.system().skillTracks(), pop.system().spells());
        if (takeTheWheel) {
            playMode.enable(hero);
            pop.registry().get(hero).setStatus(StatusBit.PLAYER_CONTROLLED, true);
        }
        return new Run(pop, driver, playMode, toasts, feedback, hero);
    }

    /** The scripted presses: one of each axis, so every kind of row gets filed and expired. */
    private static int[] tape(Run run) {
        var spells = run.pop.system().spells();
        return new int[] {
                spells.rawOf("warm_the_hands"),
                spells.rawOf("steady_the_hand"),
                spells.rawOf("draw_the_chill"),
                spells.rawOf("sting"),
        };
    }

    private static void press(Run run, int spellRaw) {
        SpellInput.applyCast(run.playMode, run.pop.registry(), run.pop.system().spells(),
                run.pop.system().skillTracks(), spellRaw, run.toasts, run.feedback,
                run.driver.currentTick());
    }

    private static void step(Run run) {
        run.driver.requestStep();
        run.feedback.afterTick(run.driver.currentTick());
    }

    /** The lowest-id named living soul that can read — a choice no RNG participates in. */
    private static int lowestIdNamedReader(DocksPopulation pop) {
        for (int i = 0; i < pop.registry().size(); i++) {
            Actor actor = pop.registry().get(i);
            if (SpellVerb.isLiterate(actor) && !actor.isDead()
                    && i < pop.identity().size() && pop.identity().get(i).named()) {
                return i;
            }
        }
        return Actor.NONE;
    }

    private static long actorsHash(ActorsSystem system) {
        WorldHasher hasher = new WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }
}
