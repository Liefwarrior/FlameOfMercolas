package com.trojia.sim;

import com.trojia.sim.world.ChunkWriter;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;
import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The M0 write-path budget gate (ARCHITECTURE.md §12): the ChunkWriter's hot
 * lane write must stay in the low-nanosecond range. Measures ns/write over
 * 10 million TEMPERATURE-lane writes after warmup and prints the number.
 *
 * <p>The asserted ceiling is 150 ns — deliberately soft so shared CI runners
 * never flake on scheduling noise; the design target is 15 ns and the printed
 * measurement is what the milestone report records. The measured configuration
 * matches production thermal writes: no readers on the TEMPERATURE lane, so
 * the change-log skip branch is taken (§6 reader-less skip rule).
 *
 * <p>Tagged {@code benchmark} and excluded from the default {@code test} task
 * (see {@code build.gradle.kts}): a hard wall-clock ceiling assertion is
 * inherently timing-sensitive (JIT warm-up variance, GC pauses, CPU frequency
 * scaling, or contention from parallel Gradle test workers can all push the
 * measurement above 150 ns on a busy shared runner for reasons unrelated to
 * any write-path regression) — this is the milestone-report/benchmark task
 * this class's own javadoc already describes, not a correctness gate that
 * should be able to fail the default build non-deterministically. Run it
 * explicitly via the {@code benchmarkTest} task.
 */
@Tag("benchmark")
final class WritePathBenchTest {

    private static final int WRITES_PER_TICK = 1_000_000;
    private static final int WARMUP_TICKS = 3;
    private static final int MEASURED_TICKS = 10;
    private static final double CEILING_NANOS = 150.0;

    @Test
    void tenMillionLaneWritesStayUnderTheCeiling() {
        TickableWorld world = WorldBuilder.create(new WorldConfig(6, 6, 3)).build();
        ChunkWriter writer = world.writer();

        // Interior cells of the 6×6×3-chunk world: x,y in [32,160), z in [8,16).
        int[] cells = new int[1 << 16];
        for (int i = 0; i < cells.length; i++) {
            int x = 32 + (i & 127);
            int y = 32 + ((i >>> 7) & 127);
            int z = 8 + ((i >>> 14) & 3);
            cells[i] = PackedPos.pack(x, y, z);
        }

        long tick = 0;
        long sink = 0;
        for (int t = 0; t < WARMUP_TICKS; t++) {
            world.beginTick(++tick);
            sink += writeBatch(writer, cells, t);
            world.commitTick(tick);
        }

        long elapsed = 0;
        for (int t = 0; t < MEASURED_TICKS; t++) {
            world.beginTick(++tick);
            long start = System.nanoTime();
            sink += writeBatch(writer, cells, t);
            elapsed += System.nanoTime() - start;
            world.commitTick(tick);
        }

        long writes = (long) MEASURED_TICKS * WRITES_PER_TICK;
        double nsPerWrite = (double) elapsed / writes;
        System.out.printf("WritePathBench: %,d TEMPERATURE writes in %,d ns -> %.2f ns/write%n",
                writes, elapsed, nsPerWrite);

        assertEquals(0, sink, "every write must be APPLIED (status 0)");
        assertTrue(nsPerWrite < CEILING_NANOS,
                "write path measured " + nsPerWrite + " ns/write, ceiling " + CEILING_NANOS);
    }

    /** One million lane writes cycling the cell table; returns the summed status codes. */
    private static long writeBatch(ChunkWriter writer, int[] cells, int salt) {
        long statuses = 0;
        int mask = cells.length - 1;
        for (int i = 0; i < WRITES_PER_TICK; i++) {
            statuses += writer.setTemperatureDeciK(cells[(i + salt) & mask], (i + salt) & 0xFFFF);
        }
        return statuses;
    }
}
