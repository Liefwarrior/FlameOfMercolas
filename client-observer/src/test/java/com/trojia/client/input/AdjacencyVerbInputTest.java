package com.trojia.client.input;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.inspect.AdjacentTargets;
import com.trojia.client.inspect.PlayModeState;
import com.trojia.client.inspect.TalkState;
import com.trojia.client.inspect.ToastQueue;
import com.trojia.client.scenario.DocksPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorGeometry;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.bark.BarkRawsLoader;
import com.trojia.sim.bark.BarkTableRegistry;
import com.trojia.sim.world.PackedPos;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The GL-free halves of the Sprint-2 adjacency verbs ({@link TheftInput#applyPickpocket}
 * and {@link TalkInput#applyTalk} — the real {@code Gdx.input} reads are thin wrappers, the
 * {@code PlayModeInputTest} convention), against the forged docks bake: the pickpocket
 * intent lands on the sim's own seam ({@code Actor.setPlayerPickpocketTarget}) for the
 * lowest-id adjacent mark, talk opens a frozen exchange against the adjacent soul, and both
 * verbs toast a legible refusal when nobody is in reach. Headless, no GL.
 */
class AdjacencyVerbInputTest {

    private static long worldSeed;
    private static DocksPopulation population;
    private static BarkTableRegistry barks;

    @BeforeAll
    static void bake() {
        FixtureWorldLoader.Loaded loaded = FixtureWorldLoader.loadDocksSurface();
        worldSeed = loaded.worldSeed();
        population = DocksPopulation.build(worldSeed, loaded.world());
        barks = BarkRawsLoader.load(RepoPaths.locate("content", "raws"));
    }

    /** The lowest-id actor with at least one adjacent neighbour at spawn (dense district). */
    private static int actorWithNeighbour() {
        ActorRegistry registry = population.registry();
        for (int i = 0; i < registry.size(); i++) {
            if (AdjacentTargets.lowestIdAdjacent(registry, i, true) != Actor.NONE) {
                return i;
            }
        }
        throw new AssertionError("no adjacent pair at spawn in a 692-soul district");
    }

    /** An actor with NO neighbour within reach at spawn (a lone beast/roof soul), or -1. */
    private static int isolatedActor() {
        ActorRegistry registry = population.registry();
        for (int i = 0; i < registry.size(); i++) {
            if (AdjacentTargets.lowestIdAdjacent(registry, i, true) == Actor.NONE) {
                return i;
            }
        }
        return -1;
    }

    @Test
    void applyPickpocketArmsTheSimIntentOnTheLowestIdAdjacentMark() {
        ActorRegistry registry = population.registry();
        int played = actorWithNeighbour();
        int expected = AdjacentTargets.lowestIdAdjacent(registry, played, false);
        assertTrue(expected != Actor.NONE, "the found neighbour is liftable");
        PlayModeState playMode = new PlayModeState();
        playMode.enable(played);
        ToastQueue toasts = new ToastQueue();
        try {
            TheftInput.applyPickpocket(playMode, registry, population.identity(), toasts);

            assertEquals(expected, registry.get(played).playerPickpocketTargetId(),
                    "the intent must land on the sim's own play-mode seam");
            assertEquals(1, toasts.visible().size());
            assertTrue(toasts.visible().get(0).text().startsWith("You slip a hand toward "),
                    toasts.visible().get(0).text());
            // The verb reaches only within the sim's own lift range.
            assertTrue(ActorGeometry.chebyshev(registry.get(played).cell(),
                    registry.get(expected).cell()) <= AdjacentTargets.REACH);
            assertEquals(PackedPos.z(registry.get(played).cell()),
                    PackedPos.z(registry.get(expected).cell()));
        } finally {
            registry.get(played).setPlayerPickpocketTarget(Actor.NONE);
        }
    }

    @Test
    void applyPickpocketIsANoOpOutsidePlayModeAndToastsWhenNothingIsInReach() {
        ActorRegistry registry = population.registry();
        ToastQueue toasts = new ToastQueue();
        TheftInput.applyPickpocket(new PlayModeState(), registry, population.identity(),
                toasts);
        assertTrue(toasts.visible().isEmpty(), "inactive play mode: no toast, no intent");

        int loner = isolatedActor();
        if (loner >= 0) {
            PlayModeState playMode = new PlayModeState();
            playMode.enable(loner);
            TheftInput.applyPickpocket(playMode, registry, population.identity(), toasts);
            assertEquals("No pocket within reach.", toasts.visible().get(0).text());
            assertEquals(Actor.NONE, registry.get(loner).playerPickpocketTargetId());
        }
    }

    @Test
    void applyTalkOpensAFrozenExchangeAndArmsTheSimTalkIntent() {
        ActorRegistry registry = population.registry();
        int played = actorWithNeighbour();
        int expected = AdjacentTargets.lowestIdAdjacent(registry, played, true);
        PlayModeState playMode = new PlayModeState();
        playMode.enable(played);
        TalkState talk = new TalkState();
        try {
            TalkInput.applyTalk(talk, playMode, registry, population.jobs(),
                    population.identity(), population.system().factionStandings(),
                    population.relationships(), barks, new ToastQueue(),
                    population.questRegistry(), population.system().questLog(),
                    worldSeed, 3_000L);

            assertTrue(talk.open());
            assertEquals(expected, talk.exchange().speakerId());
            assertEquals(3_000L, talk.exchange().tick(), "the exchange freezes at its tick");
            assertFalse(talk.exchange().barkLine().isBlank());
            // S3 §1.1: the FACT of talking enters the sim — the intent lands on the sim's
            // own play-mode seam for PlayerControlPolicy to validate and consume.
            assertEquals(expected, registry.get(played).playerTalkTargetId(),
                    "the talk intent must land on the sim's own play-mode seam");
            talk.close();
            assertFalse(talk.open());
        } finally {
            registry.get(played).setPlayerTalkTarget(Actor.NONE);
        }
    }

    @Test
    void applyTalkIsANoOpOutsidePlayMode() {
        TalkState talk = new TalkState();
        ToastQueue toasts = new ToastQueue();
        TalkInput.applyTalk(talk, new PlayModeState(), population.registry(),
                population.jobs(), population.identity(),
                population.system().factionStandings(), population.relationships(), barks,
                toasts, population.questRegistry(), population.system().questLog(),
                worldSeed, 1L);
        assertFalse(talk.open());
        assertTrue(toasts.visible().isEmpty());
    }
}
