package com.trojia.sim.json;

/**
 * An immutable JSON boolean value.
 *
 * <p>Prefer the shared {@link #TRUE}/{@link #FALSE} constants (or {@link #of(boolean)})
 * when building trees programmatically; record equality makes fresh instances
 * equivalent either way.</p>
 *
 * @param value the boolean value
 */
public record JsonBool(boolean value) implements JsonValue {

    /** The shared {@code true} instance. */
    public static final JsonBool TRUE = new JsonBool(true);

    /** The shared {@code false} instance. */
    public static final JsonBool FALSE = new JsonBool(false);

    /**
     * Returns the shared instance for the given value.
     *
     * @param value the boolean value
     * @return {@link #TRUE} or {@link #FALSE}
     */
    public static JsonBool of(boolean value) {
        return value ? TRUE : FALSE;
    }

    /**
     * Returns the canonical JSON text: {@code "true"} or {@code "false"}.
     */
    @Override
    public String toString() {
        return Boolean.toString(value);
    }
}
