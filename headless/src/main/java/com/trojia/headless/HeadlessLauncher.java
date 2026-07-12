package com.trojia.headless;

import com.trojia.sim.core.Simulation;
import com.trojia.sim.core.SimulationPipeline;
import com.trojia.sim.time.TickClock;

/**
 * CLI entry point for running the simulation without any client.
 * M0: a fixed number of heartbeat ticks. Grows into the scenario runner (M2+).
 */
public final class HeadlessLauncher {

    private HeadlessLauncher() {
    }

    public static void main(String[] args) {
        int ticks = parseTicks(args, 100);

        Simulation simulation = new Simulation(
                SimulationPipeline.of(new HeartbeatSystem(10)),
                new TickClock());

        long startNanos = System.nanoTime();
        for (int i = 0; i < ticks; i++) {
            simulation.tick();
        }
        long elapsedMillis = (System.nanoTime() - startNanos) / 1_000_000;

        System.out.println("Simulated " + simulation.clock().currentTick()
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
