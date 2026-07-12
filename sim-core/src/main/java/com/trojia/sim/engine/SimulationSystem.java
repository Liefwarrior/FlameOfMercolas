package com.trojia.sim.engine;

import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * One registered simulation system (ARCHITECTURE.md §3). Systems declare the
 * phase they run in; within a phase they run in registration order, and that
 * {@code (phase, registrationIndex)} position keys event visibility. The
 * registration list is fixed at engine boot and is part of the determinism
 * contract.
 *
 * <p>Systems reach shared state only through the seams handed to them
 * (constructor-injected {@code World}, per-tick {@link TickContext}); never
 * through statics.
 */
public interface SimulationSystem {

    /** This system's stable identity (RNG salt, save section, sub-hash name). */
    SystemId id();

    /** The pipeline phase this system runs in. Constant for the system's lifetime. */
    TickPhase phase();

    /**
     * Advances this system by exactly one tick. Deterministic: given equal
     * prior state, equal inputs and the context's RNG, the resulting state and
     * emissions are identical on every platform.
     */
    void tick(TickContext context);

    /**
     * Writes this system's complete persistent state (frontiers, quiet
     * counters, queues, buffers) to {@code out} for its TROJSAV section. PURE:
     * no state mutation — the same routine serves both the saver and the
     * freeze pipeline (ARCHITECTURE.md §1.1 #10). Legal only between ticks.
     */
    void serialize(DataOutput out) throws IOException;

    /**
     * Restores state written by {@link #serialize}. Called at load, before the
     * first tick after loading; a section this system requires but cannot read
     * is a hard fail.
     */
    void load(DataInput in) throws IOException;

    /**
     * Feeds this system's canonical logical state into {@code sink} for its
     * per-tick sub-hash (golden masters name the first divergent system via
     * these). Pure; iteration in canonical sorted key order.
     */
    void hashInto(WorldHasher.Sink sink);
}
