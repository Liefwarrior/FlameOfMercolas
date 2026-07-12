package com.trojia.sim.event;

/**
 * A charged tile crossed a color-stop boundary of its chargeable material
 * (REACTIONS phase, charge sub-system). Consumed by light same tick (stop
 * light levels) and drained to observer logs (appearance bucket).
 *
 * @param cell    packed position of the charged tile
 * @param oldStop previous color-stop ordinal (0..3)
 * @param newStop new color-stop ordinal (0..3)
 */
public record ChargeStopChangedEvent(int cell, int oldStop, int newStop) implements SimEvent {
}
