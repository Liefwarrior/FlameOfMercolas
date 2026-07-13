package com.trojia.sim.json;

import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Happy-path parsing: every value kind, whitespace, escapes, order, modes. */
final class MiniJsonParseTest {

    @Test
    void topLevelScalars() {
        assertSame(JsonNull.INSTANCE, MiniJson.parse("null"));
        assertEquals(JsonBool.TRUE, MiniJson.parse("true"));
        assertEquals(JsonBool.FALSE, MiniJson.parse("false"));
        assertEquals(new JsonNumber("42"), MiniJson.parse("42"));
        assertEquals(new JsonNumber("-0"), MiniJson.parse("-0"));
        assertEquals(new JsonString("hi"), MiniJson.parse("\"hi\""));
    }

    @Test
    void surroundingWhitespaceIsIgnored() {
        assertEquals(JsonBool.TRUE, MiniJson.parse(" \t\r\n true \r\n\t "));
        assertEquals(new JsonNumber("7"), MiniJson.parse("\n7\n"));
    }

    @Test
    void anyModeAcceptsFractionsAndExponents() {
        assertEquals(new JsonNumber("1.5"), MiniJson.parse("1.5"));
        assertEquals(new JsonNumber("0.5"), MiniJson.parse("0.5"));
        assertEquals(new JsonNumber("1e-2"), MiniJson.parse("1e-2"));
        assertEquals(new JsonNumber("1E+10"), MiniJson.parse("1E+10"));
        assertEquals(new JsonNumber("-12.34e5"), MiniJson.parse("-12.34e5"));
    }

    @Test
    void integerOnlyModeAcceptsPlainIntegers() {
        assertEquals(new JsonNumber("240"),
                MiniJson.parse("240", JsonNumberMode.INTEGER_ONLY));
        assertEquals(new JsonNumber("-7"),
                MiniJson.parse("-7", JsonNumberMode.INTEGER_ONLY));
        assertEquals(new JsonNumber("0"),
                MiniJson.parse("0", JsonNumberMode.INTEGER_ONLY));
    }

    @Test
    void emptyContainers() {
        assertEquals(new JsonObject(List.of()), MiniJson.parse("{}"));
        assertEquals(new JsonObject(List.of()), MiniJson.parse("{ \n }"));
        assertEquals(new JsonArray(List.of()), MiniJson.parse("[]"));
        assertEquals(new JsonArray(List.of()), MiniJson.parse("[ \t ]"));
    }

    @Test
    void nestedStructure() {
        JsonValue tree = MiniJson.parse("{\"a\":{\"b\":[1,2,{\"c\":null}]},\"d\":true}");
        JsonObject root = assertInstanceOf(JsonObject.class, tree);
        JsonObject a = assertInstanceOf(JsonObject.class, root.get("a"));
        JsonArray b = assertInstanceOf(JsonArray.class, a.get("b"));
        assertEquals(3, b.size());
        assertEquals(new JsonNumber("1"), b.get(0));
        JsonObject inner = assertInstanceOf(JsonObject.class, b.get(2));
        assertSame(JsonNull.INSTANCE, inner.get("c"));
        assertEquals(JsonBool.TRUE, root.get("d"));
    }

    @Test
    void objectPreservesInsertionOrder() {
        JsonObject obj = (JsonObject) MiniJson.parse("{\"z\":1,\"a\":2,\"m\":3}");
        assertEquals(List.of("z", "a", "m"),
                obj.members().stream().map(JsonObject.Member::name).toList());
    }

    @Test
    void allEscapesDecode() {
        JsonString s = (JsonString) MiniJson.parse(
                "\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\\u0041\\u00e9\"");
        assertEquals("a\"b\\c/d\b\f\n\r\tAé", s.value());
    }

    @Test
    void nulCharacterEscapeDecodes() {
        JsonString s = (JsonString) MiniJson.parse("\"\\u0000\"");
        assertEquals(1, s.value().length());
        assertEquals(0, s.value().charAt(0));
    }

    @Test
    void surrogatePairEscapesDecodeToOneCodePoint() {
        JsonString s = (JsonString) MiniJson.parse("\"\\uD83D\\uDE00\"");
        assertEquals(2, s.value().length());
        assertEquals(0x1F600, s.value().codePointAt(0));
    }

    @Test
    void rawSurrogatePairIsAccepted() {
        String emoji = new String(Character.toChars(0x1F600));
        JsonString s = (JsonString) MiniJson.parse("\"" + emoji + "\"");
        assertEquals(emoji, s.value());
    }

    @Test
    void keysAreCaseSensitiveAndEscapeResolved() {
        JsonObject obj = (JsonObject) MiniJson.parse("{\"a\":1,\"A\":2}");
        assertEquals(new JsonNumber("1"), obj.get("a"));
        assertEquals(new JsonNumber("2"), obj.get("A"));
        JsonObject escaped = (JsonObject) MiniJson.parse("{\"a\\u0062c\":9}");
        assertEquals(new JsonNumber("9"), escaped.get("abc"));
    }

    @Test
    void missingMemberVersusNullMember() {
        JsonObject obj = (JsonObject) MiniJson.parse("{\"present\":null}");
        assertTrue(obj.has("present"));
        assertSame(JsonNull.INSTANCE, obj.get("present"));
        assertNull(obj.get("absent"));
        assertFalse(obj.has("absent"));
    }

    @Test
    void utf8BytesDecode() {
        JsonObject obj = (JsonObject) MiniJson.parse(
                "{\"a\":\"é§\"}".getBytes(StandardCharsets.UTF_8));
        assertEquals(new JsonString("é§"), obj.get("a"));
    }

    @Test
    void utf8BytesRespectNumberMode() {
        assertEquals(new JsonNumber("12"), MiniJson.parse(
                "12".getBytes(StandardCharsets.UTF_8), JsonNumberMode.INTEGER_ONLY));
    }

    @Test
    void nestingUpToMaxDepthParses() {
        String doc = "[".repeat(MiniJson.MAX_DEPTH) + "]".repeat(MiniJson.MAX_DEPTH);
        JsonValue tree = MiniJson.parse(doc);
        assertInstanceOf(JsonArray.class, tree);
    }

    @Test
    void parsingIsDeterministic() {
        String doc = "{\"b\":[1,{\"c\":\"x\"},null],\"a\":true}";
        assertEquals(MiniJson.parse(doc), MiniJson.parse(doc));
        assertEquals(MiniJson.write(MiniJson.parse(doc)), MiniJson.write(MiniJson.parse(doc)));
    }
}
