package com.trojia.sim.event;

/**
 * Notification that a tile's material was swapped by its owning system (the
 * swap itself already happened via ChunkWriter — this event never causes the
 * write). Lap-consumed by the economy accumulator (damage attribution);
 * drained to observer logs. Light wakes via change logs, not this.
 *
 * @param cell           packed position of the transformed tile
 * @param fromMaterialId material before the transformation
 * @param toMaterialId   material after the transformation
 * @param cause          one of the {@code CAUSE_*} constants
 */
public record MaterialTransformedEvent(int cell, short fromMaterialId, short toMaterialId,
        int cause) implements SimEvent {

    /** Cause: fire fuel exhausted; swapped to the material's {@code burnsTo}. */
    public static final int CAUSE_BURNOUT = 0;

    /** Cause: lightstone shatter (discharge spike within Chebyshev distance 2). */
    public static final int CAUSE_SHATTER = 1;

    /** Cause: reagent solid fully worn/spent by reactions. */
    public static final int CAUSE_SPENT = 2;

    /** Cause: thermal phase transition (melt/boil/freeze of a grid material). */
    public static final int CAUSE_PHASE = 3;
}
