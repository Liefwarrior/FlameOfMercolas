package com.trojia.sim.json;

/**
 * The JSON {@code null} value.
 *
 * <p>A singleton: the only instance is {@link #INSTANCE}, so identity comparison and
 * {@code equals} coincide. Note the tree-level distinction it enables: a member whose
 * value is JSON {@code null} is <em>present</em> with value {@code INSTANCE}, whereas
 * {@link JsonObject#get(String)} returns Java {@code null} only for a <em>missing</em>
 * member.</p>
 */
public final class JsonNull implements JsonValue {

    /** The sole instance. */
    public static final JsonNull INSTANCE = new JsonNull();

    private JsonNull() {
    }

    /**
     * Returns the canonical JSON text: {@code "null"}.
     */
    @Override
    public String toString() {
        return "null";
    }
}
