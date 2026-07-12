package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** Bounds contract of the immutable world dimensions (border included). */
final class WorldConfigTest {

    @Test
    void acceptsTheMinimumAndMaximumWorlds() {
        assertEquals(27, new WorldConfig(3, 3, 3).chunkCount());
        WorldConfig max = new WorldConfig(WorldConfig.MAX_CHUNKS_XY, WorldConfig.MAX_CHUNKS_XY,
                WorldConfig.MAX_CHUNKS_Z);
        assertEquals(128 * 128 * 8, max.chunkCount());
    }

    @Test
    void maximaFitThePackedPosAddressSpace() {
        // 4096×4096×64 tiles total (border included): every tile of the maximum
        // world — border ring included — must be addressable by a 30-bit PackedPos.
        assertEquals(128, WorldConfig.MAX_CHUNKS_XY);
        assertEquals(8, WorldConfig.MAX_CHUNKS_Z);
        assertEquals(PackedPos.X_MASK + 1, WorldConfig.MAX_CHUNKS_XY * Coords.CHUNK_SIZE_X);
        assertEquals(PackedPos.Z_MASK + 1, WorldConfig.MAX_CHUNKS_Z * Coords.CHUNK_SIZE_Z);
    }

    @Test
    void rejectsDimensionsBelowTheBorderMinimum() {
        assertThrows(IllegalArgumentException.class, () -> new WorldConfig(2, 3, 3));
        assertThrows(IllegalArgumentException.class, () -> new WorldConfig(3, 2, 3));
        assertThrows(IllegalArgumentException.class, () -> new WorldConfig(3, 3, 2));
    }

    @Test
    void rejectsDimensionsAboveTheMaximum() {
        assertThrows(IllegalArgumentException.class,
                () -> new WorldConfig(WorldConfig.MAX_CHUNKS_XY + 1, 3, 3));
        assertThrows(IllegalArgumentException.class,
                () -> new WorldConfig(3, WorldConfig.MAX_CHUNKS_XY + 1, 3));
        assertThrows(IllegalArgumentException.class,
                () -> new WorldConfig(3, 3, WorldConfig.MAX_CHUNKS_Z + 1));
    }
}
