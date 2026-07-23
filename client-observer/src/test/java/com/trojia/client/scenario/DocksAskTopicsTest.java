package com.trojia.client.scenario;

import com.trojia.client.boot.FixtureWorldLoader;
import com.trojia.client.boot.RepoPaths;
import com.trojia.sim.actor.BarkSelector;
import com.trojia.sim.bark.BarkRawsLoader;
import com.trojia.sim.bark.BarkTableRegistry;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-4 WORLD "rumor verb" DoD gate: the 55-table dead-content pile (every
 * {@code personal.<notableId>} monologue + every {@code gossip.<historyId>} story authored
 * in S2 and never speakable by anything) is WIRED — over the real docks bake, every one of
 * those tables is served by {@code BarkSelector.selectAsk} for some (speaker, tick), every
 * emission resolves to authored text, forged souls stay silent, and the whole surface is a
 * pure function of {@code (worldSeed, tick, speaker, topics)} (twin emissions identical).
 * The topic lists themselves come from {@link AskTopicsBake}: parties know their own
 * stories, {@code rumors.json} knowledge-domains grant the rest.
 */
class DocksAskTopicsTest {

    /** Ticks sampled per speaker — plenty to rotate the largest topic list (deterministic). */
    private static final int SAMPLE_TICKS = 256;

    private static FixtureWorldLoader.Loaded loaded;
    private static DocksPopulation population;
    private static BarkTableRegistry tables;
    private static Map<String, Integer> notables;
    private static List<HistoryRaws.History> histories;

    @BeforeAll
    static void bake() {
        loaded = FixtureWorldLoader.loadDocksSurface();
        population = DocksPopulation.build(loaded.worldSeed(), loaded.world());
        Path rawsRoot = RepoPaths.locate("content", "raws");
        tables = BarkRawsLoader.load(rawsRoot);
        notables = NameForge.bindNotableActors(population.registry(), population.homes(),
                NotableRaws.load(rawsRoot.resolve("names").resolve("notables.json")),
                DocksPopulation.notableSpawnSites());
        histories = HistoryRaws.load(rawsRoot.resolve("names").resolve("histories.json"));
    }

    @Test
    void everyBoundNotableCarriesItsOwnTopicsAndPartiesKnowTheirOwnStories() {
        Set<String> historyIds = new HashSet<>();
        for (HistoryRaws.History history : histories) {
            historyIds.add(history.id());
        }
        for (Map.Entry<String, Integer> bound : notables.entrySet()) {
            AskTopicsBake.Topics topics = population.askTopicsOf(bound.getValue());
            assertNotNull(topics, "notable \"" + bound.getKey() + "\" has no ask-topics");
            assertEquals(bound.getKey(), topics.notableId());
            for (String id : topics.historyIds()) {
                assertTrue(historyIds.contains(id),
                        "\"" + bound.getKey() + "\" carries unknown history \"" + id + "\"");
            }
        }
        for (HistoryRaws.History history : histories) {
            assertTrue(population.askTopicsOf(notables.get(history.a())).historyIds()
                    .contains(history.id()), history.a() + " must know " + history.id());
            assertTrue(population.askTopicsOf(notables.get(history.b())).historyIds()
                    .contains(history.id()), history.b() + " must know " + history.id());
        }
    }

    /**
     * THE dead-table gate, both directions: every authored {@code personal.*}/{@code
     * gossip.*} table is speakable by some soul's topic list, and everything the topic
     * lists can emit has an authored SPECIFIC table (never just the fallback floor).
     */
    @Test
    void noAuthoredPersonalOrGossipTableIsDeadContent() {
        Set<String> speakable = new TreeSet<>();
        for (Integer actorId : notables.values()) {
            AskTopicsBake.Topics topics = population.askTopicsOf(actorId);
            speakable.add("personal." + topics.notableId());
            for (String id : topics.historyIds()) {
                speakable.add("gossip." + id);
            }
        }
        for (String key : tables.keys()) {
            boolean topicTable = (key.startsWith("personal.") || key.startsWith("gossip."));
            if (topicTable) {
                assertTrue(speakable.contains(key),
                        "authored table " + key + " is DEAD CONTENT — no soul can speak it");
            }
        }
        for (String key : speakable) {
            assertTrue(tables.contains(key),
                    "speakable topic " + key + " has no authored specific table");
        }
    }

