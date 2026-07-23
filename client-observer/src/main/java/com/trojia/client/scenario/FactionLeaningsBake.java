package com.trojia.client.scenario;

import com.trojia.sim.actor.FactionStandings;

import java.util.List;
import java.util.Map;

/**
 * The Sprint-2 faction-leaning bake: applies {@code leanings.json}'s authored standing
 * seeds to the wired {@link FactionStandings} ledger on each notable's spawn-site-bound
 * actor id — BEFORE the first tick, so seeds are ordinary bake state inside the persisted
 * triad, and BarkSelector's attitude buckets read them from tick zero (a Merchant-hostile
 * wastrel is greeted cold at the counters on day one). Draw-free, file-ordered,
 * deterministic.
 */
final class FactionLeaningsBake {

    private FactionLeaningsBake() {
    }

    /**
     * Seeds every authored leaning via {@link FactionStandings#adjust} (clamped, no-op-safe).
     *
     * @throws IllegalStateException on an unbound notable or a faction key absent from the
     *                               wired registry (the fail-fast raws contract)
     */
    static void apply(List<LeaningRaws.Leaning> leanings, Map<String, Integer> notableActors,
            FactionStandings standings) {
        if (!standings.isWired()) {
            throw new IllegalStateException("leanings.json cannot seed an UNWIRED standings ledger");
        }
        for (LeaningRaws.Leaning leaning : leanings) {
            Integer actorId = notableActors.get(leaning.notable());
            if (actorId == null) {
                throw new IllegalStateException("leanings.json names unbound notable \""
                        + leaning.notable() + "\"");
            }
            if (!standings.factions().contains(leaning.faction())) {
                throw new IllegalStateException("leanings.json names unknown faction \""
                        + leaning.faction() + "\" (not in factions.json)");
            }
            standings.adjust(actorId, standings.factions().rawId(leaning.faction()),
                    leaning.standing());
        }
    }
}
