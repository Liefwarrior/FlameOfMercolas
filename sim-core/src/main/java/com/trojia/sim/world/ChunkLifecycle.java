package com.trojia.sim.world;

/**
 * The world's freeze/thaw hooks (ARCHITECTURE.md §1.1 #4): the bubble module's
 * ticket pipelines drive these — no simulation system may. Every
 * {@link TickableWorld} built by {@link WorldBuilder} also implements this
 * interface (obtain it by cast at wiring time).
 *
 * <p>Freezing keeps the chunk's storage resident (the readable
 * FROZEN_RESIDENT rind); reads stay legal while {@link ChunkWriter} rejects
 * every write with {@link ChunkWriter#REJECTED_FROZEN}. The VOID border ring
 * is permanent: it can be neither frozen nor thawed.
 */
public interface ChunkLifecycle {

    /** Whether {@code chunkIndex} is concrete — i.e. ChunkWriter writes are accepted. */
    boolean isConcrete(int chunkIndex);

    /** Whether {@code chunkIndex} is frozen (readable-resident or evicted). */
    boolean isFrozen(int chunkIndex);

    /** Whether {@code chunkIndex} lies on the immutable VOID border ring. */
    boolean isVoid(int chunkIndex);

    /**
     * Demotes a concrete chunk to FROZEN; storage stays resident and readable.
     * Idempotent. Throws {@code IllegalStateException} for a VOID chunk.
     */
    void freeze(int chunkIndex);

    /**
     * Promotes a frozen chunk back to concrete. Idempotent. Throws
     * {@code IllegalStateException} for a VOID chunk.
     */
    void thaw(int chunkIndex);
}
