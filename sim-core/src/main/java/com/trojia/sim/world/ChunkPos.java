package com.trojia.sim.world;

/**
 * Unpacked chunk-grid coordinates for cold paths. Hot paths carry the flat
 * {@code int chunkIndex} (see {@link Coords#chunkIndexOf(int, int, int)});
 * this record exists for config, tests, tooling and logging.
 *
 * @param cx chunk-grid x (0..chunksX-1, border included)
 * @param cy chunk-grid y (0..chunksY-1, border included)
 * @param cz chunk-grid z (0..chunksZ-1, border included)
 */
public record ChunkPos(int cx, int cy, int cz) {

    /** The flat chunkIndex of this chunk under {@code coords}' dimensions. */
    public int index(Coords coords) {
        return coords.chunkIndexOf(cx, cy, cz);
    }

    /** Unpacks a flat chunkIndex under {@code coords}' dimensions. */
    public static ChunkPos ofIndex(Coords coords, int chunkIndex) {
        return new ChunkPos(coords.chunkX(chunkIndex), coords.chunkY(chunkIndex),
                coords.chunkZ(chunkIndex));
    }
}
