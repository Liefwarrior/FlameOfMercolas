package com.trojia.sim.event;

/**
 * Pooled fluid units left the FLUID lane as vapor (FLUIDS phase; fluids own
 * evaporation and boil of pooled fluid). Lap-consumed by the economy
 * accumulator; also the future steam/aether seam.
 *
 * @param cell    packed position the fluid vanished from
 * @param fluidId fluid id
 * @param units   units removed
 * @param cause   {@link #CAUSE_BOILED} or {@link #CAUSE_EVAPORATED}
 */
public record FluidVaporizedEvent(int cell, short fluidId, int units, int cause)
        implements SimEvent {

    /** Cause: temperature above the fluid's boil threshold. */
    public static final int CAUSE_BOILED = 0;

    /** Cause: ambient (hash-phase) evaporation of shallow settled fluid. */
    public static final int CAUSE_EVAPORATED = 1;
}
