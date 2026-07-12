package com.trojia.headless;

import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.engine.SystemId;
import com.trojia.sim.engine.TickContext;
import com.trojia.sim.engine.TickPhase;
import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;

/**
 * M0 placeholder system: proves the engine's phase loop runs by logging a
 * periodic heartbeat. Stateless — serialize/load/hashInto are empty.
 */
final class HeartbeatSystem implements SimulationSystem {

    private static final SystemId ID = SystemId.of("heartbeat");

    private final int period;

    HeartbeatSystem(int period) {
        if (period <= 0) {
            throw new IllegalArgumentException("period must be positive: " + period);
        }
        this.period = period;
    }

    @Override
    public SystemId id() {
        return ID;
    }

    @Override
    public TickPhase phase() {
        return TickPhase.TICK_BEGIN;
    }

    @Override
    public void tick(TickContext context) {
        if (context.tick() % period == 0) {
            System.out.println("[tick " + context.tick() + "] the world turns");
        }
    }

    @Override
    public void serialize(DataOutput out) {
        // Stateless.
    }

    @Override
    public void load(DataInput in) {
        // Stateless.
    }

    @Override
    public void hashInto(WorldHasher.Sink sink) {
        // Stateless.
    }
}
