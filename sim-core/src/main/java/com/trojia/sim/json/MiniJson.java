package com.trojia.sim.json;

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CoderResult;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;

/**
 * A strict, minimal, deterministic JSON parser and canonical writer for raws
 * loading. sim-core must stay JDK-only (ARCHITECTURE.md §2), so this replaces
 * Jackson for every in-engine JSON need.
 *
 * <p><strong>Strictness (RFC 8259, no extensions):</strong></p>
 * <ul>
 *   <li>No comments, no trailing commas, no single quotes, no unquoted keys.</li>
 *   <li>No {@code NaN}, {@code Infinity}, leading {@code +}, leading zeros,
 *       lone {@code .5} / {@code 5.} fractions.</li>
 *   <li>Whitespace is exactly space, tab, {@code '\n'}, {@code '\r'}.</li>
 *   <li>Strings reject unescaped control characters (below U+0020), unknown
 *       escapes, malformed {@code \\u} sequences, and unpaired surrogates
 *       (raw or escaped) — parsed strings are always well-formed UTF-16.</li>
 *   <li><strong>Duplicate object keys are rejected</strong> (raws hygiene),
 *       positioned at the offending key.</li>
 *   <li>A leading UTF-8 BOM is rejected (unexpected character), and the
 *       {@code byte[]} entry points decode with a strict UTF-8 decoder that
 *       reports malformed bytes instead of substituting U+FFFD.</li>
 *   <li>Nesting beyond {@link #MAX_DEPTH} levels is rejected (deterministic
 *       error instead of a stack overflow).</li>
 * </ul>
 *
 * <p><strong>Error positions:</strong> every {@link JsonParseException}
 * carries a 1-based {@code line:column} (see the position contract on
 * {@link JsonParseException}). The position pins the <em>offending</em> input:
 * the unexpected character itself; the start of a bad {@code true}/{@code
 * false}/{@code null} literal; the {@code '0'} of a leading-zero number; the
 * spot where a required digit is missing; the {@code '.'} or exponent letter
 * that violates {@link JsonNumberMode#INTEGER_ONLY}; the opening quote of an
 * unterminated string; the backslash of a bad escape; the opening quote of a
 * duplicate key; the bracket that exceeds {@link #MAX_DEPTH}; one past the
 * last character for unexpected end of input.</p>
 *
 * <p><strong>Determinism:</strong> parsing is a pure function of the input
 * bytes/text and mode; no locale, no system properties, no hash-order
 * dependence. {@link #write(JsonValue)} emits <em>canonical</em> text —
 * member insertion order, zero whitespace, number literals verbatim, and
 * minimal string escaping ({@code \" \\ \b \f \n \r \t}, other control
 * characters as lowercase {@code \\u00xx}, everything else raw) — so equal
 * trees produce identical text and {@code write(parse(write(t)))} is a fixed
 * point.</p>
 */
public final class MiniJson {

    /**
     * Maximum object/array nesting depth accepted by the parser. Exceeding it
     * raises a positioned {@link JsonParseException} rather than risking a
     * {@link StackOverflowError} whose depth would vary by JVM settings.
     */
    public static final int MAX_DEPTH = 512;

    private MiniJson() {
    }

    /**
     * Parses JSON text in {@link JsonNumberMode#ANY} mode.
     *
     * @param text the complete JSON document (any top-level value)
     * @return the immutable tree
     * @throws NullPointerException if {@code text} is {@code null}
     * @throws JsonParseException   if the text is not strictly valid JSON
     */
    public static JsonValue parse(String text) {
        return parse(text, JsonNumberMode.ANY);
    }

    /**
     * Parses JSON text under the given number policy.
     *
     * @param text the complete JSON document (any top-level value)
     * @param mode the number policy; {@link JsonNumberMode#INTEGER_ONLY} for raws
     * @return the immutable tree
     * @throws NullPointerException if any argument is {@code null}
     * @throws JsonParseException   if the text is not strictly valid JSON or
     *                              violates the number policy
     */
    public static JsonValue parse(String text, JsonNumberMode mode) {
        Objects.requireNonNull(text, "text");
        Objects.requireNonNull(mode, "mode");
        return new Parser(text, mode).parseDocument();
    }

    /**
     * Decodes UTF-8 bytes strictly, then parses in {@link JsonNumberMode#ANY} mode.
     *
     * @param utf8 the document bytes; must be well-formed UTF-8 (no BOM)
     * @return the immutable tree
     * @throws NullPointerException if {@code utf8} is {@code null}
     * @throws JsonParseException   on malformed UTF-8 (positioned at the end of
     *                              the decodable prefix, message carries the byte
     *                              offset) or invalid JSON
     */
    public static JsonValue parse(byte[] utf8) {
        return parse(utf8, JsonNumberMode.ANY);
    }

