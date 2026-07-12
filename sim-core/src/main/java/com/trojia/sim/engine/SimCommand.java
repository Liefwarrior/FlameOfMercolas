package com.trojia.sim.engine;

import com.trojia.sim.world.TileForm;

/**
 * The sealed vocabulary of external inputs (player brushes, scenario scripts,
 * debug tools). Commands are submitted in arrival order, drained by the
 * InputGate at TICK_BEGIN into the input log, and materialized either as
 * direct paints via ChunkWriter (material/form commands) or as {@code
 * External*} events consumed by the owning system (quantity commands) —
 * the gate never mutates system-owned lanes itself.
 *
 * <p>Records of primitives + ids only; cells are packed positions. Scenario
 * scripts are replayable sequences of these (ARCHITECTURE.md §1.2).
 */
public sealed interface SimCommand {

    /**
     * Paints one tile's material + form (the only direct-write command pair
     * with {@link ClearTile}).
     *
     * @param cell       packed target position
     * @param materialId material to paint
     * @param form       structural form to paint
     */
    record PaintMaterial(int cell, short materialId, TileForm form) implements SimCommand {
    }

    /**
     * Clears one tile to OPEN (air).
     *
     * @param cell packed target position
     */
    record ClearTile(int cell) implements SimCommand {
    }

    /**
     * Requests ignition; materialized as an {@code ExternalIgnition} event for
     * the fire system.
     *
     * @param cell packed target position
     */
    record Ignite(int cell) implements SimCommand {
    }

    /**
     * Requests extinguishing; consumed by the fire system.
     *
     * @param cell packed target position
     */
    record Extinguish(int cell) implements SimCommand {
    }

    /**
     * Adds fluid; materialized as an {@code ExternalFluidSpawned} event for
     * the fluid system.
     *
     * @param cell    packed target position
     * @param fluidId fluid registry id
     * @param units   units to add (positive)
     */
    record AddFluid(int cell, short fluidId, int units) implements SimCommand {
    }

    /**
     * Removes fluid; consumed by the fluid system (clamped to what is there).
     *
     * @param cell    packed target position
     * @param fluidId fluid registry id
     * @param units   units to remove (positive)
     */
    record RemoveFluid(int cell, short fluidId, int units) implements SimCommand {
    }

    /**
     * Injects (or drains, when negative) charge; materialized as an
     * {@code ExternalChargeApplied} event for the charge system.
     *
     * @param cell    packed target position
     * @param deltaCu signed charge delta in cu
     */
    record InjectCharge(int cell, int deltaCu) implements SimCommand {
    }

    /**
     * Places (or moves) a non-heat-emitting light source under a
     * caller-chosen handle; stacking resolves as max at the seed.
     *
     * @param handle caller-chosen stable source handle (registry slot key)
     * @param cell   packed source position
     * @param level  block light level 0..31
     */
    record PlaceLightSource(int handle, int cell, int level) implements SimCommand {
    }

    /**
     * Removes the light source registered under {@code handle}.
     *
     * @param handle the handle used at placement
     */
    record RemoveLightSource(int handle) implements SimCommand {
    }

    /**
     * Moves the bubble focus point; the ticket retargeter diffs against it at
     * the next BUBBLE_PROMOTE.
     *
     * @param cell packed position of the new focus
     */
    record SetFocus(int cell) implements SimCommand {
    }
}
