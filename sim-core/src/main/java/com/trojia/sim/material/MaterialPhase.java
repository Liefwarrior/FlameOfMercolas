package com.trojia.sim.material;

/**
 * The physical phase a grid material occupies as declared in its raw
 * (ARCHITECTURE.md §10 {@code "phase"}). This is the material's <em>intrinsic</em>
 * phase, not a per-tile state: molten chromatis is a distinct {@code LIQUID}
 * material minted by melting, never a hot variant of the solid.
 *
 * <p>Ordinals are save-format-stable — append only, never reorder. Gas-phase
 * grid materials do not exist in the v0 vocabulary (steam/aether is a reserved
 * fluid-side seam, BLESSING-QUEUE ruling 3).</p>
 */
public enum MaterialPhase {

    /** A rigid tile filler; the overwhelming default. */
    SOLID,

    /** A molten/pooled grid material (e.g. {@code chromatis_melt}). */
    LIQUID;

    private static final MaterialPhase[] VALUES = values();

    /**
     * Decodes a stored ordinal; inverse of {@link #ordinal()}.
     *
     * @param ordinal the stored ordinal byte
     * @return the phase
     * @throws ArrayIndexOutOfBoundsException if {@code ordinal} is not a valid ordinal
     */
    public static MaterialPhase ofOrdinal(int ordinal) {
        return VALUES[ordinal];
    }
}
