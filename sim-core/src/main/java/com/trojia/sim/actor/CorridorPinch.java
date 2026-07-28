package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * The <em>corridor-pinch</em> predicate (S7 guard-routing pass): is a cell so narrow that
 * two bodies standing on/next to it cannot pass each other?
 *
 * <p>Formally: a cell's <em>walkable extent</em> along an axis is the count of contiguous
 * walkable cells through it on that axis (the cell itself plus its unbroken walkable run
 * in both directions). A cell is <b>pinched</b> when {@code min(nsExtent, ewExtent) <= 1} —
 * i.e. at least one axis has BOTH neighbours blocked. That is exactly the 1-wide alleyway
 * / 1x1 cage signature Eli reported ("guards patrol 1-tile-wide alleyways"), and it is the
 * filter the original pre-S6 guard-jam diagnosis applied. Extent 1 needs no scan: both
 * neighbours on that axis being unwalkable IS extent 1, so this is four probes, no loop,
 * no allocation.
 *
 * <p>One predicate, two consumers by design: the observer's guard-jam instrument scores
 * adjacency ON pinched ground (so "the bug" is measured, not all guard proximity), and
 * {@link JobBehaviors}'s square-beat corner retarget REJECTS pinched corners (so a beat
 * stops parking its corner inside a cage). Pure geometry — no draws, no state, no
 * iteration order.
 */
public final class CorridorPinch {

    private CorridorPinch() {
    }

    /**
     * {@code true} when {@code cell} sits on ground whose narrower axis is one cell wide
     * (or the cell itself is unwalkable, which is narrower still). Same-z only: actors
     * move on one band, so the extents are measured in x/y.
     */
    public static boolean isPinched(int cell, Actor.WalkabilityQuery walk) {
        if (!walk.isWalkable(cell)) {
            return true;
        }
        int x = PackedPos.x(cell);
        int y = PackedPos.y(cell);
        int z = PackedPos.z(cell);
        boolean ewOpen = walk.isWalkable(PackedPos.pack(x - 1, y, z))
                || walk.isWalkable(PackedPos.pack(x + 1, y, z));
        if (!ewOpen) {
            return true; // east-west extent is 1: a north-south gut
        }
        boolean nsOpen = walk.isWalkable(PackedPos.pack(x, y - 1, z))
                || walk.isWalkable(PackedPos.pack(x, y + 1, z));
        return !nsOpen; // north-south extent is 1: an east-west gut
    }
}
