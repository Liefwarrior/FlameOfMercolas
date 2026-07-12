package com.trojia.sim.world;

/**
 * Bit masks of the FLAGS lane (1 byte per tile). The derived bits
 * ({@link #BLOCKS_MOVE}, {@link #BLOCKS_LIGHT}) are maintained automatically
 * by {@link ChunkWriter} on every material/form write; {@link #ON_FIRE} is
 * owned by the fire system. Remaining bits are reserved.
 */
public final class FlagBits {

    /** Derived: the tile blocks movement (from material + form). Writer-maintained. */
    public static final int BLOCKS_MOVE = 1;

    /** Derived: the tile blocks light (from material opacity + form). Writer-maintained. */
    public static final int BLOCKS_LIGHT = 1 << 1;

    /** The tile is burning. Sole writer: the fire system. */
    public static final int ON_FIRE = 1 << 2;

    /** Mask of all currently defined bits; the rest of the byte is reserved. */
    public static final int DEFINED_MASK = BLOCKS_MOVE | BLOCKS_LIGHT | ON_FIRE;

    private FlagBits() {
    }
}
