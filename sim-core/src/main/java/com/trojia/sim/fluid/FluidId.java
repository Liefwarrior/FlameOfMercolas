package com.trojia.sim.fluid;

/**
 * A registry-assigned fluid identity, stored in the FLUID lane's id bits
 * (ARCHITECTURE.md §3). Ids are assigned deterministically by
 * {@link FluidRegistry}: fluids sorted by string id, first key = 0.
 *
 * <p>The fluid id namespace is distinct from the material id namespace; only
 * <em>string</em> substance references ({@code meltsTo}/{@code freezesTo})
 * cross the two (BLESSING-QUEUE ruling 3).</p>
 *
 * @param raw the lane value; {@code >= 0}
 */
public record FluidId(short raw) implements Comparable<FluidId> {

    /**
     * @throws IllegalArgumentException if {@code raw} is negative
     */
    public FluidId {
        if (raw < 0) {
            throw new IllegalArgumentException("fluid id must be non-negative: " + raw);
        }
    }

    /**
     * Creates an id from an int index, range-checked to the short lane width.
     *
     * @param index the registry index, {@code 0..32767}
     * @return the id
     * @throws IllegalArgumentException if {@code index} is out of range
     */
    public static FluidId of(int index) {
        if (index < 0 || index > Short.MAX_VALUE) {
            throw new IllegalArgumentException("fluid id out of short range: " + index);
        }
        return new FluidId((short) index);
    }

    /**
     * Orders ids by their numeric value — identical to the lexicographic order
     * of the string ids they were assigned from.
     */
    @Override
    public int compareTo(FluidId other) {
        return Short.compare(raw, other.raw);
    }
}
