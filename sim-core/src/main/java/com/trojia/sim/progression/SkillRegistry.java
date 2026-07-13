package com.trojia.sim.progression;

import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Objects;

/**
 * The immutable, boot-built skill universe (PROGRESSION-SPEC.md &sect;2).
 * Mirrors {@code com.trojia.sim.material.MaterialRegistry}'s conventions:
 *
 * <p><strong>Deterministic id assignment:</strong> skills are sorted by
 * string id ({@link String#compareTo}) and numbered 0..n-1 &mdash; the same
 * raws always yield the same {@link SkillId}s on every platform.</p>
 *
 * <p>Deeply immutable and safe to share across engines and threads.</p>
 */
public final class SkillRegistry {

    private final SkillDefinition[] byId;
    private final String[] sortedKeys;

    private SkillRegistry(SkillDefinition[] byId) {
        this.byId = byId;
        this.sortedKeys = new String[byId.length];
        for (int i = 0; i < byId.length; i++) {
            sortedKeys[i] = byId[i].key();
        }
    }

    /**
     * Builds a registry from the given definitions. Input order is
     * irrelevant: ids are assigned from the sorted key order.
     *
     * @param skills the skill definitions; keys must be unique
     * @return the immutable registry
     * @throws NullPointerException     if {@code skills} or an element is {@code null}
     * @throws IllegalArgumentException if a key is duplicated, or more than
     *                                  32768 skills are supplied
     */
    public static SkillRegistry of(Collection<SkillDefinition> skills) {
        Objects.requireNonNull(skills, "skills");
        if (skills.size() > Short.MAX_VALUE + 1) {
            throw new IllegalArgumentException("too many skills for a short id: " + skills.size());
        }
        SkillDefinition[] byId = skills.toArray(new SkillDefinition[0]);
        Arrays.sort(byId, (a, b) -> a.key().compareTo(b.key()));
        for (int i = 1; i < byId.length; i++) {
            if (byId[i].key().equals(byId[i - 1].key())) {
                throw new IllegalArgumentException("duplicate skill key: " + byId[i].key());
            }
        }
        return new SkillRegistry(byId);
    }

    /** Returns the number of skills. */
    public int size() {
        return byId.length;
    }

    /**
     * Returns whether a skill with the given string id exists.
     *
     * @param key the string id
     * @return {@code true} iff present
     */
    public boolean contains(String key) {
        return Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key")) >= 0;
    }

    /**
     * Resolves a string id to its assigned numeric id.
     *
     * @param key the string id
     * @return the assigned id
     * @throws IllegalArgumentException if no such skill exists
     */
    public SkillId id(String key) {
        int index = Arrays.binarySearch(sortedKeys, Objects.requireNonNull(key, "key"));
        if (index < 0) {
            throw new IllegalArgumentException("unknown skill id: " + key);
        }
        return SkillId.of(index);
    }

    /**
     * Returns the definition for an assigned id.
     *
     * @param id the assigned id
     * @return the definition
     * @throws IllegalArgumentException if the id was not assigned by this registry
     */
    public SkillDefinition get(SkillId id) {
        return get(id.raw());
    }

    /**
     * Returns the definition at a raw id index.
     *
     * @param index the raw id, {@code 0..size()-1}
     * @return the definition
     * @throws IllegalArgumentException if {@code index} is out of range
     */
    public SkillDefinition get(int index) {
        if (index < 0 || index >= byId.length) {
            throw new IllegalArgumentException("skill id out of range: " + index);
        }
        return byId[index];
    }

    /** Returns all skills in id order (index == raw id); immutable. */
    public List<SkillDefinition> all() {
        return List.of(byId);
    }
}
