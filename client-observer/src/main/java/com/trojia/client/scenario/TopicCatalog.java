package com.trojia.client.scenario;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The presentation-side story catalog (Sprint 4 "talk topics"): per authored micro-history
 * id, the flavor {@code kind} ({@code feud|debt|romance|pact|secret}) and the two bound
 * party bodies — exactly what the talk surface needs to caption a {@code gossip.<id>}
 * topic row as a human line ("the paper Fenner holds on Tarry Jek") without the client
 * re-reading raws. Built once at bake from the micro-history bind (file order), immutable,
 * rides no save and no hash — captioning a topic can never perturb a running simulation.
 */
public final class TopicCatalog {

    /** One story's caption inputs: flavor + the two party bodies (TRUE ActorIds). */
    public record Story(String kind, int actorA, int actorB) {
    }

    /** No stories (un-storied fixtures): every lookup returns {@code null}. */
    public static final TopicCatalog EMPTY = new TopicCatalog(Map.of());

    private final Map<String, Story> stories;

    private TopicCatalog(Map<String, Story> stories) {
        this.stories = Map.copyOf(stories);
    }

    /** The caption inputs for {@code historyId}, or {@code null} when unknown. */
    public Story story(String historyId) {
        return stories.get(historyId);
    }

    /** Builds the catalog from the bake's bound histories (package-private bake types). */
    static TopicCatalog of(List<MicroHistoryBake.Bound> bound) {
        Map<String, Story> stories = new LinkedHashMap<>();
        for (MicroHistoryBake.Bound b : bound) {
            stories.put(b.history().id(),
                    new Story(b.history().kind(), b.actorA(), b.actorB()));
        }
        return new TopicCatalog(stories);
    }
}
