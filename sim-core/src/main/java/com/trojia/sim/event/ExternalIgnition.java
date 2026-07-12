package com.trojia.sim.event;

/**
 * A player/script ignition request materialized by the InputGate at TICK_BEGIN.
 * Consumed by the fire system same tick.
 *
 * @param cell packed tile position to ignite
 */
public record ExternalIgnition(int cell) implements SimEvent {
}
