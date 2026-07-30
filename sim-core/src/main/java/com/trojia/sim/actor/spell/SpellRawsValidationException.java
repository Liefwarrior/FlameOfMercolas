package com.trojia.sim.actor.spell;

/**
 * The fail-fast raws error for {@code content/raws/spells/spells.json}, mirroring
 * {@code SkillRawsValidationException}: a malformed spell is a content bug and must name the
 * exact field, never degrade into a silently-missing button.
 */
public final class SpellRawsValidationException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    /** The {@code field} value for a failure that belongs to the file, not to any one field. */
    public static final String NO_FIELD = "(file)";

    private final String file;
    private final String field;

    public SpellRawsValidationException(String file, String field, String message) {
        super(file + " [" + field + "]: " + message);
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
