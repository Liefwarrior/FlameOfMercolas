package com.trojia.sim.material;

/**
 * A registry-assigned material identity, stored in the 2-byte MATERIAL lane
 * (ARCHITECTURE.md §3, §8). Ids are assigned deterministically by
 * {@link MaterialRegistry}: materials sorted by string id, first key = 0 —
 * so the same raws always yield the same numeric ids on every platform and
 * every run.
 *
 * <p>The id is only meaningful relative to the registry that assigned it;
 * saves guard against mixups via the raws fingerprint (ARCHITECTURE.md §9).</p>
 *
 * @param raw the lane value; {@code >= 0} (the sign bit is never used, so the
 *            id round-trips safely through the unsigned-flavored lane)
 */
public record MaterialId(short raw) implements Comparable<MaterialId> {

    /**
     * @throws IllegalArgumentException if {@code raw} is negative
     */
    public MaterialId {
        if (raw < 0) {
            throw new IllegalArgumentException("material id must be non-negative: " + raw);
        }
    }

    /**
     * Creates an id from an int index, range-checked to the short lane width.
     *
     * @param index the registry index, {@code 0..32767}
     * @return the id
     * @throws IllegalArgumentException if {@code index} is out of range
     */
    public static MaterialId of(int index) {
        if (index < 0 || index > Short.MAX_VALUE) {
            throw new IllegalArgumentException("material id out of short range: " + index);
        }
        return new MaterialId((short) index);
    }

    /**
     * Orders ids by their numeric value — identical to the lexicographic order
     * of the string ids they were assigned from.
     */
    @Override
    public int compareTo(MaterialId other) {
        return Short.compare(raw, other.raw);
    }
}
