package com.trojia.sim.event;

/**
 * The fluid system consumed reagent fluid units against a contact-reactive
 * solid (FLUIDS phase). The phorys reaction system consumes it same tick and
 * owns the resulting pressure pulse and wear accounting.
 *
 * @param cell            packed position of the contact tile
 * @param fluidId         reagent fluid id
 * @param units           fluid units consumed by the contact
 * @param solidMaterialId material id of the reactive solid
 */
public record ReagentContactEvent(int cell, short fluidId, int units, short solidMaterialId)
        implements SimEvent {
}
