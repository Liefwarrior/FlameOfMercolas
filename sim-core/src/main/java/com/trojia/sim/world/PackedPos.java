package com.trojia.sim.world;

/**
 * The 30-bit packed tile position {@code (z << 24) | (y << 12) | x} — the
 * lingua franca of every hot queue and event payload (ARCHITECTURE.md §1.1
 * #15). x and y are 12-bit (0..4095), z is 6-bit (0..63); all coordinates are
 * absolute world-tile coordinates including the VOID border.
 *
 * <p>Static pure bit math only, audited once; never store per-tile objects —
 * store these ints.
 */
public final class PackedPos {

    /** Bits carrying the x coordinate. */
    public static final int X_BITS = 12;
    /** Bits carrying the y coordinate. */
    public static final int Y_BITS = 12;
    /** Bits carrying the z coordinate. */
    public static final int Z_BITS = 6;

    /** Mask of the x field (also the maximum x). */
    public static final int X_MASK = (1 << X_BITS) - 1;
    /** Mask of the y field (also the maximum y). */
    public static final int Y_MASK = (1 << Y_BITS) - 1;
    /** Mask of the z field (also the maximum z). */
    public static final int Z_MASK = (1 << Z_BITS) - 1;

    private PackedPos() {
    }

    /**
     * Packs world-tile coordinates into a 30-bit int. Arguments must already be
     * in range (0..4095, 0..4095, 0..63); this hot-path method does not validate.
     */
    public static int pack(int x, int y, int z) {
        return (z << 24) | (y << 12) | x;
    }

    /** The x coordinate (0..4095) of a packed position. */
    public static int x(int pos) {
        return pos & X_MASK;
    }

    /** The y coordinate (0..4095) of a packed position. */
    public static int y(int pos) {
        return (pos >>> 12) & Y_MASK;
    }

    /** The z coordinate (0..63) of a packed position. */
    public static int z(int pos) {
        return (pos >>> 24) & Z_MASK;
    }

    /**
     * The packed position one step in {@code dir}. The caller guarantees the
     * result stays in bounds (the immutable VOID border makes one blind step
     * from any concrete tile safe); no wraparound check is performed.
     */
    public static int step(int pos, Dir dir) {
        return pos + (dir.dz() << 24) + (dir.dy() << 12) + dir.dx();
    }
}
