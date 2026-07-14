package com.trojia.client.face;

import java.util.Map;

/**
 * One archetype: a weighted <em>view</em> over the one shared face-part library
 * (FACES-SPEC §3.3, retained; {@code gearWeights} renamed {@code headwearWeights} by the
 * unified art spec §4.6). Never a private pool — archetypes shape pool weights, not pool
 * membership beyond tag multipliers.
 *
 * @param id              archetype id, {@code [a-z0-9_]+}
 * @param headwearWeights weights for the k=0 class draw; only present classes are
 *                        reachable, every present weight is &ge; 1
 * @param tagMultipliers  integer effective-weight multipliers by part tag; missing tag =
 *                        &times;1, {@code 0} excludes, small nonzero = rare-not-never
 */
public record FaceArchetype(String id, Map<HeadwearClass, Integer> headwearWeights,
                            Map<String, Integer> tagMultipliers) {

    public FaceArchetype {
        headwearWeights = Map.copyOf(headwearWeights);
        tagMultipliers = Map.copyOf(tagMultipliers);
    }

    /** The multiplier for {@code tag}, defaulting to &times;1 when absent. */
    public int multiplier(String tag) {
        Integer m = tagMultipliers.get(tag);
        return m == null ? 1 : m;
    }
}
