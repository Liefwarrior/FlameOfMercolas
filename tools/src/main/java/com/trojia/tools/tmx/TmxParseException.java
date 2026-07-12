package com.trojia.tools.tmx;

/**
 * Raised when a {@code .tmx} or {@code .tsx} document is malformed, uses a feature
 * outside the v0 scope (see {@link com.trojia.tools.tmx package docs}), or violates a
 * model invariant (e.g. duplicate property names).
 *
 * <p>Messages include the 1-based line/column of the offending construct whenever the
 * underlying StAX reader can supply a location.</p>
 */
public class TmxParseException extends RuntimeException {

    /**
     * @param message human-readable description, never {@code null}
     */
    public TmxParseException(String message) {
        super(message);
    }

    /**
     * @param message human-readable description, never {@code null}
     * @param cause   underlying failure, typically an {@code XMLStreamException}
     */
    public TmxParseException(String message, Throwable cause) {
        super(message, cause);
    }
}
