package com.trojia.sim.actor.quest;

import com.trojia.sim.actor.ActorRawsValidationException;

import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The quest raws loader's fail-fast matrix (Sprint 3): a PRESENT file validates strictly —
 * unknown fields, duplicate ids/keys, dangling {@code to} references, terminal stages with
 * triggers, dead-end stages, and undeclared vocabulary symbols all fail loudly; a missing
 * file degrades to {@link QuestRaws#EMPTY} (the {@code BarkRawsLoader} contract).
 */
final class QuestRawsLoaderTest {

    private static QuestRaws parse(String json) {
        return QuestRawsLoader.parse(json.getBytes(StandardCharsets.UTF_8));
    }

    private static void assertRejected(String json, String becauseOf) {
        ActorRawsValidationException e = assertThrows(ActorRawsValidationException.class,
                () -> parse(json));
        assertTrue(e.getMessage().contains(becauseOf),
                "expected failure naming '" + becauseOf + "', got: " + e.getMessage());
    }

    @Test
    void theFixtureQuestParsesCompletely() {
        QuestRaws raws = QuestTestFixtures.parseFixture();
        assertEquals(1, raws.quests().size());
        QuestRaws.Quest quest = raws.quests().get(0);
        assertEquals("test-quest", quest.id());
        assertEquals(QuestRaws.Binding.FIRST_TALKER, quest.binding());
        assertEquals(4, quest.stages().size());
        assertEquals("start", quest.stages().get(0).key());
        assertTrue(quest.stages().get(3).terminal());
        assertEquals(1, quest.stages().get(1).liftItems().size());
        assertEquals(2, quest.stages().get(1).advance().size());
        assertEquals(6, quest.stages().get(3).effects().size());
        QuestRaws.Trigger search = quest.stages().get(2).advance().get(0);
        assertEquals(QuestRaws.TriggerKind.SEARCH, search.kind());
        assertEquals(12, search.resist());
        assertEquals(25, search.retryTicks());
        assertEquals("token", search.keyItem());
        assertEquals("end", search.to());
    }

    @Test
    void aMissingFileDegradesToEmptyAndAnEmptyQuestListIsLegal() {
        assertEquals(0, QuestRawsLoader.load(Path.of("does", "not", "exist")).quests().size());
        assertEquals(0, parse("{\"id\": \"quests\", \"quests\": []}").quests().size());
    }

    @Test
    void malformedJsonAndUnknownFieldsFail() {
        assertRejected("{", "malformed JSON");
        assertRejected("{\"id\": \"quests\", \"quests\": [], \"surprise\": 1}", "surprise");
    }

    @Test
    void duplicateQuestIdsFail() {
        String twice = QuestTestFixtures.QUEST_JSON.replace(
                "\"quests\": [",
                "\"quests\": [" + questBody() + ",");
        assertRejected(twice, "duplicate quest id");
    }

    @Test
    void duplicateStageKeysFail() {
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"key\": \"middle\"",
                "\"key\": \"start\""), "duplicate stage key");
    }

    @Test
    void aTriggerNamingNoStageFails() {
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"to\": \"middle\"",
                "\"to\": \"nowhere\""), "names no stage");
    }

    @Test
    void aTerminalStageWithTriggersFails() {
        assertRejected(QuestTestFixtures.QUEST_JSON.replace(
                "\"terminal\": true,",
                "\"terminal\": true, \"advance\": "
                        + "[ {\"kind\": \"after_ticks\", \"ticks\": 5, \"to\": \"start\"} ],"),
                "terminal stage must not declare");
    }

    @Test
    void aDeadEndNonTerminalStageFails() {
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"terminal\": true,", ""),
                "at least one advance trigger");
    }

    @Test
    void anUndeclaredSymbolFails() {
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"party\": \"alice\"",
                "\"party\": \"mallory\""), "not declared");
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"zone\": \"yard\"",
                "\"zone\": \"cellar\""), "not declared");
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"contextCell\": \"chest\"",
                "\"contextCell\": \"attic\""), "not declared");
    }

    @Test
    void unknownKindsAndBadEnumsFail() {
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"kind\": \"talk\"",
                "\"kind\": \"bribe\""), "unknown trigger kind");
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"kind\": \"pay\"",
                "\"kind\": \"tip\""), "unknown effect kind");
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"binding\": \"first_talker\"",
                "\"binding\": \"lottery\""), "first_talker");
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"edge\": \"GRUDGE\"",
                "\"edge\": \"NEMESIS\""), "not a RelationshipKind");
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"direction\": \"mutual\"",
                "\"direction\": \"sideways\""), "mutual");
    }

    @Test
    void integerBoundsHold() {
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"retryTicks\": 25",
                "\"retryTicks\": 0"), "out of range");
        assertRejected(QuestTestFixtures.QUEST_JSON.replace("\"coins\": 40",
                "\"coins\": 0"), "out of range");
    }

    @Test
    void theCommittedQuestRawsLoadAndBindSymbolsConsistently() {
        // The committed content file (if authored yet) must parse under the same contract.
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null && !Files.isDirectory(dir.resolve("content").resolve("raws"))) {
            dir = dir.getParent();
        }
        if (dir == null) {
            return; // no repo raws visible from this working dir — nothing to check
        }
        QuestRaws raws = QuestRawsLoader.load(dir.resolve("content").resolve("raws"));
        for (QuestRaws.Quest quest : raws.quests()) {
            assertTrue(quest.stages().size() >= 1);
        }
    }

    /** The fixture's single quest object, as a raw JSON string (for the duplicate-id test). */
    private static String questBody() {
        String json = QuestTestFixtures.QUEST_JSON;
        int start = json.indexOf('{', json.indexOf("\"quests\": ["));
        int depth = 0;
        for (int i = start; i < json.length(); i++) {
            if (json.charAt(i) == '{') {
                depth++;
            } else if (json.charAt(i) == '}') {
                depth--;
                if (depth == 0) {
                    return json.substring(start, i + 1);
                }
            }
        }
        throw new IllegalStateException("unbalanced fixture JSON");
    }
}
