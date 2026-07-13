package com.trojia.sim.json;

import java.util.Objects;

/**
 * An immutable JSON number that preserves its exact source literal.
 *
 * <p>The number is stored as the validated literal text (e.g. {@code "240"},
 * {@code "-0"}, {@code "1.50"}, {@code "1e2"}), never as a converted binary value.
 * This makes round-trips byte-exact ({@link MiniJson#write(JsonValue)} emits the
 * literal verbatim) and keeps equality deterministic: two numbers are equal iff their
 * literals are equal — {@code "1"} and {@code "1.0"} are <em>different</em> values.</p>
 *
 * <p><strong>Integer accessors are strict.</strong> {@link #asLong()} and
 * {@link #asInt()} accept only pure integer literals (no fraction, no exponent —
 * {@code "1e2"} is rejected even though it denotes an integer) and throw
 * {@link JsonDataException} on any overflow. This is the accessor-level half of the
 * integer-only raws policy; the parse-level half is {@link JsonNumberMode#INTEGER_ONLY}.</p>
 */
public final class JsonNumber implements JsonValue {

    /** The exact, validated JSON number literal. */
    private final String literal;

    /**
     * Creates a number from a JSON number literal.
     *
     * @param literal the literal text; must match the RFC 8259 number grammar
     *                {@code -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?}
     *                (so no leading zeros, no {@code +} sign, no {@code NaN}/{@code Infinity},
     *                no lone {@code .})
     * @throws NullPointerException     if {@code literal} is {@code null}
     * @throws IllegalArgumentException if {@code literal} violates the grammar
     */
    public JsonNumber(String literal) {
        Objects.requireNonNull(literal, "literal");
        if (!isValidLiteral(literal)) {
            throw new IllegalArgumentException("not a valid JSON number literal: \"" + literal + "\"");
        }
        this.literal = literal;
    }

    /**
     * Returns the number for the given long value (literal {@code Long.toString(value)}).
     *
     * @param value the value
     * @return a number whose literal is the decimal rendering of {@code value}
     */
    public static JsonNumber of(long value) {
        return new JsonNumber(Long.toString(value));
    }

    /**
     * Returns the exact source literal.
     *
     * @return the literal text; never {@code null}
     */
    public String literal() {
        return literal;
    }

    /**
     * Returns whether this literal is a pure integer (no fraction part, no exponent).
     *
     * <p>Only integral numbers may be read via {@link #asLong()}/{@link #asInt()}.
     * {@code "-0"} is integral; {@code "1.0"} and {@code "1e2"} are not.</p>
     *
     * @return {@code true} iff the literal contains no {@code '.'}, {@code 'e'} or {@code 'E'}
     */
    public boolean isIntegral() {
        return literal.indexOf('.') < 0 && literal.indexOf('e') < 0 && literal.indexOf('E') < 0;
    }

    /**
     * Returns this number as a {@code long}.
     *
     * @return the exact long value ({@code "-0"} yields {@code 0})
     * @throws JsonDataException if the literal is not integral (has a fraction or
     *                           exponent), or if it lies outside
     *                           [{@link Long#MIN_VALUE}, {@link Long#MAX_VALUE}]
     */
    public long asLong() {
        if (!isIntegral()) {
            throw new JsonDataException("not an integer literal: " + literal);
        }
        try {
            return Long.parseLong(literal);
        } catch (NumberFormatException e) {
            throw new JsonDataException("integer out of long range: " + literal);
        }
    }

    /**
     * Returns this number as an {@code int}.
     *
     * @return the exact int value
     * @throws JsonDataException if the literal is not integral, or if it lies outside
     *                           [{@link Integer#MIN_VALUE}, {@link Integer#MAX_VALUE}]
     */
    public int asInt() {
        long value = asLong();
        if (value < Integer.MIN_VALUE || value > Integer.MAX_VALUE) {
            throw new JsonDataException("integer out of int range: " + literal);
        }
        return (int) value;
    }

    /**
     * Returns this number as a {@code double} via {@link Double#parseDouble(String)}.
     *
     * <p>Deterministic (IEEE-754 round-to-nearest, identical across conforming JVMs)
     * but potentially lossy for magnitudes beyond 2^53; integer raws must use
     * {@link #asLong()}/{@link #asInt()} instead.</p>
     *
     * @return the double value
     */
    public double asDouble() {
        return Double.parseDouble(literal);
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof JsonNumber that && literal.equals(that.literal);
    }

    @Override
    public int hashCode() {
        return literal.hashCode();
    }

    /**
     * Returns the canonical JSON text: the literal itself.
     */
    @Override
    public String toString() {
        return literal;
    }

    /**
     * Validates a candidate literal against the RFC 8259 number grammar.
     *
     * @param s the candidate text
     * @return {@code true} iff {@code s} is a complete, valid JSON number
     */
    private static boolean isValidLiteral(String s) {
        int i = 0;
        int n = s.length();
        if (i < n && s.charAt(i) == '-') {
            i++;
        }
        if (i >= n) {
            return false;
        }
        char first = s.charAt(i);
        if (first == '0') {
            i++;
        } else if (first >= '1' && first <= '9') {
            while (i < n && isDigit(s.charAt(i))) {
                i++;
            }
        } else {
            return false;
        }
        if (i < n && s.charAt(i) == '.') {
            i++;
            if (i >= n || !isDigit(s.charAt(i))) {
                return false;
            }
            while (i < n && isDigit(s.charAt(i))) {
                i++;
            }
        }
        if (i < n && (s.charAt(i) == 'e' || s.charAt(i) == 'E')) {
            i++;
            if (i < n && (s.charAt(i) == '+' || s.charAt(i) == '-')) {
                i++;
            }
            if (i >= n || !isDigit(s.charAt(i))) {
                return false;
            }
            while (i < n && isDigit(s.charAt(i))) {
                i++;
            }
        }
        return i == n;
    }

    private static boolean isDigit(char c) {
        return c >= '0' && c <= '9';
    }
}