    /**
     * Emission proof over the real bake: sampling {@link #SAMPLE_TICKS} ticks per notable
     * body, the selector serves ONLY that soul's speakable keys, every emission resolves
     * to authored text, twin calls are identical — and across the roster the sampled keys
     * cover the entire speakable surface (the 55 tables are not merely reachable in
     * theory; the pinned topic lane actually rotates onto each one).
     */
    @Test
    void selectAskServesEveryTableAndOnlyTheSpeakerOwnStories() {
        Set<String> speakableUnion = new TreeSet<>();
        Set<String> emitted = new TreeSet<>();
        for (Map.Entry<String, Integer> bound : notables.entrySet()) {
            AskTopicsBake.Topics topics = population.askTopicsOf(bound.getValue());
            Set<String> own = new HashSet<>();
            own.add("personal." + topics.notableId());
            for (String id : topics.historyIds()) {
                own.add("gossip." + id);
            }
            speakableUnion.addAll(own);
            com.trojia.sim.actor.Actor speaker = population.registry().get(bound.getValue());
            for (long tick = 0; tick < SAMPLE_TICKS; tick++) {
                BarkSelector.BarkChoice choice = BarkSelector.selectAsk(loaded.worldSeed(),
                        tick, speaker, topics.notableId(), topics.historyIds());
                assertNotNull(choice, "a storied soul must never be ask-silent");
                assertTrue(own.contains(choice.tableKey()),
                        bound.getKey() + " spoke a story it does not know: "
                                + choice.tableKey());
                String text = choice.resolve(tables);
                assertTrue(text != null && !text.isBlank(),
                        choice.tableKey() + " resolved to silence");
                BarkSelector.BarkChoice twin = BarkSelector.selectAsk(loaded.worldSeed(),
                        tick, speaker, topics.notableId(), topics.historyIds());
                assertEquals(choice.tableKey(), twin.tableKey(), "ask must be pure");
                assertEquals(choice.rowDraw(), twin.rowDraw(), "ask must be pure");
                emitted.add(choice.tableKey());
            }
        }
        assertEquals(speakableUnion, emitted,
                "every speakable table must actually be SERVED within the sample window");
    }

    @Test
    void forgedSoulsStayAskSilent() {
        int forged = -1;
        for (int i = 0; i < population.registry().size(); i++) {
            if (!notables.containsValue(i)
                    && population.registry().get(i).typeId().key().equals("serf")) {
                forged = i;
                break;
            }
        }
        assertTrue(forged >= 0, "no forged serf found");
        assertNull(population.askTopicsOf(forged),
                "a forged soul carries no baked ask-topics");
        assertNull(BarkSelector.selectAsk(loaded.worldSeed(), 17,
                population.registry().get(forged), null, List.of()),
                "the selector stays silent for an un-storied soul");
    }

    /** Twin bakes compile identical topic maps (boot determinism for authored content). */
    @Test
    void twinBakesCompileIdenticalTopicLists() {
        FixtureWorldLoader.Loaded twinLoaded = FixtureWorldLoader.loadDocksSurface();
        DocksPopulation twin = DocksPopulation.build(twinLoaded.worldSeed(), twinLoaded.world());
        for (Integer actorId : notables.values()) {
            assertEquals(population.askTopicsOf(actorId), twin.askTopicsOf(actorId),
                    "twin bakes disagree on actor " + actorId + "'s topics");
        }
    }
}
