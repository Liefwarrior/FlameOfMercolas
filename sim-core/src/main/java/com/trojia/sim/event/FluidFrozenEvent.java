package com.trojia.sim.event;

/**
 * Pooled fluid units froze out of the FLUID lane (FLUIDS phase). The phase
 * transition system consumes it same tick to place the solid (e.g. ice) on the
 * material grid.
 *
 * @param cell    packed position of the frozen fluid
 * @param fluidId fluid id
 * @param units   units that froze
 */
public record FluidFrozenEvent(int cell, short fluidId, int units) implements SimEvent {
}
