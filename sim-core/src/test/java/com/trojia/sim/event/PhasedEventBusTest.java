package com.trojia.sim.event;

import com.trojia.sim.engine.SystemId;
import com.trojia.sim.engine.TickPhase;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.NoSuchElementException;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Contract tests for the phased event bus: the §4 one-lap visibility window
 * around the emitter's {@code (phase, regIndex)} position, stamp-order
 * draining, the per-topic per-tick hard cap, boot-sealed registration and
 * carry-over serialization.
 */
final class PhasedEventBusTest {

    private static final SystemId EMITTER = SystemId.of("emitter");
    private static final SystemId CONSUMER = SystemId.of("consumer");

    /**
     * An event emitted at (THERMAL, 1) of tick 1 is: invisible at-or-before
     * (THERMAL, 1) in tick 1, visible after it in tick 1; visible at-or-before
     * (THERMAL, 1) in tick 2, invisible after it in tick 2; retired by tick 3.
     */
    @Test
    void visibilityIsOneLapAroundTheEmitterPosition() {
        Events bus = Events.create();
        EventSink sink = bus.sink(EMITTER, TickPhase.THERMAL, 1);
        EventReader<ChunkThawed> before = bus.reader(CONSUMER, TickPhase.FLUIDS, 0,
                ChunkThawed.class);
        EventReader<ChunkThawed> same = bus.reader(SystemId.of("consumer-same"),
                TickPhase.THERMAL, 1, ChunkThawed.class);
        EventReader<ChunkThawed> after = bus.reader(SystemId.of("consumer-after"),
                TickPhase.REACTIONS, 0, ChunkThawed.class);
        bus.seal();

        // Tick 1: emit at (THERMAL, 1).
        bus.advanceTo(1, TickPhase.TICK_BEGIN, 0);
        bus.advanceTo(1, TickPhase.THERMAL, 1);
        sink.emit(new ChunkThawed(42));

        assertFalse(before.hasNext(), "position before the emitter must wait a lap");
        assertFalse(same.hasNext(), "the emitter's own position must wait a lap");
        assertTrue(after.hasNext(), "position after the emitter sees it same tick");
        assertEquals(new ChunkThawed(42), after.next());
        assertFalse(after.hasNext());
        bus.retireLap(1);

        // Tick 2: carry-over lap for at-or-before positions only.
        bus.advanceTo(2, TickPhase.TICK_BEGIN, 0);
        assertTrue(before.hasNext(), "position before the emitter sees it next tick");
        assertEquals(new ChunkThawed(42), before.next());
        assertTrue(same.hasNext(), "the emitter's own position sees it next tick");
        assertEquals(new ChunkThawed(42), same.next());
        assertFalse(after.hasNext(), "position after the emitter had its window last tick");
        bus.retireLap(2);

        // Tick 3: retired everywhere, drained or not.
        bus.advanceTo(3, TickPhase.TICK_BEGIN, 0);
        assertFalse(before.hasNext());
        assertFalse(same.hasNext());
        assertFalse(after.hasNext());
        assertThrows(NoSuchElementException.class, before::next);
    }

    /** An undrained event is retired after its lap regardless of consumption. */
    @Test
    void undrainedEventsAreRetiredAfterOneLap() {
        Events bus = Events.create();
        EventSink sink = bus.sink(EMITTER, TickPhase.FLUIDS, 0);
        EventReader<TileIgnited> reader = bus.reader(CONSUMER, TickPhase.THERMAL, 0,
                TileIgnited.class);
        bus.seal();

        bus.advanceTo(1, TickPhase.FLUIDS, 0);
        sink.emit(new TileIgnited(7));
        bus.retireLap(1); // consumer never drains during tick 1

        bus.advanceTo(2, TickPhase.TICK_BEGIN, 0);
        bus.retireLap(2); // ...nor during tick 2 (its last window)

        bus.advanceTo(3, TickPhase.TICK_BEGIN, 0);
        assertFalse(reader.hasNext(), "one lap is over; the event must be gone");
    }

    /** Readers drain in emission stamp order (tick, phase, regIndex, seq). */
    @Test
    void readersDrainInStampOrder() {
        Events bus = Events.create();
        EventSink fluids = bus.sink(SystemId.of("fluids"), TickPhase.FLUIDS, 0);
        EventSink thermal = bus.sink(SystemId.of("thermal"), TickPhase.THERMAL, 0);
        EventReader<TileIgnited> reader = bus.reader(CONSUMER, TickPhase.TICK_END, 0,
                TileIgnited.class);
        bus.seal();

        bus.advanceTo(1, TickPhase.FLUIDS, 0);
        fluids.emit(new TileIgnited(1));
        fluids.emit(new TileIgnited(2)); // seq breaks the tie within one position
        bus.advanceTo(1, TickPhase.THERMAL, 0);
        thermal.emit(new TileIgnited(3));
        bus.advanceTo(1, TickPhase.TICK_END, 0);

        List<Integer> cells = new ArrayList<>();
        while (reader.hasNext()) {
            cells.add(reader.next().cell());
        }
        assertEquals(List.of(1, 2, 3), cells);
    }

    /** Events of different topics do not leak into each other's readers. */
    @Test
    void topicsAreIsolated() {
        Events bus = Events.create();
        EventSink sink = bus.sink(EMITTER, TickPhase.FLUIDS, 0);
        EventReader<TileIgnited> ignitions = bus.reader(CONSUMER, TickPhase.THERMAL, 0,
                TileIgnited.class);
        EventReader<TileExtinguished> extinguishings = bus.reader(CONSUMER, TickPhase.THERMAL, 1,
                TileExtinguished.class);
        bus.seal();

        bus.advanceTo(1, TickPhase.FLUIDS, 0);
        sink.emit(new TileIgnited(5));

        assertTrue(ignitions.hasNext());
        assertFalse(extinguishings.hasNext());
        assertEquals(5, ignitions.next().cell());
    }

