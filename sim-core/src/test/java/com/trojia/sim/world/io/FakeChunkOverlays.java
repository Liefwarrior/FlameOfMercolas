package com.trojia.sim.world.io;

import com.trojia.sim.world.OverlayId;

import java.util.EnumMap;
import java.util.Map;
import java.util.TreeMap;

/**
 * Sorted-map-backed {@link ChunkOverlays} test fake: cells are kept in
 * ascending localIdx order regardless of {@link #put} order, matching the
 * canonical overlay order the codec and hasher require.
 */
final class FakeChunkOverlays implements ChunkOverlays {

    private final EnumMap<OverlayId, TreeMap<Integer, Integer>> cells =
            new EnumMap<>(OverlayId.class);

    @Override
    public int size(OverlayId overlay) {
        TreeMap<Integer, Integer> map = cells.get(overlay);
        return map == null ? 0 : map.size();
    }

    @Override
    public int localIdxAt(OverlayId overlay, int i) {
        return entryAt(overlay, i).getKey();
    }

    @Override
    public int valueAt(OverlayId overlay, int i) {
        return entryAt(overlay, i).getValue();
    }

    @Override
    public void put(OverlayId overlay, int localIdx, int value) {
        cells.computeIfAbsent(overlay, k -> new TreeMap<>()).put(localIdx, value);
    }

    private Map.Entry<Integer, Integer> entryAt(OverlayId overlay, int i) {
        TreeMap<Integer, Integer> map = cells.get(overlay);
        if (map == null || i < 0 || i >= map.size()) {
            throw new IndexOutOfBoundsException("no cell " + i + " for " + overlay);
        }
        int k = 0;
        for (Map.Entry<Integer, Integer> entry : map.entrySet()) {
            if (k++ == i) {
                return entry;
            }
        }
        throw new AssertionError("unreachable");
    }
}
