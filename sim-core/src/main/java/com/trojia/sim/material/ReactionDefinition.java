package com.trojia.sim.material;

import java.util.Objects;

/**
 * One fluid-contact reaction parsed from {@code raws/reactions/**} (the phorys
 * hydration raw), held by {@link MaterialRegistry} for the F3 reaction systems.
 * v0 knows exactly one trigger kind, {@code FLUID_CONTACT}: the fluid system's
 * REAGENT sub-phase consumes contacting units of any fluid carrying
 * {@code triggerFluidTag} and emits {@code ReagentContactEvent}; the phorys
 * reaction system turns the consumed units into a pressure pulse of magnitude
 * {@code unitsConsumed * expansion}, clamped to {@code pulseMagnitudeCap}.
 *
 * <p><strong>Wear is optional-reserved</strong> (BLESSING-QUEUE ruling 9):
 * absent wear fields in the raw mean <em>no wear</em> — the solid is
 * inexhaustible ({@code wearPerUnit == 0 && wearCapacity == 0}). The fields
 * stay in the schema so a post-v0 resource-economy hook needs no format
 * change.</p>
 *
 * @param key               unique string id from the raw
 * @param displayName       human-readable name
 * @param solidId           material id of the reactive solid; must carry
 *                          {@link MaterialFeature.ContactReactive}
 * @param triggerFluidTag   fluid tag that triggers the reaction (matching is by
 *                          tag, never by fluid id)
 * @param expansion         gas-units produced per consumed liquid unit; 1..65535
 * @param wearPerUnit       wear points per consumed unit; 0 = no wear
 * @param wearCapacity      wear points until the solid is spent; 0 = no wear
 * @param pulseGasId        fluid id of the produced gas, or {@code null} (v0:
 *                          pressure-only pulse — no gas fluid exists yet)
 * @param pulseMagnitudeCap upper clamp on a single pulse magnitude; 1..65535
 */
public record ReactionDefinition(
        String key,
        String displayName,
        String solidId,
        String triggerFluidTag,
        int expansion,
        int wearPerUnit,
        int wearCapacity,
        String pulseGasId,
        int pulseMagnitudeCap) {

    /**
     * Validates cheap structural invariants (full §10 validation happens in the
     * loader with file/field context).
     *
     * @throws NullPointerException if a required reference is {@code null}
     */
    public ReactionDefinition {
        Objects.requireNonNull(key, "key");
        Objects.requireNonNull(displayName, "displayName");
        Objects.requireNonNull(solidId, "solidId");
        Objects.requireNonNull(triggerFluidTag, "triggerFluidTag");
    }

    /** Returns whether this reaction wears the solid out (false in all of v0). */
    public boolean wears() {
        return wearPerUnit > 0 && wearCapacity > 0;
    }
}
