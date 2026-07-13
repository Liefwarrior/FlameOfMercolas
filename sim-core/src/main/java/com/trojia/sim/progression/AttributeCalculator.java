package com.trojia.sim.progression;

import java.util.List;

/**
 * Computes derived attributes as a pure function of current skill levels
 * (PROGRESSION-SPEC.md &sect;5): {@code attr = 10 + ((sum(skill_i * w_i)) >>
 * 8)}. Deliberately stateless: callers recompute on every
 * {@link SkillLevelledEvent} rather than banking a value (&sect;5's
 * "recomputed live" rule, which kills the rush-Endurance meta).
 *
 * <p>The right-shift is parenthesized deliberately per the spec's own
 * warning: unparenthesized {@code 10 + sum >> 8} would parse as
 * {@code (10 + sum) >> 8} in Java.</p>
 */
public final class AttributeCalculator {

    private AttributeCalculator() {
    }

    /**
     * Presence (PRS): a static constant, never derived, never trainable,
     * never lowerable (&sect;5 &mdash; the north-star guard: the Wielder's
     * social weight is a constant of the world). No code path in this package
     * computes or mutates this value; it is not one of {@link AttributeId}'s
     * enum constants because it has no weight row.
     */
    public static final int PRESENCE = 100;

    /**
     * Computes one attribute from a track's current skill levels.
     *
     * @param attribute the attribute to compute
     * @param registry  the registry the track is sized against (resolves weight-row skill keys)
     * @param track     the entity's current skill levels
     * @return the attribute value, {@code 10..60} at maximum skill levels
     */
    public static int compute(AttributeId attribute, SkillRegistry registry, SkillTrack track) {
        List<SkillWeight> weights = AttributeWeights.weights(attribute);
        int weightedSum = 0;
        for (SkillWeight weight : weights) {
            SkillId skillId = registry.id(weight.skillKey());
            weightedSum += track.level(skillId) * weight.weight();
        }
        return 10 + (weightedSum >> 8);
    }
}
