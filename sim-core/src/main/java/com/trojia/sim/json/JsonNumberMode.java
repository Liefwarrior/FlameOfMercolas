package com.trojia.sim.json;

/**
 * Parse-time policy for JSON number literals.
 *
 * <p>Raws files are integer-only by project convention (temperatures are integer
 * Kelvin, fixed-point values are Q8/Q16 integers — ARCHITECTURE.md §1.1 #13).
 * {@link #INTEGER_ONLY} lets a loader enforce that hygiene rule at parse time with a
 * precise {@code line:column} error instead of discovering a stray {@code 0.5} deep
 * inside registry construction.</p>
 */
public enum JsonNumberMode {

    /**
     * Accept every number the JSON grammar allows (RFC 8259): optional sign,
     * integer part, optional fraction, optional exponent.
     */
    ANY,

    /**
     * Accept only pure integer literals ({@code -?(0|[1-9][0-9]*)}). A fraction
     * ({@code .}) or exponent ({@code e}/{@code E}) anywhere in the document is a
     * {@link JsonParseException} positioned at the offending character.
     */
    INTEGER_ONLY
}
