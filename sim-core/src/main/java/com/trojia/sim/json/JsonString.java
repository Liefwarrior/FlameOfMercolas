package com.trojia.sim.json;

import java.util.Objects;

/**
 * An immutable JSON string value.
 *
 * <p>The held {@code value} is the string <em>after</em> escape resolution — e.g. the
 * source text {@code "A"} yields the value {@code "A"}. The parser guarantees the
 * value is well-formed UTF-16 (no unpaired surrogates), so it re-encodes losslessly.</p>
 *
 * @param value the decoded string content; never {@code null}
 */
public record JsonString(String value) implements JsonValue {

    /**
     * @throws NullPointerException if {@code value} is {@code null}
     */
    public JsonString {
        Objects.requireNonNull(value, "value");
    }

    /**
     * Returns the canonical JSON text of this string (quoted, minimally escaped) per
     * {@link MiniJson#write(JsonValue)}.
     */
    @Override
    public String toString() {
        return MiniJson.write(this);
    }
}
