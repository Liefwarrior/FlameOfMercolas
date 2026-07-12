package com.trojia.headless;

import com.trojia.sim.core.SimulationContext;
import com.trojia.sim.core.SimulationSystem;

/** M0 placeholder system: proves the pipeline runs by logging a periodic heartbeat. */
final class HeartbeatSystem implements SimulationSystem {

    private final int period;

    HeartbeatSystem(int period) {
        if (period <= 0) {
            throw new IllegalArgumentException("period must be positive: " + period);
        }
        this.period = period;
    }

    @Override
    public String id() {
        return "heartbeat";
    }

    @Override
    public void tick(SimulationContext context) {
        if (context.currentTick() % period == 0) {
            System.out.println("[tick " + context.currentTick() + "] the world turns");
        }
    }
}
