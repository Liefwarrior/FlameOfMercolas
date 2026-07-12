package com.trojia.tools.tmx;

import java.util.Objects;

/**
 * One Tiled custom property: a name, a declared {@link TmxPropertyType}, and the raw
 * serialized value exactly as it appeared in the document ({@code value} attribute, or
 * element text for multi-line strings).
 *
 * <p>Immutable. Typed accessors parse on demand and never mutate.</p>
 *
 * @param name  property name, never {@code null} or blank
 * @param type  declared value type, never {@code null}
 * @param value raw serialized value, never {@code null} (may be empty)
 */
public record TmxProperty(String name, TmxPropertyType type, String value) {

    /**
     * @throws NullPointerException     if any component is {@code null}
     * @throws IllegalArgumentException if {@code name} is blank
     */
    public TmxProperty {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(type, "type");
        Objects.requireNonNull(value, "value");
        if (name.isBlank()) {
            throw new IllegalArgumentException("property name must not be blank");
        }
    }

    /**
     * @return the value as an {@code int}
     * @throws IllegalStateException if {@link #type} is neither {@link TmxPropertyType#INT}
     *                               nor {@link TmxPropertyType#OBJECT}
     * @throws NumberFormatException if the raw value is not a valid integer
     */
    public int asInt() {
        if (type != TmxPropertyType.INT && type != TmxPropertyType.OBJECT) {
            throw new IllegalStateException("property \"" + name + "\" has type " + type + ", not INT/OBJECT");
        }
        return Integer.parseInt(value);
    }

    /**
     * @return the value as a {@code double}
     * @throws IllegalStateException if {@link #type} is not {@link TmxPropertyType#FLOAT}
     * @throws NumberFormatException if the raw value is not a valid number
     */
    public double asDouble() {
        if (type != TmxPropertyType.FLOAT) {
            throw new IllegalStateException("property \"" + name + "\" has type " + type + ", not FLOAT");
        }
        return Double.parseDouble(value);
    }

    /**
     * @return {@code true} iff the raw value is the literal {@code "true"}
     * @throws IllegalStateException if {@link #type} is not {@link TmxPropertyType#BOOL}
     */
    public boolean asBool() {
        if (type != TmxPropertyType.BOOL) {
            throw new IllegalStateException("property \"" + name + "\" has type " + type + ", not BOOL");
        }
        return Boolean.parseBoolean(value);
    }
}
