package com.trojia.client.scenario;

import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The Sprint-2 micro-history bake (S2-1): resolves each authored history's parties from
 * notables.json ids to their spawn-site-bound ActorIds and realizes the story as a real
 * {@link RelationshipRegistry} edge — BEFORE the first tick, so the edges are ordinary
 * bake-immutable sim state riding the existing serialize/load/hashInto triad and the
 * twin-run identity gates. Draw-free and file-ordered: a pure function of (finished bake,
 * committed raws), so twin bakes realize identical edges.
 *
 * <p>The companion {@link #bioAddenda} map feeds {@link NameForge}: each party's addendum
 * sentence is appended after its authored notable bio, in file order — how a feud reads on
 * the character sheet while the edge itself feeds BarkSelector/Barter.
 */
final class MicroHistoryBake {

    /** One realized history: the authored raw plus its two resolved ActorIds. */
    record Bound(HistoryRaws.History history, int actorA, int actorB) {
    }

    private MicroHistoryBake() {
    }

    /**
     * Realizes {@code histories} as relationship edges (symmetric for NEIGHBOR/FRIEND,
     * directed a→b for MENTOR) and returns the bound list for bio addenda and tests.
     *
     * @throws IllegalStateException when a party names an unbound notable id
     */
    static List<Bound> bake(List<HistoryRaws.History> histories,
            Map<String, Integer> notableActors, RelationshipRegistry relationships) {
        List<Bound> bound = new ArrayList<>(histories.size());
        for (HistoryRaws.History history : histories) {
            int actorA = resolve(history, history.a(), notableActors);
            int actorB = resolve(history, history.b(), notableActors);
            if (history.edge() == RelationshipKind.MENTOR) {
                relationships.addDirected(actorA, actorB, RelationshipKind.MENTOR);
            } else {
                relationships.addSymmetric(actorA, actorB, history.edge());
            }
            bound.add(new Bound(history, actorA, actorB));
        }
        return List.copyOf(bound);
    }

    /**
     * The per-actor bio addenda: every involved party's sentences joined in file order —
     * what {@link NameForge} appends after the authored notable bio.
     */
    static Map<Integer, String> bioAddenda(List<Bound> bound) {
        Map<Integer, String> addenda = new HashMap<>();
        for (Bound b : bound) {
            addenda.merge(b.actorA(), b.history().bioA(), (prior, next) -> prior + " " + next);
            addenda.merge(b.actorB(), b.history().bioB(), (prior, next) -> prior + " " + next);
        }
        return addenda;
    }

    private static int resolve(HistoryRaws.History history, String notableId,
            Map<String, Integer> notableActors) {
        Integer actorId = notableActors.get(notableId);
        if (actorId == null) {
            throw new IllegalStateException("history \"" + history.id()
                    + "\" names unbound notable \"" + notableId + "\"");
        }
        return actorId;
    }
}