    /**
     * Decodes UTF-8 bytes strictly, then parses under the given number policy.
     *
     * @param utf8 the document bytes; must be well-formed UTF-8 (no BOM)
     * @param mode the number policy; {@link JsonNumberMode#INTEGER_ONLY} for raws
     * @return the immutable tree
     * @throws NullPointerException if any argument is {@code null}
     * @throws JsonParseException   on malformed UTF-8 or invalid JSON
     */
    public static JsonValue parse(byte[] utf8, JsonNumberMode mode) {
        Objects.requireNonNull(utf8, "utf8");
        return parse(decodeUtf8(utf8), mode);
    }

    /**
     * Writes the canonical JSON text of a tree: member insertion order, no
     * whitespace, number literals verbatim, minimal string escaping.
     *
     * <p>Canonical output always reparses (in the mode the tree satisfies) to a
     * tree equal to the input, and is itself a fixed point:
     * {@code write(parse(write(t))).equals(write(t))}.</p>
     *
     * @param value the tree to write; strings must be well-formed UTF-16
     *              (guaranteed for parser-produced trees)
     * @return the canonical text; never {@code null}
     * @throws NullPointerException if {@code value} is {@code null}
     */
    public static String write(JsonValue value) {
        Objects.requireNonNull(value, "value");
        StringBuilder sb = new StringBuilder();
        writeValue(sb, value);
        return sb.toString();
    }

    // ------------------------------------------------------------------
    // Writer
    // ------------------------------------------------------------------

    private static final char[] HEX = "0123456789abcdef".toCharArray();

    private static void writeValue(StringBuilder sb, JsonValue value) {
        switch (value) {
            case JsonNull ignored -> sb.append("null");
            case JsonBool bool -> sb.append(bool.value() ? "true" : "false");
            case JsonNumber number -> sb.append(number.literal());
            case JsonString string -> writeString(sb, string.value());
            case JsonArray array -> {
                sb.append('[');
                List<JsonValue> values = array.values();
                for (int i = 0; i < values.size(); i++) {
                    if (i > 0) {
                        sb.append(',');
                    }
                    writeValue(sb, values.get(i));
                }
                sb.append(']');
            }
            case JsonObject object -> {
                sb.append('{');
                List<JsonObject.Member> members = object.members();
                for (int i = 0; i < members.size(); i++) {
                    if (i > 0) {
                        sb.append(',');
                    }
                    writeString(sb, members.get(i).name());
                    sb.append(':');
                    writeValue(sb, members.get(i).value());
                }
                sb.append('}');
            }
        }
    }

