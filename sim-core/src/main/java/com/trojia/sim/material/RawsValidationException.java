package com.trojia.sim.material;

import java.util.Objects;

/**
 * A fail-fast raws boot error (ARCHITECTURE.md §10: loader validation failures
 * abort the boot). Every instance names the offending <em>file</em> (relative
 * to the raws root, {@code '/'}-separated) and <em>field</em> (dotted path
 * within the raw, e.g. {@code features.chargeable.capacityCu}; {@code "-"}
 * when the failure is file-level, e.g. malformed JSON), so a content author
 * can fix the raw without reading loader source.
 *
 * <p>Unchecked because raws errors are unrecoverable configuration faults, not
 * conditions callers should handle.</p>
 */
public final class RawsValidationException extends RuntimeException {

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
    public RawsValidationException(String file, String field, String detail) {
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
