package com.trojia.sim.actor;

import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.Walkability;
import com.trojia.sim.world.World;

import java.util.Arrays;

/**
 * The baked cross-z connector table (Sprint 4 "the climb"): every authored vertical
 * passage of a scenario as {@code (lowCell, highCell)} pairs, extracted ONCE at
 * scenario-bake time from the world's FORM lane and injected as a {@link CivicFixtures}
 * member (the {@link RooftopTable} pattern — immutable baked config, rides no save).
 *
 * <p><b>The two authored connector shapes</b> (content/maps/README.md):
 * <ul>
 *   <li><b>STAIR pair</b> — the authoring forms {@code STAIR_UP} at {@code (x,y,z)} and
 *       {@code STAIR_DOWN} at {@code (x,y,z+1)} both collapse to {@link TileForm#STAIR}
 *       at bake, so a baked vertical passage is simply two same-column STAIR tiles on
 *       adjacent z: the link is {@code low=(x,y,z), high=(x,y,z+1)}.</li>
 *   <li><b>RAMP</b> — a walkable slope at {@code (x,y,z)} whose cell above is OPEN; its
 *       exits are the WALKABLE cells at {@code z+1} among the four orthogonal neighbor
 *       columns, one link per exit: {@code low=(x,y,z), high=(x±1|y±1, z+1)}.</li>
 * </ul>
 *
 * <p><b>Determinism.</b> {@link #extract} scans ascending {@code z}, then {@code y},
 * then {@code x}, probing orthogonal ramp exits in the fixed {@code Dir} declaration
 * order (W, E, N, S) — the link list (and therefore every {@link ZRouter} tie-break
 * downstream) is a pure function of the baked world. Dense parallel {@code int[]}s,
 * no {@code Map}/{@code Set}, no float — purity-gate clean.
 *
 * <p>{@link #EMPTY} means "no connectors wired": every cross-z consumer degrades to the
 * pre-Sprint-4 no-op (the world-less bootstrap, tests, single-z scenarios).
 */
public final class ZLinkTable {

    /** No connectors: {@link ZRouter} finds no route and cross-z movement stays a no-op. */
    public static final ZLinkTable EMPTY = new ZLinkTable(new int[0], new int[0]);

    /** Lower endpoints, link i at {@code low[i]} (always exactly one z below {@code high[i]}). */
    private final int[] low;
    /** Upper endpoints, link i at {@code high[i]}. */
    private final int[] high;

    /**
     * @param low  lower endpoint cells (packed), one per link
     * @param high upper endpoint cells (packed), one per link; {@code high[i]} must sit
     *             exactly one z above {@code low[i]} in the same column (stair) or an
     *             orthogonally adjacent column (ramp exit)
     * @throws IllegalArgumentException on length mismatch or a malformed link
     */
    public ZLinkTable(int[] low, int[] high) {
        if (low.length != high.length) {
            throw new IllegalArgumentException(
                    "low/high length mismatch: " + low.length + " != " + high.length);
        }
        for (int i = 0; i < low.length; i++) {
            if (PackedPos.z(high[i]) != PackedPos.z(low[i]) + 1) {
                throw new IllegalArgumentException("link " + i + ": high must sit exactly one z "
                        + "above low (" + PackedPos.z(low[i]) + " -> " + PackedPos.z(high[i]) + ")");
            }
            int dx = Math.abs(PackedPos.x(high[i]) - PackedPos.x(low[i]));
            int dy = Math.abs(PackedPos.y(high[i]) - PackedPos.y(low[i]));
            if (dx + dy > 1) {
                throw new IllegalArgumentException("link " + i + ": endpoints must share a column "
                        + "(stair) or be orthogonally adjacent (ramp exit): dx=" + dx + " dy=" + dy);
            }
        }
        this.low = low.clone();
        this.high = high.clone();
    }

    /** Number of baked links. */
    public int linkCount() {
        return low.length;
    }

    /** Lower endpoint cell of link {@code i}. */
    public int low(int i) {
        return low[i];
    }

