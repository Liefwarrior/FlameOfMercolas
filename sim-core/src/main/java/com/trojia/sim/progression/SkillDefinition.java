package com.trojia.sim.progression;

import java.util.Objects;

/**
 * One immutable skill definition: PROGRESSION-SPEC.md &sect;2's per-skill row
 * in loader-normalized form. Mirrors {@code com.trojia.sim.material.Material}
 * as the content-record shape for a raws-driven registry.
 *
 * <p>The record deliberately carries no {@link SkillId}: numeric identity is
 * assigned by {@link SkillRegistry} from the sorted key order and would be
 * meaningless on a free-standing definition.</p>
 *
 * @param key                unique string id from the raw ({@code "id"} field), e.g. {@code "sidearms"}
 * @param displayName        human-readable name, e.g. {@code "Sidearms"}
 * @param covers             human-readable description of what the skill covers
 * @param governingAttribute the attribute this skill feeds (&sect;5), or {@link GoverningAttribute#NONE}
 * @param aptitudeTier       Gabri's fixed aptitude for this skill (&sect;2, &sect;3.1)
 */
public record SkillDefinition(
        String key,
        String displayName,
        String covers,
        GoverningAttribute governingAttribute,
        AptitudeTier aptitudeTier) {

    /**
     * @throws NullPointerException     if a required reference is {@code null}
     * @throws IllegalArgumentException if {@code key} is empty
     */
    public SkillDefinition {
        Objects.requireNonNull(key, "key");
        Objects.requireNonNull(displayName, "displayName");
        Objects.requireNonNull(covers, "covers");
        Objects.requireNonNull(governingAttribute, "governingAttribute");
        Objects.requireNonNull(aptitudeTier, "aptitudeTier");
        if (key.isEmpty()) {
            throw new IllegalArgumentException("skill key must be non-empty");
        }
    }
}
