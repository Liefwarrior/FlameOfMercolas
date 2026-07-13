package com.trojia.sim.json;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Number edge cases: long/int bounds, overflow, integral strictness, grammar. */
final class JsonNumberTest {

    // -------------------------------------------------- long bounds

    @Test
    void asLongAtExactBounds() {
        assertEquals(Long.MAX_VALUE, new JsonNumber("9223372036854775807").asLong());
        assertEquals(Long.MIN_VALUE, new JsonNumber("-9223372036854775808").asLong());
    }

    @Test
    void asLongOverflowByOne() {
        assertThrows(JsonDataException.class,
                () -> new JsonNumber("9223372036854775808").asLong());
        assertThrows(JsonDataException.class,
                () -> new JsonNumber("-9223372036854775809").asLong());
    }

    @Test
    void asLongFarOverflow() {
        assertThrows(JsonDataException.class,
                () -> new JsonNumber("99999999999999999999999999").asLong());
    }

    // -------------------------------------------------- int bounds

    @Test
    void asIntAtExactBounds() {
        assertEquals(Integer.MAX_VALUE, new JsonNumber("2147483647").asInt());
        assertEquals(Integer.MIN_VALUE, new JsonNumber("-2147483648").asInt());
    }

    @Test
    void asIntOverflowByOne() {
        assertThrows(JsonDataException.class, () -> new JsonNumber("2147483648").asInt());
        assertThrows(JsonDataException.class, () -> new JsonNumber("-2147483649").asInt());
    }

    // -------------------------------------------------- integral strictness

    @Test
    void integerAccessorsRejectFractions() {
        assertThrows(JsonDataException.class, () -> new JsonNumber("1.5").asLong());
        assertThrows(JsonDataException.class, () -> new JsonNumber("1.0").asLong());
        assertThrows(JsonDataException.class, () -> new JsonNumber("1.5").asInt());
    }

    @Test
    void integerAccessorsRejectExponentsEvenWhenIntegral() {
        // 1e2 denotes 100, but the literal is not a pure integer — reject.
        assertThrows(JsonDataException.class, () -> new JsonNumber("1e2").asLong());
        assertThrows(JsonDataException.class, () -> new JsonNumber("2E3").asInt());
    }

    @Test
    void isIntegral() {
        assertTrue(new JsonNumber("0").isIntegral());
        assertTrue(new JsonNumber("-0").isIntegral());
        assertTrue(new JsonNumber("9223372036854775808").isIntegral());
        assertFalse(new JsonNumber("1.0").isIntegral());
        assertFalse(new JsonNumber("1e2").isIntegral());
        assertFalse(new JsonNumber("1E2").isIntegral());
    }

    @Test
    void negativeZeroIsIntegerZero() {
        assertEquals(0L, new JsonNumber("-0").asLong());
        assertEquals(0, new JsonNumber("-0").asInt());
    }

    // -------------------------------------------------- doubles

    @Test
    void asDouble() {
        assertEquals(Double.doubleToLongBits(1.5), Double.doubleToLongBits(
                new JsonNumber("1.5").asDouble()));
        assertEquals(Double.doubleToLongBits(100.0), Double.doubleToLongBits(
                new JsonNumber("1e2").asDouble()));
        assertEquals(Double.doubleToLongBits(-0.0), Double.doubleToLongBits(
                new JsonNumber("-0").asDouble()));
    }

    // -------------------------------------------------- literal grammar

    @Test
    void constructorRejectsInvalidLiterals() {
        for (String bad : new String[] {
                "", "-", "01", "-01", "00", "1.", ".5", "+1", "1e", "1e+",
                "0x10", "1 ", " 1", "--1", "NaN", "Infinity", "-Infinity", "1..2", "1ee2"}) {
            assertThrows(IllegalArgumentException.class, () -> new JsonNumber(bad),
                    "should reject: \"" + bad + "\"");
        }
    }

    @Test
    void constructorAcceptsValidLiterals() {
        for (String good : new String[] {
                "0", "-0", "10", "1.5", "0.5", "1e2", "1E+10", "1e-2", "12.30e+05",
                "-9999999999999999999999"}) {
            assertEquals(good, new JsonNumber(good).literal());
        }
    }

    // -------------------------------------------------- identity

    @Test
    void equalityIsLiteralExact() {
        assertEquals(new JsonNumber("1"), new JsonNumber("1"));
        assertNotEquals(new JsonNumber("1"), new JsonNumber("1.0"));
        assertNotEquals(new JsonNumber("1"), new JsonNumber("1e0"));
    }

    @Test
    void ofLong() {
        assertEquals(new JsonNumber("-42"), JsonNumber.of(-42));
        assertEquals(Long.MIN_VALUE, JsonNumber.of(Long.MIN_VALUE).asLong());
    }

    @Test
    void toStringIsTheLiteral() {
        assertEquals("-0", new JsonNumber("-0").toString());
    }
}