    /** Upper endpoint cell of link {@code i}. */
    public int high(int i) {
        return high[i];
    }

    /** {@code true} iff no connectors are wired (cross-z movement degrades to the no-op). */
    public boolean isEmpty() {
        return low.length == 0;
    }

    /**
     * Whether {@code (a, b)} is a baked link in either direction — the movement commit's
     * guard: only a baked connector may carry a vertical step ({@link Actor#tryStepVertical}).
     * Ascending fixed-order scan, deterministic.
     */
    public boolean linked(int a, int b) {
        for (int i = 0; i < low.length; i++) {
            if ((low[i] == a && high[i] == b) || (low[i] == b && high[i] == a)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Whether some link connecting z-levels {@code z} and {@code z + 1} exists — the
     * {@link ZRouter} feasibility probe.
     */
    public boolean anyLinkAtZ(int z) {
        for (int cell : low) {
            if (PackedPos.z(cell) == z) {
                return true;
            }
        }
        return false;
    }

    /**
     * Extracts the connector table from a baked world's FORM/FLUID lanes: every same-column
     * STAIR pair and every walkable ramp exit, in the fixed ascending {@code (z, y, x)} scan
     * order. Both endpoints of every emitted link pass {@link Walkability#isWalkable} (a
     * fluid-drowned stair is no passage). A bake-time one-shot — never called from the tick
     * loop.
     */
    public static ZLinkTable extract(World world) {
        int maxX = world.config().chunksX() * Coords.CHUNK_SIZE_X - 1;
        int maxY = world.config().chunksY() * Coords.CHUNK_SIZE_Y - 1;
        int maxZ = world.config().chunksZ() * Coords.CHUNK_SIZE_Z - 1;
        TileCursor cursor = world.cursor();
        TileCursor exitCursor = world.cursor();
        int[] lows = new int[16];
        int[] highs = new int[16];
        int count = 0;
        for (int z = 0; z < maxZ; z++) {
            for (int y = 1; y < maxY; y++) {
                for (int x = 1; x < maxX; x++) {
                    int cell = PackedPos.pack(x, y, z);
                    TileForm form = cursor.moveTo(cell).form();
                    if (form == TileForm.STAIR) {
                        if (!Walkability.isWalkable(cursor.moveTo(cell))) {
                            continue;
                        }
                        int above = PackedPos.pack(x, y, z + 1);
                        if (exitCursor.moveTo(above).form() == TileForm.STAIR
                                && Walkability.isWalkable(exitCursor.moveTo(above))) {
                            if (count == lows.length) {
                                lows = Arrays.copyOf(lows, count * 2);
                                highs = Arrays.copyOf(highs, count * 2);
                            }
                            lows[count] = cell;
                            highs[count] = above;
                            count++;
                        }
                    } else if (form == TileForm.RAMP) {
                        if (!Walkability.isWalkable(cursor.moveTo(cell))) {
                            continue;
                        }
                        // Ramp exits: walkable z+1 cells over the four orthogonal neighbor
                        // columns, probed in the fixed Dir declaration order (W, E, N, S).
                        for (int n = 0; n < RAMP_EXIT_DX.length; n++) {
                            int exit = PackedPos.pack(x + RAMP_EXIT_DX[n], y + RAMP_EXIT_DY[n],
                                    z + 1);
                            if (Walkability.isWalkable(exitCursor.moveTo(exit))) {
                                if (count == lows.length) {
                                    lows = Arrays.copyOf(lows, count * 2);
                                    highs = Arrays.copyOf(highs, count * 2);
                                }
                                lows[count] = cell;
                                highs[count] = exit;
                                count++;
                            }
                        }
                    }
                }
            }
        }
        return new ZLinkTable(Arrays.copyOf(lows, count), Arrays.copyOf(highs, count));
    }

    /** Fixed ramp-exit probe order — {@code Dir} declaration order (W, E, N, S); never varies. */
    private static final int[] RAMP_EXIT_DX = {-1, 1, 0, 0};
    private static final int[] RAMP_EXIT_DY = {0, 0, -1, 1};
}
