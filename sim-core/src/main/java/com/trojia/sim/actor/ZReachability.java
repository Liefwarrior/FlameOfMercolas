package com.trojia.sim.actor;

import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.Walkability;
import com.trojia.sim.world.World;

/**
 * The bake-time 3D reachability audit (Sprint 4 "the climb"): one flood fill over the
 * FULL walk graph — 8-neighbor same-z steps under the movers' solid-corner rule, plus
 * every baked {@link ZLinkTable} connector in both directions — from a seed cell, so the
 * "cross-z-unreachable actors" folklore number becomes a tracked per-band metric (and a
 * punch list for the world team: every walkable-but-unreachable pocket is authored
 * geometry nothing can ever route into).
 *
 * <p>A bake-time one-shot (never ticked): dense {@code boolean[]}/{@code int[]} only, a
 * plain array-backed BFS queue, fixed neighbor order — deterministic and purity-gate
 * clean. Zero sim-runtime surface; consumers are the scenario reports and tests.
 */
public final class ZReachability {

    /** Fixed 8-neighbor probe order — the {@code PathFinder} table (W, E, N, S, diagonals). */
    private static final int[] NEIGHBOR_DX = {-1, 1, 0, 0, -1, 1, -1, 1};
    private static final int[] NEIGHBOR_DY = {0, 0, -1, 1, -1, -1, 1, 1};
    private static final boolean[] NEIGHBOR_IS_DIAGONAL =
            {false, false, false, false, true, true, true, true};

    private final int width;
    private final int height;
    private final int depth;
    private final boolean[] walkable;
    private final boolean[] reached;
    private final int[] walkableByZ;
    private final int[] reachableByZ;

    private ZReachability(int width, int height, int depth, boolean[] walkable,
            boolean[] reached, int[] walkableByZ, int[] reachableByZ) {
        this.width = width;
        this.height = height;
        this.depth = depth;
        this.walkable = walkable;
        this.reached = reached;
        this.walkableByZ = walkableByZ;
        this.reachableByZ = reachableByZ;
    }

    /**
     * Floods the walk graph of {@code world} (+ {@code links}) from {@code seedCell} and
     * returns the audit. The seed should be a known hub (a street cell actors live on);
     * an unwalkable seed yields an all-unreachable audit.
     */
    public static ZReachability flood(World world, ZLinkTable links, int seedCell) {
        int width = world.config().chunksX() * Coords.CHUNK_SIZE_X;
        int height = world.config().chunksY() * Coords.CHUNK_SIZE_Y;
        int depth = world.config().chunksZ() * Coords.CHUNK_SIZE_Z;
        boolean[] walkable = new boolean[width * height * depth];
        int[] walkableByZ = new int[depth];
        TileCursor cursor = world.cursor();
        for (int z = 0; z < depth; z++) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    if (Walkability.isWalkable(cursor.moveTo(PackedPos.pack(x, y, z)))) {
                        walkable[(z * height + y) * width + x] = true;
                        walkableByZ[z]++;
                    }
                }
            }
        }
        boolean[] reached = new boolean[walkable.length];
        int[] reachableByZ = new int[depth];
        int seedIdx = denseIdx(seedCell, width, height);
        if (walkable[seedIdx]) {
            int[] queue = new int[1024];
            int head = 0;
            int tail = 0;
            reached[seedIdx] = true;
            reachableByZ[PackedPos.z(seedCell)]++;
            queue[tail++] = seedCell;
            while (head < tail) {
                int cell = queue[head++];
                int x = PackedPos.x(cell);
                int y = PackedPos.y(cell);
                int z = PackedPos.z(cell);
                for (int n = 0; n < NEIGHBOR_DX.length; n++) {
                    int nx = x + NEIGHBOR_DX[n];
                    int ny = y + NEIGHBOR_DY[n];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                        continue;
                    }
                    if (NEIGHBOR_IS_DIAGONAL[n]
                            && (!walkable[(z * height + y) * width + nx]
                                    || !walkable[(z * height + ny) * width + x])) {
                        continue; // never cut a solid diagonal wall corner (mover parity)
                    }
                    int idx = (z * height + ny) * width + nx;
                    if (walkable[idx] && !reached[idx]) {
                        reached[idx] = true;
                        reachableByZ[z]++;
                        if (tail == queue.length) {
                            queue = java.util.Arrays.copyOf(queue, tail * 2);
                        }
                        queue[tail++] = PackedPos.pack(nx, ny, z);
                    }
                }
                // Vertical connectors touching this cell, both directions, baked order.
                for (int i = 0; i < links.linkCount(); i++) {
                    int other;
                    if (links.low(i) == cell) {
                        other = links.high(i);
                    } else if (links.high(i) == cell) {
                        other = links.low(i);
                    } else {
                        continue;
                    }
                    int idx = denseIdx(other, width, height);
                    if (walkable[idx] && !reached[idx]) {
                        reached[idx] = true;
                        reachableByZ[PackedPos.z(other)]++;
                        if (tail == queue.length) {
                            queue = java.util.Arrays.copyOf(queue, tail * 2);
                        }
                        queue[tail++] = other;
                    }
                }
            }
        }
        return new ZReachability(width, height, depth, walkable, reached, walkableByZ,
                reachableByZ);
    }

    private static int denseIdx(int cell, int width, int height) {
        return (PackedPos.z(cell) * height + PackedPos.y(cell)) * width + PackedPos.x(cell);
    }

    /** Whether {@code cell} was reached from the seed (false for unwalkable cells). */
    public boolean reachable(int cell) {
        return reached[denseIdx(cell, width, height)];
    }

    /** Whether {@code cell} is walkable at all (the flood's node universe). */
    public boolean walkable(int cell) {
        return walkable[denseIdx(cell, width, height)];
    }

    /** Walkable-cell count of world z-level {@code z}. */
    public int walkableAtZ(int z) {
        return z < 0 || z >= depth ? 0 : walkableByZ[z];
    }

    /** Seed-reachable-cell count of world z-level {@code z}. */
    public int reachableAtZ(int z) {
        return z < 0 || z >= depth ? 0 : reachableByZ[z];
    }
}
