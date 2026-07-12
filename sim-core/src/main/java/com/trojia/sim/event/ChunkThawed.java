package com.trojia.sim.event;

/**
 * A chunk became concrete (BORDER) during BUBBLE_PROMOTE. All field systems
 * consume it same tick to prime their frontiers over the thawed cells.
 *
 * @param chunkIndex flat index of the thawed chunk
 */
public record ChunkThawed(int chunkIndex) implements SimEvent {
}
