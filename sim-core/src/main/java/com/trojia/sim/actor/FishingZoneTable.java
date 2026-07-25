package com.trojia.sim.actor;

/**
 * The baked fishing-zone side-table (Sprint 6 fishing, Eli's bug 6): every place a fishing
 * spot MAY surface, authored by the World survey — each zone is 2-3 contiguous open-water
 * cells plus the ONE walkable cast cell (dock/quay/strand terrain adjacent to the water,
 * possibly one z above it — a pier deck over the harbor) a fisher stands on to work it.
 * Injected through {@link CivicFixtures} exactly like {@link PatrolRouteTable} — immutable
 * baked arrays, never a runtime lane, rides no save. Purity-gate clean.
 *
 * <p>Three size classes ({@link #SMALL}/{@link #MEDIUM}/{@link #LARGE} = inshore shingle /
 * pier-side / deep harbor) drive richness: larger spots resist harder, yield more per catch,
 * and live longer once surfaced ({@link FishingSpots}' constants). {@link #EMPTY} is what
 * zone-less bakes inject: no spot ever spawns and every fishing behavior degrades to its
 * spotless fallback.
 */
public final class FishingZoneTable {

    /** Size-class ordinals (append-only). */
    public static final int SMALL = 0;
    public static final int MEDIUM = 1;
    public static final int LARGE = 2;

    /** Number of size classes. */
    public static final int SIZE_CLASSES = 3;

    /** The degraded empty table (world-less bootstrap, pre-fishing bakes). */
    public static final FishingZoneTable EMPTY =
            new FishingZoneTable(new int[0], new int[0], new int[0][]);

    private final int[] sizeClasses;
    private final int[] castCells;
    private final int[][] waterCells;

    /**
     * @param sizeClasses per-zone size-class ordinal ({@link #SMALL}/{@link #MEDIUM}/{@link #LARGE})
     * @param castCells   per-zone walkable cast cell (the fisher's stand)
     * @param waterCells  per-zone 2-3 contiguous open-water cells (the spot's area)
     */
    public FishingZoneTable(int[] sizeClasses, int[] castCells, int[][] waterCells) {
        if (sizeClasses.length != castCells.length || sizeClasses.length != waterCells.length) {
            throw new IllegalArgumentException("zone arrays must be parallel: "
                    + sizeClasses.length + "/" + castCells.length + "/" + waterCells.length);
        }
        this.sizeClasses = sizeClasses.clone();
        this.castCells = castCells.clone();
        this.waterCells = new int[waterCells.length][];
        for (int i = 0; i < waterCells.length; i++) {
            if (waterCells[i].length < 1) {
                throw new IllegalArgumentException("zone " + i + " has no water cells");
            }
            if (sizeClasses[i] < 0 || sizeClasses[i] >= SIZE_CLASSES) {
                throw new IllegalArgumentException(
                        "zone " + i + " has unknown size class " + sizeClasses[i]);
            }
            this.waterCells[i] = waterCells[i].clone();
        }
    }

    /** Number of authored zones. */
    public int zoneCount() {
        return sizeClasses.length;
    }

    /** Zone {@code i}'s size-class ordinal. */
    public int sizeClassOf(int i) {
        return sizeClasses[i];
    }

    /** Zone {@code i}'s walkable cast cell (the fisher's stand). */
    public int castCellOf(int i) {
        return castCells[i];
    }

    /** Number of water cells in zone {@code i} (2-3 by authoring convention). */
    public int waterCellCount(int i) {
        return waterCells[i].length;
    }

    /** Water cell {@code j} of zone {@code i}. */
    public int waterCellAt(int i, int j) {
        return waterCells[i][j];
    }
}
