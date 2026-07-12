package com.trojia.sim.bubble;

/**
 * The system-facing facade of the bubble: O(1) concreteness queries every
 * field system uses to skip cells outside the concrete set (queue entries
 * into frozen chunks are lazily skipped on dequeue). Handed to systems via
 * {@code TickContext.bubble()}; the managing side (tickets, freeze/thaw
 * pipelines, budgets) is internal to the bubble module.
 *
 * <p>The concrete set is stable from BUBBLE_PROMOTE through ECONOMY — answers
 * may only change across BUBBLE_PROMOTE/BUBBLE_DEMOTE.
 */
public interface ActiveBubble {

    /** The ticket level of {@code chunkIndex}. */
    TicketLevel levelOf(int chunkIndex);

    /**
     * Whether {@code chunkIndex} is concrete (ACTIVE or BORDER) — i.e. whether
     * ChunkWriter writes there are accepted.
     */
    boolean isConcrete(int chunkIndex);
}
