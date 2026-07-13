package com.trojia.sim.progression;

import java.util.Objects;

/**
 * A fail-fast skills-raws boot error, mirroring
 * {@code com.trojia.sim.material.RawsValidationException}. Every instance
 * names the offending <em>file</em> (relative to the raws root) and
 * <em>field</em> (dotted path within the raw, or {@link #NO_FIELD} for a
 * file-level failure).
 */
public final class SkillRawsValidationException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    /** Field path used when a failure is not attributable to a single field. */
    public static final String NO_FIELD = "-";

    private final String file;
    private final String field;

    /**
     * Creates a positioned raws error; the message renders as
     * {@code "<file>: <field>: <detail>"}.
     *
     * @param file   the raws file, relative to the raws root
     * @param field  the dotted field path, or {@link #NO_FIELD}
     * @param detail what is wrong and what would be right
     * @throws NullPointerException if any argument is {@code null}
     */
    public SkillRawsValidationException(String file, String field, String detail) {
        super(Objects.requireNonNull(file, "file") + ": "
                + Objects.requireNonNull(field, "field") + ": "
                + Objects.requireNonNull(detail, "detail"));
        this.file = file;
        this.field = field;
    }

    /** Returns the offending raws file, relative to the raws root. */
    public String file() {
        return file;
    }

    /** Returns the offending dotted field path, or {@link #NO_FIELD}. */
    public String field() {
        return field;
    }
}
