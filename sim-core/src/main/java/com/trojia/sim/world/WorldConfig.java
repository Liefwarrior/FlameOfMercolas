package com.trojia.sim.world;

/**
 * Immutable world dimensions, counted in chunks and INCLUDING the permanent
 * 1-chunk immutable VOID border ring on every face (ARCHITECTURE.md §1.2,
 * world-edge ruling a). Interior (paintable) space is therefore
 * {@code (chunksX-2) × (chunksY-2) × (chunksZ-2)} chunks.
 *
 * <p>Maximum world: the full 30-bit {@link PackedPos} address space of
 * 4096×4096×64 tiles, border included — i.e. 128×128×8 chunks, leaving a
 * 4032×4032×48-tile paintable interior. (The packed format is the pinned
 * lingua franca of every hot queue; the border must stay addressable, so it
 * lives inside the 4096/64 range rather than outside it.) Chunk geometry is
 * fixed at 32×32×8 tiles (see {@link Coords}).
 *
 * @param chunksX chunk columns along x, border included (3..128)
 * @param chunksY chunk columns along y, border included (3..128)
 * @param chunksZ chunk layers along z, border included (3..8)
 */
public record WorldConfig(int chunksX, int chunksY, int chunksZ) {

    /** Maximum chunksX/chunksY, border included: the 4096-tile PackedPos range. */
    public static final int MAX_CHUNKS_XY = 4096 / Coords.CHUNK_SIZE_X;
    /** Maximum chunksZ, border included: the 64-tile PackedPos range. */
    public static final int MAX_CHUNKS_Z = 64 / Coords.CHUNK_SIZE_Z;

    public WorldConfig {
        requireRange("chunksX", chunksX, MAX_CHUNKS_XY);
        requireRange("chunksY", chunksY, MAX_CHUNKS_XY);
        requireRange("chunksZ", chunksZ, MAX_CHUNKS_Z);
    }

    /** Total chunk count, border included: the length of the flat {@code Chunk[]}. */
    public int chunkCount() {
        return chunksX * chunksY * chunksZ;
    }

    private static void requireRange(String name, int value, int max) {
        if (value < 3 || value > max) {
            throw new IllegalArgumentException(
                    name + " must be in [3, " + max + "] (border included): " + value);
        }
    }
}
