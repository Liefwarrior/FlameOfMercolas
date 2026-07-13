package com.trojia.sim.progression;

/**
 * The attribute a skill's raws row declares as governing (PROGRESSION-SPEC.md
 * &sect;2 "Gov" column). {@link #NONE} is reserved for The Flame, whose
 * governing attribute is literally "&mdash; (see &sect;7)" in the spec table:
 * The Flame is not fed by, and does not feed, any of the four derived
 * attributes.
 *
 * <p>Presence (PRS) is deliberately absent here: PRS is a static constant
 * (&sect;5), never derived from skills, so it is never a skill's governing
 * attribute.</p>
 */
public enum GoverningAttribute {
    /** Might. */
    MGT,
    /** Agility. */
    AGI,
    /** Vigor. */
    VIG,
    /** Wits. */
    WIT,
    /** No governing attribute (The Flame only, PROGRESSION-SPEC.md &sect;7). */
    NONE
}
