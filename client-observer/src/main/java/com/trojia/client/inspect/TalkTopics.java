package com.trojia.client.inspect;

import com.trojia.client.scenario.AskTopicsBake;
import com.trojia.client.scenario.IdentityRegistry;
import com.trojia.client.scenario.TopicCatalog;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.quest.QuestLog;
import com.trojia.sim.actor.quest.QuestRegistry;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure content for the talk panel's TOPIC rows (Sprint 4 item 2, "choosing what to say"):
 * given the speaker's baked ask-surface ({@link AskTopicsBake.Topics} — its personal
 * monologue + the micro-histories it can speak) and the live quest state, builds the
 * numbered option list the panel offers — quest beat first (marked with the S3
 * {@link TalkText#QUEST_MARK} convention), then "their own story", then each known story
 * captioned as a human line off the {@link TopicCatalog}. GL-free so the exact rows are
 * unit-testable; {@code TalkPanelRenderer} only draws them.
 *
 * <p><b>Bounded.</b> At most {@link #MAX_TOPICS} rows (the number keys 1-9 then 0);
 * today's densest soul (Grandmother Withy: quest beat + personal + 7 granted stories)
 * offers nine — the docks gate asserts nothing authored ever exceeds the cap, so growth
 * past it fails a test instead of silently truncating.
 *
 * <p><b>Sim-silent.</b> Everything here reads baked config and live-but-read-only state;
 * building (or choosing) a topic never touches the sim.
 */
public final class TalkTopics {

    /** The number-key surface: 1-9 then 0 — ten rows, hard cap. */
    public static final int MAX_TOPICS = 10;

    /** What a topic asks about. */
    public enum Kind { QUEST, PERSONAL, GOSSIP }

    /**
     * One numbered topic row.
     *
     * @param kind        which ask family serves it
     * @param symbol      the notable id ({@code PERSONAL}) / history id ({@code GOSSIP});
     *                    {@code null} for {@code QUEST} (the beat resolves via the live
     *                    quest log, not a symbol)
     * @param label       the human caption (without its number)
     * @param questMarked whether the row renders the S3 quest marker
     */
    public record Topic(Kind kind, String symbol, String label, boolean questMarked) {
    }

    private TalkTopics() {
    }

    /**
     * Builds the topic rows {@code speaker} offers {@code listenerTrueId}: the live quest
     * beat (when this speaker serves one toward this listener — the exact
     * {@link TalkText#questBeatEntry} rule the greet already applies), the speaker's own
     * story, then its known stories in bake order. An un-storied soul with no live beat
     * gets an empty list (the panel shows plain greet-only hints, exactly as before).
     */
    public static List<Topic> topicsFor(Actor speaker, int listenerTrueId,
            AskTopicsBake.Topics askTopics, TopicCatalog catalog, QuestRegistry quests,
            QuestLog questLog, ActorRegistry registry, IdentityRegistry identity) {
        List<Topic> out = new ArrayList<>();
        int beatEntry = TalkText.questBeatEntry(quests, questLog, speaker, listenerTrueId);
        if (beatEntry >= 0) {
            int q = questLog.questOrdinalOf(beatEntry);
            out.add(new Topic(Kind.QUEST, null, quests.questTitle(q), true));
        }
        if (askTopics != null) {
            out.add(new Topic(Kind.PERSONAL, askTopics.notableId(), "their own story",
                    false));
            for (String historyId : askTopics.historyIds()) {
                if (out.size() >= MAX_TOPICS) {
                    break; // the docks gate asserts authored content never reaches here
                }
                out.add(new Topic(Kind.GOSSIP, historyId,
                        storyLabel(historyId, catalog, registry, identity), false));
            }
        }
        return List.copyOf(out);
    }

    /**
     * The human caption of one story, by its authored flavor — presented names (the
     * Persona rule), the ward's own register:
     * {@code feud} "the bad blood between A and B", {@code debt} "the paper A holds on B"
     * (A is the creditor by authoring convention), {@code romance} "A and B",
     * {@code pact} "the arrangement between A and B", {@code secret} "what A and B keep
     * quiet". An uncataloged id degrades to the raw id (never blank, never a throw).
     */
    static String storyLabel(String historyId, TopicCatalog catalog, ActorRegistry registry,
            IdentityRegistry identity) {
        TopicCatalog.Story story = catalog.story(historyId);
        if (story == null) {
            return historyId;
        }
        String a = PersonNames.fullNameOf(
                registry.get(story.actorA()).identity().presentedId(), registry, identity);
        String b = PersonNames.fullNameOf(
                registry.get(story.actorB()).identity().presentedId(), registry, identity);
        return switch (story.kind()) {
            case "feud" -> "the bad blood between " + a + " and " + b;
            case "debt" -> "the paper " + a + " holds on " + b;
            case "romance" -> a + " and " + b;
            case "pact" -> "the arrangement between " + a + " and " + b;
            case "secret" -> "what " + a + " and " + b + " keep quiet";
            default -> historyId; // an unknown future kind still captions something
        };
    }

    /** The on-screen number of row {@code index} (1-9 then 0 — the key that picks it). */
    public static int keyNumberOf(int index) {
        return (index + 1) % 10;
    }

    /** The row index a pressed number key picks ({@code 1..9} → 0..8, {@code 0} → 9). */
    public static int indexOfKeyNumber(int keyNumber) {
        return keyNumber == 0 ? 9 : keyNumber - 1;
    }
}
