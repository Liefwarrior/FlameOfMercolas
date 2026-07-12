package com.trojia.sim.core;

import com.trojia.sim.time.TickClock;

import java.util.Objects;

/**
 * Owns the world clock and the system pipeline; {@link #tick()} is the single
 * entry point through which simulated time ever advances.
 */
public final class Simulation {

    private final SimulationPipeline pipeline;
    private final TickClock clock;
    private final SimulationContext context;

    public Simulation(SimulationPipeline pipeline, TickClock clock) {
        this.pipeline = Objects.requireNonNull(pipeline, "pipeline");
        this.clock = Objects.requireNonNull(clock, "clock");
        this.context = clock::currentTick;
    }

    /** Advances the world by exactly one tick, running every system in pipeline order. */
    public void tick() {
        clock.advance();
        for (SimulationSystem system : pipeline.systems()) {
            system.tick(context);
        }
    }

    public TickClock clock() {
        return clock;
    }
}
