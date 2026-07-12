package com.trojia.sim.engine;

import com.trojia.sim.bubble.ActiveBubble;
import com.trojia.sim.bubble.TicketLevel;

/**
 * The engine's default bubble: every chunk is ACTIVE and concrete —
 * whole-map-active physics, the M2 operating mode. Stands in until the bubble
 * module's {@code TicketedBubbleManager} (F5) is wired in as the engine's
 * bubble; systems coded against {@link ActiveBubble} need no change when that
 * lands.
 */
final class AllConcreteBubble implements ActiveBubble {

    @Override
    public TicketLevel levelOf(int chunkIndex) {
        return TicketLevel.ACTIVE;
    }

    @Override
    public boolean isConcrete(int chunkIndex) {
        return true;
    }
}
