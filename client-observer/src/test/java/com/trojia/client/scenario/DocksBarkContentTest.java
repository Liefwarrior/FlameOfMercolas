package com.trojia.client.scenario;

import com.trojia.client.boot.RepoPaths;
import com.trojia.sim.actor.BarkSelector;
import com.trojia.sim.bark.BarkRawsLoader;
import com.trojia.sim.bark.BarkTableRegistry;

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The Sprint-2 WORLD bark-content DoD gates: the COMMITTED {@code content/raws/barks/barks.json}
 * validates through the SIM's own loader (the schema contract), covers the selector's full
 * type x faction-standing vocabulary with time-band refinements, gives every one of the Forty
 * Notables personal lines, and resolves non-silently for every key {@code BarkSelector} can
 * ever emit (the sparse-fallback floor). Key vocabulary is linted so a typo'd table can never
 * sit unreachable in the file.
 */
class DocksBarkContentTest {

    private static final List<String> FAMILIES = List.of("serf", "wastrel", "watch", "clergy",
            "trade", "maritime", "husbandry", "beast", "flame_of_merc");
    private static final List<String> ATTITUDES = List.of("kin", "friend", "hostile", "cold",
            "warm", "neutral");
    private static final List<String> TIMES = List.of("morning", "day", "evening", "night");
    private static final List<String> MOODS = List.of("dead", "downed", "held", "confined",
            "panicked", "harried");
    /** The six spotlight notables the sprint ruling calls out for a HANDFUL of lines each. */
    private static final List<String> SPOTLIGHT = List.of("crell", "vess", "maell", "cobb",
            "gilt", "finch");

    private static Path rawsRoot;
    private static BarkTableRegistry tables;
    private static List<String> notableIds;
    private static Set<String> gossipKeys;

    @BeforeAll
    static void load() {
        rawsRoot = RepoPaths.locate("content", "raws");
        tables = BarkRawsLoader.load(rawsRoot);
        notableIds = NotableRaws.load(rawsRoot.resolve("names").resolve("notables.json"))
                .stream().map(NotableRaws.Notable::id).toList();
        gossipKeys = new HashSet<>();
        for (HistoryRaws.History history
                : HistoryRaws.load(rawsRoot.resolve("names").resolve("histories.json"))) {
            gossipKeys.add(history.gossip());
        }
    }

    @Test
    void theCommittedTablesLoadThroughTheSimLoader() {
        assertTrue(tables.size() >= 150,
                "the authored bark universe should be substantial, saw " + tables.size());
        for (String key : tables.keys()) {
            assertTrue(tables.rowCount(key) >= 2,
                    "table " + key + " needs at least 2 rows (anti-repeat variety)");
        }
    }

    @Test
    void everyMoodAndEveryFamilyAttitudePairIsAuthored() {
        for (String mood : MOODS) {
            assertTrue(tables.contains("mood." + mood), "missing mood." + mood);
        }
        for (String family : FAMILIES) {
            assertTrue(tables.contains("greet." + family),
                    "missing family floor greet." + family);
            for (String attitude : ATTITUDES) {
                assertTrue(tables.contains("greet." + family + "." + attitude),
                        "missing greet." + family + "." + attitude);
            }
            for (String time : TIMES) {
                assertTrue(tables.contains("greet." + family + ".neutral." + time),
                        "missing time band greet." + family + ".neutral." + time);
            }
        }
        // The two flavor-critical non-neutral time lanes of the sprint plan.
        for (String time : TIMES) {
            assertTrue(tables.contains("greet.watch.cold." + time),
                    "missing greet.watch.cold." + time);
            assertTrue(tables.contains("greet.trade.warm." + time),
                    "missing greet.trade.warm." + time);
        }
    }

    @Test
    void everyNotableHasPersonalLinesAndTheSpotlightSixHaveAHandful() {
        for (String id : notableIds) {
            assertTrue(tables.contains("personal." + id),
                    "notable \"" + id + "\" has no personal bark lines");
        }
        for (String id : SPOTLIGHT) {
            assertTrue(tables.rowCount("personal." + id) >= 4,
                    "spotlight notable \"" + id + "\" needs a handful of lines, saw "
                            + tables.rowCount("personal." + id));
        }
    }

    /** Every authored key is reachable vocabulary — no typo'd table can hide in the file. */
    @Test
    void everyAuthoredKeyIsLegalSelectorOrWorldVocabulary() {
        for (String key : tables.keys()) {
            assertTrue(isLegalKey(key), "unreachable/typo'd bark table key: " + key);
        }
        // And no gossip table is an orphan: each one is claimed by an authored history.
        for (String key : tables.keys()) {
            if (key.startsWith("gossip.")) {
                assertTrue(gossipKeys.contains(key),
                        "gossip table " + key + " is claimed by no authored history");
            }
        }
    }

    private static boolean isLegalKey(String key) {
        if (key.equals("personal") || key.equals("gossip")) {
            return true; // the World fallback floors
        }
        if (key.startsWith("mood.")) {
            return MOODS.contains(key.substring("mood.".length()));
        }
        if (key.startsWith("personal.")) {
            return notableIds.contains(key.substring("personal.".length()));
        }
        if (key.startsWith("gossip.")) {
            return true; // orphan check above pins these to histories.json
        }
        if (!key.startsWith("greet.")) {
            return false;
        }
        String[] parts = key.split("\\.", -1);
        // "flame_of_merc" contains no dots, so segments are exactly the schema's.
        if (parts.length < 2 || parts.length > 4 || !FAMILIES.contains(parts[1])) {
            return false;
        }
        if (parts.length >= 3 && !ATTITUDES.contains(parts[2])) {
            return false;
        }
        return parts.length < 4 || TIMES.contains(parts[3]);
    }

    /**
     * The fallback floor: whatever (family, attitude, time) the selector emits, the fallback
     * chain lands on authored text — the streets can never go silent mid-vocabulary.
     */
    @Test
    void everySelectableGreetAndMoodKeyResolvesToText() {
        for (String mood : MOODS) {
            assertResolves("mood." + mood);
        }
        for (String family : FAMILIES) {
            for (String attitude : ATTITUDES) {
                for (String time : TIMES) {
                    assertResolves("greet." + family + "." + attitude + "." + time);
                }
            }
        }
        for (String id : notableIds) {
            assertResolves("personal." + id);
        }
        for (String gossip : gossipKeys) {
            assertResolves(gossip);
        }
    }

    private static void assertResolves(String key) {
        for (long draw : new long[] {0, 1, Long.MAX_VALUE, -1}) {
            String text = new BarkSelector.BarkChoice(key, draw).resolve(tables);
            assertTrue(text != null && !text.isBlank(),
                    "key " + key + " (draw " + draw + ") resolved to silence");
        }
    }

    /** Twin loads of the committed file are canonical-order identical (boot determinism). */
    @Test
    void twinLoadsAreIdentical() {
        BarkTableRegistry twin = BarkRawsLoader.load(rawsRoot);
        assertEquals(tables.keys(), twin.keys());
        for (String key : tables.keys()) {
            assertEquals(tables.rowCount(key), twin.rowCount(key), "row count of " + key);
            for (int r = 0; r < tables.rowCount(key); r++) {
                assertEquals(tables.row(key, r), twin.row(key, r), key + "[" + r + "]");
            }
        }
    }
}
