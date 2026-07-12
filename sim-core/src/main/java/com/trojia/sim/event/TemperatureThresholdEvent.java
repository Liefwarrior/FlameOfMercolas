package com.trojia.sim.event;

/**
 * Thermal diffusion carried a tile across a registered temperature threshold.
 * Consumed by fluids next tick (e.g. wake settled water near a heat source).
 *
 * @param cell        packed position that crossed the threshold
 * @param thresholdId id of the crossed threshold (registered at boot)
 * @param direction   {@link #RISING} or {@link #FALLING}
 */
public record TemperatureThresholdEvent(int cell, int thresholdId, int direction)
        implements SimEvent {

    /** Direction: temperature rose across the threshold. */
    public static final int RISING = 1;

    /** Direction: temperature fell across the threshold. */
    public static final int FALLING = -1;
}
