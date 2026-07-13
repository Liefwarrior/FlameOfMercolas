package com.trojia.sim.json;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Every malformed-input class, each with its asserted 1-based line:column.
 * These positions are contract: loader error messages point modders at the
 * exact offending character, so a position change is a breaking change.
 */
final class MiniJsonErrorTest {

    private static JsonParseException failsAt(String json, int line, int column) {
        JsonParseException e =
                assertThrows(JsonParseException.class, () -> MiniJson.parse(json));
        assertEquals(line, e.getLine(), "line of: " + json);
        assertEquals(column, e.getColumn(), "column of: " + json);
        return e;
    }

    private static JsonParseException failsIntegerOnlyAt(String json, int line, int column) {
        JsonParseException e = assertThrows(JsonParseException.class,
                () -> MiniJson.parse(json, JsonNumberMode.INTEGER_ONLY));
        assertEquals(line, e.getLine(), "line of: " + json);
        assertEquals(column, e.getColumn(), "column of: " + json);
        return e;
    }

    private static void assertMentions(JsonParseException e, String fragment) {
        assertTrue(e.getMessage().contains(fragment),
                "expected message containing \"" + fragment + "\" but was: " + e.getMessage());
    }

    // -------------------------------------------------- document structure

    @Test
    void emptyInput() {
        assertMentions(failsAt("", 1, 1), "unexpected end of input");
    }

    @Test
    void whitespaceOnlyInput() {
        assertMentions(failsAt("   \n  ", 2, 3), "unexpected end of input");
    }

    @Test
    void garbageAtStart() {
        assertMentions(failsAt("@", 1, 1), "unexpected character");
    }

    @Test
    void trailingContent() {
        assertMentions(failsAt("{} {}", 1, 4), "trailing content");
        assertMentions(failsAt("[1,2] tail", 1, 7), "trailing content");
        assertMentions(failsAt("nullx", 1, 5), "trailing content");
    }

    // -------------------------------------------------- objects

    @Test
    void unterminatedObject() {
        assertMentions(failsAt("{", 1, 2), "end of input in object");
        assertMentions(failsAt("{\"a\"", 1, 5), "end of input in object");
        assertMentions(failsAt("{\"a\":1", 1, 7), "end of input in object");
        assertMentions(failsAt("{\"a\":1,", 1, 8), "end of input in object");
    }

    @Test
    void missingColon() {
        assertMentions(failsAt("{\"a\" 1}", 1, 6), "expected ':'");
    }

    @Test
    void missingValueAfterColon() {
        assertMentions(failsAt("{\"a\":}", 1, 6), "unexpected character");
    }

    @Test
    void missingCommaBetweenMembers() {
        assertMentions(failsAt("{\"a\":1 \"b\":2}", 1, 8), "expected ',' or '}'");
    }

    @Test
    void trailingCommaInObject() {
        assertMentions(failsAt("{\"a\":1,}", 1, 8), "expected '\"'");
    }

    @Test
    void unquotedKey() {
        assertMentions(failsAt("{a:1}", 1, 2), "expected '\"'");
    }

    @Test
    void duplicateKey() {
        assertMentions(failsAt("{\"a\":1,\"a\":2}", 1, 8), "duplicate key \"a\"");
    }

    @Test
    void duplicateKeyMultiLine() {
        assertMentions(failsAt("{\n \"id\": 1,\n \"id\": 2\n}", 3, 2), "duplicate key \"id\"");
    }

    @Test
    void duplicateKeyAfterEscapeResolution() {
        // "ab" spelled plainly, then via b — same key after decoding.
        assertMentions(failsAt("{\"ab\":1,\"a\\u0062\":2}", 1, 9), "duplicate key \"ab\"");
    }

    // -------------------------------------------------- arrays

    @Test
    void unterminatedArray() {
        assertMentions(failsAt("[", 1, 2), "unexpected end of input");
        assertMentions(failsAt("[1", 1, 3), "end of input in array");
    }

    @Test
    void trailingCommaInArray() {
        assertMentions(failsAt("[1,]", 1, 4), "unexpected character");
    }

    @Test
    void leadingCommaInArray() {
        assertMentions(failsAt("[,1]", 1, 2), "unexpected character");
    }

    @Test
    void missingCommaBetweenElements() {
        assertMentions(failsAt("[1 2]", 1, 4), "expected ',' or ']'");
    }

    // -------------------------------------------------- literals

    @Test
    void misspelledLiterals() {
        assertMentions(failsAt("tru", 1, 1), "expected 'true'");
        assertMentions(failsAt("falsy", 1, 1), "expected 'false'");
        assertMentions(failsAt("nul", 1, 1), "expected 'null'");
    }

    @Test
    void wrongCaseLiteral() {
        assertMentions(failsAt("True", 1, 1), "unexpected character");
    }

    @Test
    void misspelledLiteralOnLaterLine() {
        assertMentions(failsAt("{\n  \"a\": tru\n}", 2, 8), "expected 'true'");
    }

    // -------------------------------------------------- rejected extensions

    @Test
    void commentsAreRejected() {
        assertMentions(failsAt("// c\n1", 1, 1), "unexpected character");
        assertMentions(failsAt("/* c */ 1", 1, 1), "unexpected character");
        assertMentions(failsAt("[1, // c\n2]", 1, 5), "unexpected character");
    }

    @Test
    void singleQuotesAreRejected() {
        assertMentions(failsAt("'a'", 1, 1), "unexpected character");
        assertMentions(failsAt("{\"a\":'b'}", 1, 6), "unexpected character");
    }

    @Test
    void nanAndInfinityAreRejected() {
        assertMentions(failsAt("NaN", 1, 1), "unexpected character");
        assertMentions(failsAt("Infinity", 1, 1), "unexpected character");
        assertMentions(failsAt("-Infinity", 1, 2), "expected a digit");
    }

