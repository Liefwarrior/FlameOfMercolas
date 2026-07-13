package com.trojia.sim.json;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Canonical writer: insertion order, zero whitespace, minimal escaping, and
 * the round-trip fixed-point guarantee {@code write(parse(write(t))) == write(t)}.
 */
final class MiniJsonWriteTest {

    private static void assertCanonical(String expected, String input) {
        assertEquals(expected, MiniJson.write(MiniJson.parse(input)));
    }

    @Test
    void scalars() {
        assertEquals("null", MiniJson.write(JsonNull.INSTANCE));
        assertEquals("true", MiniJson.write(JsonBool.TRUE));
        assertEquals("false", MiniJson.write(JsonBool.FALSE));
        assertEquals("42", MiniJson.write(new JsonNumber("42")));
        assertEquals("-0", MiniJson.write(new JsonNumber("-0")));
        assertEquals("1e2", MiniJson.write(new JsonNumber("1e2")));
        assertEquals("\"hi\"", MiniJson.write(new JsonString("hi")));
    }

    @Test
    void emptyContainers() {
        assertCanonical("{}", "{ }");
        assertCanonical("[]", "[ \n ]");
    }

    @Test
    void whitespaceIsStrippedAndOrderKept() {
        assertCanonical("{\"b\":1,\"a\":[true,null,\"x\"]}",
                " { \"b\" : 1 , \"a\" : [ true , null , \"x\" ] } ");
    }

    @Test
    void numberLiteralsAreEmittedVerbatim() {
        assertCanonical("[0,-0,1.50,1e2,1E+10]", "[0, -0, 1.50, 1e2, 1E+10]");
    }

    @Test
    void shortEscapesAreUsed() {
        assertEquals("\"a\\\"b\\\\c\\b\\f\\n\\r\\t\"",
                MiniJson.write(new JsonString("a\"b\\c\b\f\n\r\t")));
    }

    @Test
    void solidusIsNotEscaped() {
        assertCanonical("\"/\"", "\"\\/\"");
    }

    @Test
    void unicodeEscapesCanonicalizeToRawCharacters() {
        assertCanonical("\"Aé\"", "\"\\u0041\\u00e9\"");
    }

    @Test
    void otherControlCharactersUseLowercaseHex() {
        assertCanonical("\"\\u000b\"", "\"\\u000B\"");
        assertEquals("\"\\u0000\\u0001\\u001f\"",
                MiniJson.write(new JsonString(new String(new char[] {0, 1, 0x1F}))));
    }

    @Test
    void surrogatePairsAreWrittenRaw() {
        String emoji = new String(Character.toChars(0x1F600));
        assertCanonical("\"" + emoji + "\"", "\"\\uD83D\\uDE00\"");
    }

    @Test
    void nestedOrderIsPreserved() {
        assertCanonical("{\"z\":{\"m\":[{\"k2\":1,\"k1\":2}]},\"a\":0}",
                "{\"z\": {\"m\": [{\"k2\": 1, \"k1\": 2}]}, \"a\": 0}");
    }

    @Test
    void toStringDelegatesToCanonicalWriter() {
        JsonValue tree = MiniJson.parse("{ \"a\" : [ 1 , null ] }");
        assertEquals("{\"a\":[1,null]}", tree.toString());
    }

    @Test
    void writeParseWriteIsAFixedPoint() {
        String[] docs = {
                "{\"id\":\"chromatis\",\"tags\":[\"metal\",\"alloy\"],\"meltK\":2600}",
                "[1,[2,[3,[4,[]]]],{\"deep\":{\"deeper\":null}}]",
                "\"\\u0007 text with \\\"quotes\\\" and \\\\ slashes / \"",
                "-12.34e5",
                "{\"order\":1,\"matters\":2,\"here\":3}",
        };
        for (String doc : docs) {
            String once = MiniJson.write(MiniJson.parse(doc));
            String twice = MiniJson.write(MiniJson.parse(once));
            assertEquals(once, twice, "not a fixed point for: " + doc);
            assertEquals(MiniJson.parse(doc), MiniJson.parse(once),
                    "reparse changed the tree for: " + doc);
        }
    }

    @Test
    void equalTreesProduceIdenticalText() {
        JsonValue built = new JsonObject(List.of(
                new JsonObject.Member("a", new JsonNumber("1")),
                new JsonObject.Member("b", new JsonArray(List.of(JsonBool.FALSE)))));
        JsonValue parsed = MiniJson.parse("{ \"a\": 1, \"b\": [false] }");
        assertEquals(built, parsed);
        assertEquals(MiniJson.write(built), MiniJson.write(parsed));
    }
}
