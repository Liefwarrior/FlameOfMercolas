package com.trojia.sim.event;

/**
 * A chargeable tile reached its saturation percentage; saturation heat starts
 * flowing into neighbors via the heat command buffer next tick (REACTIONS
 * phase, charge sub-system). Lap-consumed by the economy accumulator; drained
 * to observer logs.
 *
 * @param cell packed position of the saturated tile
 */
public record ChargeSaturatedEvent(int cell) implements SimEvent {
}