    // -------------------------------------------------- numbers

    @Test
    void leadingZeros() {
        assertMentions(failsAt("01", 1, 1), "leading zeros");
        assertMentions(failsAt("00", 1, 1), "leading zeros");
        assertMentions(failsAt("-01", 1, 2), "leading zeros");
    }

    @Test
    void leadingPlusSign() {
        assertMentions(failsAt("+1", 1, 1), "unexpected character");
    }

    @Test
    void bareFraction() {
        assertMentions(failsAt(".5", 1, 1), "unexpected character");
    }

    @Test
    void danglingDecimalPoint() {
        assertMentions(failsAt("5.", 1, 3), "digit after decimal point");
        assertMentions(failsAt("1.e2", 1, 3), "digit after decimal point");
        assertMentions(failsAt("[0.1, 2.]", 1, 9), "digit after decimal point");
    }

    @Test
    void danglingExponent() {
        assertMentions(failsAt("1e", 1, 3), "digit in exponent");
        assertMentions(failsAt("1e+", 1, 4), "digit in exponent");
    }

    @Test
    void bareMinusSign() {
        assertMentions(failsAt("-", 1, 2), "expected a digit");
        assertMentions(failsAt("-x", 1, 2), "expected a digit");
    }

    // -------------------------------------------------- integer-only mode

    @Test
    void integerOnlyRejectsDecimalPoint() {
        assertMentions(failsIntegerOnlyAt("1.5", 1, 2), "integer-only");
        assertMentions(failsIntegerOnlyAt("{\"a\": 2.5}", 1, 8), "integer-only");
        assertMentions(failsIntegerOnlyAt("[1, 2.0]", 1, 6), "integer-only");
    }

    @Test
    void integerOnlyRejectsExponent() {
        assertMentions(failsIntegerOnlyAt("1e3", 1, 2), "integer-only");
        assertMentions(failsIntegerOnlyAt("2E+2", 1, 2), "integer-only");
    }

    @Test
    void anyModeAcceptsWhatIntegerOnlyRejects() {
        assertEquals(new JsonNumber("1.5"), MiniJson.parse("1.5"));
        assertEquals(new JsonNumber("1e3"), MiniJson.parse("1e3"));
    }

    // -------------------------------------------------- strings

    @Test
    void unterminatedString() {
        assertMentions(failsAt("\"abc", 1, 1), "unterminated string");
        assertMentions(failsAt("\"ab\\", 1, 1), "unterminated string");
        assertMentions(failsAt("\"ab\\u12", 1, 1), "unterminated string");
    }

    @Test
    void rawControlCharactersInString() {
        assertMentions(failsAt("\"ab\nc\"", 1, 4), "unescaped control character");
        assertMentions(failsAt("\"a\tb\"", 1, 3), "unescaped control character");
    }

    @Test
    void invalidEscape() {
        assertMentions(failsAt("\"\\x\"", 1, 2), "invalid escape");
    }

    @Test
    void invalidUnicodeEscape() {
        assertMentions(failsAt("\"\\u12G4\"", 1, 2), "invalid unicode escape");
        assertMentions(failsAt("\"\\u12\"", 1, 2), "invalid unicode escape");
    }

    @Test
    void unpairedSurrogates() {
        assertMentions(failsAt("\"\\uD800\"", 1, 2), "unpaired surrogate");
        assertMentions(failsAt("\"\\uDC00\"", 1, 2), "unpaired surrogate");
        assertMentions(failsAt("\"\\uD800x\"", 1, 2), "unpaired surrogate");
        assertMentions(failsAt("\"\\uD800\\uD801\"", 1, 2), "unpaired surrogate");
        assertMentions(failsAt("\"" + (char) 0xD800 + "\"", 1, 2), "unpaired surrogate");
    }

    // -------------------------------------------------- bytes and depth

    @Test
    void utf8BomIsRejected() {
        byte[] bom = {(byte) 0xEF, (byte) 0xBB, (byte) 0xBF, '{', '}'};
        JsonParseException e =
                assertThrows(JsonParseException.class, () -> MiniJson.parse(bom));
        assertEquals(1, e.getLine());
        assertEquals(1, e.getColumn());
        assertMentions(e, "unexpected character");
    }

    @Test
    void malformedUtf8IsRejectedWithPosition() {
        byte[] badContinuation = {'1', (byte) 0xFF};
        JsonParseException e = assertThrows(JsonParseException.class,
                () -> MiniJson.parse(badContinuation));
        assertEquals(1, e.getLine());
        assertEquals(2, e.getColumn());
        assertMentions(e, "malformed UTF-8");
        assertMentions(e, "byte offset 1");

        byte[] truncatedOnLineTwo = {'\n', (byte) 0xC3};
        JsonParseException e2 = assertThrows(JsonParseException.class,
                () -> MiniJson.parse(truncatedOnLineTwo));
        assertEquals(2, e2.getLine());
        assertEquals(1, e2.getColumn());
        assertMentions(e2, "malformed UTF-8");

        byte[] overlongSlash = {'"', (byte) 0xC0, (byte) 0xAF, '"'};
        JsonParseException e3 = assertThrows(JsonParseException.class,
                () -> MiniJson.parse(overlongSlash));
        assertMentions(e3, "malformed UTF-8");
        assertMentions(e3, "byte offset 1");
    }

    @Test
    void nestingBeyondMaxDepthIsRejected() {
        String doc = "[".repeat(MiniJson.MAX_DEPTH + 1);
        JsonParseException e = failsAt(doc, 1, MiniJson.MAX_DEPTH + 1);
        assertMentions(e, "nesting depth");
    }
}
