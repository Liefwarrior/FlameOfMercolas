package com.trojia.sim.event;

/**
 * A player/script fluid injection materialized by the InputGate at TICK_BEGIN.
 * Consumed by the fluid system same tick (fluids own the FLUID lane; the gate
 * never writes it directly).
 *
 * @param cell    packed tile position receiving the fluid
 * @param fluidId fluid registry id
 * @param units   fluid quantity in fluid units (positive)
 */
public record ExternalFluidSpawned(int cell, short fluidId, int units) implements SimEvent {
}
