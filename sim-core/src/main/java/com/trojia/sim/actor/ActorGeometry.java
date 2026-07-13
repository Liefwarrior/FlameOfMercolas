package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * Pure cell-math helpers shared by actor movement/perception (ACTORS-SPEC.md
 * §2.4-§2.5). Actors move on one z-level (top-down tile grid, DECISIONS.md);
 * Chebyshev distance is computed over x/y only.
 */
public final class ActorGeometry {

    private ActorGeometry() {
    }

    /** Chebyshev (x/y) distance between two {@code PackedPos} cells. */
    public static int chebyshev(int cellA, int cellB) {
        int dx = Math.abs(PackedPos.x(cellA) - PackedPos.x(cellB));
        int dy = Math.abs(PackedPos.y(cellA) - PackedPos.y(cellB));
        return Math.max(dx, dy);
    }
}
