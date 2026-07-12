package com.trojia.headless;

import com.trojia.sim.engine.EngineConfig;
import com.trojia.sim.engine.SimCommand;
import com.trojia.sim.engine.SimulationEngine;
import com.trojia.sim.engine.Simulations;
import com.trojia.sim.world.PackedPos;
import com.trojia.sim.world.TickableWorld;
import com.trojia.sim.world.TileForm;
import com.trojia.sim.world.WorldBuilder;
import com.trojia.sim.world.WorldConfig;

import java.util.List;

/**
 * CLI entry point for running the simulation without any client.
 * F1: a fixed number of heartbeat ticks through the real engine over a real
 * dense-lane world (paints materialize through the ChunkWriter at TICK_BEGIN).
 * Grows into the scenario runner (ScenarioMain, M2+).
 */
public final class HeadlessLauncher {

    /** Heartbeat world: 4×4×3 chunks (128×128×24 tiles) including the VOID border. */
    private static final WorldConfig WORLD = new WorldConfig(4, 4, 3);

    private HeadlessLauncher() {
    }

    public static void main(String[] args) {
        int ticks = parseTicks(args, 100);

        TickableWorld world = WorldBuilder.create(WORLD).build();
        SimulationEngine engine = Simulations.create(
                new EngineConfig(0L),
                world,
                List.of(new HeartbeatSystem(10)));

        // A few interior paints so every heartbeat run exercises the real
        // InputGate -> ChunkWriter -> change-plumbing path, not just the clock.
        engine.submit(new SimCommand.PaintMaterial(
                PackedPos.pack(40, 40, 10), (short) 1, TileForm.WALL));
        engine.submit(new SimCommand.PaintMaterial(
                PackedPos.pack(41, 40, 10), (short) 1, TileForm.FLOOR));

        long startNanos = System.nanoTime();
        engine.step(ticks);
        long elapsedMillis = (System.nanoTime() - startNanos) / 1_000_000;

        System.out.println("Simulated " + engine.currentTick()
                + " ticks in " + elapsedMillis + " ms. Granadad awaits.");
    }

    private static int parseTicks(String[] args, int fallback) {
        for (int i = 0; i < args.length - 1; i++) {
            if ("--ticks".equals(args[i])) {
                return Integer.parseInt(args[i + 1]);
            }
        }
        return fallback;
    }
}
