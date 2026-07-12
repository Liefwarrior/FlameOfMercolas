package com.trojia.sim.event;

/**
 * A player/script charge injection materialized by the InputGate at TICK_BEGIN.
 * Consumed by the charge system same tick via its command buffer (the charge
 * system is the sole writer of the CHARGE overlay).
 *
 * @param cell    packed tile position of the chargeable tile
 * @param deltaCu signed charge delta in charge units (cu)
 */
public record ExternalChargeApplied(int cell, int deltaCu) implements SimEvent {
}
