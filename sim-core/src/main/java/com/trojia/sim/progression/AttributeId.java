package com.trojia.sim.progression;

/**
 * The four skill-derived attributes of PROGRESSION-SPEC.md &sect;5. Presence
 * (PRS) is deliberately excluded: it is a static constant (see
 * {@link AttributeCalculator#PRESENCE}), never derived from skills, never
 * trainable, never lowerable.
 */
public enum AttributeId {
    /** Might. */
    MGT,
    /** Agility. */
    AGI,
    /** Vigor. */
    VIG,
    /** Wits. */
    WIT
}
