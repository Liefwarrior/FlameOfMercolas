package com.trojia.sim.engine;

import com.trojia.sim.bubble.ActiveBubble;
import com.trojia.sim.event.EventReader;
import com.trojia.sim.event.EventSink;
import com.trojia.sim.event.SimEvent;
import com.trojia.sim.random.RandomSource;

/**
 * The per-system, per-tick facade handed to {@link SimulationSystem#tick}.
 * Each system receives its own context, pre-bound to the system's pipeline
 * position — the RNG is already salted with the system's id and rebound to
 * the current tick, and the event seams stamp this system's {@code (phase,
 * regIndex)}. World access is constructor-injected at wiring time, not
 * carried here.
 */
public interface TickContext {

    /** The tick currently being simulated (first tick is 1). */
    long tick();

    /** The phase currently executing (always this system's declared phase). */
    TickPhase phase();

    /**
     * This system's counter-based random source, bound to (worldSeed, tick,
     * systemSalt); the system supplies only spatial keys and draw indices.
     */
    RandomSource rng();

    /**
     * This system's registered reader for {@code topic}. Registration
     * happened at boot; calling with a topic this system never registered is
     * a programming error ({@code IllegalStateException}).
     */
    <E extends SimEvent> EventReader<E> events(Class<E> topic);

    /** This system's bound event sink. */
    EventSink emit();

    /** Concreteness queries for skipping cells outside the concrete set. */
    ActiveBubble bubble();
}
