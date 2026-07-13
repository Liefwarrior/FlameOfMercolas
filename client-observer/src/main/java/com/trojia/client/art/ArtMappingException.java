package com.trojia.client.art;

/**
 * Thrown when {@code art-mapping.json} fails load-time validation
 * (TILE-ART-SPEC section 7.2 — "boot fails").
 *
 * <p>The message aggregates <em>every</em> problem found, one per line, mirroring the
 * spec's "report the full list of missing names, not just the first" rule so one boot
 * failure surfaces all mapping defects at once.
 */
public final class ArtMappingException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    public ArtMappingException(String message) {
        super(message);
    }

    public ArtMappingException(String message, Throwable cause) {
        super(message, cause);
    }
}
