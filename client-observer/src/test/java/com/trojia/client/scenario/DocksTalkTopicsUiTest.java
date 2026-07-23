package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.client.inspect.TalkText;
import com.trojia.client.inspect.TalkTopics;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.bark.BarkRawsLoader;
import com.trojia.sim.bark.BarkTableRegistry;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-4 CLIENT talk-topics gate, over the real docks bake: every bound notable's
 * topic rows fit the number-key surface ({@code <=} {@link TalkTopics#MAX_TOPICS}), every
 * PERSONAL/GOSSIP row's asked exchange resolves AUTHORED text (never the silent
 * fallback), quest beats ride the rows marked with the S3 convention, captions are human
 * lines, forged souls offer no rows, and the whole surface is pure (twin asks identical).
 * Headless, no GL.
 */
class DocksTalkTopicsUiTest {

    private static FixtureWorldLoader.Loaded loaded;
    private static DocksPopulation population;
    private static BarkTableRegistry barks;
    private static Map<String, Integer> notables;

    /** A mood-free mid-morning tick (no one downed/held at bake; greet windows open). */
    private static final long TICK = 3_000L;

    @BeforeAll
    static void bake() {
        loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        Path rawsRoot = RepoPaths.locate("content", "raws");
        barks = BarkRawsLoader.load(rawsRoot);
        notables = NameForge.bindNotableActors(population.registry(), population.homes(),
                NotableRaws.load(rawsRoot.resolve("names").resolve("notables.json")),
                DocksPopulation.notableSpawnSites());
    }

    private static List<TalkTopics.Topic> topicsOf(int actorId, int listenerId) {
        return TalkTopics.topicsFor(population.registry().get(actorId), listenerId,
                population.askTopicsOf(actorId), population.topicCatalog(),
                population.questRegistry(), population.system().questLog(),
                population.registry(), population.identity());
    }

    @Test
    void everyNotableFitsTheNumberKeySurfaceAndEveryAskResolvesAuthoredText() {
        int listener = 0; // any body; PERSONAL/GOSSIP asks do not read the listener
        for (Map.Entry<String, Integer> bound : notables.entrySet()) {
            List<TalkTopics.Topic> topics = topicsOf(bound.getValue(), listener);
            assertTrue(topics.size() <= TalkTopics.MAX_TOPICS,
                    bound.getKey() + " offers " + topics.size()
                            + " rows — grow past the number keys and this gate demands "
                            + "pagination, not silent truncation");
            assertFalse(topics.isEmpty(), bound.getKey() + " must offer rows");
            for (TalkTopics.Topic topic : topics) {
                assertFalse(topic.label().isBlank(), bound.getKey() + ": blank label");
                if (topic.kind() == TalkTopics.Kind.QUEST) {
                    assertTrue(topic.questMarked(), "quest rows carry the S3 marker");
                    continue;
                }
                TalkText.Exchange asked = TalkText.ask(loaded.worldSeed(), TICK,
                        bound.getValue(), listener, population.registry(),
                        population.jobs(), population.identity(),
                        population.system().factionStandings(),
                        population.relationships(), barks, population.questRegistry(),
                        population.system().questLog(), topic);
                assertNotEquals(TalkText.SAYS_NOTHING, asked.barkLine(),
                        bound.getKey() + " asked about " + topic.symbol()
                                + " resolved to silence");
                assertTrue(asked.contextLine().equals("[personal]")
                                || asked.contextLine().equals("[rumor]"),
                        "asked tag reads the ask family, got " + asked.contextLine());
                // Purity: the identical ask serves the identical line.
                TalkText.Exchange twin = TalkText.ask(loaded.worldSeed(), TICK,
                        bound.getValue(), listener, population.registry(),
                        population.jobs(), population.identity(),
                        population.system().factionStandings(),
                        population.relationships(), barks, population.questRegistry(),
                        population.system().questLog(), topic);
                assertEquals(asked, twin, "asking must be a pure function");
            }
        }
    }

    @Test
    void widowsPaperPartiesLeadWithTheMarkedQuestRow() {
        // Withy is both a Widow's-Paper stage-1 party AND the ward's widest gossip — the
        // densest surface in the game: quest row first (marked), then personal, then the
        // eight granted stories = exactly the ten-key cap.
        int withy = notables.get("withy");
        List<TalkTopics.Topic> topics = topicsOf(withy, 0);
        assertEquals(TalkTopics.Kind.QUEST, topics.get(0).kind());
        assertTrue(topics.get(0).questMarked());
        assertEquals("The Widow's Paper", topics.get(0).label());
        assertEquals(TalkTopics.Kind.PERSONAL, topics.get(1).kind());
        assertEquals("their own story", topics.get(1).label());
        assertEquals(9, topics.size(),
                "withy: quest beat + personal + her 7 granted stories");
    }

    @Test
    void storyCaptionsAreHumanLinesBuiltFromTheCatalog() {
        // netter-fenner-debt: kind=debt, a=fenner (creditor), b=netter.
        String label = topicsOf(notables.get("netter"), 0).stream()
                .filter(t -> "netter-fenner-debt".equals(t.symbol()))
                .findFirst().orElseThrow().label();
        assertTrue(label.startsWith("the paper "),
                "debt captions read 'the paper A holds on B', got: " + label);
        assertTrue(label.contains(" holds on "), label);
        assertFalse(label.contains("netter-fenner-debt"), "captions are prose, not ids");
    }

    @Test
    void forgedSoulsOfferNoTopicRows() {
        int forged = -1;
        for (int i = 0; i < population.registry().size(); i++) {
            if (!notables.containsValue(i)
                    && population.registry().get(i).typeId().key().equals("serf")) {
                forged = i;
                break;
            }
        }
        assertTrue(forged >= 0);
        assertEquals(List.of(), topicsOf(forged, 0),
                "an un-storied soul keeps the plain greet-only panel");
    }

    @Test
    void theQuestRowServesTheBeatExchangeVerbatim() {
        // Asking the marked row must reproduce the S3 beat exchange exactly (marker,
        // [*] tag, the quest table's line) — the same content the greet already serves.
        int netter = notables.get("netter");
        List<TalkTopics.Topic> topics = topicsOf(netter, 0);
        TalkTopics.Topic quest = topics.get(0);
        assertEquals(TalkTopics.Kind.QUEST, quest.kind());
        TalkText.Exchange asked = TalkText.ask(loaded.worldSeed(), TICK, netter, 0,
                population.registry(), population.jobs(), population.identity(),
                population.system().factionStandings(), population.relationships(), barks,
                population.questRegistry(), population.system().questLog(), quest);
        TalkText.Exchange greet = TalkText.greet(loaded.worldSeed(), TICK, netter, 0,
                population.registry(), population.jobs(), population.identity(),
                population.system().factionStandings(), population.relationships(), barks,
                population.questRegistry(), population.system().questLog());
        assertEquals(greet, asked, "the quest row re-serves the beat exchange");
        assertTrue(asked.nameLine().startsWith(TalkText.QUEST_MARK));
        assertEquals(TalkText.QUEST_TAG, asked.contextLine());
    }

    @Test
    void askingNeverMovesTheSimHash() {
        // The sim-silence proof through the WHOLE client path: twin systems tick in
        // lockstep; one gets a barrage of topic asks mid-run, the other stays untouched —
        // the hardened ACTORS-section hashes must match anyway (the presentation-lane
        // contract BarkSelector documents, exercised end to end).
        FixtureWorldLoader.Loaded loadedA = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation a = DocksPopulation.build(loadedA.worldSeed(), loadedA.world());
        com.trojia.client.time.SimulationDriver driverA =
                new com.trojia.client.time.SimulationDriver(loadedA.world(),
                        loadedA.worldSeed(),
                        List.<com.trojia.sim.engine.SimulationSystem>of(a.system()));
        FixtureWorldLoader.Loaded loadedB = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation b = DocksPopulation.build(loadedB.worldSeed(), loadedB.world());
        com.trojia.client.time.SimulationDriver driverB =
                new com.trojia.client.time.SimulationDriver(loadedB.world(),
                        loadedB.worldSeed(),
                        List.<com.trojia.sim.engine.SimulationSystem>of(b.system()));
        int withy = notables.get("withy");
        for (int t = 1; t <= 100; t++) {
            driverA.requestStep();
            driverB.requestStep();
            // Ask soul A's densest surface every tick of the second half.
            if (t >= 50) {
                for (TalkTopics.Topic topic : TalkTopics.topicsFor(a.registry().get(withy),
                        0, a.askTopicsOf(withy), a.topicCatalog(), a.questRegistry(),
                        a.system().questLog(), a.registry(), a.identity())) {
                    TalkText.ask(loadedA.worldSeed(), t, withy, 0, a.registry(), a.jobs(),
                            a.identity(), a.system().factionStandings(), a.relationships(),
                            barks, a.questRegistry(), a.system().questLog(), topic);
                }
            }
        }
        assertEquals(actorsHash(b.system()), actorsHash(a.system()),
                "asking topics must never perturb the running twin");
    }

    private static long actorsHash(com.trojia.sim.actor.ActorsSystem system) {
        com.trojia.sim.world.io.WorldHasher hasher = new com.trojia.sim.world.io.WorldHasher();
        system.hashInto(hasher.sectionSink(system.id()));
        return hasher.sectionHash(system.id());
    }
}
