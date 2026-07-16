package com.trojia.sim.actor;

/**
 * The baked prison-cell side-table (Phase-2 STEP C, Pass 10): the ordered list of
 * K34 Guardhouse holding-cell floor cells, injected through the {@link ActorsSystem}
 * constructor exactly like {@code arrestHoldCell} — a fixed baked {@code int[]}, never
 * a runtime lane, so it carries no mutable state and rides no save.
 *
 * <p>This generalizes the single scalar {@code arrestHoldCell} into a registry: at
 * arrest {@link com.trojia.sim.actor.job.JobBehaviors} assigns the <em>lowest-free</em>
 * cell by a deterministic ascending scan (respecting {@link Actor#MAX_OCCUPANTS_PER_CELL}),
 * stamps it on the arrested actor ({@link Actor#assignedHoldCell()}), and {@link HeldPolicy}
 * escorts to that per-prisoner cell — so six simultaneous prisoners occupy six distinct cells
 * instead of piling on one. Dense {@code int[]}, ascending bake order — deterministic and
 * purity-gate clean (no {@code Map}/{@code Set}, no float).
 *
 * <p>{@link #EMPTY} is what the world-less bootstrap and any no-cells bake inject; with it
 * assignment finds no cell and custody degrades to the single {@code arrestHoldCell} (or, if
 * that is unset too, "hold in place") — the same graceful degradation the scalar had.
 */
public final class PrisonCellRegistry {

    /** The degraded empty registry the world-less/no-cells bake injects. */
    public static final PrisonCellRegistry EMPTY = new PrisonCellRegistry(new int[0]);

    private final int[] cells;

    public PrisonCellRegistry(int[] cells) {
        this.cells = cells.clone();
    }

    /** Number of holding cells. */
    public int size() {
        return cells.length;
    }

    /** The cell at dense index {@code index} (ascending bake order). */
    public int cellAt(int index) {
        return cells[index];
    }
}
