package com.trojia.sim.event;

import com.trojia.sim.engine.SystemId;
import com.trojia.sim.engine.TickPhase;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.List;

/**
 * Boot-time factory and engine-facing control surface of the event plumbing.
 * The bus implementation ({@code PhasedEventBus}) is package-private; systems
 * only ever see {@link EventSink} and {@link EventReader}, and the engine
 * drives lifecycle through this facade.
 *
 * <p>Registration (sinks and readers) happens at engine boot and is sealed
 * before tick 1; the registration set is part of the determinism contract.
 * One instance per engine — never shared across engines in one JVM.
 */
public interface Events {

    /**
     * Hard per-topic per-tick event cap; exceeding it fails identically in all
     * builds (ARCHITECTURE.md §3).
     */
    int MAX_EVENTS_PER_TOPIC_PER_TICK = 65_536;

    /** Creates an empty, unsealed bus. */
    static Events create() {
        return new PhasedEventBus();
    }

    /**
     * The canonical, save-format-stable topic order (§5 taxonomy order). The
     * index of a topic here is its stable topic id — it keys the EVNT section
     * layout, so the order is append-only. The engine uses this list to
     * pre-register every system's readers at boot.
     */
    static List<Class<? extends SimEvent>> topics() {
        return EventCodec.topics();
    }

    /**
     * Registers an emitter at pipeline position {@code (system, phase,
     * regIndex)} and returns its bound sink. Boot-time only.
     *
     * @throws IllegalStateException once sealed
     */
    EventSink sink(SystemId system, TickPhase phase, int regIndex);

    /**
     * Registers a consumer of {@code topic} at pipeline position
     * {@code (system, phase, regIndex)} and returns its private cursor
     * (resolving the topic id now, never per tick). Boot-time only.
     *
     * @throws IllegalStateException once sealed
     */
    <E extends SimEvent> EventReader<E> reader(SystemId system, TickPhase phase, int regIndex,
            Class<E> topic);

    /** Forbids further registration; called once by the engine before tick 1. */
    void seal();

    /**
     * Engine-only: advances the bus's stamp position to {@code (tick, phase,
     * regIndex)} before the system at that position runs. Positions advance
     * monotonically within a tick.
     */
    void advanceTo(long tick, TickPhase phase, int regIndex);

    /**
     * Engine-only, at TICK_END of {@code tick}: retires every event that has
     * completed its one-lap visibility window.
     */
    void retireLap(long tick);

    /**
     * Serializes the carry-over lap (events emitted this tick that are still
     * visible next tick) for the TROJSAV {@code EVNT} section. Pure; legal
     * only between ticks.
     */
    void serializeCarryOver(DataOutput out) throws IOException;

    /** Restores a carry-over lap written by {@link #serializeCarryOver}. */
    void loadCarryOver(DataInput in) throws IOException;
}
