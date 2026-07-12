package com.trojia.sim.event;

/**
 * The phase transition system melted/boiled/froze a grid material (THERMAL
 * phase). Fluids consume it next tick to spawn or remove the pooled liquid
 * ({@code yieldUnits} comes from the material's {@code meltYieldUnits} raw).
 *
 * @param cell           packed position of the transitioned tile
 * @param fromMaterialId material before the transition
 * @param toMaterialId   material after the transition
 * @param yieldUnits     fluid units to spawn (positive) or remove (zero when none)
 */
public record MaterialPhaseChangedEvent(int cell, short fromMaterialId, short toMaterialId,
        int yieldUnits) implements SimEvent {
}