    /** The 65,536/topic/tick cap hard-fails on the overflowing emit and resets next tick. */
    @Test
    void perTopicPerTickCapHardFails() {
        Events bus = Events.create();
        EventSink sink = bus.sink(EMITTER, TickPhase.FLUIDS, 0);
        bus.seal();
        bus.advanceTo(1, TickPhase.FLUIDS, 0);

        ChunkThawed event = new ChunkThawed(1);
        for (int i = 0; i < Events.MAX_EVENTS_PER_TOPIC_PER_TICK; i++) {
            sink.emit(event);
        }
        assertThrows(IllegalStateException.class, () -> sink.emit(event),
                "the cap-exceeding emit must hard-fail");

        // Another topic is unaffected this tick; the capped topic resets next tick.
        sink.emit(new ChunkFrozen(1));
        bus.retireLap(1);
        bus.advanceTo(2, TickPhase.FLUIDS, 0);
        sink.emit(event);
    }

    /** Registration is boot-time only; emitting needs a sealed bus. */
    @Test
    void registrationSealsAtBoot() {
        Events bus = Events.create();
        EventSink sink = bus.sink(EMITTER, TickPhase.FLUIDS, 0);

        assertThrows(IllegalStateException.class, () -> sink.emit(new ChunkThawed(1)),
                "emitting on an unsealed bus is a programming error");

        bus.seal();
        assertThrows(IllegalStateException.class,
                () -> bus.sink(EMITTER, TickPhase.THERMAL, 0));
        assertThrows(IllegalStateException.class,
                () -> bus.reader(CONSUMER, TickPhase.THERMAL, 0, ChunkThawed.class));
        assertThrows(IllegalStateException.class, bus::seal);
    }

    /** The bus position may never move backwards within a tick. */
    @Test
    void advanceIsMonotonic() {
        Events bus = Events.create();
        bus.seal();
        bus.advanceTo(1, TickPhase.THERMAL, 2);
        assertThrows(IllegalStateException.class,
                () -> bus.advanceTo(1, TickPhase.THERMAL, 1));
        assertThrows(IllegalStateException.class,
                () -> bus.advanceTo(0, TickPhase.TICK_BEGIN, 0));
        bus.advanceTo(1, TickPhase.THERMAL, 2); // same position is legal (idempotent)
        bus.advanceTo(2, TickPhase.TICK_BEGIN, 0);
    }

    /** Only registered topic classes may be read. */
    @Test
    void unknownTopicRegistrationFails() {
        Events bus = Events.create();
        assertThrows(IllegalArgumentException.class,
                () -> bus.reader(CONSUMER, TickPhase.THERMAL, 0, SimEvent.class));
    }

    /**
     * Carry-over round trip: events still inside their lap at TICK_END
     * serialize, load into a fresh bus and honor the same visibility window —
     * the §6 {@code run K+N ≡ save@K, load, run N} rule for the bus.
     */
    @Test
    void carryOverLapSurvivesSerializeLoadRoundTrip() throws IOException {
        Events original = Events.create();
        EventSink sink = original.sink(EMITTER, TickPhase.THERMAL, 0);
        original.seal();

        original.advanceTo(5, TickPhase.THERMAL, 0);
        sink.emit(new TileIgnited(11));
        sink.emit(new FluidFrozenEvent(12, (short) 2, 30));
        original.retireLap(5);

        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        original.serializeCarryOver(new DataOutputStream(bytes));

        Events loaded = Events.create();
        EventReader<TileIgnited> ignitions = loaded.reader(CONSUMER, TickPhase.FLUIDS, 0,
                TileIgnited.class);
        EventReader<FluidFrozenEvent> freezes = loaded.reader(CONSUMER, TickPhase.FLUIDS, 1,
                FluidFrozenEvent.class);
        loaded.seal();
        loaded.loadCarryOver(new DataInputStream(new ByteArrayInputStream(bytes.toByteArray())));

        // Tick 6 on the loaded bus: the carry-over lap is still visible...
        loaded.advanceTo(6, TickPhase.TICK_BEGIN, 0);
        assertTrue(ignitions.hasNext());
        assertEquals(new TileIgnited(11), ignitions.next());
        assertTrue(freezes.hasNext());
        assertEquals(new FluidFrozenEvent(12, (short) 2, 30), freezes.next());
        loaded.retireLap(6);

        // ...and retires exactly when it would have on the original bus.
        loaded.advanceTo(7, TickPhase.TICK_BEGIN, 0);
        assertFalse(ignitions.hasNext());
        assertFalse(freezes.hasNext());
    }

    /** Serialized carry-over is byte-deterministic for equal bus content. */
    @Test
    void carryOverSerializationIsByteDeterministic() throws IOException {
        byte[] first = carryOverBytes();
        byte[] second = carryOverBytes();
        assertEquals(first.length, second.length);
        for (int i = 0; i < first.length; i++) {
            assertEquals(first[i], second[i], "byte " + i + " differs");
        }
    }

    private static byte[] carryOverBytes() throws IOException {
        Events bus = Events.create();
        EventSink sink = bus.sink(EMITTER, TickPhase.REACTIONS, 2);
        bus.seal();
        bus.advanceTo(9, TickPhase.REACTIONS, 2);
        sink.emit(new EnergyDischargedEvent(3, 60_000, 60_000));
        sink.emit(new ChargeStopChangedEvent(3, 1, 2));
        bus.retireLap(9);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        bus.serializeCarryOver(new DataOutputStream(bytes));
        return bytes.toByteArray();
    }
}
