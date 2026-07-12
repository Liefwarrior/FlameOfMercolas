package com.trojia.sim.world;

/**
 * Handle to one registered dense lane. Instances are minted exclusively by a
 * {@link LaneRegistry} (no global lane table); the index is the lane's stable
 * position in registry order, which is also the canonical lane order for
 * hashing and saves.
 *
 * @param index        registration ordinal, dense from 0
 * @param name         canonical lower-case lane name (see {@link Lanes})
 * @param bytesPerTile lane cell width: 1 (byte lane) or 2 (short lane)
 */
public record LaneId(int index, String name, int bytesPerTile) {

    public LaneId {
        if (index < 0) {
            throw new IllegalArgumentException("lane index must be >= 0: " + index);
        }
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("lane name must be non-empty");
        }
        if (bytesPerTile != 1 && bytesPerTile != 2) {
            throw new IllegalArgumentException("bytesPerTile must be 1 or 2: " + bytesPerTile);
        }
    }
}
