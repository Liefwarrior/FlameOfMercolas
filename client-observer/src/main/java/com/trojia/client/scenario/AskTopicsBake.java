package com.trojia.client.scenario;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The Sprint-4 ask-topic bake ("the rumor verb"): compiles, per bound notable body, the
 * symbol lists the observer's talk surface passes to {@code BarkSelector.selectAsk} — the
 * speaker's own {@code notableId} (its {@code personal.<id>} monologue table) and the ids
 * of every micro-history it can SPEAK: the histories it is a party to, plus the ones its
 * authored {@link RumorRaws} knowledge-domain grants. This is the wiring that turns the
 * S2-authored {@code personal.*}/{@code gossip.*} bark tables from dead content into the
 * ward's speakable stories.
 *
 * <p><b>Deterministic and presentation-only.</b> A pure function of (bound histories in
 * file order, committed rumor domains, the notable bind map): per-actor history lists keep
 * {@code histories.json} file order, so twin bakes hand the selector identical topic lists
 * and the same {@code (seed, tick, speaker)} always serves the same line. Nothing here
 * touches sim state — the maps live client-side beside the bake, ride no save and no hash.
 *
 * <p><b>Fail-fast cross-validation</b> (the bake owns both vocabularies): a domain naming
 * an unknown history, an unknown knower, or a knower that is already one of the history's
 * own parties (parties are implicit — re-declaring one is authoring rot) fails the bake.
 */
public final class AskTopicsBake {

    /**
     * One soul's speakable ask-surface: {@code notableId} for its {@code personal.<id>}
     * table, {@code historyIds} for its {@code gossip.<id>} stories (file order). Every
     * bound notable gets an entry (an un-storied notable carries an empty history list —
     * its personal monologue is still speakable); forged souls get none (the selector
     * stays silent for them).
     */
    public record Topics(String notableId, List<String> historyIds) {
    }

    private AskTopicsBake() {
    }

    /**
     * Compiles the per-actor topic map from the micro-history bake's bound list (file
     * order, parties already resolved), the rumor domains, and the notable bind map.
     */
    static Map<Integer, Topics> bake(List<MicroHistoryBake.Bound> boundHistories,
            List<RumorRaws.Domain> domains, Map<String, Integer> notableActors) {
        // Every bound notable speaks at least its own personal table.
        Map<Integer, String> notableIdOf = new HashMap<>();
        Map<Integer, List<String>> historiesOf = new LinkedHashMap<>();
        for (Map.Entry<String, Integer> bound : notableActors.entrySet()) {
            notableIdOf.put(bound.getValue(), bound.getKey());
            historiesOf.put(bound.getValue(), new ArrayList<>());
        }
        // Index the domains by history id (the loader guarantees at most one row each).
        Map<String, RumorRaws.Domain> domainOf = new HashMap<>();
        for (RumorRaws.Domain domain : domains) {
            domainOf.put(domain.history(), domain);
        }
        // Walk histories in file order so every per-actor list inherits that order.
        for (MicroHistoryBake.Bound bound : boundHistories) {
            String id = bound.history().id();
            historiesOf.get(bound.actorA()).add(id);
            historiesOf.get(bound.actorB()).add(id);
            RumorRaws.Domain domain = domainOf.remove(id);
            if (domain == null) {
                continue;
            }
            for (String knower : domain.knowers()) {
                if (knower.equals(bound.history().a()) || knower.equals(bound.history().b())) {
                    throw new IllegalStateException("rumors.json: domain \"" + id
                            + "\" re-declares party \"" + knower
                            + "\" as a knower (parties are implicit)");
                }
                Integer actorId = notableActors.get(knower);
                if (actorId == null) {
                    throw new IllegalStateException("rumors.json: domain \"" + id
                            + "\" names unknown knower \"" + knower + "\"");
                }
                historiesOf.get(actorId).add(id);
            }
        }
        if (!domainOf.isEmpty()) {
            throw new IllegalStateException(
                    "rumors.json: domains name unknown histories " + domainOf.keySet());
        }
        Map<Integer, Topics> out = new LinkedHashMap<>();
        for (Map.Entry<Integer, List<String>> entry : historiesOf.entrySet()) {
            out.put(entry.getKey(), new Topics(notableIdOf.get(entry.getKey()),
                    List.copyOf(entry.getValue())));
        }
        return Map.copyOf(out);
    }
}
