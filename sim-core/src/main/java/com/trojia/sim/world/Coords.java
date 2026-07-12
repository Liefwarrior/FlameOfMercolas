package com.trojia.sim.world;

/**
 * All chunk/tile index bit math for one world's dimensions, audited once
 * (ARCHITECTURE.md §8): chunks are 32×32×8 tiles, {@code localIdx =
 * (z<<10)|(y<<5)|x} with chunk-local coordinates, and the flat {@code Chunk[]}
 * is indexed by {@code chunkIndex = (cz * chunksY + cy) * chunksX + cx} —
 * ascending chunkIndex is THE canonical chunk order for every side-effectful
 * iteration, hash, and save.
 *
 * <p>Pure integer math; no validation on hot paths. Instances are immutable
 * and owned by the world (no static world size).
 */
public final class Coords {

    /** Tiles per chunk along x. */
    public static final int CHUNK_SIZE_X = 32;
    /** Tiles per chunk along y. */
    public static final int CHUNK_SIZE_Y = 32;
    /** Tiles per chunk along z. */
    public static final int CHUNK_SIZE_Z = 8;
    /** Tiles per chunk: 32*32*8 = 8192. */
    public static final int TILES_PER_CHUNK = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;

    private final int chunksX;
    private final int chunksY;
    private final int chunksZ;

    private Coords(int chunksX, int chunksY, int chunksZ) {
        this.chunksX = chunksX;
        this.chunksY = chunksY;
        this.chunksZ = chunksZ;
    }

    /** Bit math bound to {@code config}'s dimensions (border included). */
    public static Coords of(WorldConfig config) {
        return new Coords(config.chunksX(), config.chunksY(), config.chunksZ());
    }

    /** Total chunk count: the length of the flat {@code Chunk[]}. */
    public int chunkCount() {
        return chunksX * chunksY * chunksZ;
    }

    /** The chunkIndex containing a packed tile position. */
    public int chunkIndex(int packedPos) {
        int cx = PackedPos.x(packedPos) >>> 5;
        int cy = PackedPos.y(packedPos) >>> 5;
        int cz = PackedPos.z(packedPos) >>> 3;
        return chunkIndexOf(cx, cy, cz);
    }

    /** The chunkIndex of chunk-grid coordinates (no bounds check). */
    public int chunkIndexOf(int cx, int cy, int cz) {
        return (cz * chunksY + cy) * chunksX + cx;
    }

    /** Chunk-grid x of a chunkIndex. */
    public int chunkX(int chunkIndex) {
        return chunkIndex % chunksX;
    }

    /** Chunk-grid y of a chunkIndex. */
    public int chunkY(int chunkIndex) {
        return (chunkIndex / chunksX) % chunksY;
    }

    /** Chunk-grid z of a chunkIndex. */
    public int chunkZ(int chunkIndex) {
        return chunkIndex / (chunksX * chunksY);
    }

    /**
     * The tile's index within its chunk's dense lanes:
     * {@code (localZ<<10)|(localY<<5)|localX}, range [0, 8192).
     */
    public int localIdx(int packedPos) {
        int lx = PackedPos.x(packedPos) & (CHUNK_SIZE_X - 1);
        int ly = PackedPos.y(packedPos) & (CHUNK_SIZE_Y - 1);
        int lz = PackedPos.z(packedPos) & (CHUNK_SIZE_Z - 1);
        return (lz << 10) | (ly << 5) | lx;
    }

    /** Reconstructs the packed world position of {@code localIdx} within {@code chunkIndex}. */
    public int packedPos(int chunkIndex, int localIdx) {
        int x = (chunkX(chunkIndex) << 5) | (localIdx & 31);
        int y = (chunkY(chunkIndex) << 5) | ((localIdx >>> 5) & 31);
        int z = (chunkZ(chunkIndex) << 3) | ((localIdx >>> 10) & 7);
        return PackedPos.pack(x, y, z);
    }

    /**
     * Whether {@code chunkIndex} lies on the immutable VOID border ring (any
     * face). Border chunks are in-bounds and readable; {@link ChunkWriter}
     * rejects every write into them.
     */
    public boolean isVoidBorder(int chunkIndex) {
        int cx = chunkX(chunkIndex);
        int cy = chunkY(chunkIndex);
        int cz = chunkZ(chunkIndex);
        return cx == 0 || cx == chunksX - 1
                || cy == 0 || cy == chunksY - 1
                || cz == 0 || cz == chunksZ - 1;
    }
}
