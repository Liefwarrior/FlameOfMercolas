package com.trojia.tools.palette;

/**
 * Signals that palette generation failed deterministically: unreadable or malformed
 * raws, schema violations (missing {@code id}/{@code phase}), duplicate or colliding
 * material ids, or a treatment whose target does not exist.
 *
 * <p>Generation is all-or-nothing — this exception means no output was (or should be)
 * written.</p>
 */
public class PaletteGenerationException extends RuntimeException {

    /**
     * @param message human-readable failure description naming the offending file or id
     */
    public PaletteGenerationException(String message) {
        super(message);
    }

    /**
     * @param message human-readable failure description naming the offending file or id
     * @param cause   underlying failure, may be {@code null}
     */
    public PaletteGenerationException(String message, Throwable cause) {
        super(message, cause);
    }
}
