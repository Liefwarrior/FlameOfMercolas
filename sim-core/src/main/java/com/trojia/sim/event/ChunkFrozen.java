package com.trojia.sim.event;

/**
 * A chunk left the concrete set during BUBBLE_DEMOTE. Field systems consume it
 * next tick to drop queue/frontier entries (pending mass-bearing carry-over was
 * already folded into the chunk summary by the freeze pipeline); the economy
 * accumulator notes the transition.
 *
 * @param chunkIndex flat index of the frozen chunk
 */
public record ChunkFrozen(int chunkIndex) implements SimEvent {
}
