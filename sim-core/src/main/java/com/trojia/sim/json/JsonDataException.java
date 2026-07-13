package com.trojia.sim.json;

/**
 * Thrown when a well-formed JSON tree is accessed with the wrong expectation — e.g.
 * {@link JsonNumber#asLong()} on a fractional literal, or an integer accessor whose
 * value overflows the requested primitive type.
 *
 * <p>Distinct from {@link JsonParseException}: parse errors mean the <em>text</em> was
 * malformed (and carry line/column); data errors mean the <em>tree</em> did not fit
 * the caller's schema expectation.</p>
 */
public final class JsonDataException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    /**
     * Creates a data-access error.
     *
     * @param message description of the mismatch
     */
    public JsonDataException(String message) {
        super(message);
    }
}
