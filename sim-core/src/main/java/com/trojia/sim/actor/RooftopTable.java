package com.trojia.sim.actor;

import com.trojia.sim.world.PackedPos;

/**
 * The baked rooftop-region table (Sprint 1 progression wiring): the authored roof planes of
 * a scenario as inclusive world-coordinate boxes {@code (x0..x1, y0..y1, z0..z1)}. The one
 * consumer this sprint is {@link PlayerControlPolicy}'s skyrunning hook: a PLAYED actor
 * committing a step onto a rooftop cell earns skyrunning use-XP ("rooftop runs" are the
 * skill's own raws-listed covers) — the lead-ruled exception to PROGRESSION-SPEC &sect;3.2's
 * no-locomotion-XP rule, priced at the spec's smallest base award and satiation-bounded per
 * roof region so circling one roofline decays to the 25% floor like any other grind.
 *
 * <p>Immutable baked config (a {@link CivicFixtures} member, the {@link RestrictedZoneTable}
 * precedent): carries no mutable state, rides no save. {@link #EMPTY} means "no roofs wired"
 * — the hook never fires (world-less bootstrap, tests, scenarios without authored roofs).
 */
public final class RooftopTable {

    /** No rooftop regions: {@link #contains} is always false. */
    public static final RooftopTable EMPTY = new RooftopTable(new int[0]);

    /** Inclusive boxes, packed as {@code {x0,y0,z0, x1,y1,z1}} sextuples (world coords). */
    private final int[] boxes;

    /**
     * @param boxes inclusive world-coordinate boxes as {@code {x0,y0,z0, x1,y1,z1}}
     *              sextuples, low corner first
     * @throws IllegalArgumentException if the array is not a whole number of sextuples
     */
    public RooftopTable(int[] boxes) {
        if (boxes.length % 6 != 0) {
            throw new IllegalArgumentException(
                    "rooftop boxes must be {x0,y0,z0,x1,y1,z1} sextuples: " + boxes.length);
        }
        this.boxes = boxes.clone();
    }

    /** Number of baked rooftop boxes. */
    public int boxCount() {
        return boxes.length / 6;
    }

    /** {@code true} iff {@code cell} lies inside any baked rooftop box. */
    public boolean contains(int cell) {
        int x = PackedPos.x(cell);
        int y = PackedPos.y(cell);
        int z = PackedPos.z(cell);
        for (int i = 0; i < boxes.length; i += 6) {
            if (x >= boxes[i] && y >= boxes[i + 1] && z >= boxes[i + 2]
                    && x <= boxes[i + 3] && y <= boxes[i + 4] && z <= boxes[i + 5]) {
                return true;
            }
        }
        return false;
    }
}
