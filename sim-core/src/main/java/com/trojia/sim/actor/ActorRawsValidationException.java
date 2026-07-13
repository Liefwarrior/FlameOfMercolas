package com.trojia.sim.actor;

import java.util.Objects;

/**
 * A fail-fast raws boot error for the actor package (mirrors
 * {@code com.trojia.sim.material.RawsValidationException}'s shape): every
 * instance names the offending file and dotted field so a content author can
 * fix the raw without reading loader source (ACTORS-SPEC.md §6, §10.3, §11.5).
 */
public final class ActorRawsValidationException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    /** Field path used when a failure is not attributable to a single field. */
    public static final String NO_FIELD = "-";

    private final String file;
    private final String field;

    public ActorRawsValidationException(String file, String field, String detail) {
        super(Objects.requireNonNull(file, "file") + ": "
                + Objects.requireNonNull(field, "field") + ": "
                + Objects.requireNonNull(detail, "detail"));
        this.file = file;
        this.field = field;
    }

    public String file() {
        return file;
    }

    public String field() {
        return field;
    }
}
