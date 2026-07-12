package com.trojia.sim.event;

/**
 * The fire system stopped a tile burning (quench, fuel-out or command;
 * THERMAL phase, fire sub-system). Lap-consumed by the economy accumulator;
 * drained to observer logs.
 *
 * @param cell packed position of the extinguished tile
 */
public record TileExtinguished(int cell) implements SimEvent {
}
