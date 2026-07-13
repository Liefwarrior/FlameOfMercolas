package com.trojia.tools.validate;

import java.util.Objects;

/**
 * One human-readable validation finding (the "validator error UX" of ARCHITECTURE.md
 * section 12, M1): what went wrong, where, and how to fix it.
 *
 * <p>Immutable value. Coordinates use {@link #NO_COORD} when the finding is not tied
 * to a single tile; {@link #layerPath} is empty when the finding is map- or file-wide.</p>
 *
 * @param severity  {@link Severity#ERROR} blocks import; {@link Severity#WARNING} never does
 * @param passId    stable id of the emitting pass (e.g. {@code "materials"}), never {@code null} or blank
 * @param source    map file name or raws file path the finding is about, never {@code null} or blank
 * @param layerPath offending layer path (e.g. {@code "z:+0/terrain"}), never {@code null}, may be empty
 * @param x         tile column, or {@link #NO_COORD}
 * @param y         tile row, or {@link #NO_COORD}
 * @param message   one-sentence statement of the defect, never {@code null} or blank
 * @param hint      concrete fix hint, never {@code null}, may be empty
 */
public record ValidationIssue(Severity severity, String passId, String source, String layerPath,
                              int x, int y, String message, String hint) {

    /** Sentinel for "no tile coordinate applies". */
    public static final int NO_COORD = -1;

    /** Finding severity. Only {@code ERROR} findings fail a check (exit code 1). */
    public enum Severity {
        /** Blocks import; the content must be fixed. */
        ERROR,
        /** Advisory only; never fails validation. */
        WARNING
    }

    /**
     * @throws NullPointerException     if any reference component is {@code null}
     * @throws IllegalArgumentException if {@code passId}, {@code source} or {@code message}
     *                                  is blank, or exactly one coordinate is {@link #NO_COORD}
     */
    public ValidationIssue {
        Objects.requireNonNull(severity, "severity");
        Objects.requireNonNull(passId, "passId");
        Objects.requireNonNull(source, "source");
        Objects.requireNonNull(layerPath, "layerPath");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(hint, "hint");
        if (passId.isBlank() || source.isBlank() || message.isBlank()) {
            throw new IllegalArgumentException("passId, source and message must not be blank");
        }
        if ((x == NO_COORD) != (y == NO_COORD)) {
            throw new IllegalArgumentException("coordinates must be both set or both NO_COORD: (" + x + "," + y + ")");
        }
    }

    /** @return {@code true} iff this finding carries a tile coordinate */
    public boolean hasCoordinates() {
        return x != NO_COORD;
    }

    /**
     * Renders the single-line human-readable form, e.g.
     * {@code ERROR [materials] tavern.tmx z:+0/terrain (13,15): unknown material "granit". Fix: did you mean "granite"?}
     *
     * @return the formatted line, never {@code null}
     */
    public String format() {
        StringBuilder sb = new StringBuilder(96);
        sb.append(severity).append(" [").append(passId).append("] ").append(source);
        if (!layerPath.isEmpty()) {
            sb.append(' ').append(layerPath);
        }
        if (hasCoordinates()) {
            sb.append(" (").append(x).append(',').append(y).append(')');
        }
        sb.append(": ").append(message);
        if (!hint.isEmpty()) {
            sb.append(" Fix: ").append(hint);
        }
        return sb.toString();
    }
}
