package com.trojia.sim;

import com.trojia.sim.engine.EngineConfig;
import com.trojia.sim.engine.SimCommand;
import com.trojia.sim.engine.SimulationEngine;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.engine.Simulations;
import com.trojia.sim.engine.SystemId;
import com.trojia.sim.engine.TickContext;
import com.trojia.sim.engine.TickPhase;
import com.trojia.sim.random.RandomSource;
import com.trojia.sim.world.ChunkWriter;
import com.trojia.sim.world.Lanes;
import com.trojia.sim.world.OverlayId;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.World;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;
import com.trojia.sim.world.change.ActiveSet;
import com.trojia.sim.world.change.ChangeLogReader;
import com.trojia.sim.world.io.WorldHasher;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * The M0 acceptance gate (ARCHITECTURE.md §12): two {@code SimulationEngine}
 * instances in ONE JVM, same seed, same scripted 1000-tick SimCommand sequence
 * — including paints across chunks and into the VOID border (rejected) — must
 * produce identical per-tick {@link WorldHasher} hash chains. And the run must
 * satisfy {@code run K+N ≡ save@K, load, run N}: saving engine A at tick 500,
 * loading a third engine from the file and running the remaining 500 scripted
 * ticks lands on the same per-tick chain and final hash.
 *
 * <p>The registered system exercises the full change plumbing end-to-end each
 * tick: InputGate paints → ChunkWriter → MATERIAL ChangeLog → ActiveSet dedupe
 * → RNG-derived TEMPERATURE writes → ChunkRevisions commit; plus FLUID lane
 * and CHARGE overlay writes so lanes and overlays all carry hash weight.
 */
final class TwinRunDeterminismTest {

    /** 4×3×3 chunks (128×96×24 tiles) incl. border; interior chunks (1,1,1) and (2,1,1). */
    private static final WorldConfig CONFIG = new WorldConfig(4, 3, 3);
    private static final long SEED = 0x5EED_CAFEL;
    private static final int TICKS = 1000;
    private static final int SAVE_TICK = 500;

    /**
     * The plumbing-exercising system: drains its MATERIAL change-log cursor
     * into an {@link ActiveSet} frontier (dedupe), then writes an RNG-derived
     * temperature at every frontier cell, one FLUID-lane value per tick, and a
     * CHARGE overlay cell every 5th tick (cleared every 7th). Stateless across
     * tick boundaries: the cursor is fully drained and the frontier fully
     * polled every tick, so serialize/load are empty.
     */
    private static final class LaneScribbler implements SimulationSystem {

        private static final SystemId ID = SystemId.of("scribbler");

        private final ChunkWriter writer;
        private final ChangeLogReader materialLog;
        private final ActiveSet frontier;

        LaneScribbler(World world) {
            this.writer = world.writer();
            this.materialLog = world.changeLogs()
                    .register(ID, world.lanes().byIndex(Lanes.MATERIAL_INDEX));
            this.frontier = new ActiveSet(world.coords());
        }

        @Override
        public SystemId id() {
            return ID;
        }

        @Override
        public TickPhase phase() {
            return TickPhase.THERMAL;
        }

        @Override
        public void tick(TickContext context) {
            while (materialLog.hasNext()) {
                frontier.add(materialLog.next());
            }
            while (!frontier.isEmpty()) {
                int cell = frontier.poll();
                writer.setTemperatureDeciK(cell, (int) (context.rng().draw(cell, 0) & 0xFFFF));
            }
            long tick = context.tick();
            long r = context.rng().draw(0xF10DL, 1);
            writer.setFluidBits(interiorCell(r), (int) ((r >>> 24) & 0xFFFF));
            if (tick % 5 == 0) {
                long c = context.rng().draw(0xC4A6EL, 2);
                writer.setOverlay(interiorCell(c), OverlayId.CHARGE, (int) ((c >>> 24) & 0xFFFF));
            }
            if (tick % 7 == 0) {
                long c = context.rng().draw(0xC4A6EL, 3);
                writer.clearOverlay(interiorCell(c), OverlayId.CHARGE);
            }
        }

        @Override
        public void serialize(DataOutput out) {
            // Drained empty at every TICK_END: nothing to persist.
        }

        @Override
        public void load(DataInput in) {
            // Nothing persisted.
        }

        @Override
        public void hashInto(WorldHasher.Sink sink) {
            // All authored state lives in world lanes/overlays.
        }
    }

