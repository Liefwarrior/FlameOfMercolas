package com.trojia.sim.world.io;

import com.trojia.sim.world.ChunkView;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.LaneId;

import java.util.List;
import java.util.Random;

/**
 * Array-backed {@link ChunkView} test fake: plain primitive arrays per lane,
 * no world behind it. Mirrors the core lane layout of {@code Lanes} by default
 * so codec/hasher tests exercise realistic widths.
 */
final class FakeChunkView implements ChunkView {

    private final int chunkIndex;
    private final LaneId[] lanes;
    private final Object[] arrays;

    FakeChunkView(int chunkIndex, List<LaneId> lanesInOrder) {
        this.chunkIndex = chunkIndex;
        this.lanes = lanesInOrder.toArray(new LaneId[0]);
        this.arrays = new Object[lanes.length];
        for (int i = 0; i < lanes.length; i++) {
            arrays[i] = lanes[i].bytesPerTile() == 2
                    ? new short[Coords.TILES_PER_CHUNK]
                    : new byte[Coords.TILES_PER_CHUNK];
        }
    }

    /** The seven core lanes with the widths of ARCHITECTURE.md §8. */
    static List<LaneId> coreLanes() {
        return List.of(
                new LaneId(0, "material", 2),
                new LaneId(1, "form", 1),
                new LaneId(2, "flags", 1),
                new LaneId(3, "temperature", 2),
                new LaneId(4, "fluid", 2),
                new LaneId(5, "light", 2),
                new LaneId(6, "opacity", 1));
    }

    /** Fills every lane with reproducible pseudo-random content (runs + noise). */
    void fillRandom(long seed) {
        Random random = new Random(seed);
        for (int i = 0; i < lanes.length; i++) {
            if (arrays[i] instanceof short[] shorts) {
                int pos = 0;
                while (pos < shorts.length) {
                    int run = 1 + random.nextInt(random.nextBoolean() ? 200 : 3);
                    short value = (short) random.nextInt(0x10000);
                    for (int k = 0; k < run && pos < shorts.length; k++) {
                        shorts[pos++] = value;
                    }
                }
            } else {
                byte[] bytes = (byte[]) arrays[i];
                int pos = 0;
                while (pos < bytes.length) {
                    int run = 1 + random.nextInt(random.nextBoolean() ? 200 : 3);
                    byte value = (byte) random.nextInt(0x100);
                    for (int k = 0; k < run && pos < bytes.length; k++) {
                        bytes[pos++] = value;
                    }
                }
            }
        }
    }

    @Override
    public int chunkIndex() {
        return chunkIndex;
    }

    @Override
    public short[] shortLane(LaneId lane) {
        Object array = arrays[lane.index()];
        if (!(array instanceof short[] shorts)) {
            throw new IllegalArgumentException("lane '" + lane.name() + "' is not a short lane");
        }
        return shorts;
    }

    @Override
    public byte[] byteLane(LaneId lane) {
        Object array = arrays[lane.index()];
        if (!(array instanceof byte[] bytes)) {
            throw new IllegalArgumentException("lane '" + lane.name() + "' is not a byte lane");
        }
        return bytes;
    }
}
