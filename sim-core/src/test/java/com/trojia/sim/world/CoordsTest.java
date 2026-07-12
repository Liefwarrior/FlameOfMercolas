package com.trojia.sim.world;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Audit of the chunk/tile index bit math: localIdx formula checked literally,
 * chunkIndex↔grid and (chunkIndex, localIdx)↔packedPos round-trips exhausted
 * over a full small world, and the VOID border predicate recomputed from
 * first principles.
 */
final class CoordsTest {

    private static final WorldConfig CONFIG = new WorldConfig(4, 3, 3);
    private static final Coords COORDS = Coords.of(CONFIG);

    @Test
    void chunkGeometryConstants() {
        assertEquals(32, Coords.CHUNK_SIZE_X);
        assertEquals(32, Coords.CHUNK_SIZE_Y);
        assertEquals(8, Coords.CHUNK_SIZE_Z);
        assertEquals(8192, Coords.TILES_PER_CHUNK);
        assertEquals(4 * 3 * 3, COORDS.chunkCount());
        assertEquals(CONFIG.chunkCount(), COORDS.chunkCount());
    }

    @Test
    void localIdxMatchesTheSpecFormulaExhaustivelyOverOneChunk() {
        for (int z = 0; z < 8; z++) {
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    int expected = (z << 10) | (y << 5) | x;
                    // Same local tile in two different chunks: identical localIdx.
                    assertEquals(expected, COORDS.localIdx(PackedPos.pack(x, y, z)));
                    assertEquals(expected,
                            COORDS.localIdx(PackedPos.pack(96 + x, 64 + y, 16 + z)));
                }
            }
        }
    }

    @Test
    void chunkGridRoundTripsExhaustively() {
        int seen = 0;
        for (int cz = 0; cz < 3; cz++) {
            for (int cy = 0; cy < 3; cy++) {
                for (int cx = 0; cx < 4; cx++) {
                    int chunkIndex = COORDS.chunkIndexOf(cx, cy, cz);
                    assertEquals(cx, COORDS.chunkX(chunkIndex));
                    assertEquals(cy, COORDS.chunkY(chunkIndex));
                    assertEquals(cz, COORDS.chunkZ(chunkIndex));
                    assertTrue(chunkIndex >= 0 && chunkIndex < COORDS.chunkCount());
                    seen++;
                }
            }
        }
        assertEquals(COORDS.chunkCount(), seen);
    }

    @Test
    void packedPosRoundTripsExhaustivelyOverTheWholeWorld() {
        for (int chunkIndex = 0; chunkIndex < COORDS.chunkCount(); chunkIndex++) {
            for (int localIdx = 0; localIdx < Coords.TILES_PER_CHUNK; localIdx++) {
                int packedPos = COORDS.packedPos(chunkIndex, localIdx);
                if (COORDS.chunkIndex(packedPos) != chunkIndex
                        || COORDS.localIdx(packedPos) != localIdx) {
                    assertEquals(chunkIndex + "/" + localIdx,
                            COORDS.chunkIndex(packedPos) + "/" + COORDS.localIdx(packedPos));
                }
            }
        }
    }

    @Test
    void chunkIndexOfPackedPosMatchesGridDerivation() {
        for (int z = 0; z < 3 * 8; z++) {
            for (int y = 0; y < 3 * 32; y += 7) {
                for (int x = 0; x < 4 * 32; x += 5) {
                    int pos = PackedPos.pack(x, y, z);
                    assertEquals(COORDS.chunkIndexOf(x >>> 5, y >>> 5, z >>> 3),
                            COORDS.chunkIndex(pos));
                }
            }
        }
    }

    @Test
    void voidBorderIsExactlyTheOuterRing() {
        for (int cz = 0; cz < 3; cz++) {
            for (int cy = 0; cy < 3; cy++) {
                for (int cx = 0; cx < 4; cx++) {
                    boolean expected = cx == 0 || cx == 3 || cy == 0 || cy == 2
                            || cz == 0 || cz == 2;
                    assertEquals(expected, COORDS.isVoidBorder(COORDS.chunkIndexOf(cx, cy, cz)),
                            "chunk (" + cx + "," + cy + "," + cz + ")");
                }
            }
        }
    }

    @Test
    void maximumWorldRoundTripsAtTheCorners() {
        Coords max = Coords.of(new WorldConfig(WorldConfig.MAX_CHUNKS_XY,
                WorldConfig.MAX_CHUNKS_XY, WorldConfig.MAX_CHUNKS_Z));
        int last = max.chunkCount() - 1;
        assertEquals(128 * 128 * 8, max.chunkCount());
        assertEquals(127, max.chunkX(last));
        assertEquals(127, max.chunkY(last));
        assertEquals(7, max.chunkZ(last));
        int lastLocal = Coords.TILES_PER_CHUNK - 1;
        int pos = max.packedPos(last, lastLocal);
        // The far corner of the maximum world is the far corner of PackedPos space.
        assertEquals(PackedPos.pack(4095, 4095, 63), pos);
        assertEquals(last, max.chunkIndex(pos));
        assertEquals(lastLocal, max.localIdx(pos));
    }
}
