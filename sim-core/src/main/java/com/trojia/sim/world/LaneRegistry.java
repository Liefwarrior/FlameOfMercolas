package com.trojia.sim.world;

import java.util.ArrayList;
import java.util.List;

/**
 * Mints and resolves the dense lanes of one world. Instance-owned by the
 * {@link WorldBuilder} — there is deliberately no static lane table, so two
 * engines in one JVM can carry different lane sets (ARCHITECTURE.md §3).
 *
 * <p>Registration order is the canonical lane order for chunk layout, hashing
 * and saves. The registry is sealed when the world is built; late registration
 * is a programming error. Iteration by ascending {@link LaneId#index()} is the
 * only sanctioned iteration order.
 */
public final class LaneRegistry {

    private final List<LaneId> lanes = new ArrayList<>();
    private boolean sealed;

    /** Creates an empty registry; {@link WorldBuilder} registers the core lanes first. */
    public LaneRegistry() {
    }

    /**
     * Registers a lane and returns its handle. Names must be unique;
     * {@code bytesPerTile} must be 1 or 2. Throws {@code IllegalStateException}
     * once sealed, {@code IllegalArgumentException} on a duplicate name.
     */
    public LaneId register(String name, int bytesPerTile) {
        if (sealed) {
            throw new IllegalStateException("lane registry is sealed: cannot register " + name);
        }
        if (name != null && contains(name)) {
            throw new IllegalArgumentException("duplicate lane name: " + name);
        }
        LaneId lane = new LaneId(lanes.size(), name, bytesPerTile);
        lanes.add(lane);
        return lane;
    }

    /** The lane registered under {@code name}; throws {@code IllegalArgumentException} if absent. */
    public LaneId byName(String name) {
        for (int i = 0; i < lanes.size(); i++) {
            LaneId lane = lanes.get(i);
            if (lane.name().equals(name)) {
                return lane;
            }
        }
        throw new IllegalArgumentException("no lane registered under name: " + name);
    }

    /** The lane with registration ordinal {@code index} (0..count-1). */
    public LaneId byIndex(int index) {
        if (index < 0 || index >= lanes.size()) {
            throw new IllegalArgumentException(
                    "lane index out of range [0, " + lanes.size() + "): " + index);
        }
        return lanes.get(index);
    }

    /** Number of registered lanes. */
    public int count() {
        return lanes.size();
    }

    /** Whether a lane named {@code name} is registered. */
    public boolean contains(String name) {
        for (int i = 0; i < lanes.size(); i++) {
            if (lanes.get(i).name().equals(name)) {
                return true;
            }
        }
        return false;
    }

    /** Forbids further registration; called exactly once by {@link WorldBuilder#build()}. */
    public void seal() {
        if (sealed) {
            throw new IllegalStateException("lane registry is already sealed");
        }
        sealed = true;
    }
}
