package com.trojia.sim.json;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;

/** Tree model contracts: immutability, duplicate rejection, equality semantics. */
final class JsonTreeModelTest {

    @Test
    void objectRejectsDuplicateMembersAtConstruction() {
        List<JsonObject.Member> members = List.of(
                new JsonObject.Member("a", new JsonNumber("1")),
                new JsonObject.Member("a", new JsonNumber("2")));
        assertThrows(IllegalArgumentException.class, () -> new JsonObject(members));
    }

    @Test
    void objectMemberListIsImmutable() {
        JsonObject obj = (JsonObject) MiniJson.parse("{\"a\":1}");
        assertThrows(UnsupportedOperationException.class,
                () -> obj.members().add(new JsonObject.Member("b", JsonNull.INSTANCE)));
    }

    @Test
    void objectIsDefensivelyCopied() {
        List<JsonObject.Member> source = new ArrayList<>();
        source.add(new JsonObject.Member("a", new JsonNumber("1")));
        JsonObject obj = new JsonObject(source);
        source.add(new JsonObject.Member("b", new JsonNumber("2")));
        assertEquals(1, obj.size());
    }

    @Test
    void arrayValueListIsImmutable() {
        JsonArray arr = (JsonArray) MiniJson.parse("[1]");
        assertThrows(UnsupportedOperationException.class,
                () -> arr.values().add(JsonNull.INSTANCE));
    }

    @Test
    void arrayIsDefensivelyCopiedAndBoundsChecked() {
        List<JsonValue> source = new ArrayList<>();
        source.add(JsonBool.TRUE);
        JsonArray arr = new JsonArray(source);
        source.add(JsonBool.FALSE);
        assertEquals(1, arr.size());
        assertThrows(IndexOutOfBoundsException.class, () -> arr.get(1));
        assertThrows(IndexOutOfBoundsException.class, () -> arr.get(-1));
    }

    @Test
    void nullsAreRejectedEverywhere() {
        assertThrows(NullPointerException.class, () -> new JsonString(null));
        assertThrows(NullPointerException.class, () -> new JsonNumber(null));
        assertThrows(NullPointerException.class, () -> new JsonObject.Member(null, JsonNull.INSTANCE));
        assertThrows(NullPointerException.class, () -> new JsonObject.Member("k", null));
        assertThrows(NullPointerException.class, () -> MiniJson.parse((String) null));
        assertThrows(NullPointerException.class, () -> MiniJson.parse((byte[]) null));
        assertThrows(NullPointerException.class, () -> MiniJson.parse("1", null));
        assertThrows(NullPointerException.class, () -> MiniJson.write(null));
    }

    @Test
    void equalityIsStructuralAndWhitespaceInsensitive() {
        assertEquals(MiniJson.parse("{\"a\":1}"), MiniJson.parse("{ \"a\" : 1 }"));
        assertEquals(MiniJson.parse("[1,2]"), MiniJson.parse("[ 1 , 2 ]"));
    }

    @Test
    void objectEqualityIsOrderSensitive() {
        assertNotEquals(MiniJson.parse("{\"a\":1,\"b\":2}"),
                MiniJson.parse("{\"b\":2,\"a\":1}"));
    }

    @Test
    void boolInstancesAreShared() {
        assertSame(JsonBool.TRUE, JsonBool.of(true));
        assertSame(JsonBool.FALSE, JsonBool.of(false));
        assertEquals(JsonBool.TRUE, new JsonBool(true));
    }

    @Test
    void nullIsASingletonWithCanonicalText() {
        assertSame(JsonNull.INSTANCE, MiniJson.parse("null"));
        assertEquals("null", JsonNull.INSTANCE.toString());
    }

    @Test
    void exhaustiveSwitchOverSealedHierarchy() {
        // Compile-time exhaustiveness: no default arm. If a JsonValue variant is
        // ever added, this test stops compiling — by design.
        for (String doc : new String[] {"null", "true", "1", "\"s\"", "[]", "{}"}) {
            String kind = switch (MiniJson.parse(doc)) {
                case JsonNull ignored -> "null";
                case JsonBool ignored -> "bool";
                case JsonNumber ignored -> "number";
                case JsonString ignored -> "string";
                case JsonArray ignored -> "array";
                case JsonObject ignored -> "object";
            };
            assertEquals(kind, kind); // reachable for every variant
        }
    }
}