    private static void writeString(StringBuilder sb, String s) {
        sb.append('"');
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"' -> sb.append("\\\"");
                case '\\' -> sb.append("\\\\");
                case '\b' -> sb.append("\\b");
                case '\f' -> sb.append("\\f");
                case '\n' -> sb.append("\\n");
                case '\r' -> sb.append("\\r");
                case '\t' -> sb.append("\\t");
                default -> {
                    if (c < 0x20) {
                        sb.append("\\u00").append(HEX[(c >> 4) & 0xF]).append(HEX[c & 0xF]);
                    } else {
                        sb.append(c);
                    }
                }
            }
        }
        sb.append('"');
    }

    // ------------------------------------------------------------------
    // Strict UTF-8 decoding
    // ------------------------------------------------------------------

    private static String decodeUtf8(byte[] bytes) {
        CharsetDecoder decoder = StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT);
        ByteBuffer in = ByteBuffer.wrap(bytes);
        CharBuffer out = CharBuffer.allocate(bytes.length);
        CoderResult result = decoder.decode(in, out, true);
        if (!result.isError()) {
            result = decoder.flush(out);
        }
        if (result.isError()) {
            // Position the error at the end of the cleanly decoded prefix.
            int line = 1;
            int column = 1;
            for (int i = 0; i < out.position(); i++) {
                if (out.get(i) == '\n') {
                    line++;
                    column = 1;
                } else {
                    column++;
                }
            }
            throw new JsonParseException(
                    "malformed UTF-8 (byte offset " + in.position() + ")", line, column);
        }
        out.flip();
        return out.toString();
    }

    // ------------------------------------------------------------------
    // Parser
    // ------------------------------------------------------------------

    /** Single-use recursive-descent parser over one document. */
    private static final class Parser {

        private final String src;
        private final JsonNumberMode mode;
        private int pos;
        private int line = 1;
        private int column = 1;
        private int depth;

        Parser(String src, JsonNumberMode mode) {
            this.src = src;
            this.mode = mode;
        }

        JsonValue parseDocument() {
            skipWhitespace();
            JsonValue value = parseValue();
            skipWhitespace();
            if (!atEnd()) {
                throw err("unexpected trailing content");
            }
            return value;
        }

        // -------------------------------------------------- primitives

        private boolean atEnd() {
            return pos >= src.length();
        }

        private char peek() {
            return src.charAt(pos);
        }

        private void advance() {
            if (src.charAt(pos) == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
            pos++;
        }

        private void skipWhitespace() {
            while (!atEnd()) {
                char c = peek();
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    advance();
                } else {
                    return;
                }
            }
        }

        private JsonParseException err(String detail) {
            return new JsonParseException(detail, line, column);
        }

        private static JsonParseException errAt(String detail, int line, int column) {
            return new JsonParseException(detail, line, column);
        }

        private static boolean isDigit(char c) {
            return c >= '0' && c <= '9';
        }

        private static String printable(char c) {
            if (c < 0x20 || c == 0x7F) {
                return String.format("U+%04X", (int) c);
            }
            return "'" + c + "'";
        }

        // -------------------------------------------------- values

        private JsonValue parseValue() {
            if (atEnd()) {
                throw err("unexpected end of input");
            }
            char c = peek();
            return switch (c) {
                case '{' -> parseObject();
                case '[' -> parseArray();
                case '"' -> new JsonString(parseRawString());
                case 't' -> parseLiteral("true", JsonBool.TRUE);
                case 'f' -> parseLiteral("false", JsonBool.FALSE);
                case 'n' -> parseLiteral("null", JsonNull.INSTANCE);
                default -> {
                    if (c == '-' || isDigit(c)) {
                        yield parseNumber();
                    }
                    throw err("unexpected character " + printable(c));
                }
            };
        }

        private JsonValue parseLiteral(String expected, JsonValue value) {
            if (!src.startsWith(expected, pos)) {
                throw err("invalid literal, expected '" + expected + "'");
            }
            for (int i = 0; i < expected.length(); i++) {
                advance();
            }
            return value;
        }

        private JsonNumber parseNumber() {
            int start = pos;
            if (peek() == '-') {
                advance();
            }
            if (atEnd() || !isDigit(peek())) {
                throw err("expected a digit in number");
            }
            if (peek() == '0') {
                int zeroLine = line;
                int zeroColumn = column;
                advance();
                if (!atEnd() && isDigit(peek())) {
                    throw errAt("leading zeros are not allowed", zeroLine, zeroColumn);
                }
            } else {
                while (!atEnd() && isDigit(peek())) {
                    advance();
                }
            }
            if (!atEnd() && peek() == '.') {
                if (mode == JsonNumberMode.INTEGER_ONLY) {
                    throw err("decimal point not allowed in integer-only mode");
                }
                advance();
                if (atEnd() || !isDigit(peek())) {
                    throw err("expected a digit after decimal point");
                }
                while (!atEnd() && isDigit(peek())) {
                    advance();
                }
            }
            if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
                if (mode == JsonNumberMode.INTEGER_ONLY) {
                    throw err("exponent not allowed in integer-only mode");
                }
                advance();
                if (!atEnd() && (peek() == '+' || peek() == '-')) {
                    advance();
                }
                if (atEnd() || !isDigit(peek())) {
                    throw err("expected a digit in exponent");
                }
                while (!atEnd() && isDigit(peek())) {
                    advance();
                }
            }
            return new JsonNumber(src.substring(start, pos));
        }

        // -------------------------------------------------- strings

        /**
         * Parses a string token (caller guarantees {@code peek() == '"'}) and
         * returns its decoded value, validating escapes and surrogate pairing.
         */
        private String parseRawString() {
            int openLine = line;
            int openColumn = column;
            advance(); // opening quote
            StringBuilder sb = new StringBuilder();
            boolean pendingHigh = false;
            int highLine = 0;
            int highColumn = 0;
            while (true) {
                if (atEnd()) {
                    throw errAt("unterminated string", openLine, openColumn);
                }
                char c = peek();
                if (c == '"') {
                    if (pendingHigh) {
                        throw errAt("unpaired surrogate in string", highLine, highColumn);
                    }
                    advance();
                    return sb.toString();
                }
                int unitLine = line;
                int unitColumn = column;
                char unit;
                if (c == '\\') {
                    unit = parseEscape(openLine, openColumn);
                } else if (c < 0x20) {
                    throw err("unescaped control character " + printable(c) + " in string");
                } else {
                    advance();
                    unit = c;
                }
                if (pendingHigh) {
                    if (!Character.isLowSurrogate(unit)) {
                        throw errAt("unpaired surrogate in string", highLine, highColumn);
                    }
                    pendingHigh = false;
                } else if (Character.isHighSurrogate(unit)) {
                    pendingHigh = true;
                    highLine = unitLine;
                    highColumn = unitColumn;
                } else if (Character.isLowSurrogate(unit)) {
                    throw errAt("unpaired surrogate in string", unitLine, unitColumn);
                }
                sb.append(unit);
            }
        }

        /**
         * Parses one escape sequence (caller guarantees {@code peek() == '\\'})
         * and returns the UTF-16 unit it denotes. Errors are positioned at the
         * backslash.
         */
        private char parseEscape(int openLine, int openColumn) {
            int escLine = line;
            int escColumn = column;
            advance(); // backslash
            if (atEnd()) {
                throw errAt("unterminated string", openLine, openColumn);
            }
            char e = peek();
            switch (e) {
                case '"', '\\', '/' -> {
                    advance();
                    return e;
                }
                case 'b' -> {
                    advance();
                    return '\b';
                }
                case 'f' -> {
                    advance();
                    return '\f';
                }
                case 'n' -> {
                    advance();
                    return '\n';
                }
                case 'r' -> {
                    advance();
                    return '\r';
                }
                case 't' -> {
                    advance();
                    return '\t';
                }
                case 'u' -> {
                    advance();
                    int unit = 0;
                    for (int i = 0; i < 4; i++) {
                        if (atEnd()) {
                            throw errAt("unterminated string", openLine, openColumn);
                        }
                        int digit = Character.digit(peek(), 16);
                        if (digit < 0) {
                            throw errAt("invalid unicode escape", escLine, escColumn);
                        }
                        unit = (unit << 4) | digit;
                        advance();
                    }
                    return (char) unit;
                }
                default -> throw errAt(
                        "invalid escape sequence \\" + printable(e), escLine, escColumn);
            }
        }

        // -------------------------------------------------- containers

        private JsonObject parseObject() {
            if (++depth > MAX_DEPTH) {
                throw err("maximum nesting depth exceeded (" + MAX_DEPTH + ")");
            }
            advance(); // '{'
            skipWhitespace();
            List<JsonObject.Member> members = new ArrayList<>();
            if (!atEnd() && peek() == '}') {
                advance();
                depth--;
                return new JsonObject(members);
            }
            Set<String> seen = new HashSet<>(); // membership only — never iterated
            while (true) {
                skipWhitespace();
                if (atEnd()) {
                    throw err("unexpected end of input in object");
                }
                if (peek() != '"') {
                    throw err("expected '\"' to begin object key");
                }
                int keyLine = line;
                int keyColumn = column;
                String key = parseRawString();
                if (!seen.add(key)) {
                    throw errAt("duplicate key \"" + key + "\"", keyLine, keyColumn);
                }
                skipWhitespace();
                if (atEnd()) {
                    throw err("unexpected end of input in object");
                }
                if (peek() != ':') {
                    throw err("expected ':' after object key");
                }
                advance();
                skipWhitespace();
                JsonValue value = parseValue();
                members.add(new JsonObject.Member(key, value));
                skipWhitespace();
                if (atEnd()) {
                    throw err("unexpected end of input in object");
                }
                char c = peek();
                if (c == ',') {
                    advance();
                    continue;
                }
                if (c == '}') {
                    advance();
                    depth--;
                    return new JsonObject(members);
                }
                throw err("expected ',' or '}' in object");
            }
        }

        private JsonArray parseArray() {
            if (++depth > MAX_DEPTH) {
                throw err("maximum nesting depth exceeded (" + MAX_DEPTH + ")");
            }
            advance(); // '['
            skipWhitespace();
            List<JsonValue> values = new ArrayList<>();
            if (!atEnd() && peek() == ']') {
                advance();
                depth--;
                return new JsonArray(values);
            }
            while (true) {
                skipWhitespace();
                values.add(parseValue());
                skipWhitespace();
                if (atEnd()) {
                    throw err("unexpected end of input in array");
                }
                char c = peek();
                if (c == ',') {
                    advance();
                    continue;
                }
                if (c == ']') {
                    advance();
                    depth--;
                    return new JsonArray(values);
                }
                throw err("expected ',' or ']' in array");
            }
        }
    }
}
