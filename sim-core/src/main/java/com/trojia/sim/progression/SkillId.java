package com.trojia.sim.progression;

/**
 * A registry-assigned skill identity. Ids are assigned deterministically by
 * {@link SkillRegistry}: skills sorted by string id, first key = 0 &mdash; so
 * the same raws always yield the same numeric ids on every platform and every
 * run. Mirrors {@code com.trojia.sim.material.MaterialId}.
 *
 * <p>The id is only meaningful relative to the registry that assigned it.</p>
 *
 * @param raw the raw index; {@code >= 0}
 */
public record SkillId(short raw) implements Comparable<SkillId> {

    /**
     * @throws IllegalArgumentException if {@code raw} is negative
     */
    public SkillId {
        if (raw < 0) {
            throw new IllegalArgumentException("skill id must be non-negative: " + raw);
        }
    }

    /**
     * Creates an id from an int index, range-checked to the short lane width.
     *
     * @param index the registry index, {@code 0..32767}
     * @return the id
     * @throws IllegalArgumentException if {@code index} is out of range
     */
    public static SkillId of(int index) {
        if (index < 0 || index > Short.MAX_VALUE) {
            throw new IllegalArgumentException("skill id out of short range: " + index);
        }
        return new SkillId((short) index);
    }

    /** Orders ids by their numeric value. */
    @Override
    public int compareTo(SkillId other) {
        return Short.compare(raw, other.raw);
    }
}
