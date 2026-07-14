package com.trojia.headless;

import com.trojia.sim.bubble.ActiveBubble;
import com.trojia.sim.engine.TickContext;
import com.trojia.sim.engine.TickPhase;
import com.trojia.sim.event.EventReader;
import com.trojia.sim.event.EventSink;
import com.trojia.sim.event.SimEvent;
import com.trojia.sim.random.RandomSource;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Minimal coverage for {@link HeartbeatSystem} — the headless module had no
 * {@code src/test} directory at all despite full JUnit wiring in {@code
 * build.gradle.kts}, so {@code gradlew test} was silently running zero tests
 * here. Covers the constructor guard and the "prints only on multiples of
 * period" tick behavior.
 */
final class HeartbeatSystemTest {

    @Test
    void constructorRejectsZeroPeriod() {
        assertThrows(IllegalArgumentException.class, () -> new HeartbeatSystem(0));
    }

    @Test
    void constructorRejectsNegativePeriod() {
        assertThrows(IllegalArgumentException.class, () -> new HeartbeatSystem(-1));
    }

    @Test
    void ticksOnlyPrintOnMultiplesOfThePeriod() {
        HeartbeatSystem system = new HeartbeatSystem(3);
        PrintStream original = System.out;
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        System.setOut(new PrintStream(captured));
        try {
            system.tick(new FakeTickContext(1L));
            system.tick(new FakeTickContext(2L));
            system.tick(new FakeTickContext(3L));
            system.tick(new FakeTickContext(4L));
            system.tick(new FakeTickContext(6L));
        } finally {
            System.setOut(original);
        }
        String output = captured.toString();
        assertFalse(output.contains("[tick 1]"), "non-multiple ticks must not print");
        assertFalse(output.contains("[tick 2]"), "non-multiple ticks must not print");
        assertTrue(output.contains("[tick 3]"), "a multiple-of-period tick must print");
        assertFalse(output.contains("[tick 4]"), "non-multiple ticks must not print");
        assertTrue(output.contains("[tick 6]"), "the next multiple-of-period tick must print");
        assertEquals(2, output.lines().count(), "exactly two of the five ticks are multiples of 3");
    }

    /** A {@link TickContext} test double exposing only {@link #tick()} — all this system reads. */
    private static final class FakeTickContext implements TickContext {

        private final long tick;

        FakeTickContext(long tick) {
            this.tick = tick;
        }

        @Override
        public long tick() {
            return tick;
        }

        @Override
        public TickPhase phase() {
            throw new UnsupportedOperationException("HeartbeatSystem never reads phase()");
        }

        @Override
        public RandomSource rng() {
            throw new UnsupportedOperationException("HeartbeatSystem never reads rng()");
        }

        @Override
        public <E extends SimEvent> EventReader<E> events(Class<E> topic) {
            throw new UnsupportedOperationException("HeartbeatSystem never reads events()");
        }

        @Override
        public EventSink emit() {
            throw new UnsupportedOperationException("HeartbeatSystem never emits");
        }

        @Override
        public ActiveBubble bubble() {
            throw new UnsupportedOperationException("HeartbeatSystem never reads bubble()");
        }
    }
}
