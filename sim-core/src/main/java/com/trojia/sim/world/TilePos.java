package com.trojia.sim.world;

/**
 * Unpacked world-tile coordinates for cold paths (config, tests, tooling,
 * logging). Hot paths never allocate these — they carry {@link PackedPos} ints.
 *
 * @param x world tile x (0..4095)
 * @param y world tile y (0..4095)
 * @param z world tile z (0..63)
 */
public record TilePos(int x, int y, int z) {

    /** The 30-bit packed form of this position. */
    public int pack() {
        return PackedPos.pack(x, y, z);
    }

    /** Unpacks a 30-bit packed position. */
    public static TilePos ofPacked(int packedPos) {
        return new TilePos(PackedPos.x(packedPos), PackedPos.y(packedPos), PackedPos.z(packedPos));
    }
}
