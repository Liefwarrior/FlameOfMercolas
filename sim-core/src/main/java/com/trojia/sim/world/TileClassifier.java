package com.trojia.sim.world;

/**
 * Derives the writer-maintained FLAGS bits ({@link FlagBits#BLOCKS_MOVE},
 * {@link FlagBits#BLOCKS_LIGHT}) from a tile's material + form; consulted by
 * {@link ChunkWriter} on every material/form write and by world construction
 * for the initial fill. Worlds are built with {@link #formOnly()} until the
 * material registry (M1) supplies a material-aware policy via
 * {@link WorldBuilder#classifier}.
 *
 * <p>Implementations must be pure, allocation-free and deterministic — the
 * derived bits are sim state and feed the golden-master hash.
 */
public interface TileClassifier {

    /** Whether a tile of {@code materialId} in {@code form} blocks movement. */
    boolean blocksMove(short materialId, TileForm form);

    /** Whether a tile of {@code materialId} in {@code form} blocks light. */
    boolean blocksLight(short materialId, TileForm form);

    /**
     * The material-agnostic default policy: {@link TileForm#WALL} and
     * {@link TileForm#VOID} block both movement and light; every other form
     * blocks neither.
     */
    static TileClassifier formOnly() {
        return FormOnlyClassifier.INSTANCE;
    }
}