    /** Maps 64 random bits onto an interior tile of {@link #CONFIG}. */
    private static int interiorCell(long r) {
        int x = 32 + (int) (r & 63);           // interior x: [32, 96)
        int y = 32 + (int) ((r >>> 6) & 31);   // interior y: [32, 64)
        int z = 8 + (int) ((r >>> 12) & 7);    // interior z: [8, 16)
        return PackedPos.pack(x, y, z);
    }

    /**
     * The scripted command sequence for one tick: a pure function of the tick
     * number, so the twin and the loaded engine replay it identically. Every
     * tick paints two interior cells (crossing both interior chunks over time)
     * and one VOID-border cell (rejected by the writer, dropped); periodically
     * clears a tile and submits quantity commands (events).
     */
    private static List<SimCommand> scriptFor(long tick) {
        List<SimCommand> commands = new ArrayList<>();
        long r = RandomSource.mix64(0xD00DL * tick + 0xBEEFL);
        TileForm form = (tick & 1) == 0 ? TileForm.WALL : TileForm.FLOOR;
        commands.add(new SimCommand.PaintMaterial(
                interiorCell(r), (short) (1 + (r & 0xFF)), form));
        long r2 = RandomSource.mix64(r);
        commands.add(new SimCommand.PaintMaterial(
                interiorCell(r2), (short) (1 + (r2 & 0xFF)), TileForm.WALL));
        // A paint into the VOID border ring (x < 32): rejected, never applied.
        commands.add(new SimCommand.PaintMaterial(
                PackedPos.pack((int) (r2 >>> 32) & 31, 40, 10), (short) 7, TileForm.WALL));
        if (tick % 3 == 0) {
            commands.add(new SimCommand.ClearTile(interiorCell(RandomSource.mix64(r2))));
        }
        if (tick % 4 == 0) {
            commands.add(new SimCommand.Ignite(interiorCell(r ^ r2)));
        }
        if (tick % 6 == 0) {
            commands.add(new SimCommand.AddFluid(interiorCell(r + r2), (short) 1, 250));
        }
        if (tick % 9 == 0) {
            commands.add(new SimCommand.InjectCharge(interiorCell(r - r2), 600));
        }
        return commands;
    }

    private static SimulationEngine boot() {
        TickableWorld world = WorldBuilder.create(CONFIG).build();
        return Simulations.create(new EngineConfig(SEED), world,
                List.of(new LaneScribbler(world)));
    }

    private static long hashOf(World world) {
        WorldHasher hasher = new WorldHasher();
        hasher.hashWorld(world);
        return hasher.combinedHash();
    }

    @Test
    void twinRunsProduceIdenticalHashChainsAndSaveLoadResumesThem(@TempDir Path dir)
            throws IOException {
        SimulationEngine a = boot();
        SimulationEngine b = boot();

        // Border writes are rejected with a status code, never applied silently.
        assertEquals(ChunkWriter.REJECTED_VOID, a.world().writer()
                .setMaterialAndForm(PackedPos.pack(5, 40, 10), (short) 7, TileForm.WALL));

        Path save = dir.resolve("twin-run-500.trojsav");
        long[] chainA = new long[TICKS + 1];
        long[] chainB = new long[TICKS + 1];
        for (int tick = 1; tick <= TICKS; tick++) {
            for (SimCommand command : scriptFor(tick)) {
                a.submit(command);
                b.submit(command);
            }
            a.tick();
            b.tick();
            chainA[tick] = hashOf(a.world());
            chainB[tick] = hashOf(b.world());
            if (tick == SAVE_TICK) {
                a.save(save);
            }
        }
        assertArrayEquals(chainA, chainB,
                "twin engines with the same seed and script must produce identical hash chains");

        // run K+N ≡ save@K, load, run N — including the world, the input log,
        // the event carry-over lap and the per-system RNG rebinding.
        SimulationEngine c = Simulations.load(new EngineConfig(SEED), save,
                world -> List.<SimulationSystem>of(new LaneScribbler(world)));
        assertEquals(SAVE_TICK, c.currentTick(), "loaded engine resumes at the save tick");
        assertEquals(chainA[SAVE_TICK], hashOf(c.world()),
                "loaded world must hash identically to the saved world");

        long[] chainC = new long[TICKS + 1];
        for (int tick = SAVE_TICK + 1; tick <= TICKS; tick++) {
            for (SimCommand command : scriptFor(tick)) {
                c.submit(command);
            }
            c.tick();
            chainC[tick] = hashOf(c.world());
        }
        for (int tick = SAVE_TICK + 1; tick <= TICKS; tick++) {
            assertEquals(chainA[tick], chainC[tick],
                    "loaded run diverged from the original at tick " + tick);
        }
        assertEquals(chainA[TICKS], chainC[TICKS], "final hashes must match");
    }
}
