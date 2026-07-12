package com.trojia.sim.event;

/**
 * The fire system set a tile burning (THERMAL phase, fire sub-system).
 * Lap-consumed by the economy accumulator; drained to observer logs.
 *
 * @param cell packed position of the newly burning tile
 */
public record TileIgnited(int cell) implements SimEvent {
}
