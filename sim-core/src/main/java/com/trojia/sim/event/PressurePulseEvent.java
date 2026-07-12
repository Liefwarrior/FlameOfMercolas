package com.trojia.sim.event;

/**
 * The phorys reaction system released an expansion gas pulse (REACTIONS
 * phase). Consumed by fluids next tick — the one-tick latency is documented
 * canon (ARCHITECTURE.md §5).
 *
 * @param cell      packed position of the pulse origin
 * @param gasId     fluid id of the expansion gas
 * @param magnitude pulse magnitude in fluid units
 */
public record PressurePulseEvent(int cell, short gasId, int magnitude) implements SimEvent {
}
