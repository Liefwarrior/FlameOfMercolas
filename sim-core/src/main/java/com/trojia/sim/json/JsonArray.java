package com.trojia.sim.json;

import java.util.List;

/**
 * An immutable JSON array.
 *
 * <p>Elements keep their source order. Equality is element-wise and order-sensitive.</p>
 */
public final class JsonArray implements JsonValue {

    /** Elements in source order; immutable. */
    private final List<JsonValue> values;

    /**
     * Creates an array holding the given elements in the given order.
     *
     * <p>The list is defensively copied; later mutation of the argument does not
     * affect this array.</p>
     *
     * @param values the elements in order; JSON {@code null} elements are represented
     *               by {@link JsonNull#INSTANCE}
     * @throws NullPointerException if {@code values} or any element is {@code null}
     */
    public JsonArray(List<JsonValue> values) {
        this.values = List.copyOf(values);
    }

    /**
     * Returns the elements in source order.
     *
     * @return an immutable list; never {@code null}
     */
    public List<JsonValue> values() {
        return values;
    }

    /**
     * Returns the number of elements.
     *
     * @return the element count, {@code >= 0}
     */
    public int size() {
        return values.size();
    }

    /**
     * Returns the element at the given index.
     *
     * @param index the zero-based element index
     * @return the element; never {@code null}
     * @throws IndexOutOfBoundsException if {@code index < 0 || index >= size()}
     */
    public JsonValue get(int index) {
        return values.get(index);
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof JsonArray that && values.equals(that.values);
    }

    @Override
    public int hashCode() {
        return values.hashCode();
    }

    /**
     * Returns the canonical JSON text of this array per {@link MiniJson#write(JsonValue)}.
     */
    @Override
    public String toString() {
        return MiniJson.write(this);
    }
}
