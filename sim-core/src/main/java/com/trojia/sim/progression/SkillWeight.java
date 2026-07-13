package com.trojia.sim.progression;

/**
 * One skill's contribution weight to a derived attribute
 * (PROGRESSION-SPEC.md &sect;5). A skill may appear in more than one
 * attribute's weight row &mdash; e.g. Skyrunning feeds both AGI (64) and VIG
 * (20) &mdash; so this is a flat {@code (skillId, weight)} pair, not a
 * one-skill-one-attribute mapping.
 *
 * @param skillKey the skill's raws string id (resolved against a {@link SkillRegistry})
 * @param weight   the contribution weight; weights for one attribute sum to exactly 128
 */
public record SkillWeight(String skillKey, int weight) {
}
