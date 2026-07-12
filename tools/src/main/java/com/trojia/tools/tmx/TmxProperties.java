package com.trojia.tools.tmx;

import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * Immutable, ordered collection of Tiled custom properties.
 *
 * <p><strong>Determinism contract:</strong> {@link #asList()} preserves document order
 * exactly. Property names are unique within one block; a document carrying duplicates
 * is rejected at construction (deterministic hard fail rather than silent last-wins).</p>
 */
public final class TmxProperties {

    private static final TmxProperties EMPTY = new TmxProperties(List.of());

    private final List<TmxProperty> properties;

    private TmxProperties(List<TmxProperty> properties) {
        this.properties = properties;
    }

    /** @return the shared empty instance */
    public static TmxProperties empty() {
        return EMPTY;
    }

    /**
     * @param properties properties in document order; defensively copied
     * @return an immutable view over a copy of {@code properties}
     * @throws NullPointerException if the list or any element is {@code null}
     * @throws TmxParseException    if two properties share a name
     */
    public static TmxProperties of(List<TmxProperty> properties) {
        Objects.requireNonNull(properties, "properties");
        if (properties.isEmpty()) {
            return EMPTY;
        }
        List<TmxProperty> copy = List.copyOf(properties);
        for (int i = 0; i < copy.size(); i++) {
            for (int j = i + 1; j < copy.size(); j++) {
                if (copy.get(i).name().equals(copy.get(j).name())) {
                    throw new TmxParseException("duplicate property name \"" + copy.get(i).name() + "\"");
                }
            }
        }
        return new TmxProperties(copy);
    }

    /** @return properties in document order; immutable */
    public List<TmxProperty> asList() {
        return properties;
    }

    /**
     * @param name property name to look up, never {@code null}
     * @return the property with that name, if present
     */
    public Optional<TmxProperty> find(String name) {
        Objects.requireNonNull(name, "name");
        for (TmxProperty p : properties) {
            if (p.name().equals(name)) {
                return Optional.of(p);
            }
        }
        return Optional.empty();
    }

    /** @return {@code true} iff no properties are present */
    public boolean isEmpty() {
        return properties.isEmpty();
    }

    /** @return number of properties */
    public int size() {
        return properties.size();
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof TmxProperties other && properties.equals(other.properties);
    }

    @Override
    public int hashCode() {
        return properties.hashCode();
    }

    @Override
    public String toString() {
        return "TmxProperties" + properties;
    }
}
