package com.trojia.client.world;

/**
 * Mutable current-z cursor for the observer's z-scrub keys, clamped to a fixed inclusive
 * world z-range (M1 Behavior 3, DoD7). Pure state, no libGDX dependency — the input
 * layer calls {@link #up()}/{@link #down()} once per key press, the renderer reads
 * {@link #z()} once per frame.
 *
 * <p>Moving past either bound is a no-op rather than a crash or a wraparound: {@link #up()}
 * saturates at {@link #maxZ()}, {@link #down()} saturates at {@link #minZ()}.
 */
public final class ZLevelCursor {

    private final int minZ;
    private final int maxZ;
    private int z;

    /**
     * @param minZ     inclusive lower bound
     * @param maxZ     inclusive upper bound
     * @param initialZ starting z, clamped into {@code [minZ, maxZ]} if given out of range
     * @throws IllegalArgumentException if {@code minZ > maxZ}
     */
    public ZLevelCursor(int minZ, int maxZ, int initialZ) {
        if (minZ > maxZ) {
            throw new IllegalArgumentException("minZ (" + minZ + ") must be <= maxZ (" + maxZ + ")");
        }
        this.minZ = minZ;
        this.maxZ = maxZ;
        this.z = clamp(initialZ);
    }

    /** The current z-level. */
    public int z() {
        return z;
    }

    /** The inclusive lower bound. */
    public int minZ() {
        return minZ;
    }

    /** The inclusive upper bound. */
    public int maxZ() {
        return maxZ;
    }

    /** Moves one z-level up (toward higher z); a no-op already at {@link #maxZ()}. */
    public int up() {
        z = clamp(z + 1);
        return z;
    }

    /** Moves one z-level down (toward lower z); a no-op already at {@link #minZ()}. */
    public int down() {
        z = clamp(z - 1);
        return z;
    }

    /**
     * Jumps directly to {@code targetZ}, clamped into {@code [minZ, maxZ]} (the
     * follow-camera snaps the viewed floor to the followed actor's z when it changes
     * levels). Out-of-range targets saturate at the nearer bound rather than throwing.
     */
    public int to(int targetZ) {
        z = clamp(targetZ);
        return z;
    }

    private int clamp(int value) {
        return Math.max(minZ, Math.min(maxZ, value));
    }
}
